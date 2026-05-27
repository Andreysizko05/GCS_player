#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include "GstVideoReceiver.h"
#include "VideoSettingsConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QWidget>
#include <QtMultimedia/QVideoSink>
#include <QtMultimediaWidgets/QVideoWidget>

#include <algorithm>

namespace
{
int containerIndexFromTransport(GstVideoReceiver::Transport transport)
{
    switch (transport) {
    case GstVideoReceiver::Transport::UsbCamera:
        return 2;
    case GstVideoReceiver::Transport::UdpMpegTs:
        return 1;
    case GstVideoReceiver::Transport::UdpRtp:
        return 0;
    }

    return 0;
}

GstVideoReceiver::Transport transportFromContainerIndex(int index)
{
    if (index == 2) {
        return GstVideoReceiver::Transport::UsbCamera;
    }
    if (index == 1) {
        return GstVideoReceiver::Transport::UdpMpegTs;
    }

    return GstVideoReceiver::Transport::UdpRtp;
}

int codecIndexFromCodec(GstVideoReceiver::Codec codec)
{
    switch (codec) {
    case GstVideoReceiver::Codec::H265:
        return 1;
    case GstVideoReceiver::Codec::H264:
        return 0;
    }

    return 0;
}

GstVideoReceiver::Codec codecFromCodecIndex(int index)
{
    return index == 1
        ? GstVideoReceiver::Codec::H265
        : GstVideoReceiver::Codec::H264;
}

VideoSettingsConfig::Settings videoSettingsFromUi(const Ui::MainWindow* ui)
{
    VideoSettingsConfig::Settings settings;
    settings.transport = transportFromContainerIndex(ui->videoContainerComboBox->currentIndex());
    settings.codec = codecFromCodecIndex(ui->videoCodecComboBox->currentIndex());
    settings.bindAddress = ui->videoAddressLineEdit->text().trimmed();
    if (settings.bindAddress.isEmpty()) {
        settings.bindAddress = QStringLiteral("0.0.0.0");
    }
    settings.port = static_cast<quint16>(ui->videoPortSpinBox->value());
    settings.lowLatency = ui->lowLatencyCheckBox->isChecked();
    return settings;
}

void applyVideoSettingsToUi(Ui::MainWindow* ui, const VideoSettingsConfig::Settings& settings)
{
    ui->videoContainerComboBox->setCurrentIndex(containerIndexFromTransport(settings.transport));
    ui->videoCodecComboBox->setCurrentIndex(codecIndexFromCodec(settings.codec));
    ui->videoAddressLineEdit->setText(settings.bindAddress);
    ui->videoPortSpinBox->setValue(settings.port);
    ui->lowLatencyCheckBox->setChecked(settings.lowLatency);
}
} // namespace

MainWindow::MainWindow(QWidget *parent, bool startVideoReceiver)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->videoFrameLabel->setStyleSheet(QStringLiteral("background-color: black; color: white;"));
    ui->videoFrameLabel->setScaledContents(false);
    ui->videoFrameLabel->setAlignment(Qt::AlignCenter);
    setupVideoSettingsUi();
    loadVideoSettings();

    if (startVideoReceiver) {
        ensureVideoWidget();
        restartVideoReceiver();
    }
}

MainWindow::~MainWindow()
{
    if (mVideoReceiver != nullptr)
    {
        mVideoReceiver->stop();
        mVideoReceiver->wait();
    }

    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    updateVideoSurfaceGeometry();
}

void MainWindow::setupVideoSettingsUi()
{
    if (ui->videoContainerComboBox->count() < 3) {
        ui->videoContainerComboBox->addItem(QStringLiteral("USB Camera"));
    }
    setupUsbSettingsUi();

    connect(ui->applyVideoSettingsButton, &QPushButton::clicked, this, &MainWindow::applyVideoSettings);
    connect(
        ui->videoContainerComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::onVideoContainerChanged
    );
    onVideoContainerChanged(ui->videoContainerComboBox->currentIndex());
}

