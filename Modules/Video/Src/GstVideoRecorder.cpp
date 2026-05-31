#include "GstVideoRecorder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <algorithm>

namespace
{
bool elementHasProperty(GstElement* element, const char* propertyName)
{
    return element != nullptr
        && propertyName != nullptr
        && g_object_class_find_property(G_OBJECT_GET_CLASS(element), propertyName) != nullptr;
}

bool elementFactoryAvailable(const char* factoryName)
{
    GstElementFactory* factory = gst_element_factory_find(factoryName);
    if (factory == nullptr) {
        return false;
    }

    gst_object_unref(factory);
    return true;
}

GstElement* makeElement(const char* factoryName, const char* elementName, QStringList& missingElements)
{
    GstElement* element = gst_element_factory_make(factoryName, elementName);
    if (element == nullptr) {
        missingElements.append(QString::fromLatin1(factoryName));
    }
    return element;
}

bool setElementIntProperty(GstElement* element, const char* propertyName, int value)
{
    if (!elementHasProperty(element, propertyName)) {
        return false;
    }

    g_object_set(element, propertyName, value, nullptr);
    return true;
}

bool setElementBoolProperty(GstElement* element, const char* propertyName, gboolean value)
{
    if (!elementHasProperty(element, propertyName)) {
        return false;
    }

    g_object_set(element, propertyName, value, nullptr);
    return true;
}

bool setElementObjectArg(GstElement* element, const char* propertyName, const char* value)
{
    if (!elementHasProperty(element, propertyName)) {
        return false;
    }

    gst_util_set_object_arg(G_OBJECT(element), propertyName, value);
    return true;
}

void configureQueueElement(GstElement* queue, bool leaky)
{
    if (queue == nullptr) {
        return;
    }

    g_object_set(
        queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", leaky ? G_GUINT64_CONSTANT(0) : G_GUINT64_CONSTANT(5000000000),
        nullptr
    );
    if (leaky && elementHasProperty(queue, "leaky")) {
        g_object_set(queue, "leaky", 2, nullptr);
    }
}

QString recordingContainerExtension(GstVideoRecorder::Container container)
{
    return container == GstVideoRecorder::Container::Mp4
        ? QStringLiteral("mp4")
        : QStringLiteral("mkv");
}

QString recordingDirectoryForSettings(const GstVideoRecorder::Settings& settings)
{
    QString directoryPath = settings.directory.trimmed();
    if (directoryPath.isEmpty()) {
        directoryPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (!directoryPath.isEmpty()) {
            directoryPath = QDir(directoryPath).filePath(QStringLiteral("GCS_player"));
        }
    }
    if (directoryPath.isEmpty()) {
        directoryPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("recordings"));
    }

    QDir directory(directoryPath);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        return {};
    }
    return directory.absolutePath();
}

