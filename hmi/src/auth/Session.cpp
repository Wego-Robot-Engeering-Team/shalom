#include "auth/Session.h"

namespace hmi::auth {
namespace {

// 현장 계정 정책이 정해지기 전까지 쓰는 자리표시 자격증명이다. 값이 코드에
// 박혀 있으므로 이것으로 무엇을 막을 수는 없다 — 이 화면의 목적은 조작
// 이력에 이름을 남기는 것이고, 막는 일은 하드웨어 비상정지와 로봇 안전
// 노드가 한다.
constexpr auto kId = "admin";
constexpr auto kPassword = "admin";

}  // namespace

Session &Session::instance()
{
    static Session s;
    return s;
}

bool Session::signIn(const QString &id, const QString &password, QString *err)
{
    // 어느 쪽이 틀렸는지 말하지 않는다. 아이디가 맞았다는 것만 알려 줘도
    // 절반을 알려 주는 셈이고, 조작자에게는 어차피 같은 대응이다.
    if (id != QLatin1String(kId) || password != QLatin1String(kPassword)) {
        if (err)
            *err = QStringLiteral("아이디 또는 비밀번호가 맞지 않습니다.");
        return false;
    }

    id_ = id;
    signedIn_ = true;
    emit signedInChanged(true);
    return true;
}

void Session::signOut()
{
    if (!signedIn_)
        return;
    signedIn_ = false;
    id_.clear();
    emit signedInChanged(false);
}

}  // namespace hmi::auth
