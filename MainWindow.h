#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    Ui::MainWindow *ui;

    int mVideoAreaLeft = 0;
    int mVideoAreaTop = 0;
    int mVideoAreaRight = 0;
    int mVideoAreaBottom = 0;
    float mFrameWidthFactor = 0.995f;
    float mFrameHeightFactor = 0.945f;
};
#endif // MAINWINDOW_H
