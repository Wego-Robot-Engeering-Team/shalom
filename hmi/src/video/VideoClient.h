#pragma once

// Live viewfinder receiver: RTSP/H.264 in, QImage out.
//
// The pictures do not come over the control link. A stale frame is worse than
// a dropped one here - when the view lags, the operator keeps moving the arm
// and overshoots - so the robot sends them on their own RTP/UDP socket and
// this class pulls that stream. The control link stays thin and never has to
// queue video behind pose and health.
//
// Decoding is done in software on purpose. At 720p 15 fps that costs a few
// percent of one core, and it avoids assuming anything about the GPU or the
// driver on whatever machine the station is delivered on - the same reason the
// 3D posture view is a software rasteriser.
//
// This is optional at build time. Without GStreamer the class still exists and
// reports that video is unavailable, so the rest of the station builds and runs
// unchanged.

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

namespace hmi::video {

/// What the viewfinder is doing, for the panel and the diagnostics screen.
enum class State {
    Stopped,     ///< nobody asked for it
    Connecting,  ///< pipeline up, no frame yet
    Playing,     ///< frames arriving
    Failed,      ///< pipeline error, or the stream went quiet
};

QString describe(State s);

class VideoClient : public QObject {
    Q_OBJECT
public:
    explicit VideoClient(QObject *parent = nullptr);
    ~VideoClient() override;

    /// True when the build has a decoder. False means every start() fails.
    static bool available();

    /// Idempotent. Starting an already-running stream on the same URL does
    /// nothing, so a panel can call this every time it is shown.
    void start(const QString &url);
    void stop();

    /// Re-establishes the stream after a short delay.
    ///
    /// The robot tears down and rebuilds its RTSP media when the quality
    /// preset changes, so the pipeline we are holding goes dead. The station
    /// asked for that change, so it should not sit and wait for the watchdog
    /// to notice - it knows the picture is about to bounce.
    void restartSoon(int delayMs = 1200);

    State state() const { return state_; }
    /// Frames per second over the last second. Shown on the diagnostics screen
    /// so "no picture" can be told from "no link".
    double fps() const { return fps_; }

    /// GStreamer state. Public because the appsink callback, which lives
    /// outside the class, has to reach it.
    struct Impl;

signals:
    void frameReady(const QImage &frame);
    void stateChanged(hmi::video::State state, const QString &detail);

private:
    void setState(State s, const QString &detail = {});

    /// Rebuilds the pipeline. RTSP clients do not reconnect on their own: if
    /// the robot reboots or the link drops, the picture simply freezes and
    /// nothing says so.
    void restart();
    void checkAlive();

    Impl *d_ = nullptr;

    QString url_;
    State state_ = State::Stopped;
    QTimer watchdog_;
    QTimer restartTimer_;
    QTimer fpsTimer_;
    int framesSinceTick_ = 0;
    double fps_ = 0.0;
    int consecutiveFailures_ = 0;
};

}  // namespace hmi::video
