#include "GstVideoReceiver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>
#include <QPainter>
#include <QStringList>

#include <gst/video/video-info.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace
{
constexpr int kPlaceholderWidth = 1280;
constexpr int kPlaceholderHeight = 720;

bool elementHasProperty(GstElement* element, const char* propertyName)
{
    return element != nullptr
        && propertyName != nullptr
        && g_object_class_find_property(G_OBJECT_GET_CLASS(element), propertyName) != nullptr;
}

QString transportName(GstVideoReceiver::Transport transport)
{
    switch (transport) {
    case GstVideoReceiver::Transport::UsbCamera:
        return QStringLiteral("USB camera");
    case GstVideoReceiver::Transport::UdpRtp:
        return QStringLiteral("UDP/RTP");
    case GstVideoReceiver::Transport::UdpMpegTs:
        return QStringLiteral("UDP/MPEG-TS");
    }

    return QStringLiteral("UDP");
}

bool setElementStringProperty(GstElement* element, const char* propertyName, const QString& value)
{
    if (value.isEmpty() || !elementHasProperty(element, propertyName)) {
        return false;
    }

    const QByteArray bytes = value.toUtf8();
    g_object_set(element, propertyName, bytes.constData(), nullptr);
    return true;
}

bool setElementIntProperty(GstElement* element, const char* propertyName, int value)
{
    if (!elementHasProperty(element, propertyName)) {
        return false;
    }

    g_object_set(element, propertyName, value, nullptr);
    return true;
}

void configureUsbSourceElement(
    GstElement* source,
    const GstVideoReceiver::StreamSettings& settings)
{
    if (source == nullptr) {
        return;
    }

    if (settings.usbDeviceIndex >= 0 && setElementIntProperty(source, "device-index", settings.usbDeviceIndex)) {
        // Some capture backends (Media Foundation, AVFoundation) select cameras
        // by index instead of a device path.
    } else if (!settings.usbDeviceId.isEmpty()) {
        if (!setElementStringProperty(source, "device", settings.usbDeviceId)
            && !setElementStringProperty(source, "device-path", settings.usbDeviceId)) {
            setElementStringProperty(source, "device-name", settings.usbDeviceName);
        }
    } else if (!settings.usbDeviceName.isEmpty()) {
        setElementStringProperty(source, "device-name", settings.usbDeviceName);
    }

    if (elementHasProperty(source, "do-timestamp")) {
        g_object_set(source, "do-timestamp", TRUE, nullptr);
    }
}

QString codecName(GstVideoReceiver::Codec codec)
{
    switch (codec) {
    case GstVideoReceiver::Codec::H264:
        return QStringLiteral("H.264");
    case GstVideoReceiver::Codec::H265:
        return QStringLiteral("H.265");
    }

    return QStringLiteral("video");
}

const char* rtpEncodingName(GstVideoReceiver::Codec codec)
{
    return codec == GstVideoReceiver::Codec::H265 ? "H265" : "H264";
}

const char* depayloaderFactory(GstVideoReceiver::Codec codec)
{
    return codec == GstVideoReceiver::Codec::H265 ? "rtph265depay" : "rtph264depay";
}

const char* parserFactory(GstVideoReceiver::Codec codec)
{
    return codec == GstVideoReceiver::Codec::H265 ? "h265parse" : "h264parse";
}

QVideoFrame imageToVideoFrame(const QImage& image)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    return QVideoFrame(source);
}

QString createMessageText(const QString& line)
{
    return line.isEmpty() ? QStringLiteral("Waiting for video stream...") : line;
}

QVideoFrame makePlaceholderFrame(const QString& text)
{
    QImage image(
        kPlaceholderWidth,
        kPlaceholderHeight,
        QImage::Format_RGB32
    );
    image.fill(QColor(10, 14, 18));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(238, 242, 245));

    QFont titleFont = painter.font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(
        QRect(0, 0, image.width(), image.height() / 2),
        Qt::AlignCenter,
        QStringLiteral("GStreamer Video Receiver")
    );

    QFont bodyFont = painter.font();
    bodyFont.setPointSize(14);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.drawText(
        QRect(80, image.height() / 2 - 20, image.width() - 160, 120),
        Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
        createMessageText(text)
    );

    return imageToVideoFrame(image);
}

void prependPath(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }

    QByteArray currentPath = qgetenv("PATH");
