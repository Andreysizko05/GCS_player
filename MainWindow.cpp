#include "MainWindow.h"
#include "./ui_MainWindow.h"

#ifdef GCS_ENABLE_GSTREAMER
#include "GstVideoReceiver.h"

#include <QtMultimedia/QVideoSink>
#include <QtMultimediaWidgets/QVideoWidget>
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->videoFrameLabel->setStyleSheet(QStringLiteral("background-color: black; color: white;"));
    ui->videoFrameLabel->setScaledContents(false);
    ui->videoFrameLabel->setAlignment(Qt::AlignCenter);

#ifdef GCS_ENABLE_GSTREAMER
    mVideoWidget = new QVideoWidget(ui->centralwidget);
    mVideoWidget->setObjectName(QStringLiteral("videoFrameSinkWidget"));
    mVideoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    mVideoWidget->setStyleSheet(QStringLiteral("background-color: black;"));
    mVideoWidget->setGeometry(ui->videoFrameLabel->geometry());
    mVideoWidget->show();
    ui->videoFrameLabel->hide();

    mVideoReceiver = new GstVideoReceiver(this);
    connect(mVideoReceiver, &GstVideoReceiver::frameReady, this, &MainWindow::onVideoFrameReady);
    connect(mVideoReceiver, &GstVideoReceiver::receiverMessage, this, &MainWindow::onVideoReceiverMessage);
    connect(mVideoReceiver, &GstVideoReceiver::receiverError, this, &MainWindow::onVideoReceiverError);
    mVideoReceiver->start();
#else
    ui->videoFrameLabel->setText(QStringLiteral("GStreamer support is disabled.\nUse a *-gstreamer preset to enable video."));
#endif
}

MainWindow::~MainWindow()
{
#ifdef GCS_ENABLE_GSTREAMER
    if (mVideoReceiver != nullptr)
    {
        mVideoReceiver->stop();
        mVideoReceiver->wait();
    }
#endif

    delete ui;
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

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
    constexpr double targetAspectRatio = 16.0 / 9.0;

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

QWidget* MainWindow::videoSurfaceWidget() const
{
#ifdef GCS_ENABLE_GSTREAMER
    if (mVideoWidget != nullptr) {
        return mVideoWidget;
    }
#endif
    return ui->videoFrameLabel;
}

#ifdef GCS_ENABLE_GSTREAMER
void MainWindow::onVideoFrameReady(const QVideoFrame& frame)
{
    if (mVideoWidget == nullptr || mVideoWidget->videoSink() == nullptr || !frame.isValid()) {
        return;
    }

    mVideoWidget->videoSink()->setVideoFrame(frame);
}

void MainWindow::onVideoReceiverMessage(const QString& message)
{
    ui->statusbar->showMessage(message, 3000);
}

void MainWindow::onVideoReceiverError(const QString& message)
{
    ui->statusbar->showMessage(message, 5000);
}
#endif
