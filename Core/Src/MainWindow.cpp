#include "MainWindow.h"
#include "./ui_MainWindow.h"

#include "GstVideoReceiver.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QtMultimedia/QVideoSink>
#include <QtMultimediaWidgets/QVideoWidget>

MainWindow::MainWindow(QWidget *parent, bool startVideoReceiver)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->videoFrameLabel->setStyleSheet(QStringLiteral("background-color: black; color: white;"));
    ui->videoFrameLabel->setScaledContents(false);
    ui->videoFrameLabel->setAlignment(Qt::AlignCenter);
    setupVideoSettingsUi();

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
    connect(ui->applyVideoSettingsButton, &QPushButton::clicked, this, &MainWindow::applyVideoSettings);
    connect(
        ui->videoContainerComboBox,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::onVideoContainerChanged
    );
    onVideoContainerChanged(ui->videoContainerComboBox->currentIndex());
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

    GstVideoReceiver::StreamSettings settings;
    settings.transport = ui->videoContainerComboBox->currentIndex() == 1
        ? GstVideoReceiver::Transport::UdpMpegTs
        : GstVideoReceiver::Transport::UdpRtp;
    settings.codec = ui->videoCodecComboBox->currentIndex() == 1
        ? GstVideoReceiver::Codec::H265
        : GstVideoReceiver::Codec::H264;
    settings.udpHost = ui->videoAddressLineEdit->text().trimmed();
    if (settings.udpHost.isEmpty()) {
        settings.udpHost = QStringLiteral("0.0.0.0");
        ui->videoAddressLineEdit->setText(settings.udpHost);
    }
    settings.udpPort = static_cast<quint16>(ui->videoPortSpinBox->value());
    settings.lowLatency = ui->lowLatencyCheckBox->isChecked();

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
}

void MainWindow::onVideoContainerChanged(int index)
{
    const bool rtpSelected = index == 0;
    ui->videoCodecComboBox->setEnabled(rtpSelected);
    ui->videoCodecLabel->setEnabled(rtpSelected);
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
