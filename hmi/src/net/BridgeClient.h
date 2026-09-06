#pragma once

// The real connection to the robot, over raw TCP.
//
// Implements docs/bridge_protocol.md. Everything the control station sends and
// receives passes through here; it is the only place that knows the wire
// format exists.
//
// WHAT THIS CLASS IS RESPONSIBLE FOR
// ----------------------------------
//   - framing, via FrameDecoder (protocol section 1.1)
//   - the heartbeat in both directions, and deciding when the pose has gone
//     stale (section 5)
//   - reconnecting with backoff, and *not* resuming anything afterwards
//   - assembling per-channel messages into whole telemetry snapshots
//   - the link counters the diagnostics panel shows
//
// WHAT IT IS NOT RESPONSIBLE FOR
// ------------------------------
// Safety. The one-second emergency stop and the three-second
// communication-loss stop are enforced by the robot's safety node, which acts
// on its own when this client goes quiet. Nothing here should be written as
// though the robot depends on it to stop.

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include "net/Envelope.h"
#include "net/Framing.h"
#include "robot/RobotLink.h"

class QTcpSocket;
class QTimer;

namespace hmi::net {

class BridgeClient : public hmi::robot::RobotLink {
    Q_OBJECT
public:
    BridgeClient(QString host, quint16 port, QObject *parent = nullptr);
    ~BridgeClient() override;

    void connectToBridge();

    /// Closes the connection and stops reconnecting. Used when the operator
    /// deliberately disconnects, so that it does not silently come back.
    void disconnectFromBridge();

    // ---- RobotLink ------------------------------------------------------
    void setCmdVel(double vx, double vy, double wz) override;
    void requestGoal(double x, double y, double theta) override;
    void cancelNav() override;

    void setWaypoints(const QList<QVariantMap> &waypoints) override;
    void setBatteryPolicy(double returnAt, double departAt) override;
    QList<QVariantMap> waypoints() const override { return waypoints_; }

    void missionStart() override;
    void missionPause() override;
    void missionResume() override;
    void missionStop() override;
    hmi::robot::MissionState missionState() const override { return mission_; }

    void engageEstop() override;
    void releaseEstop() override;
    bool estopEngaged() const override { return estop_; }

    void setMode(hmi::robot::DriveMode mode) override;
    hmi::robot::DriveMode mode() const override { return mode_; }

    void setArmJointGoal(const QList<double> &q) override;
    void setArmPreset(const QString &name) override;
    void stopArm() override;

    bool isConnected() const override;
    QString describe() const override;

signals:
    /// A map arrived. Separate from telemetry because it is large and rare.
    void mapReceived(const QByteArray &pngBytes, const QJsonObject &meta);

private:
    void onConnected();
    void onDisconnected();
    void onSocketError();
    void onReadyRead();

    void sendEnvelope(const Envelope &env);

    /// Sends a command and remembers it so a missing response can be reported
    /// rather than silently swallowed.
    void sendRequest(const QString &channel, const QJsonObject &payload = {});

    /// Publishes a loss-tolerant message. Dropped when the socket is backed up,
    /// because queueing stale velocity commands is worse than skipping them.
    void publish(const QString &channel, const QJsonObject &payload);

    void handleFrame(const Frame &frame);
    void handleResponse(const Envelope &env);
    void handlePublish(const Envelope &env);
    void handleHeartbeat(const Envelope &env);

    void scheduleReconnect();
    void resetLinkState();
    void checkTimeouts();
    void emitTelemetry();

    QString host_;
    quint16 port_;

    QTcpSocket *socket_ = nullptr;
    FrameDecoder decoder_;

    QTimer *heartbeatTimer_ = nullptr;   ///< outgoing, 5 Hz
    QTimer *watchdogTimer_ = nullptr;    ///< checks for silence and timeouts
    QTimer *reconnectTimer_ = nullptr;
    QTimer *telemetryTimer_ = nullptr;   ///< emits assembled snapshots

    /// Backoff grows to a ceiling rather than retrying tightly: a bridge that
    /// is down stays down for minutes, and hammering it fills the log.
    int reconnectDelayMs_ = 500;
    bool wantConnection_ = false;

    /// Distinguishes the first connection from a reconnection, so the log does
    /// not report "link restored" for a link that was never up.
    bool everConnected_ = false;

    /// Robot id pinned from the first envelope that carried one. Cleared on
    /// disconnect: whatever answers next is not guaranteed to be the same
    /// machine.
    QString robotId_;

    /// Last battery policy given, resent on every (re)connect. Zero means the
    /// station has not been told one yet.
    double batteryReturnAt_ = 0.0;
    double batteryDepartAt_ = 0.0;

    qint64 heartbeatSeq_ = 0;
    QHash<qint64, qint64> heartbeatSentAt_;   ///< seq -> monotonic ms
    qint64 lastHeartbeatMs_ = 0;
    qint64 lastPoseMs_ = 0;
    QElapsedTimer clock_;

    struct Pending {
        QString channel;
        qint64 sentAtMs;
    };
    QHash<QString, Pending> pending_;

    QHash<QString, qint64> lastSeq_;   ///< channel -> last seen sequence

    hmi::robot::Telemetry telemetry_;
    QList<QVariantMap> waypoints_;
    hmi::robot::MissionState mission_ = hmi::robot::MissionState::Idle;
    hmi::robot::DriveMode mode_ = hmi::robot::DriveMode::Auto;
    bool estop_ = false;

    qint64 rxBytes_ = 0;
    qint64 txBytes_ = 0;
    qint64 lastThroughputMs_ = 0;
};

}  // namespace hmi::net
