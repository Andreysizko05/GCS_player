#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "UsbCameraManager.h"
#include "VideoSettingsConfig.h"

#include <QHash>
#include <QMainWindow>
#include <QMap>
#include <QSize>
#include <QtMultimedia/QVideoFrame>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class GstVideoReceiver;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QVideoWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr, bool startVideoReceiver = true);
    ~MainWindow();

private:
    void setupVideoSettingsUi();
    void loadVideoSettings();
    void applyVideoSettingsToWidgets(const VideoSettingsConfig::Settings& settings);
    VideoSettingsConfig::Settings currentVideoSettings() const;
    bool saveVideoSettings() const;
    void ensureVideoWidget();
    void restartVideoReceiver();
    void setupCustomPipelineUi();
    void setupUsbSettingsUi();
    void setupRecordingSettingsUi();
    void refreshUsbDevices(const QString& preferredDeviceId = QString());
    void refreshUsbModes(const QString& preferredModeCaps = QString());
    void refreshUsbControls(const QMap<QString, UsbCameraControlState>& preferredStates = {});
    void clearUsbControls();
    QString selectedUsbDeviceId() const;
    QString selectedUsbDeviceName() const;
    int selectedUsbDeviceIndex() const;
    QString selectedUsbModeCaps() const;
    QMap<QString, UsbCameraControlState> usbControlStatesFromUi() const;
    void applyUsbControl(const QString& controlId);

    Ui::MainWindow *ui;

private slots:
    void applyVideoSettings();
    void onVideoContainerChanged(int index);
    void onUsbCameraChanged(int index);
    void onRefreshUsbDevicesClicked();
    void onResetVideoSettingsClicked();
    void onBrowseRecordingDirectoryClicked();
    void onVideoFrameReady(const QVideoFrame& frame);
    void onVideoSizeChanged(const QSize& size);
    void onVideoReceiverMessage(const QString& message);
    void onVideoReceiverError(const QString& message);

private:
    struct UsbControlWidgets {
        UsbCameraControl control;
        QSlider* slider = nullptr;
        QSpinBox* spinBox = nullptr;
        QCheckBox* autoCheckBox = nullptr;
    };

    GstVideoReceiver* mVideoReceiver = nullptr;
    QVideoWidget* mVideoWidget = nullptr;
    QComboBox* mUsbCameraComboBox = nullptr;
    QComboBox* mUsbModeComboBox = nullptr;
    QCheckBox* mRecordingEnabledCheckBox = nullptr;
    QComboBox* mRecordingContainerComboBox = nullptr;
    QLineEdit* mRecordingDirectoryLineEdit = nullptr;
    QPushButton* mBrowseRecordingDirectoryButton = nullptr;
    QPlainTextEdit* mCustomPipelineTextEdit = nullptr;
    QPushButton* mRefreshUsbDevicesButton = nullptr;
    QPushButton* mResetVideoSettingsButton = nullptr;
    QGroupBox* mUsbControlsGroupBox = nullptr;
    QFormLayout* mUsbControlsLayout = nullptr;
    QHash<QString, UsbControlWidgets> mUsbControlWidgets;
    bool mUpdatingUsbUi = false;

    QSize mVideoSize;
};
#endif // MAINWINDOW_H
