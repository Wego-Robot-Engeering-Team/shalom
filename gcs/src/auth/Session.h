#pragma once

// Operator session and privilege gate.
//
// Two roles:
//   - operator: everyday running - drive, capture, start and stop a mission
//   - admin:    releasing the emergency stop, teaching and deleting locations,
//               changing settings
//
// WHAT THIS IS AND IS NOT
// -----------------------
// This is operational access control, not a security boundary. The credential
// is a salted PBKDF2 hash in the local settings store, so anyone with
// filesystem access to the control PC can replace it. That is acceptable
// because the goal is to stop casual and accidental misuse - a passer-by
// clearing an emergency stop, an operator changing a calibrated location by
// mistake - and to attribute actions in the audit log.
//
// It must not be presented to the customer as protection against a determined
// attacker, and it is never the thing that keeps the robot from moving: the
// authority for stopping is the hardware emergency stop and the robot-side
// safety node (protocol section 4).
//
// Repeated failures are rate limited so that guessing is slow and, more
// usefully, so that the attempts show up in the event log.

#include <QDateTime>
#include <QObject>
#include <QString>

namespace gcs::auth {

enum class Role { Operator, Admin };

QString roleLabel(Role role);

class Session : public QObject {
    Q_OBJECT
public:
    static Session &instance();

    /// True until an administrator password has been set, which happens on
    /// first launch. The application must not run with no credential
    /// configured, because then every privileged action would be open.
    bool needsInitialSetup() const;

    /// Stores the administrator password. Rejects anything shorter than the
    /// minimum length and returns false.
    bool setAdminPassword(const QString &password, QString *err = nullptr);

    /// Starts a session. An operator needs a name for the audit trail; an
    /// administrator additionally needs the password.
    bool signIn(const QString &displayName, Role role, const QString &password,
                QString *err = nullptr);
    void signOut();

    bool isSignedIn() const { return signedIn_; }
    Role role() const { return role_; }
    QString displayName() const { return displayName_; }
    QDateTime signedInAt() const { return signedInAt_; }

    /// Verifies the administrator password without changing the session, for
    /// gating a single privileged action such as releasing the emergency stop.
    bool verifyAdmin(const QString &password, QString *err = nullptr);

    /// Seconds remaining before another attempt is accepted; 0 when not
    /// locked out.
    int lockoutRemainingSeconds() const;

signals:
    void signedInChanged();

    /// Emitted on every accepted or rejected privileged check, so the caller
    /// can record it. `detail` is safe to log: it never contains the password.
    void authAttempt(bool accepted, const QString &detail);

private:
    Session();
    bool checkPassword(const QString &password) const;
    void registerFailure();
    void clearFailures();

    bool signedIn_ = false;
    Role role_ = Role::Operator;
    QString displayName_;
    QDateTime signedInAt_;
};

}  // namespace gcs::auth
