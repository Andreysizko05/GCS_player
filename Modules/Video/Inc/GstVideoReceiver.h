#ifndef GSTVIDEORECEIVER_H
#define GSTVIDEORECEIVER_H

#include "GstVideoRecorder.h"
#include "UsbCameraManager.h"

#include <QMap>
#include <QSize>
#include <QString>
#include <QThread>

#include <QtMultimedia/QVideoFrame>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>

class GstVideoReceiver : public QThread
{
    Q_OBJECT

public:
    enum class Transport {
        UdpRtp,
        UdpMpegTs,
        Rtsp,
        TcpMpegTs,
        CustomPipeline,
        UsbCamera
    };

    enum class Codec {
        H264,
        H265
    };

    using RecordingContainer = GstVideoRecorder::Container;

    struct StreamSettings {
        Transport transport = Transport::UdpRtp;
        Codec codec = Codec::H264;
        QString udpHost = QStringLiteral("0.0.0.0");
        quint16 udpPort = 5600;
        QString streamUrl;
        QString customPipeline;
        bool lowLatency = false;
        int rtpPayload = 96;
        int rtpClockRate = 90000;
        int jitterLatencyMs = 50;
        int mpegTsLowLatencyMs = 0;
        int frameTimeoutMs = 3000;
        int restartDelayMs = 1000;
        int appSinkMaxBuffers = 1;
        QString appSinkFormat = QStringLiteral("BGRA");
        QString usbDeviceId;
        QString usbDeviceName;
        int usbDeviceIndex = -1;
        QString usbModeCaps;
        QMap<QString, UsbCameraControlState> usbControls;
        bool recordingEnabled = false;
        RecordingContainer recordingContainer = RecordingContainer::Matroska;
        QString recordingDirectory;
        int recordingBitrateKbps = 8000;
    };

    explicit GstVideoReceiver(QObject* parent = nullptr);
    explicit GstVideoReceiver(const StreamSettings& settings, QObject* parent = nullptr);
    ~GstVideoReceiver() override;

    void stop();
    StreamSettings streamSettings() const;

signals:
    void frameReady(const QVideoFrame& frame);
    void videoSizeChanged(const QSize& size);
    void receiverMessage(const QString& message);
    void receiverError(const QString& message);

protected:
    void run() override;

private:
    using EncodedVideoKind = GstVideoRecorder::EncodedVideoKind;

    bool createCustomPipeline();
    bool createPipeline();
    void destroyPipeline();
    void finalizeRecording();
    bool processBusMessages();
    GstFlowReturn processSample(GstAppSink* sink);
    bool attachEncodedRecordingBranch(GstElement* tee, EncodedVideoKind kind);
    bool attachRawRecordingBranch(GstElement* tee);
    bool createDynamicRtspReceiveChain(GstPad* sourcePad, GstCaps* caps);
    bool linkDynamicEncodedPad(GstPad* sourcePad, GstCaps* caps);

    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer userData);
    static void onRtspPadAdded(GstElement* src, GstPad* newPad, gpointer userData);
    static void onTsDemuxPadAdded(GstElement* src, GstPad* newPad, gpointer userData);
    static void onEncodedPadAdded(GstElement* src, GstPad* newPad, gpointer userData);
    static void onDecoderPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

    StreamSettings m_settings;
    GstVideoRecorder m_recorder;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_seenFrame{false};
    std::atomic<qint64> m_lastFrameTimestampMs{0};
    std::atomic<int> m_lastFrameWidth{0};
    std::atomic<int> m_lastFrameHeight{0};

    GstElement* m_pipeline = nullptr;
    GstElement* m_udpSource = nullptr;
    GstElement* m_tcpSource = nullptr;
    GstElement* m_rtspSource = nullptr;
    GstElement* m_usbSource = nullptr;
    GstElement* m_usbCapsFilter = nullptr;
    GstElement* m_jitterBuffer = nullptr;
    GstElement* m_depayloader = nullptr;
    GstElement* m_parser = nullptr;
    GstElement* m_tsDemux = nullptr;
    GstElement* m_parseBin = nullptr;
    GstElement* m_recordTee = nullptr;
    GstElement* m_decodeQueue = nullptr;
    GstElement* m_decoder = nullptr;
    GstElement* m_videoConvert = nullptr;
    GstElement* m_videoCapsFilter = nullptr;
    GstElement* m_appSink = nullptr;
    GstBus* m_bus = nullptr;
    std::atomic_bool m_restartRequested{false};
};

#endif // GSTVIDEORECEIVER_H
