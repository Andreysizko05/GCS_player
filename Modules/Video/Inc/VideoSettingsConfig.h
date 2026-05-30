#ifndef VIDEOSETTINGSCONFIG_H
#define VIDEOSETTINGSCONFIG_H

#include "GstVideoReceiver.h"
#include "UsbCameraManager.h"

#include <QMap>
#include <QString>

class VideoSettingsConfig
{
public:
    struct Settings {
        GstVideoReceiver::Transport transport = GstVideoReceiver::Transport::UdpRtp;
        GstVideoReceiver::Codec codec = GstVideoReceiver::Codec::H264;
        QString bindAddress = QStringLiteral("0.0.0.0");
        quint16 port = 5600;
        QString streamUrl;
        QString customPipeline;
        bool lowLatency = false;
        QString usbDeviceId;
        QString usbDeviceName;
        int usbDeviceIndex = -1;
        QString usbModeCaps;
        QMap<QString, UsbCameraControlState> usbControls;
    };

    struct LoadResult {
        Settings settings;
        bool ok = true;
        bool created = false;
        QString errorMessage;
    };

    VideoSettingsConfig();
    explicit VideoSettingsConfig(const QString& configPath);

    QString configPath() const;
    LoadResult loadOrCreate() const;
    bool save(const Settings& settings, QString* errorMessage = nullptr) const;

    static QString defaultConfigPath();

private:
    QString mConfigPath;
};

#endif // VIDEOSETTINGSCONFIG_H