#ifdef Q_OS_WIN
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const QByteArray nativePath = QDir::toNativeSeparators(path).toUtf8();
    if (currentPath.isEmpty()) {
        qputenv("PATH", nativePath);
    } else {
        qputenv("PATH", nativePath + QByteArray(1, separator) + currentPath);
    }
}

void setEnvIfPathExists(const char* name, const QString& path)
{
    if (QFileInfo::exists(path)) {
        qputenv(name, QDir::toNativeSeparators(path).toUtf8());
    }
}

QString findFirstExistingPath(const QStringList& candidates)
{
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void prepareGStreamerEnvironment()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pluginDir = QDir(appDir).filePath(QStringLiteral("gstreamer-1.0"));
    const QString gioModulesDir = QDir(appDir).filePath(QStringLiteral("gio/modules"));
    const QString runtimeDir = QDir(appDir).filePath(QStringLiteral("gstreamer-runtime"));
    const QString toolsDir = QDir(appDir).filePath(QStringLiteral("gstreamer-tools"));
#ifdef Q_OS_WIN
    const QString scannerFileName = QStringLiteral("gst-plugin-scanner.exe");
#else
    const QString scannerFileName = QStringLiteral("gst-plugin-scanner");
#endif
    const QString scannerPath = findFirstExistingPath({
        QDir(toolsDir).filePath(scannerFileName),
        QDir(QDir(toolsDir).filePath(QStringLiteral("gstreamer-1.0"))).filePath(scannerFileName)
    });
    const bool hasBundledPluginDir = QFileInfo::exists(pluginDir);
    const bool hasBundledScanner = QFileInfo::exists(scannerPath);
    const bool useBundledRuntime = hasBundledPluginDir && hasBundledScanner;

    prependPath(appDir);

    if (useBundledRuntime) {
        prependPath(runtimeDir);
        prependPath(toolsDir);
        setEnvIfPathExists("GST_PLUGIN_PATH", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_PATH_1_0", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH", pluginDir);
        setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH_1_0", pluginDir);
        setEnvIfPathExists("GIO_EXTRA_MODULES", gioModulesDir);
        setEnvIfPathExists("GST_PLUGIN_SCANNER", scannerPath);
        setEnvIfPathExists("GST_PLUGIN_SCANNER_1_0", scannerPath);
    }

    qputenv("GST_REGISTRY_FORK", QByteArrayLiteral("no"));
    qputenv("GST_REGISTRY_REUSE_PLUGIN_SCANNER", QByteArrayLiteral("no"));
}

bool ensureGStreamerInitialized(QString& errorMessage)
{
    static std::once_flag initFlag;
    static bool initialized = false;
    static QString initError;

    std::call_once(initFlag, []() {
        prepareGStreamerEnvironment();

        GError* error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error)) {
            if (error != nullptr) {
                initError = QString::fromUtf8(error->message);
                g_error_free(error);
            } else {
                initError = QStringLiteral("Unknown GStreamer initialization error.");
            }
            return;
        }

        gst_debug_set_default_threshold(GST_LEVEL_WARNING);
        initialized = true;
    });

    errorMessage = initError;
    return initialized;
}
} // namespace

GstVideoReceiver::GstVideoReceiver(QObject* parent)
    : GstVideoReceiver(StreamSettings(), parent)
{
}

GstVideoReceiver::GstVideoReceiver(const StreamSettings& settings, QObject* parent)
    : QThread(parent)
    , m_settings(settings)
{
    qRegisterMetaType<QVideoFrame>("QVideoFrame");
}

GstVideoReceiver::~GstVideoReceiver()
{
    stop();
    wait();
}

void GstVideoReceiver::stop()
{
    m_stopRequested.store(true, std::memory_order_relaxed);
}

GstVideoReceiver::StreamSettings GstVideoReceiver::streamSettings() const
{
    return m_settings;
}