void MainWindow::setupUsbSettingsUi()
{
    mUsbCameraComboBox = new QComboBox(ui->videoSettingsDockContents);
    mUsbCameraComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mUsbCameraComboBox->setMinimumContentsLength(18);

    mRefreshUsbDevicesButton = new QPushButton(QStringLiteral("Refresh"), ui->videoSettingsDockContents);

    auto* cameraField = new QWidget(ui->videoSettingsDockContents);
    auto* cameraLayout = new QHBoxLayout(cameraField);
    cameraLayout->setContentsMargins(0, 0, 0, 0);
    cameraLayout->addWidget(mUsbCameraComboBox, 1);
    cameraLayout->addWidget(mRefreshUsbDevicesButton);
    ui->videoSettingsFormLayout->addRow(QStringLiteral("Camera"), cameraField);

    mUsbModeComboBox = new QComboBox(ui->videoSettingsDockContents);
    mUsbModeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mUsbModeComboBox->setMinimumContentsLength(18);
    ui->videoSettingsFormLayout->addRow(QStringLiteral("Mode"), mUsbModeComboBox);

    mUsbControlsGroupBox = new QGroupBox(QStringLiteral("UVC Controls"), ui->videoSettingsDockContents);
    mUsbControlsLayout = new QFormLayout(mUsbControlsGroupBox);
    mUsbControlsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    const int applyButtonIndex = ui->videoSettingsOuterLayout->indexOf(ui->applyVideoSettingsButton);
    ui->videoSettingsOuterLayout->insertWidget(
        applyButtonIndex >= 0 ? applyButtonIndex : 1,
        mUsbControlsGroupBox
    );

    connect(
        mUsbCameraComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::onUsbCameraChanged
    );
    connect(
        mRefreshUsbDevicesButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onRefreshUsbDevicesClicked
    );

    refreshUsbDevices();
}

void MainWindow::loadVideoSettings()
{
    const VideoSettingsConfig config;
    const VideoSettingsConfig::LoadResult result = config.loadOrCreate();
    applyVideoSettingsToUi(ui, result.settings);
    refreshUsbDevices(result.settings.usbDeviceId);
    refreshUsbModes(result.settings.usbModeCaps);
    refreshUsbControls(result.settings.usbControls);
    onVideoContainerChanged(ui->videoContainerComboBox->currentIndex());

    if (!result.ok) {
        ui->statusbar->showMessage(
            QStringLiteral("Unable to load local video settings: %1").arg(result.errorMessage),
            5000
        );
    }
}

bool MainWindow::saveVideoSettings() const
{
    const VideoSettingsConfig config;
    return config.save(currentVideoSettings());
}

VideoSettingsConfig::Settings MainWindow::currentVideoSettings() const
{
    VideoSettingsConfig::Settings settings = videoSettingsFromUi(ui);
    settings.usbDeviceId = selectedUsbDeviceId();
    settings.usbDeviceName = selectedUsbDeviceName();
    settings.usbDeviceIndex = selectedUsbDeviceIndex();
    settings.usbModeCaps = selectedUsbModeCaps();
    settings.usbControls = usbControlStatesFromUi();
    return settings;
}

void MainWindow::refreshUsbDevices(const QString& preferredDeviceId)
{
    if (mUsbCameraComboBox == nullptr) {
        return;
    }

    mUpdatingUsbUi = true;
    const QSignalBlocker blocker(mUsbCameraComboBox);
    mUsbCameraComboBox->clear();

    const QVector<UsbCameraDevice> devices = UsbCameraManager::devices();
    for (const UsbCameraDevice& device : devices) {
        const QString label = device.backend.isEmpty()
            ? device.displayName
            : QStringLiteral("%1 (%2)").arg(device.displayName, device.backend);
        mUsbCameraComboBox->addItem(label, device.id);
        mUsbCameraComboBox->setItemData(
            mUsbCameraComboBox->count() - 1,
            device.displayName,
            Qt::UserRole + 1
        );
        mUsbCameraComboBox->setItemData(
            mUsbCameraComboBox->count() - 1,
            device.index,
            Qt::UserRole + 2
        );
    }

    const int preferredIndex = preferredDeviceId.isEmpty()
        ? -1
        : mUsbCameraComboBox->findData(preferredDeviceId);
    if (preferredIndex >= 0) {
        mUsbCameraComboBox->setCurrentIndex(preferredIndex);
    } else if (mUsbCameraComboBox->count() > 0) {
        mUsbCameraComboBox->setCurrentIndex(0);
    }

    mUpdatingUsbUi = false;
    refreshUsbModes();
    refreshUsbControls();
}

void MainWindow::refreshUsbModes(const QString& preferredModeCaps)
{
    if (mUsbModeComboBox == nullptr) {
        return;
    }

    const QSignalBlocker blocker(mUsbModeComboBox);
    mUsbModeComboBox->clear();
    mUsbModeComboBox->addItem(QStringLiteral("Auto"), QString());

    const QString deviceId = selectedUsbDeviceId();
    if (!deviceId.isEmpty()) {
        const QVector<UsbCameraMode> modes = UsbCameraManager::modes(deviceId, selectedUsbDeviceIndex());
        for (const UsbCameraMode& mode : modes) {
            mUsbModeComboBox->addItem(mode.label, mode.caps);
        }
    }

    const int preferredIndex = preferredModeCaps.isEmpty()
        ? 0
        : mUsbModeComboBox->findData(preferredModeCaps);
    mUsbModeComboBox->setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
}

