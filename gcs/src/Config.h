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

#include <QObject>
#include <QString>

namespace gcs {

class Config : public QObject {
    Q_OBJECT
public:
    static Config &instance();

    // ---- connection ------------------------------------------------------
    QString bridgeHost() const;
    void setBridgeHost(const QString &host);

    int bridgePort() const;
    void setBridgePort(int port);

    int cameraPort() const;
    void setCameraPort(int port);

    // ---- appearance ------------------------------------------------------
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
    /// Emitted whenever any value changes, so views can re-read what they use.
    void changed();

    /// Emitted specifically for changes that require rebuilding the stylesheet.
    void appearanceChanged();

private:
    Config();
};

}  // namespace gcs
