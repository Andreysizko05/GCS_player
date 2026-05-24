#ifndef VIDEOSETTINGSCONFIG_H
#define VIDEOSETTINGSCONFIG_H

#include "GstVideoReceiver.h"

#include <QString>

class VideoSettingsConfig
{
public:
    struct Settings {
        GstVideoReceiver::Transport transport = GstVideoReceiver::Transport::UdpRtp;
        GstVideoReceiver::Codec codec = GstVideoReceiver::Codec::H264;
        QString bindAddress = QStringLiteral("0.0.0.0");
        quint16 port = 5600;
        bool lowLatency = false;
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
