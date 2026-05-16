#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSize>
#include <QtMultimedia/QVideoFrame>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class GstVideoReceiver;
class QVideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool startVideoReceiver = true);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupVideoSettingsUi();
    void ensureVideoWidget();
    void restartVideoReceiver();
    void updateVideoSurfaceGeometry();
    double targetVideoAspectRatio() const;
    QWidget* videoSurfaceWidget() const;

    Ui::MainWindow *ui;

private slots:
    void applyVideoSettings();
    void onVideoContainerChanged(int index);
    void onVideoFrameReady(const QVideoFrame& frame);
    void onVideoSizeChanged(const QSize& size);
    void onVideoReceiverMessage(const QString& message);
    void onVideoReceiverError(const QString& message);

private:
    GstVideoReceiver* mVideoReceiver = nullptr;
    QVideoWidget* mVideoWidget = nullptr;

    int mVideoAreaLeft = 0;
    int mVideoAreaTop = 0;
    int mVideoAreaRight = 0;
    int mVideoAreaBottom = 0;
    QSize mVideoSize;
    float mFrameWidthFactor = 0.995f;
    float mFrameHeightFactor = 0.945f;
};
#endif // MAINWINDOW_H
