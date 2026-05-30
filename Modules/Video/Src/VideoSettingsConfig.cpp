#include "VideoSettingsConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStandardPaths>

namespace
{
QString transportName(GstVideoReceiver::Transport transport)
{
    switch (transport) {
    case GstVideoReceiver::Transport::UsbCamera:
        return QStringLiteral("usb-camera");
    case GstVideoReceiver::Transport::CustomPipeline:
        return QStringLiteral("custom-pipeline");
    case GstVideoReceiver::Transport::TcpMpegTs:
        return QStringLiteral("tcp-mpeg-ts");
    case GstVideoReceiver::Transport::Rtsp:
        return QStringLiteral("rtsp");
    case GstVideoReceiver::Transport::UdpMpegTs:
        return QStringLiteral("udp-mpeg-ts");
    case GstVideoReceiver::Transport::UdpRtp:
        return QStringLiteral("udp-rtp");
    }

    return QStringLiteral("udp-rtp");
}

GstVideoReceiver::Transport transportFromName(const QString& name)
{
    if (name.compare(QStringLiteral("usb-camera"), Qt::CaseInsensitive) == 0) {
        return GstVideoReceiver::Transport::UsbCamera;
    }
    if (name.compare(QStringLiteral("custom-pipeline"), Qt::CaseInsensitive) == 0) {
        return GstVideoReceiver::Transport::CustomPipeline;
    }
    if (name.compare(QStringLiteral("tcp-mpeg-ts"), Qt::CaseInsensitive) == 0) {
        return GstVideoReceiver::Transport::TcpMpegTs;
    }
    if (name.compare(QStringLiteral("rtsp"), Qt::CaseInsensitive) == 0) {
        return GstVideoReceiver::Transport::Rtsp;
    }
    if (name.compare(QStringLiteral("udp-mpeg-ts"), Qt::CaseInsensitive) == 0) {
        return GstVideoReceiver::Transport::UdpMpegTs;
    }

    return GstVideoReceiver::Transport::UdpRtp;
}

QString codecName(GstVideoReceiver::Codec codec)
{
    switch (codec) {
    case GstVideoReceiver::Codec::H265:
        return QStringLiteral("h265");
    case GstVideoReceiver::Codec::H264:
        return QStringLiteral("h264");
    }

    return QStringLiteral("h264");
}

GstVideoReceiver::Codec codecFromName(const QString& name)
{
    return name.compare(QStringLiteral("h265"), Qt::CaseInsensitive) == 0
        ? GstVideoReceiver::Codec::H265
        : GstVideoReceiver::Codec::H264;
}

quint16 portFromValue(const QJsonValue& value, quint16 fallback)
{
    if (!value.isDouble()) {
        return fallback;
    }

    const int port = value.toInt(fallback);
    if (port < 1 || port > 65535) {
        return fallback;
    }

    return static_cast<quint16>(port);
}
} // namespace

VideoSettingsConfig::VideoSettingsConfig()
    : VideoSettingsConfig(defaultConfigPath())
{
}

VideoSettingsConfig::VideoSettingsConfig(const QString& configPath)
    : mConfigPath(configPath)
{
}

QString VideoSettingsConfig::configPath() const
{
    return mConfigPath;
}

