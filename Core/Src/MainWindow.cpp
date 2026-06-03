#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include "GstVideoReceiver.h"
#include "ThrottledSlider.h"
#include "VideoSettingsConfig.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QVBoxLayout>
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
        return 5;
    case GstVideoReceiver::Transport::CustomPipeline:
        return 4;
    case GstVideoReceiver::Transport::TcpMpegTs:
        return 3;
    case GstVideoReceiver::Transport::Rtsp:
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
    if (index == 5) {
        return GstVideoReceiver::Transport::UsbCamera;
    }
    if (index == 4) {
        return GstVideoReceiver::Transport::CustomPipeline;
    }
    if (index == 3) {
        return GstVideoReceiver::Transport::TcpMpegTs;
    }
    if (index == 2) {
        return GstVideoReceiver::Transport::Rtsp;
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

int recordingContainerIndexFromContainer(GstVideoReceiver::RecordingContainer container)
{
    return container == GstVideoReceiver::RecordingContainer::Mp4 ? 1 : 0;
}

GstVideoReceiver::RecordingContainer recordingContainerFromIndex(int index)
{
    return index == 1
        ? GstVideoReceiver::RecordingContainer::Mp4
        : GstVideoReceiver::RecordingContainer::Matroska;
}

QString defaultRecordingDirectory()
{
    QString moviesPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (moviesPath.isEmpty()) {
        moviesPath = QCoreApplication::applicationDirPath();
    }

    return QDir(moviesPath).filePath(QStringLiteral("GCS_player"));
}

VideoSettingsConfig::Settings videoSettingsFromUi(const Ui::MainWindow* ui)
{
    VideoSettingsConfig::Settings settings;
    settings.transport = transportFromContainerIndex(ui->videoContainerComboBox->currentIndex());
    settings.codec = codecFromCodecIndex(ui->videoCodecComboBox->currentIndex());
    const QString address = ui->videoAddressLineEdit->text().trimmed();
    if (settings.transport == GstVideoReceiver::Transport::Rtsp) {
        settings.streamUrl = address;
    } else {
        settings.bindAddress = address;
    }
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
    ui->videoAddressLineEdit->setText(
        settings.transport == GstVideoReceiver::Transport::Rtsp
            ? settings.streamUrl
            : settings.bindAddress
    );
    ui->videoPortSpinBox->setValue(settings.port);
    ui->lowLatencyCheckBox->setChecked(settings.lowLatency);
}

UsbCameraControlState defaultStateForControl(const UsbCameraControl& control)
{
    UsbCameraControlState state;
    state.value = std::clamp(control.defaultValue, control.minimum, control.maximum);
    state.automatic = control.supportsAuto && control.defaultAutomatic;
    return state;
}

bool isUsbCameraTransportSelected(const Ui::MainWindow* ui)
{
    return transportFromContainerIndex(ui->videoContainerComboBox->currentIndex())
        == GstVideoReceiver::Transport::UsbCamera;
}

bool isZoomControlId(const QString& controlId)
{
    return controlId.compare(QStringLiteral("camera:zoom"), Qt::CaseInsensitive) == 0;
}

bool isZoomControl(const UsbCameraControl& control)
{
    return isZoomControlId(control.id)
        || control.displayName.contains(QStringLiteral("zoom"), Qt::CaseInsensitive);
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

void MainWindow::setupVideoSettingsUi()
{
    ui->videoContainerComboBox->clear();
    ui->videoContainerComboBox->addItem(QStringLiteral("RTP over UDP"));
    ui->videoContainerComboBox->addItem(QStringLiteral("MPEG-TS over UDP (auto)"));
    ui->videoContainerComboBox->addItem(QStringLiteral("RTSP"));
    ui->videoContainerComboBox->addItem(QStringLiteral("MPEG-TS over TCP"));
    ui->videoContainerComboBox->addItem(QStringLiteral("Custom GStreamer"));
    ui->videoContainerComboBox->addItem(QStringLiteral("USB Camera"));

    setupCustomPipelineUi();
    setupUsbSettingsUi();
    setupRecordingSettingsUi();

    connect(ui->applyVideoSettingsButton, &QPushButton::clicked, this, &MainWindow::applyVideoSettings);
    connect(
        ui->videoContainerComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::onVideoContainerChanged
    );
    onVideoContainerChanged(ui->videoContainerComboBox->currentIndex());
}

void MainWindow::setupCustomPipelineUi()
{
    mCustomPipelineTextEdit = new QPlainTextEdit(ui->videoSettingsDockContents);
    mCustomPipelineTextEdit->setMinimumHeight(96);
    mCustomPipelineTextEdit->setPlaceholderText(QStringLiteral(
        "rtspsrc location=rtsp://192.168.144.25:8554/main.264 latency=25 ! "
        "application/x-rtp ! decodebin3 ! videoconvert ! "
        "video/x-raw,format=BGRA ! appsink name=preview-sink"
    ));
    ui->videoSettingsFormLayout->addRow(QStringLiteral("Pipeline"), mCustomPipelineTextEdit);
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
    auto* controlsOuterLayout = new QVBoxLayout(mUsbControlsGroupBox);
    mUsbControlsLayout = new QFormLayout();
    mUsbControlsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    controlsOuterLayout->addLayout(mUsbControlsLayout);

    mUsbDefaultsButton = new QPushButton(QStringLiteral("Defaults"), mUsbControlsGroupBox);
    mUsbDefaultsButton->setVisible(false);
    controlsOuterLayout->addWidget(mUsbDefaultsButton, 0, Qt::AlignRight);

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
        mUsbModeComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::onUsbModeChanged
    );
    connect(
        mRefreshUsbDevicesButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onRefreshUsbDevicesClicked
    );
    connect(
        mUsbDefaultsButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onUsbDefaultsClicked
    );

    refreshUsbDevices();
}

void MainWindow::setupRecordingSettingsUi()
{
    mRecordingEnabledCheckBox = new QCheckBox(QStringLiteral("Record"), ui->videoSettingsDockContents);
    ui->videoSettingsFormLayout->addRow(QStringLiteral("Recording"), mRecordingEnabledCheckBox);

    mRecordingContainerComboBox = new QComboBox(ui->videoSettingsDockContents);
    mRecordingContainerComboBox->addItem(QStringLiteral("MKV"), static_cast<int>(GstVideoReceiver::RecordingContainer::Matroska));
    mRecordingContainerComboBox->addItem(QStringLiteral("MP4"), static_cast<int>(GstVideoReceiver::RecordingContainer::Mp4));
    ui->videoSettingsFormLayout->addRow(QStringLiteral("File Type"), mRecordingContainerComboBox);

    mRecordingDirectoryLineEdit = new QLineEdit(ui->videoSettingsDockContents);
    mRecordingDirectoryLineEdit->setText(defaultRecordingDirectory());

    mBrowseRecordingDirectoryButton = new QPushButton(QStringLiteral("Browse"), ui->videoSettingsDockContents);

    auto* directoryField = new QWidget(ui->videoSettingsDockContents);
    auto* directoryLayout = new QHBoxLayout(directoryField);
    directoryLayout->setContentsMargins(0, 0, 0, 0);
    directoryLayout->addWidget(mRecordingDirectoryLineEdit, 1);
    directoryLayout->addWidget(mBrowseRecordingDirectoryButton);
    ui->videoSettingsFormLayout->addRow(QStringLiteral("Folder"), directoryField);

    connect(
        mBrowseRecordingDirectoryButton,
        &QPushButton::clicked,
        this,
        &MainWindow::onBrowseRecordingDirectoryClicked
    );
}

void MainWindow::loadVideoSettings()
{
    const VideoSettingsConfig config;
    const VideoSettingsConfig::LoadResult result = config.loadOrCreate();
    applyVideoSettingsToWidgets(result.settings);

    if (!result.ok) {
        ui->statusbar->showMessage(
            QStringLiteral("Unable to load local video settings: %1").arg(result.errorMessage),
            5000
        );
    }
}

void MainWindow::applyVideoSettingsToWidgets(const VideoSettingsConfig::Settings& settings)
{
    applyVideoSettingsToUi(ui, settings);
    if (mCustomPipelineTextEdit != nullptr) {
        mCustomPipelineTextEdit->setPlainText(settings.customPipeline);
    }
    if (mRecordingEnabledCheckBox != nullptr) {
        mRecordingEnabledCheckBox->setChecked(settings.recordingEnabled);
    }
    if (mRecordingContainerComboBox != nullptr) {
        mRecordingContainerComboBox->setCurrentIndex(
            recordingContainerIndexFromContainer(settings.recordingContainer)
        );
    }
    if (mRecordingDirectoryLineEdit != nullptr) {
        mRecordingDirectoryLineEdit->setText(
            settings.recordingDirectory.isEmpty()
                ? defaultRecordingDirectory()
                : settings.recordingDirectory
        );
    }
    refreshUsbDevices(settings.usbDeviceId);
    refreshUsbModes(settings.usbModeCaps);
    refreshUsbControls(settings.usbControls);
    onVideoContainerChanged(ui->videoContainerComboBox->currentIndex());
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
    if (mCustomPipelineTextEdit != nullptr) {
        settings.customPipeline = mCustomPipelineTextEdit->toPlainText().trimmed();
    }
    if (mRecordingEnabledCheckBox != nullptr) {
        settings.recordingEnabled = mRecordingEnabledCheckBox->isChecked();
    }
    if (mRecordingContainerComboBox != nullptr) {
        settings.recordingContainer = recordingContainerFromIndex(mRecordingContainerComboBox->currentIndex());
    }
    if (mRecordingDirectoryLineEdit != nullptr) {
        settings.recordingDirectory = mRecordingDirectoryLineEdit->text().trimmed();
    }
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
        if (preferredStates.contains(control.id) && !isZoomControl(control)) {
            control.state = preferredStates.value(control.id);
        }

        auto* rowWidget = new QWidget(mUsbControlsGroupBox);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);

        auto* autoCheckBox = new QCheckBox(QStringLiteral("Auto"), rowWidget);
        autoCheckBox->setVisible(control.supportsAuto);
        autoCheckBox->setChecked(control.supportsAuto && control.state.automatic);
        rowLayout->addWidget(autoCheckBox);

        auto* slider = new ThrottledSlider(Qt::Horizontal, rowWidget);
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

        connect(slider, &QSlider::valueChanged, this, [this, spinBox](int value) {
            if (mUpdatingUsbUi) {
                return;
            }
            const QSignalBlocker blocker(spinBox);
            spinBox->setValue(value);
        });
        connect(slider, &ThrottledSlider::throttledValueChanged, this, [this, controlId = control.id](int value) {
            Q_UNUSED(value)
            if (mUpdatingUsbUi) {
                return;
            }
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
    updateUsbDefaultsButtonVisibility();
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

void MainWindow::updateUsbDefaultsButtonVisibility()
{
    if (mUsbDefaultsButton == nullptr) {
        return;
    }

    const bool usbSelected = transportFromContainerIndex(ui->videoContainerComboBox->currentIndex())
        == GstVideoReceiver::Transport::UsbCamera;
    mUsbDefaultsButton->setVisible(usbSelected && !mUsbControlWidgets.isEmpty());
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
        if (isZoomControl(it.value().control)) {
            continue;
        }

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

bool MainWindow::resetZoomControlToDefault()
{
    for (auto it = mUsbControlWidgets.cbegin(); it != mUsbControlWidgets.cend(); ++it) {
        if (!isZoomControl(it.value().control)) {
            continue;
        }

        QString errorMessage;
        if (!UsbCameraManager::setControl(
                selectedUsbDeviceId(),
                it.key(),
                defaultStateForControl(it.value().control),
                &errorMessage)) {
            ui->statusbar->showMessage(
                QStringLiteral("Unable to reset zoom to default: %1").arg(errorMessage),
                5000
            );
            return false;
        }

        return true;
    }

    return false;
}

void MainWindow::applyUsbSelectionChange()
{
    if (!isUsbCameraTransportSelected(ui)) {
        return;
    }

    applyVideoSettings();
    refreshUsbControls();
    if (resetZoomControlToDefault()) {
        refreshUsbControls();
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
    ui->videoSurfaceLayout->addWidget(mVideoWidget);
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
    ui->videoAddressLineEdit->setText(
        videoSettings.transport == GstVideoReceiver::Transport::Rtsp
            ? videoSettings.streamUrl
            : videoSettings.bindAddress
    );

    GstVideoReceiver::StreamSettings settings;
    settings.transport = videoSettings.transport;
    settings.codec = videoSettings.codec;
    settings.udpHost = videoSettings.bindAddress;
    settings.udpPort = videoSettings.port;
    settings.streamUrl = videoSettings.streamUrl;
    settings.customPipeline = videoSettings.customPipeline;
    settings.lowLatency = videoSettings.lowLatency;
    settings.usbDeviceId = videoSettings.usbDeviceId;
    settings.usbDeviceName = videoSettings.usbDeviceName;
    settings.usbDeviceIndex = videoSettings.usbDeviceIndex;
    settings.usbModeCaps = videoSettings.usbModeCaps;
    settings.usbControls = videoSettings.usbControls;
    settings.recordingEnabled = videoSettings.recordingEnabled;
    settings.recordingContainer = videoSettings.recordingContainer;
    settings.recordingDirectory = videoSettings.recordingDirectory;
    settings.recordingBitrateKbps = videoSettings.recordingBitrateKbps;

    mVideoSize = QSize();
    ui->detectedResolutionValueLabel->setText(QStringLiteral("Auto"));

    mVideoReceiver = new GstVideoReceiver(settings, this);
    connect(mVideoReceiver, &GstVideoReceiver::frameReady, this, &MainWindow::onVideoFrameReady);
    connect(mVideoReceiver, &GstVideoReceiver::videoSizeChanged, this, &MainWindow::onVideoSizeChanged);
    connect(mVideoReceiver, &GstVideoReceiver::receiverMessage, this, &MainWindow::onVideoReceiverMessage);
    connect(mVideoReceiver, &GstVideoReceiver::receiverError, this, &MainWindow::onVideoReceiverError);
    mVideoReceiver->start();

    ui->statusbar->showMessage(QStringLiteral("Video receiver restarted."), 3000);
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
    const bool rtspSelected = transport == GstVideoReceiver::Transport::Rtsp;
    const bool tcpSelected = transport == GstVideoReceiver::Transport::TcpMpegTs;
    const bool networkSelected = transport == GstVideoReceiver::Transport::UdpRtp
        || transport == GstVideoReceiver::Transport::UdpMpegTs
        || rtspSelected
        || tcpSelected;
    const bool customSelected = transport == GstVideoReceiver::Transport::CustomPipeline;
    const bool usbSelected = transport == GstVideoReceiver::Transport::UsbCamera;

    ui->videoCodecComboBox->setEnabled(rtpSelected);
    ui->videoCodecLabel->setEnabled(rtpSelected);
    ui->videoCodecComboBox->setVisible(rtpSelected);
    ui->videoCodecLabel->setVisible(rtpSelected);
    ui->videoAddressLineEdit->setVisible(networkSelected);
    ui->videoAddressLabel->setVisible(networkSelected);
    ui->videoPortSpinBox->setVisible(networkSelected && !rtspSelected);
    ui->videoPortLabel->setVisible(networkSelected && !rtspSelected);
    ui->lowLatencyCheckBox->setVisible(networkSelected);

    if (rtspSelected) {
        ui->videoAddressLabel->setText(QStringLiteral("RTSP URL"));
    } else if (tcpSelected) {
        ui->videoAddressLabel->setText(QStringLiteral("Host"));
    } else {
        ui->videoAddressLabel->setText(QStringLiteral("Bind Address"));
    }
    ui->videoPortLabel->setText(tcpSelected ? QStringLiteral("TCP Port") : QStringLiteral("UDP Port"));

    if (mCustomPipelineTextEdit != nullptr) {
        mCustomPipelineTextEdit->setVisible(customSelected);
        if (ui->videoSettingsFormLayout->labelForField(mCustomPipelineTextEdit) != nullptr) {
            ui->videoSettingsFormLayout->labelForField(mCustomPipelineTextEdit)->setVisible(customSelected);
        }
    }

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
    updateUsbDefaultsButtonVisibility();
}

void MainWindow::onUsbCameraChanged(int index)
{
    Q_UNUSED(index)

    if (mUpdatingUsbUi) {
        return;
    }

    refreshUsbModes();
    refreshUsbControls();
    applyUsbSelectionChange();
}

void MainWindow::onUsbModeChanged(int index)
{
    Q_UNUSED(index)

    if (mUpdatingUsbUi) {
        return;
    }

    applyUsbSelectionChange();
}

void MainWindow::onRefreshUsbDevicesClicked()
{
    refreshUsbDevices(selectedUsbDeviceId());
}

void MainWindow::onUsbDefaultsClicked()
{
    if (mUsbControlWidgets.isEmpty()) {
        return;
    }

    const QMessageBox::StandardButton button = QMessageBox::question(
        this,
        QStringLiteral("Restore Defaults"),
        QStringLiteral("Reset UVC controls for the selected camera to their defaults?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (button != QMessageBox::Yes) {
        return;
    }

    const QString deviceId = selectedUsbDeviceId();
    QStringList errors;
    for (auto it = mUsbControlWidgets.cbegin(); it != mUsbControlWidgets.cend(); ++it) {
        QString errorMessage;
        if (!UsbCameraManager::setControl(
                deviceId,
                it.key(),
                defaultStateForControl(it.value().control),
                &errorMessage)) {
            errors.append(errorMessage);
        }
    }

    refreshUsbControls();
    if (!saveVideoSettings()) {
        errors.append(QStringLiteral("Unable to save local video settings."));
    }

    if (!errors.isEmpty()) {
        ui->statusbar->showMessage(
            QStringLiteral("Unable to restore all UVC defaults: %1").arg(errors.first()),
            5000
        );
        return;
    }

    ui->statusbar->showMessage(QStringLiteral("UVC controls restored to defaults."), 3000);
}

void MainWindow::onBrowseRecordingDirectoryClicked()
{
    const QString currentPath = mRecordingDirectoryLineEdit != nullptr
        ? mRecordingDirectoryLineEdit->text().trimmed()
        : QString();
    const QString selectedPath = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select Recording Folder"),
        currentPath.isEmpty() ? defaultRecordingDirectory() : currentPath
    );
    if (!selectedPath.isEmpty() && mRecordingDirectoryLineEdit != nullptr) {
        mRecordingDirectoryLineEdit->setText(selectedPath);
    }
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
}

void MainWindow::onVideoReceiverMessage(const QString& message)
{
    ui->statusbar->showMessage(message, 3000);
}

void MainWindow::onVideoReceiverError(const QString& message)
{
    ui->statusbar->showMessage(message, 5000);
}