void GstVideoReceiver::run()
{
    QString initError;
    if (!ensureGStreamerInitialized(initError)) {
        const QString message = QStringLiteral("GStreamer init failed: %1").arg(initError);
        emit receiverError(message);
        emit frameReady(makePlaceholderFrame(message));
        return;
    }

    QString videoDescription;
    if (m_settings.transport == Transport::UsbCamera) {
        videoDescription = m_settings.usbDeviceName.isEmpty()
            ? QStringLiteral("USB camera")
            : QStringLiteral("USB camera %1").arg(m_settings.usbDeviceName);
    } else if (m_settings.transport == Transport::UdpMpegTs) {
        videoDescription = QStringLiteral("%1 video, codec auto-detected")
            .arg(transportName(m_settings.transport));
    } else {
        videoDescription = QStringLiteral("%1/%2 video")
            .arg(transportName(m_settings.transport), codecName(m_settings.codec));
    }

    emit frameReady(makePlaceholderFrame(
        m_settings.transport == Transport::UsbCamera
            ? QStringLiteral("Opening %1.").arg(videoDescription)
            : QStringLiteral("Listening on %1:%2 for %3.")
                .arg(m_settings.udpHost)
                .arg(m_settings.udpPort)
                .arg(videoDescription)
    ));

    while (!m_stopRequested.load(std::memory_order_relaxed)) {
        emit receiverMessage(QStringLiteral("Connecting to the video stream..."));

        if (!createPipeline()) {
            if (m_stopRequested.load(std::memory_order_relaxed)) {
                break;
            }

            emit frameReady(makePlaceholderFrame(
                QStringLiteral("Unable to create the GStreamer pipeline. Retrying...")
            ));
            msleep(static_cast<unsigned long>(m_settings.restartDelayMs));
            continue;
        }

        bool keepRunning = true;
        while (!m_stopRequested.load(std::memory_order_relaxed) && keepRunning) {
            keepRunning = processBusMessages();

            const qint64 lastFrameMs = m_lastFrameTimestampMs.load(std::memory_order_relaxed);
            if (lastFrameMs > 0) {
                const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - lastFrameMs;
                if (elapsedMs > m_settings.frameTimeoutMs) {
                    keepRunning = false;
                    emit receiverError(QStringLiteral(
                        "Video timeout: no frames received for %1 ms. Restarting pipeline."
                    ).arg(elapsedMs));
                }
            }
        }

        destroyPipeline();

        if (!m_stopRequested.load(std::memory_order_relaxed)) {
            emit frameReady(makePlaceholderFrame(
                QStringLiteral("Video stream lost. Reconnecting...")
            ));
            msleep(static_cast<unsigned long>(m_settings.restartDelayMs));
        }
    }

    destroyPipeline();
    emit receiverMessage(QStringLiteral("Video receiver stopped."));
}