void MainWindow::refreshUsbControls(const QMap<QString, UsbCameraControlState>& preferredStates)
{
    if (mUsbControlsLayout == nullptr) {
        return;
    }

    mUpdatingUsbUi = true;
    clearUsbControls();

    const QString deviceId = selectedUsbDeviceId();
    QVector<UsbCameraControl> controls = deviceId.isEmpty()
        ? QVector<UsbCameraControl>()
        : UsbCameraManager::controls(deviceId);

    for (UsbCameraControl& control : controls) {
        if (preferredStates.contains(control.id)) {
            control.state = preferredStates.value(control.id);
        }

        auto* rowWidget = new QWidget(mUsbControlsGroupBox);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* autoCheckBox = new QCheckBox(QStringLiteral("Auto"), rowWidget);
        autoCheckBox->setVisible(control.supportsAuto);
        autoCheckBox->setChecked(control.supportsAuto && control.state.automatic);
        rowLayout->addWidget(autoCheckBox);

        auto* slider = new QSlider(Qt::Horizontal, rowWidget);
        slider->setRange(control.minimum, control.maximum);
        slider->setSingleStep(control.step);
        slider->setPageStep(control.step * 5);
        slider->setValue(std::clamp(control.state.value, control.minimum, control.maximum));
        rowLayout->addWidget(slider, 1);

        auto* spinBox = new QSpinBox(rowWidget);
        spinBox->setRange(control.minimum, control.maximum);
        spinBox->setSingleStep(control.step);
        spinBox->setKeyboardTracking(false);
        spinBox->setValue(slider->value());
        rowLayout->addWidget(spinBox);

        const bool manualControlsEnabled = !autoCheckBox->isChecked();
        slider->setEnabled(manualControlsEnabled);
        spinBox->setEnabled(manualControlsEnabled);

        UsbControlWidgets widgets;
        widgets.control = control;
        widgets.slider = slider;
        widgets.spinBox = spinBox;
        widgets.autoCheckBox = autoCheckBox;
        mUsbControlWidgets.insert(control.id, widgets);

        connect(slider, &QSlider::valueChanged, this, [this, spinBox, controlId = control.id](int value) {
            if (mUpdatingUsbUi) {
                return;
            }
            const QSignalBlocker blocker(spinBox);
            spinBox->setValue(value);
            applyUsbControl(controlId);
        });
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, slider, controlId = control.id](int value) {
            if (mUpdatingUsbUi) {
                return;
            }
            const QSignalBlocker blocker(slider);
            slider->setValue(value);
            applyUsbControl(controlId);
        });
        connect(autoCheckBox, &QCheckBox::toggled, this, [this, slider, spinBox, controlId = control.id](bool checked) {
            slider->setEnabled(!checked);
            spinBox->setEnabled(!checked);
            if (!mUpdatingUsbUi) {
                applyUsbControl(controlId);
            }
        });

        mUsbControlsLayout->addRow(control.displayName, rowWidget);
    }

    if (controls.isEmpty()) {
        auto* emptyLabel = new QLabel(QStringLiteral("No adjustable controls reported."), mUsbControlsGroupBox);
        emptyLabel->setWordWrap(true);
        mUsbControlsLayout->addRow(emptyLabel);
    }

    mUpdatingUsbUi = false;
}

