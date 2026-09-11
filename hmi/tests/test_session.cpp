// 로그인은 조작 이력에 이름을 남기기 위한 것이다. 막는 장치가 아니므로
// 검사할 것도 많지 않다 — 맞는 자격증명을 받아들이고, 틀린 것을 거절하며,
// 거절했을 때 아무도 서명되지 않은 채로 남는지만 본다.
//
// 마지막 항목이 중요하다. 실패한 로그인 뒤에 세션이 서명된 채로 남으면
// 이력에 엉뚱한 이름이 찍히고, 그 이력은 없느니만 못하다.

#include <QtTest>

#include "auth/Session.h"

using hmi::auth::Session;

class TestSession : public QObject {
    Q_OBJECT

private slots:
    void init() { Session::instance().signOut(); }

    void starts_signed_out()
    {
        QVERIFY(!Session::instance().isSignedIn());
        QVERIFY(Session::instance().displayName().isEmpty());
    }

    void accepts_the_credential()
    {
        QString err;
        QVERIFY2(Session::instance().signIn(QStringLiteral("admin"),
                                            QStringLiteral("admin"), &err),
                 qPrintable(err));
        QVERIFY(Session::instance().isSignedIn());
        QCOMPARE(Session::instance().displayName(), QStringLiteral("admin"));
    }

    void rejects_wrong_password()
    {
        QString err;
        QVERIFY(!Session::instance().signIn(QStringLiteral("admin"),
                                            QStringLiteral("nope"), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!Session::instance().isSignedIn());
        QVERIFY(Session::instance().displayName().isEmpty());
    }

    void rejects_wrong_id()
    {
        QVERIFY(!Session::instance().signIn(QStringLiteral("root"),
                                            QStringLiteral("admin")));
        QVERIFY(!Session::instance().isSignedIn());
    }

    /// 실패해도 직전 세션이 남지 않는다.
    void failure_does_not_keep_previous_name()
    {
        QVERIFY(Session::instance().signIn(QStringLiteral("admin"),
                                           QStringLiteral("admin")));
        Session::instance().signOut();
        QVERIFY(!Session::instance().signIn(QStringLiteral("admin"),
                                            QStringLiteral("wrong")));
        QVERIFY(Session::instance().displayName().isEmpty());
    }

    void reports_sign_in_changes()
    {
        QSignalSpy spy(&Session::instance(), &Session::signedInChanged);
        QVERIFY(Session::instance().signIn(QStringLiteral("admin"),
                                           QStringLiteral("admin")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
        Session::instance().signOut();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }
};

QTEST_MAIN(TestSession)
#include "test_session.moc"
