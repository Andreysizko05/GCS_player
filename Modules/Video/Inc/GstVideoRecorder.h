#ifndef GSTVIDEORECORDER_H
#define GSTVIDEORECORDER_H

#include <QString>

#include <gst/gst.h>

class GstVideoRecorder
{
public:
    enum class Container {
        Matroska,
        Mp4
    };

    enum class EncodedVideoKind {
        H264,
        H265,
        Mjpeg,
        Unknown
    };

    struct Settings {
        bool enabled = false;
        Container container = Container::Matroska;
        QString directory;
        int bitrateKbps = 8000;
    };

    GstVideoRecorder();
    explicit GstVideoRecorder(const Settings& settings);

    GstVideoRecorder(const GstVideoRecorder&) = delete;
    GstVideoRecorder& operator=(const GstVideoRecorder&) = delete;

    const Settings& settings() const;
    bool enabled() const;
    GstElement* recordBin() const;

    bool attachEncodedBranch(
        GstElement* pipeline,
        GstElement* tee,
        EncodedVideoKind kind,
        QString& statusMessage,
        QString& errorMessage);
    bool attachRawBranch(
        GstElement* pipeline,
        GstElement* tee,
        QString& statusMessage,
        QString& errorMessage);
    void finalize(
        GstElement* pipeline,
        GstBus* bus,
        QString& statusMessage,
        QString& errorMessage);
    void reset();

    static EncodedVideoKind encodedVideoKindFromCaps(GstCaps* caps);
    static EncodedVideoKind encodedVideoKindFromStructure(const GstStructure* structure);
    static EncodedVideoKind encodedVideoKindFromRtpCaps(GstCaps* caps);

private:
    GstElement* createEncodedRecordingBin(
        EncodedVideoKind kind,
        QString& filePath,
        QString& errorMessage) const;
    GstElement* createRawRecordingBin(QString& filePath, QString& errorMessage) const;

    Settings m_settings;
    GstElement* m_recordBin = nullptr;
};

#endif // GSTVIDEORECORDER_H