QString recordingFilePath(
    const GstVideoRecorder::Settings& settings,
    const QString& codecLabel)
{
    const QString directoryPath = recordingDirectoryForSettings(settings);
    if (directoryPath.isEmpty()) {
        return {};
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString extension = recordingContainerExtension(settings.container);
    const QString cleanCodec = codecLabel.isEmpty() ? QStringLiteral("video") : codecLabel;
    const QString baseName = QStringLiteral("gcs-%1-%2.%3").arg(timestamp, cleanCodec, extension);
    return QDir(directoryPath).filePath(baseName);
}

QString recordingMuxerFactory(
    GstVideoRecorder::Container container,
    const QString& codecLabel)
{
    if (container == GstVideoRecorder::Container::Matroska) {
        return QStringLiteral("matroskamux");
    }

    if (codecLabel.compare(QStringLiteral("mjpeg"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("qtmux");
    }

    return QStringLiteral("mp4mux");
}

void configureMuxer(GstElement* muxer)
{
    if (muxer == nullptr) {
        return;
    }

    setElementBoolProperty(muxer, "faststart", TRUE);
}

QStringList h264EncoderCandidates()
{
#ifdef Q_OS_WIN
    return {
        QStringLiteral("d3d12h264enc"),
        QStringLiteral("nvd3d11h264enc"),
        QStringLiteral("mfh264enc"),
        QStringLiteral("amfh264enc"),
        QStringLiteral("nvh264enc"),
        QStringLiteral("x264enc"),
        QStringLiteral("openh264enc")
    };
#elif defined(Q_OS_LINUX)
    return {
        QStringLiteral("v4l2slh264enc"),
        QStringLiteral("v4l2h264enc"),
        QStringLiteral("vah264enc"),
        QStringLiteral("vaapih264enc"),
        QStringLiteral("nvh264enc"),
        QStringLiteral("x264enc"),
        QStringLiteral("openh264enc")
    };
#else
    return {
        QStringLiteral("vtenc_h264_hw"),
        QStringLiteral("x264enc"),
        QStringLiteral("openh264enc")
    };
#endif
}

QString selectH264EncoderFactory()
{
    for (const QString& factory : h264EncoderCandidates()) {
        if (elementFactoryAvailable(factory.toLatin1().constData())) {
            return factory;
        }
    }
    return {};
}

QString rawEncoderInputFormat(const QString& encoderFactory)
{
    if (encoderFactory == QStringLiteral("x264enc")
        || encoderFactory == QStringLiteral("openh264enc")) {
        return QStringLiteral("I420");
    }

    return QStringLiteral("NV12");
}

void configureH264Encoder(GstElement* encoder, const QString& factoryName, int bitrateKbps)
{
    if (encoder == nullptr) {
        return;
    }

    const int safeBitrateKbps = std::max(256, bitrateKbps);
    if (factoryName == QStringLiteral("openh264enc")) {
        setElementIntProperty(encoder, "bitrate", safeBitrateKbps * 1000);
        setElementIntProperty(encoder, "max-bitrate", safeBitrateKbps * 1000);
    } else {
        setElementIntProperty(encoder, "bitrate", safeBitrateKbps);
        setElementIntProperty(encoder, "max-bitrate", safeBitrateKbps);
    }
    setElementBoolProperty(encoder, "zerolatency", TRUE);
    setElementBoolProperty(encoder, "repeat-sequence-header", TRUE);
    setElementObjectArg(encoder, "speed-preset", "ultrafast");
    setElementObjectArg(encoder, "preset", "p1");
    setElementObjectArg(encoder, "tune", "zerolatency");
    setElementObjectArg(encoder, "rc-mode", "cbr");
}

bool addLinkedElementsToBin(GstElement* bin, const QVector<GstElement*>& elements)
{
    if (bin == nullptr || elements.isEmpty() || elements.first() == nullptr) {
        return false;
    }

    for (GstElement* element : elements) {
        if (element == nullptr) {
            return false;
        }
        gst_bin_add(GST_BIN(bin), element);
    }

    for (int index = 0; index + 1 < elements.size(); ++index) {
        if (!gst_element_link(elements.at(index), elements.at(index + 1))) {
            return false;
        }
    }

    GstPad* sinkPad = gst_element_get_static_pad(elements.first(), "sink");
    if (sinkPad == nullptr) {
        return false;
    }

    GstPad* ghostPad = gst_ghost_pad_new("sink", sinkPad);
    gst_object_unref(sinkPad);
    if (ghostPad == nullptr) {
        return false;
    }

    return gst_element_add_pad(bin, ghostPad) != FALSE;
}

void syncElementWithPipeline(GstElement* element)
{
    if (element != nullptr) {
        gst_element_sync_state_with_parent(element);
    }
}
} // namespace

GstVideoRecorder::GstVideoRecorder(const Settings& settings)
    : m_settings(settings)
{
}

const GstVideoRecorder::Settings& GstVideoRecorder::settings() const
{
    return m_settings;
}

bool GstVideoRecorder::enabled() const
{
    return m_settings.enabled;
}

GstElement* GstVideoRecorder::recordBin() const
{
    return m_recordBin;
}

GstVideoRecorder::EncodedVideoKind GstVideoRecorder::encodedVideoKindFromStructure(
    const GstStructure* structure)
{
    if (structure == nullptr) {
        return EncodedVideoKind::Unknown;
    }

    const QString name = QString::fromUtf8(gst_structure_get_name(structure));
    if (name == QStringLiteral("video/x-h264")) {
        return EncodedVideoKind::H264;
    }
    if (name == QStringLiteral("video/x-h265")) {
        return EncodedVideoKind::H265;
    }
    if (name == QStringLiteral("image/jpeg")) {
        return EncodedVideoKind::Mjpeg;
    }

    return EncodedVideoKind::Unknown;
}

GstVideoRecorder::EncodedVideoKind GstVideoRecorder::encodedVideoKindFromCaps(GstCaps* caps)
{
    if (caps == nullptr || gst_caps_is_empty(caps)) {
        return EncodedVideoKind::Unknown;
    }

    return encodedVideoKindFromStructure(gst_caps_get_structure(caps, 0));
}

GstVideoRecorder::EncodedVideoKind GstVideoRecorder::encodedVideoKindFromRtpCaps(GstCaps* caps)
{
    if (caps == nullptr || gst_caps_is_empty(caps)) {
        return EncodedVideoKind::Unknown;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    if (structure == nullptr) {
        return EncodedVideoKind::Unknown;
    }

    const char* encodingNameRaw = gst_structure_get_string(structure, "encoding-name");
    const QString encodingName = QString::fromUtf8(encodingNameRaw != nullptr ? encodingNameRaw : "");
    if (encodingName.compare(QStringLiteral("H264"), Qt::CaseInsensitive) == 0) {
        return EncodedVideoKind::H264;
    }
    if (encodingName.compare(QStringLiteral("H265"), Qt::CaseInsensitive) == 0
        || encodingName.compare(QStringLiteral("HEVC"), Qt::CaseInsensitive) == 0) {
        return EncodedVideoKind::H265;
    }
    if (encodingName.compare(QStringLiteral("JPEG"), Qt::CaseInsensitive) == 0
        || encodingName.compare(QStringLiteral("MJPEG"), Qt::CaseInsensitive) == 0) {
        return EncodedVideoKind::Mjpeg;
    }

    return EncodedVideoKind::Unknown;
}

GstElement* GstVideoRecorder::createEncodedRecordingBin(
    EncodedVideoKind kind,
    QString& filePath,
    QString& errorMessage) const
{
    QString codecLabel;
    QString parserFactoryName;
    switch (kind) {
    case EncodedVideoKind::H264:
        codecLabel = QStringLiteral("h264");
        parserFactoryName = QStringLiteral("h264parse");
        break;
    case EncodedVideoKind::H265:
        codecLabel = QStringLiteral("h265");
        parserFactoryName = QStringLiteral("h265parse");
        break;
    case EncodedVideoKind::Mjpeg:
        codecLabel = QStringLiteral("mjpeg");
        if (elementFactoryAvailable("jpegparse")) {
            parserFactoryName = QStringLiteral("jpegparse");
        }
        break;
    case EncodedVideoKind::Unknown:
        errorMessage = QStringLiteral("Recording cannot identify the encoded video format.");
        return nullptr;
    }

    filePath = recordingFilePath(m_settings, codecLabel);
    if (filePath.isEmpty()) {
        errorMessage = QStringLiteral("Recording folder is not writable.");
        return nullptr;
    }

    QStringList missingElements;
    GstElement* bin = gst_bin_new("encoded-record-bin");
    GstElement* queue = makeElement("queue", "encoded-record-queue", missingElements);
    GstElement* parser = parserFactoryName.isEmpty()
        ? nullptr
        : makeElement(parserFactoryName.toLatin1().constData(), "encoded-record-parser", missingElements);
    const QString muxerFactoryName = recordingMuxerFactory(m_settings.container, codecLabel);
    GstElement* muxer = makeElement(muxerFactoryName.toLatin1().constData(), "record-muxer", missingElements);
    GstElement* fileSink = makeElement("filesink", "record-file-sink", missingElements);

    if (!missingElements.isEmpty() || bin == nullptr) {
        errorMessage = QStringLiteral("Missing GStreamer recording element(s): %1.")
            .arg(missingElements.join(QStringLiteral(", ")));
        if (bin != nullptr) {
            gst_object_unref(bin);
        }
        return nullptr;
    }

    configureQueueElement(queue, false);
    if (parser != nullptr) {
        setElementIntProperty(parser, "config-interval", -1);
    }
    configureMuxer(muxer);
    g_object_set(
        fileSink,
        "location", QDir::toNativeSeparators(filePath).toUtf8().constData(),
        "sync", FALSE,
        "async", FALSE,
        nullptr
    );

    QVector<GstElement*> elements = { queue };
    if (parser != nullptr) {
        elements.append(parser);
    }
    elements.append(muxer);
    elements.append(fileSink);

    if (!addLinkedElementsToBin(bin, elements)) {
        errorMessage = QStringLiteral("Unable to link the encoded recording branch.");
        gst_object_unref(bin);
        return nullptr;
    }

    return bin;
}

GstElement* GstVideoRecorder::createRawRecordingBin(QString& filePath, QString& errorMessage) const
{
    const QString encoderFactoryName = selectH264EncoderFactory();
    if (encoderFactoryName.isEmpty()) {
        errorMessage = QStringLiteral("No H.264 encoder is available for raw recording.");
        return nullptr;
    }

    filePath = recordingFilePath(m_settings, QStringLiteral("raw-h264"));
    if (filePath.isEmpty()) {
        errorMessage = QStringLiteral("Recording folder is not writable.");
        return nullptr;
    }

    QStringList missingElements;
    GstElement* bin = gst_bin_new("raw-record-bin");
    GstElement* queue = makeElement("queue", "raw-record-queue", missingElements);
    GstElement* videoConvert = makeElement("videoconvert", "raw-record-convert", missingElements);
    GstElement* capsFilter = makeElement("capsfilter", "raw-record-caps", missingElements);
    GstElement* encoder = makeElement(encoderFactoryName.toLatin1().constData(), "raw-record-h264-encoder", missingElements);
    GstElement* parser = makeElement("h264parse", "raw-record-h264-parser", missingElements);
    const QString muxerFactoryName = recordingMuxerFactory(m_settings.container, QStringLiteral("h264"));
    GstElement* muxer = makeElement(muxerFactoryName.toLatin1().constData(), "raw-record-muxer", missingElements);
    GstElement* fileSink = makeElement("filesink", "raw-record-file-sink", missingElements);

    if (!missingElements.isEmpty() || bin == nullptr) {
        errorMessage = QStringLiteral("Missing GStreamer raw recording element(s): %1.")
            .arg(missingElements.join(QStringLiteral(", ")));
        if (bin != nullptr) {
            gst_object_unref(bin);
        }
        return nullptr;
    }

    configureQueueElement(queue, false);
    const QByteArray rawFormat = rawEncoderInputFormat(encoderFactoryName).toUtf8();
    GstCaps* encoderCaps = gst_caps_new_simple(
        "video/x-raw",
        "format", G_TYPE_STRING, rawFormat.constData(),
        nullptr
    );
    g_object_set(capsFilter, "caps", encoderCaps, nullptr);
    gst_caps_unref(encoderCaps);

    configureH264Encoder(encoder, encoderFactoryName, m_settings.bitrateKbps);
    setElementIntProperty(parser, "config-interval", -1);
    configureMuxer(muxer);
    g_object_set(
        fileSink,
        "location", QDir::toNativeSeparators(filePath).toUtf8().constData(),
        "sync", FALSE,
        "async", FALSE,
        nullptr
    );

    const QVector<GstElement*> elements = {
        queue,
        videoConvert,
        capsFilter,
        encoder,
        parser,
        muxer,
        fileSink
    };

    if (!addLinkedElementsToBin(bin, elements)) {
        errorMessage = QStringLiteral("Unable to link the raw H.264 recording branch.");
        gst_object_unref(bin);
        return nullptr;
    }

    return bin;
}

bool GstVideoRecorder::attachEncodedBranch(
    GstElement* pipeline,
    GstElement* tee,
    EncodedVideoKind kind,
    QString& statusMessage,
    QString& errorMessage)
{
    if (!m_settings.enabled || tee == nullptr) {
        return true;
    }
    if (pipeline == nullptr) {
        errorMessage = QStringLiteral("Recording pipeline is not available.");
        return false;
    }
    if (m_recordBin != nullptr) {
        return true;
    }

    QString filePath;
    m_recordBin = createEncodedRecordingBin(kind, filePath, errorMessage);
    if (m_recordBin == nullptr) {
        return false;
    }

    gst_bin_add(GST_BIN(pipeline), m_recordBin);
    if (!gst_element_link(tee, m_recordBin)) {
        errorMessage = QStringLiteral("Unable to link the encoded recording branch.");
        gst_bin_remove(GST_BIN(pipeline), m_recordBin);
        m_recordBin = nullptr;
        return false;
    }

    syncElementWithPipeline(m_recordBin);
    if (kind == EncodedVideoKind::Mjpeg && m_settings.container == Container::Mp4) {
        statusMessage = QStringLiteral(
            "MJPEG is written through the QuickTime muxer to avoid transcoding."
        );
        statusMessage.append(QLatin1Char('\n'));
    }
    statusMessage.append(QStringLiteral("Recording to %1.").arg(QDir::toNativeSeparators(filePath)));
    return true;
}

bool GstVideoRecorder::attachRawBranch(
    GstElement* pipeline,
    GstElement* tee,
    QString& statusMessage,
    QString& errorMessage)
{
    if (!m_settings.enabled || tee == nullptr) {
        return true;
    }
    if (pipeline == nullptr) {
        errorMessage = QStringLiteral("Recording pipeline is not available.");
        return false;
    }
    if (m_recordBin != nullptr) {
        return true;
    }

    QString filePath;
    m_recordBin = createRawRecordingBin(filePath, errorMessage);
    if (m_recordBin == nullptr) {
        return false;
    }

    gst_bin_add(GST_BIN(pipeline), m_recordBin);
    if (!gst_element_link(tee, m_recordBin)) {
        errorMessage = QStringLiteral("Unable to link the raw recording branch.");
        gst_bin_remove(GST_BIN(pipeline), m_recordBin);
        m_recordBin = nullptr;
        return false;
    }

    syncElementWithPipeline(m_recordBin);
    statusMessage = QStringLiteral("Recording to %1.").arg(QDir::toNativeSeparators(filePath));
    return true;
}

void GstVideoRecorder::finalize(
    GstElement* pipeline,
    GstBus* bus,
    QString& statusMessage,
    QString& errorMessage)
{
    if (pipeline == nullptr || m_recordBin == nullptr || bus == nullptr) {
        return;
    }

    GstState currentState = GST_STATE_NULL;
    gst_element_get_state(pipeline, &currentState, nullptr, 0);
    if (currentState < GST_STATE_PAUSED) {
        return;
    }

    gst_element_send_event(pipeline, gst_event_new_eos());
    GstMessage* message = gst_bus_timed_pop_filtered(
        bus,
        3 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR)
    );
    if (message == nullptr) {
        statusMessage = QStringLiteral("Recording finalize timed out.");
        return;
    }

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* error = nullptr;
        gchar* debugInfo = nullptr;
        gst_message_parse_error(message, &error, &debugInfo);
        if (error != nullptr) {
            errorMessage = QStringLiteral("Recording finalize error: %1.")
                .arg(QString::fromUtf8(error->message));
            g_error_free(error);
        }
        if (debugInfo != nullptr) {
            g_free(debugInfo);
        }
    } else {
        statusMessage = QStringLiteral("Recording finalized.");
    }

    gst_message_unref(message);
}

void GstVideoRecorder::reset()
{
    m_recordBin = nullptr;
}
