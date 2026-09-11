#pragma once

// Who is operating the station.
//
// The only thing this decides is what goes in the audit trail. Every logged
// action carries the operator's name, so that "who released the emergency
// stop" has an answer after the fact. It is not a security boundary and must
// not be presented as one.
//
// The credential is a fixed placeholder (admin / admin) while the account
// policy for the site is still open. That is a deliberate stopgap, not an
// oversight: a hardcoded password stops nobody, and the value here is the
// attribution, not the gate. Replacing it with real accounts changes this file
// and nothing else - the rest of the application only ever asks for
// displayName().
//
// The authority for stopping the robot is the hardware emergency stop and the
// robot-side safety node (protocol section 4). Nothing here can move or stop
// the machine.

#include <QObject>
#include <QString>

namespace hmi::auth {

class Session : public QObject {
    Q_OBJECT
public:
    static Session &instance();

    /// Checks the credential and starts a session. Returns false with a reason
    /// in `err` when it does not match.
    bool signIn(const QString &id, const QString &password, QString *err = nullptr);
    void signOut();

    bool isSignedIn() const { return signedIn_; }

    /// Name to put in the audit trail. Empty when nobody is signed in.
    QString displayName() const { return signedIn_ ? id_ : QString(); }

signals:
    void signedInChanged(bool signedIn);

private:
    Session() = default;

    bool signedIn_ = false;
    QString id_;
};

}  // namespace hmi::auth
