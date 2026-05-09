#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#ifdef GCS_ENABLE_GSTREAMER
#include <QtMultimedia/QVideoFrame>
#endif

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

#ifdef GCS_ENABLE_GSTREAMER
class GstVideoReceiver;
class QVideoWidget;
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QWidget* videoSurfaceWidget() const;

    Ui::MainWindow *ui;

#ifdef GCS_ENABLE_GSTREAMER
private slots:
    void onVideoFrameReady(const QVideoFrame& frame);
    void onVideoReceiverMessage(const QString& message);
    void onVideoReceiverError(const QString& message);
#endif

private:
#ifdef GCS_ENABLE_GSTREAMER
    GstVideoReceiver* mVideoReceiver = nullptr;
    QVideoWidget* mVideoWidget = nullptr;
#endif

    int mVideoAreaLeft = 0;
    int mVideoAreaTop = 0;
    int mVideoAreaRight = 0;
    int mVideoAreaBottom = 0;
    float mFrameWidthFactor = 0.995f;
    float mFrameHeightFactor = 0.945f;
};
#endif // MAINWINDOW_H