void MainWindow::clearUsbControls()
{
    mUsbControlWidgets.clear();
    if (mUsbControlsLayout == nullptr) {
        return;
    }

    QLayoutItem* item = nullptr;
    while ((item = mUsbControlsLayout->takeAt(0)) != nullptr) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

QString MainWindow::selectedUsbDeviceId() const
{
    return mUsbCameraComboBox != nullptr
        ? mUsbCameraComboBox->currentData(Qt::UserRole).toString()
        : QString();
}

QString MainWindow::selectedUsbDeviceName() const
{
    return mUsbCameraComboBox != nullptr
        ? mUsbCameraComboBox->currentData(Qt::UserRole + 1).toString()
        : QString();
}

int MainWindow::selectedUsbDeviceIndex() const
{
    return mUsbCameraComboBox != nullptr
        ? mUsbCameraComboBox->currentData(Qt::UserRole + 2).toInt()
        : -1;
}

QString MainWindow::selectedUsbModeCaps() const
{
    return mUsbModeComboBox != nullptr
        ? mUsbModeComboBox->currentData(Qt::UserRole).toString()
        : QString();
}

QMap<QString, UsbCameraControlState> MainWindow::usbControlStatesFromUi() const
{
    QMap<QString, UsbCameraControlState> states;
    for (auto it = mUsbControlWidgets.cbegin(); it != mUsbControlWidgets.cend(); ++it) {
        UsbCameraControlState state;
        state.value = it.value().spinBox != nullptr ? it.value().spinBox->value() : it.value().control.state.value;
        state.automatic = it.value().autoCheckBox != nullptr && it.value().autoCheckBox->isChecked();
        states.insert(it.key(), state);
    }

    return states;
}

void MainWindow::applyUsbControl(const QString& controlId)
{
    const auto it = mUsbControlWidgets.constFind(controlId);
    if (it == mUsbControlWidgets.cend()) {
        return;
    }

    UsbCameraControlState state;
    state.value = it.value().spinBox != nullptr ? it.value().spinBox->value() : it.value().control.state.value;
    state.automatic = it.value().autoCheckBox != nullptr && it.value().autoCheckBox->isChecked();

    QString errorMessage;
    if (!UsbCameraManager::setControl(selectedUsbDeviceId(), controlId, state, &errorMessage)) {
        ui->statusbar->showMessage(errorMessage, 5000);
    }
}

void MainWindow::ensureVideoWidget()
{
    if (mVideoWidget != nullptr) {
        return;
    }

    mVideoWidget = new QVideoWidget(ui->centralwidget);
    mVideoWidget->setObjectName(QStringLiteral("videoFrameSinkWidget"));
    mVideoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    mVideoWidget->setStyleSheet(QStringLiteral("background-color: black;"));
    mVideoWidget->setGeometry(ui->videoFrameLabel->geometry());
    mVideoWidget->show();
    ui->videoFrameLabel->hide();
}

void MainWindow::restartVideoReceiver()
{
    ensureVideoWidget();

    if (mVideoReceiver != nullptr) {
        mVideoReceiver->stop();
        mVideoReceiver->wait();
        delete mVideoReceiver;
        mVideoReceiver = nullptr;
    }

    const VideoSettingsConfig::Settings videoSettings = currentVideoSettings();
    ui->videoAddressLineEdit->setText(videoSettings.bindAddress);

    GstVideoReceiver::StreamSettings settings;
    settings.transport = videoSettings.transport;
    settings.codec = videoSettings.codec;
    settings.udpHost = videoSettings.bindAddress;
    settings.udpPort = videoSettings.port;
    settings.lowLatency = videoSettings.lowLatency;
    settings.usbDeviceId = videoSettings.usbDeviceId;
    settings.usbDeviceName = videoSettings.usbDeviceName;
    settings.usbDeviceIndex = videoSettings.usbDeviceIndex;
    settings.usbModeCaps = videoSettings.usbModeCaps;
    settings.usbControls = videoSettings.usbControls;

    mVideoSize = QSize();
    ui->detectedResolutionValueLabel->setText(QStringLiteral("Auto"));

    mVideoReceiver = new GstVideoReceiver(settings, this);
    connect(mVideoReceiver, &GstVideoReceiver::frameReady, this, &MainWindow::onVideoFrameReady);
    connect(mVideoReceiver, &GstVideoReceiver::videoSizeChanged, this, &MainWindow::onVideoSizeChanged);
    connect(mVideoReceiver, &GstVideoReceiver::receiverMessage, this, &MainWindow::onVideoReceiverMessage);
    connect(mVideoReceiver, &GstVideoReceiver::receiverError, this, &MainWindow::onVideoReceiverError);
    mVideoReceiver->start();

    ui->statusbar->showMessage(QStringLiteral("Video receiver restarted."), 3000);
    updateVideoSurfaceGeometry();
}

void MainWindow::updateVideoSurfaceGeometry()
{
    const QSize availableSize = ui->centralwidget->size();
    const QSize frameSize(
        static_cast<int>(availableSize.width() * mFrameWidthFactor),
        static_cast<int>(availableSize.height() * mFrameHeightFactor)
    );
    const int frameOriginX = (availableSize.width() - frameSize.width()) / 2;
    const int frameOriginY = (availableSize.height() - frameSize.height()) / 2;

    if (frameSize.width() <= 0 || frameSize.height() <= 0)
    {
        mVideoAreaLeft = 0;
        mVideoAreaTop = 0;
        mVideoAreaRight = 0;
        mVideoAreaBottom = 0;
        return;
    }

    const double frameAspectRatio = static_cast<double>(frameSize.width()) / frameSize.height();
    const double targetAspectRatio = targetVideoAspectRatio();

    QRect targetRect; // vertical/horizontal black fill-lines
    if (frameAspectRatio < targetAspectRatio)
    {
        const int targetHeight = static_cast<int>(frameSize.width() / targetAspectRatio);
        const int yOffset = (frameSize.height() - targetHeight) / 2;
        targetRect = QRect(0, yOffset, frameSize.width(), targetHeight);
    }
    else
    {
        const int targetWidth = static_cast<int>(frameSize.height() * targetAspectRatio);
        const int xOffset = (frameSize.width() - targetWidth) / 2;
        targetRect = QRect(xOffset, 0, targetWidth, frameSize.height());
    }

    targetRect.translate(frameOriginX, frameOriginY);
    videoSurfaceWidget()->setGeometry(targetRect);
	targetRect.getCoords(&mVideoAreaLeft, &mVideoAreaTop, &mVideoAreaRight, &mVideoAreaBottom);
}

double MainWindow::targetVideoAspectRatio() const
{
    if (mVideoSize.isValid() && mVideoSize.height() > 0) {
        return static_cast<double>(mVideoSize.width()) / mVideoSize.height();
    }

    return 16.0 / 9.0;
}

QWidget* MainWindow::videoSurfaceWidget() const
{
    if (mVideoWidget != nullptr) {
        return mVideoWidget;
    }

    return ui->videoFrameLabel;
}

void MainWindow::onVideoFrameReady(const QVideoFrame& frame)
{
    if (mVideoWidget == nullptr || mVideoWidget->videoSink() == nullptr || !frame.isValid()) {
        return;
    }

    mVideoWidget->videoSink()->setVideoFrame(frame);
}

void MainWindow::applyVideoSettings()
{
    restartVideoReceiver();
    if (!saveVideoSettings()) {
        ui->statusbar->showMessage(QStringLiteral("Unable to save local video settings."), 5000);
    }
}

void MainWindow::onVideoContainerChanged(int index)
{
    const GstVideoReceiver::Transport transport = transportFromContainerIndex(index);
    const bool rtpSelected = transport == GstVideoReceiver::Transport::UdpRtp;
    const bool udpSelected = transport == GstVideoReceiver::Transport::UdpRtp
        || transport == GstVideoReceiver::Transport::UdpMpegTs;
    const bool usbSelected = transport == GstVideoReceiver::Transport::UsbCamera;

    ui->videoCodecComboBox->setEnabled(rtpSelected);
    ui->videoCodecLabel->setEnabled(rtpSelected);
    ui->videoCodecComboBox->setVisible(!usbSelected);
    ui->videoCodecLabel->setVisible(!usbSelected);
    ui->videoAddressLineEdit->setVisible(udpSelected);
    ui->videoAddressLabel->setVisible(udpSelected);
    ui->videoPortSpinBox->setVisible(udpSelected);
    ui->videoPortLabel->setVisible(udpSelected);
    ui->lowLatencyCheckBox->setVisible(udpSelected);

    if (mUsbCameraComboBox != nullptr) {
        QWidget* cameraField = mUsbCameraComboBox->parentWidget();
        cameraField->setVisible(usbSelected);
        if (ui->videoSettingsFormLayout->labelForField(cameraField) != nullptr) {
            ui->videoSettingsFormLayout->labelForField(cameraField)->setVisible(usbSelected);
        }
    }
    if (mUsbModeComboBox != nullptr) {
        mUsbModeComboBox->setVisible(usbSelected);
        if (ui->videoSettingsFormLayout->labelForField(mUsbModeComboBox) != nullptr) {
            ui->videoSettingsFormLayout->labelForField(mUsbModeComboBox)->setVisible(usbSelected);
        }
    }
    if (mUsbControlsGroupBox != nullptr) {
        mUsbControlsGroupBox->setVisible(usbSelected);
    }
}

void MainWindow::onUsbCameraChanged(int index)
{
    Q_UNUSED(index)

    if (mUpdatingUsbUi) {
        return;
    }

    refreshUsbModes();
    refreshUsbControls();
}

void MainWindow::onRefreshUsbDevicesClicked()
{
    refreshUsbDevices(selectedUsbDeviceId());
}

void MainWindow::onVideoSizeChanged(const QSize& size)
{
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return;
    }

    mVideoSize = size;
    ui->detectedResolutionValueLabel->setText(
        QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())
    );
    updateVideoSurfaceGeometry();
}

void MainWindow::onVideoReceiverMessage(const QString& message)
{
    ui->statusbar->showMessage(message, 3000);
}

void MainWindow::onVideoReceiverError(const QString& message)
{
    ui->statusbar->showMessage(message, 5000);
}
