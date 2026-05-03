#include "MainWindow.h"
#include "./ui_MainWindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
}

MainWindow::~MainWindow()
{

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

    ui->videoFrameLabel->setGeometry(0, 0, frameSize.width(), frameSize.height());

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

	targetRect.getCoords(&mVideoAreaLeft, &mVideoAreaTop, &mVideoAreaRight, &mVideoAreaBottom);
}