bool GstVideoReceiver::createPipeline()
{
    destroyPipeline();

    m_seenFrame.store(false, std::memory_order_relaxed);
    m_lastFrameTimestampMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);
    m_lastFrameWidth.store(0, std::memory_order_relaxed);
    m_lastFrameHeight.store(0, std::memory_order_relaxed);

    const bool useRtp = m_settings.transport == Transport::UdpRtp;
    const bool useMpegTs = m_settings.transport == Transport::UdpMpegTs;
    const bool useUsb = m_settings.transport == Transport::UsbCamera;
    const QString usbModeCaps = m_settings.usbModeCaps.trimmed();
    const bool useUsbDecoder = useUsb
        && !usbModeCaps.isEmpty()
        && !usbModeCaps.startsWith(QStringLiteral("video/x-raw"), Qt::CaseInsensitive);

    m_pipeline = gst_pipeline_new("gcs-player-video-pipeline");
    if (useUsb) {
        const QByteArray sourceFactory = UsbCameraManager::sourceFactoryName().toUtf8();
        m_usbSource = gst_element_factory_make(sourceFactory.constData(), "usb-source");
        m_usbCapsFilter = gst_element_factory_make("capsfilter", "usb-caps-filter");
    } else {
        m_udpSource = gst_element_factory_make("udpsrc", "udp-source");
    }
    if (useRtp) {
        if (!m_settings.lowLatency) {
            m_jitterBuffer = gst_element_factory_make("rtpjitterbuffer", "rtp-jitter-buffer");
        }
        m_depayloader = gst_element_factory_make(depayloaderFactory(m_settings.codec), "rtp-depay");
        m_parser = gst_element_factory_make(parserFactory(m_settings.codec), "video-parser");
    } else if (useMpegTs) {
        m_tsDemux = gst_element_factory_make("tsdemux", "mpeg-ts-demux");
    }
    m_decodeQueue = gst_element_factory_make("queue", "decode-queue");
    if (!useUsb || useUsbDecoder) {
        m_decoder = gst_element_factory_make("decodebin3", "video-decoder");
        if (m_decoder == nullptr) {
            m_decoder = gst_element_factory_make("decodebin", "video-decoder");
        }
    }
    m_videoConvert = gst_element_factory_make("videoconvert", "video-convert");
    m_videoCapsFilter = gst_element_factory_make("capsfilter", "video-caps-filter");
    m_appSink = gst_element_factory_make("appsink", "preview-sink");

    QStringList missingElements;
    auto trackMissingElement = [&missingElements](GstElement* element, const QString& name) {
        if (element == nullptr) {
            missingElements.append(name);
        }
    };

    trackMissingElement(m_pipeline, QStringLiteral("pipeline"));
    if (useUsb) {
        trackMissingElement(m_usbSource, UsbCameraManager::sourceFactoryName());
        trackMissingElement(m_usbCapsFilter, QStringLiteral("capsfilter"));
    } else {
        trackMissingElement(m_udpSource, QStringLiteral("udpsrc"));
    }
    if (useRtp) {
        if (!m_settings.lowLatency) {
            trackMissingElement(m_jitterBuffer, QStringLiteral("rtpjitterbuffer"));
        }
        trackMissingElement(m_depayloader, QString::fromUtf8(depayloaderFactory(m_settings.codec)));
        trackMissingElement(m_parser, QString::fromUtf8(parserFactory(m_settings.codec)));
    } else if (useMpegTs) {
        trackMissingElement(m_tsDemux, QStringLiteral("tsdemux"));
    }
    trackMissingElement(m_decodeQueue, QStringLiteral("queue"));
    if (!useUsb || useUsbDecoder) {
        trackMissingElement(m_decoder, QStringLiteral("decodebin3/decodebin"));
    }
    trackMissingElement(m_videoConvert, QStringLiteral("videoconvert"));
    trackMissingElement(m_videoCapsFilter, QStringLiteral("capsfilter"));
    trackMissingElement(m_appSink, QStringLiteral("appsink"));

    if (!missingElements.isEmpty()) {
        emit receiverError(QStringLiteral("Missing GStreamer element(s): %1.")
            .arg(missingElements.join(QStringLiteral(", "))));
        destroyPipeline();
        return false;
    }

    GstCaps* udpCaps = nullptr;
    if (useRtp) {
        udpCaps = gst_caps_new_simple(
            "application/x-rtp",
            "media", G_TYPE_STRING, "video",
            "encoding-name", G_TYPE_STRING, rtpEncodingName(m_settings.codec),
            "payload", G_TYPE_INT, m_settings.rtpPayload,
            "clock-rate", G_TYPE_INT, m_settings.rtpClockRate,
            nullptr
        );
    }

    const QByteArray appSinkFormat = m_settings.appSinkFormat.toUtf8();
    GstCaps* sinkCaps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, appSinkFormat.constData(),
        nullptr
    );

    const QByteArray udpHost = m_settings.udpHost.toUtf8();
    if (m_udpSource != nullptr) {
        g_object_set(
            m_udpSource,
            "address", udpHost.constData(),
            "port", static_cast<int>(m_settings.udpPort),
            nullptr
        );
    }
    if (udpCaps != nullptr) {
        g_object_set(m_udpSource, "caps", udpCaps, nullptr);
    }
    if (m_usbSource != nullptr) {
        configureUsbSourceElement(m_usbSource, m_settings);
    }
    if (m_usbCapsFilter != nullptr && !usbModeCaps.isEmpty()) {
        const QByteArray modeCapsText = usbModeCaps.toUtf8();
        GstCaps* usbCaps = gst_caps_from_string(modeCapsText.constData());
        if (usbCaps == nullptr) {
            emit receiverError(QStringLiteral("Invalid USB camera mode caps: %1.")
                .arg(m_settings.usbModeCaps));
            destroyPipeline();
            return false;
        }
        g_object_set(m_usbCapsFilter, "caps", usbCaps, nullptr);
        gst_caps_unref(usbCaps);
    }
    if (m_settings.transport == Transport::UsbCamera && !m_settings.usbControls.isEmpty()) {
        for (auto it = m_settings.usbControls.cbegin(); it != m_settings.usbControls.cend(); ++it) {
            QString controlError;
            if (!UsbCameraManager::setControl(m_settings.usbDeviceId, it.key(), it.value(), &controlError)) {
                emit receiverMessage(controlError);
            }
        }
    }
    if (m_jitterBuffer != nullptr) {
        g_object_set(
            m_jitterBuffer,
            "latency", m_settings.jitterLatencyMs,
            "drop-on-latency", TRUE,
            nullptr
        );
    }
    if (m_parser != nullptr) {
        g_object_set(m_parser, "config-interval", 1, nullptr);
    }
    if (m_tsDemux != nullptr && m_settings.lowLatency && elementHasProperty(m_tsDemux, "latency")) {
        g_object_set(m_tsDemux, "latency", std::max(0, m_settings.mpegTsLowLatencyMs), nullptr);
    }
    if (m_decodeQueue != nullptr) {
        g_object_set(
            m_decodeQueue,
            "max-size-buffers", 2,
            "max-size-bytes", 0,
            "max-size-time", 0,
            "leaky", 2,
            nullptr
        );
    }
    g_object_set(
        m_appSink,
        "emit-signals", TRUE,
        "max-buffers", m_settings.appSinkMaxBuffers,
        "drop", TRUE,
        "sync", FALSE,
        "enable-last-sample", FALSE,
        nullptr
    );
    g_object_set(m_videoCapsFilter, "caps", sinkCaps, nullptr);

    if (udpCaps != nullptr) {
        gst_caps_unref(udpCaps);
    }
    gst_caps_unref(sinkCaps);

    if (m_tsDemux != nullptr) {
        g_signal_connect(m_tsDemux, "pad-added", G_CALLBACK(onTsDemuxPadAdded), this);
    }
    if (m_decoder != nullptr) {
        g_signal_connect(m_decoder, "pad-added", G_CALLBACK(onDecoderPadAdded), this);
    }
    g_signal_connect(m_appSink, "new-sample", G_CALLBACK(onNewSample), this);

    if (useRtp) {
        gst_bin_add_many(
            GST_BIN(m_pipeline),
            m_udpSource,
            m_depayloader,
            m_parser,
            m_decodeQueue,
            m_decoder,
            m_videoConvert,
            m_videoCapsFilter,
            m_appSink,
            nullptr
        );
        if (m_jitterBuffer != nullptr) {
            gst_bin_add(GST_BIN(m_pipeline), m_jitterBuffer);
        }

        const gboolean receiveChainLinked = m_jitterBuffer != nullptr
            ? gst_element_link_many(
                m_udpSource,
                m_jitterBuffer,
                m_depayloader,
                m_parser,
                m_decodeQueue,
                m_decoder,
                nullptr)
            : gst_element_link_many(
                m_udpSource,
                m_depayloader,
                m_parser,
                m_decodeQueue,
                m_decoder,
                nullptr);

        if (!receiveChainLinked) {
            emit receiverError(QStringLiteral("Unable to link the RTP receive chain."));
            destroyPipeline();
            return false;
        }
    } else if (useMpegTs) {
        gst_bin_add_many(
            GST_BIN(m_pipeline),
            m_udpSource,
            m_tsDemux,
            m_decodeQueue,
            m_decoder,
            m_videoConvert,
            m_videoCapsFilter,
            m_appSink,
            nullptr
        );

        if (!gst_element_link(m_udpSource, m_tsDemux)
            || !gst_element_link_many(m_decodeQueue, m_decoder, nullptr))
        {
            emit receiverError(QStringLiteral("Unable to link the MPEG-TS receive chain."));
            destroyPipeline();
            return false;
        }
    } else if (useUsb) {
        if (useUsbDecoder) {
            gst_bin_add_many(
                GST_BIN(m_pipeline),
                m_usbSource,
                m_usbCapsFilter,
                m_decodeQueue,
                m_decoder,
                m_videoConvert,
                m_videoCapsFilter,
                m_appSink,
                nullptr
            );

            if (!gst_element_link_many(m_usbSource, m_usbCapsFilter, m_decodeQueue, m_decoder, nullptr)) {
                emit receiverError(QStringLiteral("Unable to link the USB camera compressed receive chain."));
                destroyPipeline();
                return false;
            }
        } else {
            gst_bin_add_many(
                GST_BIN(m_pipeline),
                m_usbSource,
                m_usbCapsFilter,
                m_decodeQueue,
                m_videoConvert,
                m_videoCapsFilter,
                m_appSink,
                nullptr
            );

            if (!gst_element_link_many(m_usbSource, m_usbCapsFilter, m_decodeQueue, m_videoConvert, nullptr)) {
                emit receiverError(QStringLiteral("Unable to link the USB camera raw receive chain."));
                destroyPipeline();
                return false;
            }
        }
    }

    if (!gst_element_link_many(m_videoConvert, m_videoCapsFilter, m_appSink, nullptr)) {
        emit receiverError(QStringLiteral("Unable to link the video preview chain."));
        destroyPipeline();
        return false;
    }

    m_bus = gst_element_get_bus(m_pipeline);
    if (m_bus == nullptr) {
        emit receiverError(QStringLiteral("Unable to acquire the GStreamer bus."));
        destroyPipeline();
        return false;
    }

    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        emit receiverError(QStringLiteral("Unable to start the GStreamer pipeline."));
        destroyPipeline();
        return false;
    }

    return true;
}