VideoSettingsConfig::LoadResult VideoSettingsConfig::loadOrCreate() const
{
    LoadResult result;

    if (!QFile::exists(mConfigPath)) {
        QString errorMessage;
        if (!save(result.settings, &errorMessage)) {
            result.ok = false;
            result.errorMessage = errorMessage;
        } else {
            result.created = true;
        }

        return result;
    }

    QFile configFile(mConfigPath);
    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.ok = false;
        result.errorMessage = configFile.errorString();
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument configDocument = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.ok = false;
        result.errorMessage = parseError.errorString();
        return result;
    }
    if (!configDocument.isObject()) {
        result.ok = false;
        result.errorMessage = QStringLiteral("Config root must be a JSON object.");
        return result;
    }

    const QJsonObject config = configDocument.object();
    result.settings.transport = transportFromName(config.value(QStringLiteral("container")).toString());
    result.settings.codec = codecFromName(config.value(QStringLiteral("codec")).toString());

    const QString bindAddress = config.value(QStringLiteral("bindAddress")).toString().trimmed();
    if (!bindAddress.isEmpty()) {
        result.settings.bindAddress = bindAddress;
    }

    result.settings.port = portFromValue(config.value(QStringLiteral("port")), result.settings.port);
    result.settings.streamUrl = config.value(QStringLiteral("streamUrl")).toString().trimmed();
    result.settings.customPipeline = config.value(QStringLiteral("customPipeline")).toString().trimmed();

    const QJsonValue lowLatencyValue = config.value(QStringLiteral("lowLatency"));
    if (lowLatencyValue.isBool()) {
        result.settings.lowLatency = lowLatencyValue.toBool();
    }

    result.settings.usbDeviceId = config.value(QStringLiteral("usbDeviceId")).toString();
    result.settings.usbDeviceName = config.value(QStringLiteral("usbDeviceName")).toString();
    result.settings.usbDeviceIndex = config.value(QStringLiteral("usbDeviceIndex")).toInt(-1);
    result.settings.usbModeCaps = config.value(QStringLiteral("usbModeCaps")).toString();
    const QJsonObject usbControls = config.value(QStringLiteral("usbControls")).toObject();
    for (auto it = usbControls.begin(); it != usbControls.end(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }

        const QJsonObject controlObject = it.value().toObject();
        UsbCameraControlState state;
        state.value = controlObject.value(QStringLiteral("value")).toInt();
        state.automatic = controlObject.value(QStringLiteral("automatic")).toBool();
        result.settings.usbControls.insert(it.key(), state);
    }

    return result;
}

bool VideoSettingsConfig::save(const Settings& settings, QString* errorMessage) const
{
    const QFileInfo configFileInfo(mConfigPath);
    QDir configDir(configFileInfo.absolutePath());
    if (!configDir.exists() && !configDir.mkpath(QStringLiteral("."))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Unable to create config directory.");
        }
        return false;
    }

    QJsonObject config;
    config.insert(QStringLiteral("container"), transportName(settings.transport));
    config.insert(QStringLiteral("codec"), codecName(settings.codec));
    config.insert(QStringLiteral("bindAddress"), settings.bindAddress.trimmed());
    config.insert(QStringLiteral("port"), settings.port);
    config.insert(QStringLiteral("streamUrl"), settings.streamUrl.trimmed());
    config.insert(QStringLiteral("customPipeline"), settings.customPipeline.trimmed());
    config.insert(QStringLiteral("lowLatency"), settings.lowLatency);
    config.insert(QStringLiteral("usbDeviceId"), settings.usbDeviceId);
    config.insert(QStringLiteral("usbDeviceName"), settings.usbDeviceName);
    config.insert(QStringLiteral("usbDeviceIndex"), settings.usbDeviceIndex);
    config.insert(QStringLiteral("usbModeCaps"), settings.usbModeCaps);

    QJsonObject usbControls;
    for (auto it = settings.usbControls.cbegin(); it != settings.usbControls.cend(); ++it) {
        QJsonObject controlObject;
        controlObject.insert(QStringLiteral("value"), it.value().value);
        controlObject.insert(QStringLiteral("automatic"), it.value().automatic);
        usbControls.insert(it.key(), controlObject);
    }
    config.insert(QStringLiteral("usbControls"), usbControls);

    QFile configFile(mConfigPath);
    if (!configFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = configFile.errorString();
        }
        return false;
    }

    const QByteArray data = QJsonDocument(config).toJson(QJsonDocument::Compact);
    if (configFile.write(data) != data.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = configFile.errorString();
        }
        return false;
    }

    configFile.close();
    if (configFile.error() != QFile::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = configFile.errorString();
        }
        return false;
    }

    return true;
}

QString VideoSettingsConfig::defaultConfigPath()
{
    QString configDirPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configDirPath.isEmpty()) {
        configDirPath = QCoreApplication::applicationDirPath();
    }

    return QDir(configDirPath).filePath(QStringLiteral("video-settings.local.json"));
}
