#ifndef GSTVIDEORECEIVER_H
#define GSTVIDEORECEIVER_H

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
        UdpMpegTs
    };

    enum class Codec {
        H264,
        H265
    };

    struct StreamSettings {
        Transport transport = Transport::UdpRtp;
        Codec codec = Codec::H264;
        QString udpHost = QStringLiteral("0.0.0.0");
        quint16 udpPort = 5600;
        bool lowLatency = false;
        int rtpPayload = 96;
        int rtpClockRate = 90000;
        int jitterLatencyMs = 50;
        int frameTimeoutMs = 3000;
        int restartDelayMs = 1000;
        int appSinkMaxBuffers = 1;
        QString appSinkFormat = QStringLiteral("BGRA");
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
    bool createPipeline();
    void destroyPipeline();
    bool processBusMessages();
    GstFlowReturn processSample(GstAppSink* sink);

    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer userData);
    static void onTsDemuxPadAdded(GstElement* src, GstPad* newPad, gpointer userData);
    static void onDecoderPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

    StreamSettings m_settings;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_seenFrame{false};
    std::atomic<qint64> m_lastFrameTimestampMs{0};
    std::atomic<int> m_lastFrameWidth{0};
    std::atomic<int> m_lastFrameHeight{0};

    GstElement* m_pipeline = nullptr;
    GstElement* m_udpSource = nullptr;
    GstElement* m_jitterBuffer = nullptr;
    GstElement* m_depayloader = nullptr;
    GstElement* m_parser = nullptr;
    GstElement* m_tsDemux = nullptr;
    GstElement* m_decodeQueue = nullptr;
    GstElement* m_decoder = nullptr;
    GstElement* m_videoConvert = nullptr;
    GstElement* m_videoCapsFilter = nullptr;
    GstElement* m_appSink = nullptr;
    GstBus* m_bus = nullptr;
};

#endif // GSTVIDEORECEIVER_H