void GstVideoReceiver::destroyPipeline()
{
    if (m_pipeline != nullptr) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }

    if (m_bus != nullptr) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    if (m_pipeline != nullptr) {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    m_udpSource = nullptr;
    m_usbSource = nullptr;
    m_usbCapsFilter = nullptr;
    m_jitterBuffer = nullptr;
    m_depayloader = nullptr;
    m_parser = nullptr;
    m_tsDemux = nullptr;
    m_decodeQueue = nullptr;
    m_decoder = nullptr;
    m_videoConvert = nullptr;
    m_videoCapsFilter = nullptr;
    m_appSink = nullptr;
}

bool GstVideoReceiver::processBusMessages()
{
    if (m_bus == nullptr) {
        return false;
    }

    GstMessage* message = gst_bus_timed_pop_filtered(
        m_bus,
        50 * GST_MSECOND,
        static_cast<GstMessageType>(
            GST_MESSAGE_ERROR |
            GST_MESSAGE_EOS |
            GST_MESSAGE_WARNING |
            GST_MESSAGE_STATE_CHANGED
        )
    );

    if (message == nullptr) {
        return true;
    }

    bool shouldContinue = true;

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
    {
        GError* error = nullptr;
        gchar* debugInfo = nullptr;
        gst_message_parse_error(message, &error, &debugInfo);

        const QString errorText = error != nullptr
            ? QString::fromUtf8(error->message)
            : QStringLiteral("Unknown GStreamer error.");
        emit receiverError(QStringLiteral("GStreamer error: %1").arg(errorText));

        if (error != nullptr) {
            g_error_free(error);
        }
        if (debugInfo != nullptr) {
            g_free(debugInfo);
        }

        shouldContinue = false;
        break;
    }
    case GST_MESSAGE_EOS:
        emit receiverMessage(QStringLiteral("End of stream received. Restarting pipeline."));
        shouldContinue = false;
        break;
    case GST_MESSAGE_WARNING:
    {
        GError* warning = nullptr;
        gchar* debugInfo = nullptr;
        gst_message_parse_warning(message, &warning, &debugInfo);

        if (warning != nullptr) {
            emit receiverMessage(QStringLiteral("GStreamer warning: %1").arg(QString::fromUtf8(warning->message)));
            g_error_free(warning);
        }
        if (debugInfo != nullptr) {
            g_free(debugInfo);
        }
        break;
    }
    case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
            GstState oldState = GST_STATE_NULL;
            GstState newState = GST_STATE_NULL;
            GstState pendingState = GST_STATE_NULL;
            gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
            Q_UNUSED(oldState)
            Q_UNUSED(pendingState)
            if (newState == GST_STATE_PLAYING) {
                emit receiverMessage(QStringLiteral("Video pipeline is running."));
            }
        }
        break;
    default:
        break;
    }

    gst_message_unref(message);
    return shouldContinue;
}

