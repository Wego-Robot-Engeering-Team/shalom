#include "auth/Session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>

namespace gcs::auth {
namespace {

/// PBKDF2 반복 횟수. 로그인은 사람이 기다리는 동작이므로 수십 ms 는 허용된다.
constexpr int kIterations = 120000;
constexpr int kKeyLength = 32;
constexpr int kMinPasswordLength = 8;

/// 잠금 정책. 온라인 추측을 느리게 만들되, 현장에서 오타 한두 번으로
/// 작업이 막히지 않을 정도로 둔다.
constexpr int kMaxFailures = 5;
constexpr int kLockoutSeconds = 60;

QSettings &store()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("WEGO Robotics"), QStringLiteral("SHALOM GCS"));
    return s;
}

QByteArray derive(const QString &password, const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                              password.toUtf8(), salt,
                                              kIterations, kKeyLength);
}

}  // namespace

QString roleLabel(Role role)
{
    return role == Role::Admin ? QStringLiteral("관리자") : QStringLiteral("운용자");
}

Session &Session::instance()
{
    static Session s;
    return s;
}

Session::Session() = default;

bool Session::needsInitialSetup() const
{
    return store().value(QStringLiteral("auth/admin_hash")).toByteArray().isEmpty();
}

bool Session::setAdminPassword(const QString &password, QString *err)
{
    if (password.size() < kMinPasswordLength) {
        if (err)
            *err = QStringLiteral("비밀번호는 %1자 이상이어야 합니다.").arg(kMinPasswordLength);
        return false;
    }

    QByteArray salt(16, 0);
    QRandomGenerator::system()->generate(salt.begin(), salt.end());

    auto &s = store();
    s.setValue(QStringLiteral("auth/admin_salt"), salt);
    s.setValue(QStringLiteral("auth/admin_hash"), derive(password, salt));
    s.setValue(QStringLiteral("auth/admin_set_at"), QDateTime::currentDateTime());
    clearFailures();
    return true;
}

bool Session::checkPassword(const QString &password) const
{
    const auto &s = store();
    const QByteArray salt = s.value(QStringLiteral("auth/admin_salt")).toByteArray();
    const QByteArray expected = s.value(QStringLiteral("auth/admin_hash")).toByteArray();
    if (salt.isEmpty() || expected.isEmpty())
        return false;

    // 길이가 같을 때 상수 시간 비교. 타이밍 차이로 정답 자릿수를 흘리지 않는다.
    const QByteArray actual = derive(password, salt);
    if (actual.size() != expected.size())
        return false;
    quint8 diff = 0;
    for (int i = 0; i < actual.size(); ++i)
        diff |= quint8(actual[i]) ^ quint8(expected[i]);
    return diff == 0;
}

int Session::lockoutRemainingSeconds() const
{
    const auto &s = store();
    if (s.value(QStringLiteral("auth/failures"), 0).toInt() < kMaxFailures)
        return 0;
    const QDateTime until = s.value(QStringLiteral("auth/locked_until")).toDateTime();
    if (!until.isValid())
        return 0;
    const qint64 left = QDateTime::currentDateTime().secsTo(until);
    return left > 0 ? int(left) : 0;
}

void Session::registerFailure()
{
    auto &s = store();
    const int n = s.value(QStringLiteral("auth/failures"), 0).toInt() + 1;
    s.setValue(QStringLiteral("auth/failures"), n);
    if (n >= kMaxFailures) {
        s.setValue(QStringLiteral("auth/locked_until"),
                   QDateTime::currentDateTime().addSecs(kLockoutSeconds));
    }
}

void Session::clearFailures()
{
    auto &s = store();
    s.remove(QStringLiteral("auth/failures"));
    s.remove(QStringLiteral("auth/locked_until"));
}

bool Session::verifyAdmin(const QString &password, QString *err)
{
    if (const int wait = lockoutRemainingSeconds(); wait > 0) {
        if (err)
            *err = QStringLiteral("시도가 너무 많습니다. %1초 후 다시 시도하십시오.").arg(wait);
        emit authAttempt(false, QStringLiteral("잠금 상태에서 시도"));
        return false;
    }

    if (!checkPassword(password)) {
        registerFailure();
        if (err)
            *err = QStringLiteral("관리자 비밀번호가 일치하지 않습니다.");
        emit authAttempt(false, QStringLiteral("비밀번호 불일치"));
        return false;
    }

    clearFailures();
    emit authAttempt(true, QStringLiteral("관리자 인증 성공"));
    return true;
}

bool Session::signIn(const QString &displayName, Role role, const QString &password,
                     QString *err)
{
    if (displayName.trimmed().isEmpty()) {
        if (err)
            *err = QStringLiteral("이름을 입력하십시오. 조작 이력에 기록됩니다.");
        return false;
    }
    if (role == Role::Admin && !verifyAdmin(password, err))
        return false;

    signedIn_ = true;
    role_ = role;
    displayName_ = displayName.trimmed();
    signedInAt_ = QDateTime::currentDateTime();
    emit signedInChanged();
    return true;
}

void Session::signOut()
{
    signedIn_ = false;
    displayName_.clear();
    role_ = Role::Operator;
    emit signedInChanged();
}

}  // namespace gcs::auth
