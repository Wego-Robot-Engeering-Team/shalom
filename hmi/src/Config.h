#pragma once

// Persistent application settings.
//
// Backed by QSettings, so values survive restarts and live in the platform's
// normal location (registry on Windows, an ini under ~/.config on Linux).
//
// A note on what is *not* here: the safety timings - emergency stop response,
// the communication-loss stop, the jog deadman - are enforced by the robot's
// safety node, not by this application (protocol section 4). Exposing them as
// editable fields would suggest the control station can change them, which it
// cannot. They are surfaced read-only, sourced from what the robot reports.

#include <QList>
#include <QObject>
#include <QString>

namespace hmi {

/// One robot the station can connect to.
///
/// The address is the whole of it. A robot running the MuJoCo simulator on
/// this machine is reached at 127.0.0.1 and is not special: the station speaks
/// the same protocol either way, and pretending otherwise would mean the
/// screen shows something the robot never said.
struct RobotEntry {
    QString name;   ///< what the operator calls it
    QString host;
    int port = 9090;
};

class Config : public QObject {
    Q_OBJECT
public:
    static Config &instance();

    // ---- connection ------------------------------------------------------
    //
    // The station keeps a list of robots and one of them is current. The
    // single-address accessors below answer for whichever is current, so
    // everything that just wants "where do I connect" is unaffected by the
    // list existing.
    QList<RobotEntry> robots() const;
    void setRobots(const QList<RobotEntry> &robots);

    /// Index into robots(). Out-of-range values are clamped, because a list
    /// edited down to fewer entries must not leave the station pointing at
    /// nothing.
    int currentRobot() const;
    void setCurrentRobot(int index);

    QString bridgeHost() const;
    void setBridgeHost(const QString &host);

    int bridgePort() const;
    void setBridgePort(int port);

    // ---- appearance ------------------------------------------------------
    // ---- battery policy ---------------------------------------------------
    // Percentages. Enforced by the robot; stored here so the setting survives
    // a restart and can be shipped pre-filled.
    double batteryReturnPercent() const;
    void setBatteryReturnPercent(double pct);
    double batteryDeparturePercent() const;
    void setBatteryDeparturePercent(double pct);

    QString theme() const;              ///< "light" or "dark"
    void setTheme(const QString &name);

    /// Multiplies every font size in the stylesheet. Control rooms are often
    /// viewed from further away than a desk, and the operator may not be the
    /// person who set the machine up.
    double uiScale() const;
    void setUiScale(double scale);

    // ---- operation -------------------------------------------------------
    double defaultLinearSpeed() const;  ///< m/s, initial jog slider position
    void setDefaultLinearSpeed(double v);

    double defaultAngularSpeed() const; ///< rad/s
    void setDefaultAngularSpeed(double v);

    // ---- logging ---------------------------------------------------------
    QString logDirectory() const;
    void setLogDirectory(const QString &dir);

    int logRetentionDays() const;
    void setLogRetentionDays(int days);

    // ---- storage ---------------------------------------------------------
    QString nasMountPath() const;
    void setNasMountPath(const QString &path);

    /// Restores every value to its default. Does not touch credentials.
    void resetToDefaults();

signals:
    /// Emitted for changes that require rebuilding the stylesheet.
    ///
    /// There was a second, broader `changed()` next to this one. Nothing ever
    /// listened to it: every screen reads the values it needs when it opens,
    /// and the ones that must react while open are the appearance ones.
    void appearanceChanged();

private:
    Config();
};

}  // namespace hmi
