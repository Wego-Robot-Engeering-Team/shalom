#include "auth/Session.h"

#include <utility>

#include <QCryptographicHash>
#include <QDateTime>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSettings>

namespace hmi::auth {
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
                       QStringLiteral("WEGO Robotics"), QStringLiteral("Inspection HMI"));
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
        record(false, QStringLiteral("잠금 상태에서 시도"));
        return false;
    }

    if (!checkPassword(password)) {
        registerFailure();
        if (err)
            *err = QStringLiteral("관리자 비밀번호가 일치하지 않습니다.");
        record(false, QStringLiteral("비밀번호 불일치"));
        return false;
    }

    clearFailures();
    record(true, QStringLiteral("관리자 인증 성공"));
    return true;
}

void Session::record(bool accepted, const QString &detail)
{
    // 로그인 화면은 창과 이벤트 로그가 생기기 전에 돈다. 그때의 시도를
    // 흘려보내면 이력은 재미있는 대목이 지난 뒤부터 시작한다. 들을 사람이
    // 없으면 쌓아 두었다가 넘긴다.
    if (receivers(SIGNAL(authAttempt(bool, QString))) > 0) {
        emit authAttempt(accepted, detail);
        return;
    }
    pending_.append({QDateTime::currentDateTime(), accepted, detail});
    // 쌓인 것을 아무도 가져가지 않는 경우까지 대비해 한도를 둔다.
    while (pending_.size() > 32)
        pending_.removeFirst();
}

QList<Session::Attempt> Session::takePendingAttempts()
{
    return std::exchange(pending_, {});
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

void Session::signInAsDeveloper()
{
    signedIn_ = true;
    role_ = Role::Admin;
    // 이력에 실제 조작자가 아님이 드러나야 한다. "관리자" 같은 그럴듯한 이름을
    // 쓰면 나중에 로그를 읽는 사람이 진짜 사람인 줄 안다.
    displayName_ = QStringLiteral("개발 빌드");
    signedInAt_ = QDateTime::currentDateTime();
    emit signedInChanged();
}

void Session::signOut()
{
    signedIn_ = false;
    displayName_.clear();
    role_ = Role::Operator;
    emit signedInChanged();
}

}  // namespace hmi::auth
