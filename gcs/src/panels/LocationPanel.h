#pragma once

// Location teaching panel.
//
// Named poses are recorded two ways (protocol section 8):
//
//   - **from the robot's current pose**: drive there, confirm the arm reaches
//     the target, then save. The pose is known-reachable because the robot is
//     standing in it.
//   - **from a map click**: quick to place, but nothing verifies the robot can
//     actually get there or work from there.
//
// Capturing from the robot is the primary method for inspection points and the
// dock; map clicks are for sketching a route before the robot is on site.
//
// Before a capture is accepted the current pose is validated. A pose recorded
// while the robot was moving, or from a stale link, produces a waypoint that
// looks fine on screen and only reveals itself during the acceptance run - at
// which point fixing it means putting the robot back in the pit.

#include <QVariantMap>
#include <QWidget>

class QLabel;
class QPushButton;

namespace gcs::ui {

class Badge;
class Card;

/// Everything the panel needs in order to decide whether a capture is sound.
struct RobotSnapshot {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
    double speed = 0.0;        ///< m/s, magnitude
    bool poseFresh = false;    ///< false once the link has gone quiet
    bool localizationOk = true;
    int visibleTagId = -1;     ///< -1 when no AprilTag is currently detected
};

/// Outcome of validating a capture request.
struct CaptureCheck {
    bool allowed = false;   ///< hard blocks clear this
    bool degraded = false;  ///< soft warnings set this
    QString reason;         ///< shown to the operator
    QString code;           ///< catalog code for the event log
};

class LocationPanel : public QWidget {
    Q_OBJECT
public:
    explicit LocationPanel(QWidget *parent = nullptr);

    /// Called on every telemetry frame; drives the enabled state and the
    /// readiness hint.
    void setSnapshot(const RobotSnapshot &snap);

    void setDock(const QVariantMap &location);   ///< empty map clears it
    void setHome(const QVariantMap &location);

    /// Validates a capture against the current snapshot. Exposed so that the
    /// same rule is used by the panel and by any other caller, and so it can
    /// be tested without a UI.
    static CaptureCheck checkCapture(const RobotSnapshot &snap, const QString &kind);

signals:
    /// kind is "inspection", "dock" or "home".
    void captureFromRobot(const QString &kind);

    /// Requests that the map enter click-to-place mode for this kind.
    void captureFromMap(const QString &kind);

    /// Overwrites an existing location with the current robot pose, keeping
    /// its id and its place in the sequence.
    void reteachRequested(const QString &kind);

    void gotoRequested(const QString &kind);

private:
    QWidget *buildFixedRow(const QString &kind, const QString &title);
    void refreshEnabled();

    Card *card_ = nullptr;
    Badge *ready_ = nullptr;
    QLabel *hint_ = nullptr;

    QHash<QString, QLabel *> valueLabels_;
    QList<QPushButton *> captureButtons_;
    QHash<QString, QPushButton *> gotoButtons_;

    RobotSnapshot snap_;
};

}  // namespace gcs::ui
