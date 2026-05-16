#ifndef GSTVIDEORECEIVER_H
#define GSTVIDEORECEIVER_H

#include <QSize>
#include <QThread>

#include <QtMultimedia/QVideoFrame>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>

#define GST_VIDEO_RECEIVER_UDP_HOST "0.0.0.0"
#define GST_VIDEO_RECEIVER_UDP_PORT 5600
#define GST_VIDEO_RECEIVER_RTP_MEDIA "video"
#define GST_VIDEO_RECEIVER_RTP_ENCODING "H264"
#define GST_VIDEO_RECEIVER_RTP_PAYLOAD 96
#define GST_VIDEO_RECEIVER_RTP_CLOCK_RATE 90000
#define GST_VIDEO_RECEIVER_JITTER_LATENCY_MS 50
#define GST_VIDEO_RECEIVER_FRAME_TIMEOUT_MS 3000
#define GST_VIDEO_RECEIVER_RESTART_DELAY_MS 1000
#define GST_VIDEO_RECEIVER_APPSINK_MAX_BUFFERS 1
#define GST_VIDEO_RECEIVER_APPSINK_FORMAT "BGRA"
#define GST_VIDEO_RECEIVER_PLACEHOLDER_WIDTH 1280
#define GST_VIDEO_RECEIVER_PLACEHOLDER_HEIGHT 720

class GstVideoReceiver : public QThread
{
    Q_OBJECT

public:
    explicit GstVideoReceiver(QObject* parent = nullptr);
    ~GstVideoReceiver() override;

    void stop();

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
    static void onDecoderPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

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
    GstElement* m_decodeQueue = nullptr;
    GstElement* m_decoder = nullptr;
    GstElement* m_videoConvert = nullptr;
    GstElement* m_videoCapsFilter = nullptr;
    GstElement* m_appSink = nullptr;
    GstBus* m_bus = nullptr;
};

#endif // GSTVIDEORECEIVER_H