GstFlowReturn GstVideoReceiver::processSample(GstAppSink* sink)
{
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (sample == nullptr) {
        return GST_FLOW_OK;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (caps == nullptr || buffer == nullptr) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstVideoInfo videoInfo;
    if (!gst_video_info_from_caps(&videoInfo, caps)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstMapInfo mapInfo;
    if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    const int width = static_cast<int>(GST_VIDEO_INFO_WIDTH(&videoInfo));
    const int height = static_cast<int>(GST_VIDEO_INFO_HEIGHT(&videoInfo));
    const int stride = GST_VIDEO_INFO_PLANE_STRIDE(&videoInfo, 0);

    if (width <= 0 || height <= 0 || stride <= 0) {
        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    QImage image(width, height, QImage::Format_ARGB32);
    if (image.isNull()) {
        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    const int destinationStride = image.bytesPerLine();
    const int rowBytes = std::min(destinationStride, stride);
    const auto* source = reinterpret_cast<const uchar*>(mapInfo.data);
    uchar* destination = image.bits();

    if (rowBytes <= 0 || mapInfo.size < static_cast<gsize>((height - 1) * stride + rowBytes)) {
        gst_buffer_unmap(buffer, &mapInfo);
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    for (int y = 0; y < height; ++y) {
        std::memcpy(destination + y * destinationStride, source + y * stride, rowBytes);
    }

    gst_buffer_unmap(buffer, &mapInfo);
    gst_sample_unref(sample);

    QVideoFrame frame(image);
    if (!frame.isValid()) {
        return GST_FLOW_OK;
    }

    m_lastFrameTimestampMs.store(QDateTime::currentMSecsSinceEpoch(), std::memory_order_relaxed);

    if (!m_seenFrame.exchange(true, std::memory_order_relaxed)) {
        emit receiverMessage(QStringLiteral("Video stream is active."));
    }

    const int previousWidth = m_lastFrameWidth.exchange(width, std::memory_order_relaxed);
    const int previousHeight = m_lastFrameHeight.exchange(height, std::memory_order_relaxed);
    if (previousWidth != width || previousHeight != height) {
        emit videoSizeChanged(QSize(width, height));
    }

    emit frameReady(frame);
    return GST_FLOW_OK;
}

GstFlowReturn GstVideoReceiver::onNewSample(GstAppSink* sink, gpointer userData)
{
    auto* self = static_cast<GstVideoReceiver*>(userData);
    return self != nullptr ? self->processSample(sink) : GST_FLOW_ERROR;
}

void GstVideoReceiver::onTsDemuxPadAdded(GstElement* src, GstPad* newPad, gpointer userData)
{
    Q_UNUSED(src)

    auto* self = static_cast<GstVideoReceiver*>(userData);
    if (self == nullptr || self->m_decodeQueue == nullptr) {
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(newPad);
    if (caps == nullptr) {
        caps = gst_pad_query_caps(newPad, nullptr);
    }
    if (caps == nullptr) {
        return;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* name = structure != nullptr ? gst_structure_get_name(structure) : nullptr;
    const bool isVideoPad = name != nullptr && g_str_has_prefix(name, "video/");
    gst_caps_unref(caps);

    if (!isVideoPad) {
        return;
    }

    GstPad* sinkPad = gst_element_get_static_pad(self->m_decodeQueue, "sink");
    if (sinkPad == nullptr) {
        return;
    }

    if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref(sinkPad);
        return;
    }

    if (gst_pad_link(newPad, sinkPad) != GST_PAD_LINK_OK) {
        emit self->receiverError(QStringLiteral("Unable to link the MPEG-TS video stream to the decoder."));
    }

    gst_object_unref(sinkPad);
}

void GstVideoReceiver::onDecoderPadAdded(GstElement* src, GstPad* newPad, gpointer userData)
{
    Q_UNUSED(src)

    auto* self = static_cast<GstVideoReceiver*>(userData);
    if (self == nullptr || self->m_videoConvert == nullptr) {
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(newPad);
    if (caps == nullptr) {
        caps = gst_pad_query_caps(newPad, nullptr);
    }
    if (caps == nullptr) {
        return;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* name = structure != nullptr ? gst_structure_get_name(structure) : nullptr;
    const bool isVideoPad = name != nullptr && g_str_has_prefix(name, "video/");
    gst_caps_unref(caps);

    if (!isVideoPad) {
        return;
    }

    GstPad* sinkPad = gst_element_get_static_pad(self->m_videoConvert, "sink");
    if (sinkPad == nullptr) {
        return;
    }

    if (gst_pad_is_linked(sinkPad)) {
        gst_object_unref(sinkPad);
        return;
    }

    if (gst_pad_link(newPad, sinkPad) != GST_PAD_LINK_OK) {
        emit self->receiverError(QStringLiteral("Unable to link the decoder output to videoconvert."));
    }

    gst_object_unref(sinkPad);
}
