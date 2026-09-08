#include "video/VideoClient.h"

#include <QDebug>

#ifdef HMI_WITH_VIDEO
#    include <gst/app/gstappsink.h>
#    include <gst/gst.h>
#endif

namespace hmi::video {

QString describe(State s)
{
    switch (s) {
    case State::Stopped:    return QStringLiteral("꺼짐");
    case State::Connecting: return QStringLiteral("연결 중");
    case State::Playing:    return QStringLiteral("수신 중");
    case State::Failed:     return QStringLiteral("실패");
    }
    return {};
}

#ifdef HMI_WITH_VIDEO

struct VideoClient::Impl {
    GstElement *pipeline = nullptr;
    GstElement *sink = nullptr;
    VideoClient *owner = nullptr;
};

namespace {

/// appsink 는 GStreamer 스레드에서 부른다. QImage 를 그 스레드에서 그대로
/// 넘기면 위젯이 다른 스레드에서 그려지므로, 복사해서 큐 연결로 던진다.
GstFlowReturn onNewSample(GstAppSink *sink, gpointer user)
{
    auto *impl = static_cast<VideoClient::Impl *>(user);
    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample)
        return GST_FLOW_OK;

    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstStructure *st = caps ? gst_caps_get_structure(caps, 0) : nullptr;

    int w = 0, h = 0;
    if (st) {
        gst_structure_get_int(st, "width", &w);
        gst_structure_get_int(st, "height", &h);
    }

    GstMapInfo map;
    if (w > 0 && h > 0 && buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        // GStreamer 는 행을 4 바이트에 맞춰 채운다. 그 보폭을 무시하면
        // 그림이 비스듬히 밀려 나온다.
        const int stride = GST_ROUND_UP_4(w * 3);
        if (gsize(stride) * gsize(h) <= map.size) {
            const QImage view(map.data, w, h, stride, QImage::Format_RGB888);
            const QImage frame = view.copy();   // 버퍼는 곧 풀린다
            QMetaObject::invokeMethod(impl->owner, [impl, frame] {
                emit impl->owner->frameReady(frame);
            }, Qt::QueuedConnection);
        }
        gst_buffer_unmap(buf, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

}  // namespace

bool VideoClient::available() { return true; }

VideoClient::VideoClient(QObject *parent) : QObject(parent), d_(new Impl)
{
    d_->owner = this;
    if (!gst_is_initialized())
        gst_init(nullptr, nullptr);

    // 살아 있는지 본다. RTSP 클라이언트는 스스로 재접속하지 않아서, 로봇이
    // 재부팅하거나 무선이 끊기면 화면이 검은 채로 멈춘 뒤 아무 말이 없다.
    watchdog_.setInterval(3000);
    connect(&watchdog_, &QTimer::timeout, this, &VideoClient::checkAlive);

    fpsTimer_.setInterval(1000);
    connect(&fpsTimer_, &QTimer::timeout, this, [this] {
        fps_ = framesSinceTick_;
        framesSinceTick_ = 0;
    });

    connect(this, &VideoClient::frameReady, this, [this] {
        ++framesSinceTick_;
        consecutiveFailures_ = 0;
        if (state_ != State::Playing)
            setState(State::Playing);
    });
}

VideoClient::~VideoClient()
{
    stop();
    delete d_;
}

void VideoClient::start(const QString &url)
{
    if (url == url_ && (state_ == State::Playing || state_ == State::Connecting))
        return;
    url_ = url;
    restart();
}

void VideoClient::restart()
{
    stop();
    if (url_.isEmpty())
        return;

    // latency 기본값이 2000 ms 다. 그대로 두면 2 초 밀린 화면을 보게 되고,
    // 영상을 UDP 로 따로 보낸 이유가 통째로 사라진다. appsink 쪽도 쌓이지
    // 않게 최신 한 장만 들고 버린다.
    const QString desc =
        QStringLiteral(
            "rtspsrc location=%1 latency=50 drop-on-latency=true "
            "! rtph264depay ! h264parse ! avdec_h264 ! videoconvert "
            "! video/x-raw,format=RGB "
            "! appsink name=out sync=false max-buffers=1 drop=true emit-signals=false")
            .arg(url_);

    GError *err = nullptr;
    d_->pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
    if (!d_->pipeline) {
        const QString detail = err ? QString::fromUtf8(err->message)
                                   : QStringLiteral("파이프라인을 세우지 못했다");
        if (err)
            g_error_free(err);
        setState(State::Failed, detail);
        watchdog_.start();
        return;
    }
    if (err)
        g_error_free(err);

    d_->sink = gst_bin_get_by_name(GST_BIN(d_->pipeline), "out");
    GstAppSinkCallbacks cb{};
    cb.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(d_->sink), &cb, d_, nullptr);

    if (gst_element_set_state(d_->pipeline, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        setState(State::Failed, QStringLiteral("스트림을 열지 못했다"));
    } else {
        setState(State::Connecting);
    }
    watchdog_.start();
    fpsTimer_.start();
}

void VideoClient::stop()
{
    watchdog_.stop();
    fpsTimer_.stop();
    fps_ = 0.0;
    framesSinceTick_ = 0;
    if (d_->sink) {
        gst_object_unref(d_->sink);
        d_->sink = nullptr;
    }
    if (d_->pipeline) {
        gst_element_set_state(d_->pipeline, GST_STATE_NULL);
        gst_object_unref(d_->pipeline);
        d_->pipeline = nullptr;
    }
    if (state_ != State::Stopped)
        setState(State::Stopped);
}

void VideoClient::checkAlive()
{
    if (framesSinceTick_ > 0 || fps_ > 0.0)
        return;

    // 세 번 연속 조용하면 파이프라인을 통째로 다시 세운다. 그 사이 화면은
    // "연결 중" 으로 남아, 멈춘 그림이 살아 있는 것처럼 보이지 않는다.
    if (++consecutiveFailures_ < 3) {
        if (state_ == State::Playing)
            setState(State::Connecting, QStringLiteral("영상이 끊겼다"));
        return;
    }
    consecutiveFailures_ = 0;
    setState(State::Connecting, QStringLiteral("다시 연결하는 중"));
    const QString url = url_;
    stop();
    url_ = url;
    restart();
}

#else   // GStreamer 없이 빌드한 경우

struct VideoClient::Impl {};

bool VideoClient::available() { return false; }

VideoClient::VideoClient(QObject *parent) : QObject(parent), d_(nullptr) {}
VideoClient::~VideoClient() = default;

void VideoClient::start(const QString &url)
{
    url_ = url;
    setState(State::Failed, QStringLiteral("이 빌드에는 영상 디코더가 없습니다"));
}

void VideoClient::stop() { setState(State::Stopped); }
void VideoClient::restart() {}
void VideoClient::checkAlive() {}

#endif

void VideoClient::setState(State s, const QString &detail)
{
    if (s == state_ && detail.isEmpty())
        return;
    state_ = s;
    emit stateChanged(s, detail);
}

}  // namespace hmi::video
