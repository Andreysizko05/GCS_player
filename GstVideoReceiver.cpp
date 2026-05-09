#include "GstVideoReceiver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>
#include <QPainter>
#include <QStringList>

#include <QtMultimedia/QVideoFrameFormat>

#include <gst/video/video-info.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace
{
QVideoFrame imageToVideoFrame(const QImage& image)
{
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QVideoFrame frame(QVideoFrameFormat(source.size(), QVideoFrameFormat::Format_BGRA8888));

    if (!frame.map(QVideoFrame::WriteOnly)) {
        return {};
    }

    const int destinationStride = frame.bytesPerLine(0);
    const int rowBytes = std::min(destinationStride, static_cast<int>(source.bytesPerLine()));
    uchar* destination = frame.bits(0);

    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(destination + y * destinationStride, source.constScanLine(y), rowBytes);
    }

    frame.unmap();
    return frame;
}

QString createMessageText(const QString& line)
{
    return line.isEmpty() ? QStringLiteral("Waiting for video stream...") : line;
}

QVideoFrame makePlaceholderFrame(const QString& text)
{
    QImage image(
        GST_VIDEO_RECEIVER_PLACEHOLDER_WIDTH,
        GST_VIDEO_RECEIVER_PLACEHOLDER_HEIGHT,
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

void prepareGStreamerEnvironment()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pluginDir = QDir(appDir).filePath(QStringLiteral("gstreamer-1.0"));
    const QString gioModulesDir = QDir(appDir).filePath(QStringLiteral("gio/modules"));
    const QString runtimeDir = QDir(appDir).filePath(QStringLiteral("gstreamer-runtime"));
    const QString toolsDir = QDir(appDir).filePath(QStringLiteral("gstreamer-tools"));
#ifdef Q_OS_WIN
    const QString scannerPath = QDir(toolsDir).filePath(QStringLiteral("gst-plugin-scanner.exe"));
#else
    const QString scannerPath = QDir(toolsDir).filePath(QStringLiteral("gst-plugin-scanner"));
#endif

    prependPath(appDir);
    prependPath(runtimeDir);
    prependPath(toolsDir);
    setEnvIfPathExists("GST_PLUGIN_PATH", pluginDir);
    setEnvIfPathExists("GST_PLUGIN_PATH_1_0", pluginDir);
    setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH", pluginDir);
    setEnvIfPathExists("GST_PLUGIN_SYSTEM_PATH_1_0", pluginDir);
    setEnvIfPathExists("GIO_EXTRA_MODULES", gioModulesDir);
    setEnvIfPathExists("GST_PLUGIN_SCANNER", scannerPath);
    setEnvIfPathExists("GST_PLUGIN_SCANNER_1_0", scannerPath);
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
    : QThread(parent)
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

void GstVideoReceiver::run()
{
    QString initError;
    if (!ensureGStreamerInitialized(initError)) {
        const QString message = QStringLiteral("GStreamer init failed: %1").arg(initError);
        emit receiverError(message);
        emit frameReady(makePlaceholderFrame(message));
        return;
    }

    emit frameReady(makePlaceholderFrame(
        QStringLiteral("Listening on UDP %1 for RTP/%2 video.")
            .arg(GST_VIDEO_RECEIVER_UDP_PORT)
            .arg(QStringLiteral(GST_VIDEO_RECEIVER_RTP_ENCODING))
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
            msleep(GST_VIDEO_RECEIVER_RESTART_DELAY_MS);
            continue;
        }

        bool keepRunning = true;
        while (!m_stopRequested.load(std::memory_order_relaxed) && keepRunning) {
            keepRunning = processBusMessages();

            const qint64 lastFrameMs = m_lastFrameTimestampMs.load(std::memory_order_relaxed);
            if (lastFrameMs > 0) {
                const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - lastFrameMs;
                if (elapsedMs > GST_VIDEO_RECEIVER_FRAME_TIMEOUT_MS) {
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
            msleep(GST_VIDEO_RECEIVER_RESTART_DELAY_MS);
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

    m_pipeline = gst_pipeline_new("gcs-player-video-pipeline");
    m_udpSource = gst_element_factory_make("udpsrc", "udp-source");
    m_jitterBuffer = gst_element_factory_make("rtpjitterbuffer", "rtp-jitter-buffer");
    m_depayloader = gst_element_factory_make("rtph264depay", "rtp-h264-depay");
    m_parser = gst_element_factory_make("h264parse", "h264-parser");
    m_decodeQueue = gst_element_factory_make("queue", "decode-queue");
    m_decoder = gst_element_factory_make("decodebin3", "video-decoder");
    if (m_decoder == nullptr) {
        m_decoder = gst_element_factory_make("decodebin", "video-decoder");
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
    trackMissingElement(m_udpSource, QStringLiteral("udpsrc"));
    trackMissingElement(m_jitterBuffer, QStringLiteral("rtpjitterbuffer"));
    trackMissingElement(m_depayloader, QStringLiteral("rtph264depay"));
    trackMissingElement(m_parser, QStringLiteral("h264parse"));
    trackMissingElement(m_decodeQueue, QStringLiteral("queue"));
    trackMissingElement(m_decoder, QStringLiteral("decodebin3/decodebin"));
    trackMissingElement(m_videoConvert, QStringLiteral("videoconvert"));
    trackMissingElement(m_videoCapsFilter, QStringLiteral("capsfilter"));
    trackMissingElement(m_appSink, QStringLiteral("appsink"));

    if (!missingElements.isEmpty()) {
        emit receiverError(QStringLiteral("Missing GStreamer element(s): %1.")
            .arg(missingElements.join(QStringLiteral(", "))));
        destroyPipeline();
        return false;
    }

    GstCaps* udpCaps = gst_caps_new_simple(
        "application/x-rtp",
        "media", G_TYPE_STRING, GST_VIDEO_RECEIVER_RTP_MEDIA,
        "encoding-name", G_TYPE_STRING, GST_VIDEO_RECEIVER_RTP_ENCODING,
        "payload", G_TYPE_INT, GST_VIDEO_RECEIVER_RTP_PAYLOAD,
        "clock-rate", G_TYPE_INT, GST_VIDEO_RECEIVER_RTP_CLOCK_RATE,
        nullptr
    );

    GstCaps* sinkCaps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, GST_VIDEO_RECEIVER_APPSINK_FORMAT,
        nullptr
    );

    g_object_set(
        m_udpSource,
        "address", GST_VIDEO_RECEIVER_UDP_HOST,
        "port", GST_VIDEO_RECEIVER_UDP_PORT,
        "caps", udpCaps,
        nullptr
    );
    g_object_set(
        m_jitterBuffer,
        "latency", GST_VIDEO_RECEIVER_JITTER_LATENCY_MS,
        "drop-on-latency", TRUE,
        nullptr
    );
    g_object_set(m_parser, "config-interval", 1, nullptr);
    g_object_set(
        m_appSink,
        "emit-signals", TRUE,
        "max-buffers", GST_VIDEO_RECEIVER_APPSINK_MAX_BUFFERS,
        "drop", TRUE,
        "sync", FALSE,
        "enable-last-sample", FALSE,
        nullptr
    );
    g_object_set(m_videoCapsFilter, "caps", sinkCaps, nullptr);

    gst_caps_unref(udpCaps);
    gst_caps_unref(sinkCaps);

    g_signal_connect(m_decoder, "pad-added", G_CALLBACK(onDecoderPadAdded), this);
    g_signal_connect(m_appSink, "new-sample", G_CALLBACK(onNewSample), this);

    gst_bin_add_many(
        GST_BIN(m_pipeline),
        m_udpSource,
        m_jitterBuffer,
        m_depayloader,
        m_parser,
        m_decodeQueue,
        m_decoder,
        m_videoConvert,
        m_videoCapsFilter,
        m_appSink,
        nullptr
    );

    if (!gst_element_link_many(
            m_udpSource,
            m_jitterBuffer,
            m_depayloader,
            m_parser,
            m_decodeQueue,
            m_decoder,
            nullptr))
    {
        emit receiverError(QStringLiteral("Unable to link the RTP receive chain."));
        destroyPipeline();
        return false;
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
    m_jitterBuffer = nullptr;
    m_depayloader = nullptr;
    m_parser = nullptr;
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

    QVideoFrame frame(QVideoFrameFormat(QSize(width, height), QVideoFrameFormat::Format_BGRA8888));
    const bool frameMapped = frame.map(QVideoFrame::WriteOnly);

    if (frameMapped) {
        const int destinationStride = frame.bytesPerLine(0);
        const int rowBytes = std::min(destinationStride, stride);
        uchar* destination = frame.bits(0);
        const auto* source = reinterpret_cast<const uchar*>(mapInfo.data);

        for (int y = 0; y < height; ++y) {
            std::memcpy(destination + y * destinationStride, source + y * stride, rowBytes);
        }

        frame.unmap();
    }

    gst_buffer_unmap(buffer, &mapInfo);
    gst_sample_unref(sample);

    if (!frameMapped) {
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
