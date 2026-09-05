// Authentication tests.
//
// This is operational access control, not a security boundary (see
// auth/Session.h). What matters is that it behaves predictably: the right
// password is accepted, the wrong one is not, and repeated failures slow down
// and leave a trace.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "auth/Session.h"

using namespace gcs::auth;

class TestSession : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // 테스트가 개발자의 실제 설정을 건드리지 않게 격리한다.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("WEGO Robotics"));
        QCoreApplication::setApplicationName(QStringLiteral("SHALOM GCS"));
    }

    /// PBKDF2 파생을 표준 벡터로 못박는다.
    ///
    /// 왕복 테스트(설정 → 검증)만으로는 인자를 뒤바꿔 넘기는 실수를 잡을 수
    /// 없다. password 와 salt 를 서로 바꿔도 왕복은 여전히 성공하기 때문이다.
    /// 알려진 답과 대조해야 그런 배선 오류가 드러난다.
    void pbkdf2_matchesKnownAnswer()
    {
        // RFC 계열 표준 벡터: PBKDF2-HMAC-SHA256("password", "salt", c=1, dkLen=32)
        const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha256, QByteArrayLiteral("password"),
            QByteArrayLiteral("salt"), 1, 32);
        QCOMPARE(key.toHex(),
                 QByteArrayLiteral("120fb6cffcf8b32c43e7225256c4f837"
                                   "a86548c92ccc35480805987cb70be17b"));

        // c=2 벡터도 함께 확인해 반복 횟수가 실제로 반영되는지 본다.
        const QByteArray key2 = QPasswordDigestor::deriveKeyPbkdf2(
            QCryptographicHash::Sha256, QByteArrayLiteral("password"),
            QByteArrayLiteral("salt"), 2, 32);
        QCOMPARE(key2.toHex(),
                 QByteArrayLiteral("ae4d0c95af6b46d32d0adff928f06dd0"
                                   "2a303f8ef3c251dfd6e2d85a95474c43"));
        QVERIFY2(key != key2, "반복 횟수가 결과에 반영되어야 한다");
    }

    void setPassword_rejectsTooShort()
    {
        QString err;
        QVERIFY(!Session::instance().setAdminPassword(QStringLiteral("short"), &err));
        QVERIFY(!err.isEmpty());
    }

    void setPassword_thenVerifies()
    {
        QVERIFY(Session::instance().setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QVERIFY(Session::instance().verifyAdmin(QStringLiteral("pit-inspect-2026")));
    }

    void verify_rejectsWrongPassword()
    {
        QVERIFY(Session::instance().setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QString err;
        QVERIFY(!Session::instance().verifyAdmin(QStringLiteral("wrong-password"), &err));
        QVERIFY(!err.isEmpty());
    }

    /// 같은 비밀번호라도 저장할 때마다 salt 가 달라야 한다.
    /// 그러지 않으면 해시 하나로 여러 설치를 동시에 무력화할 수 있다.
    void hashIsSalted()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("same-password-1")));
        QSettings store(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("WEGO Robotics"), QStringLiteral("SHALOM GCS"));
        const QByteArray hash1 = store.value(QStringLiteral("auth/admin_hash")).toByteArray();

        QVERIFY(s.setAdminPassword(QStringLiteral("same-password-1")));
        store.sync();
        const QByteArray hash2 = store.value(QStringLiteral("auth/admin_hash")).toByteArray();

        QVERIFY(!hash1.isEmpty());
        QVERIFY2(hash1 != hash2, "같은 비밀번호가 같은 해시를 내면 salt 가 없는 것이다");
    }

    /// 비밀번호가 평문으로 남으면 안 된다.
    void passwordIsNotStoredInPlaintext()
    {
        const QString pw = QStringLiteral("plaintext-canary-99");
        QVERIFY(Session::instance().setAdminPassword(pw));

        QSettings store(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("WEGO Robotics"), QStringLiteral("SHALOM GCS"));
        store.sync();
        for (const auto &key : store.allKeys()) {
            const QString value = store.value(key).toString();
            QVERIFY2(!value.contains(pw), qPrintable(QStringLiteral("평문 발견: %1").arg(key)));
        }
    }

    void repeatedFailures_triggerLockout()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("pit-inspect-2026")));   // 실패 카운터 초기화

        QString err;
        for (int i = 0; i < 5; ++i)
            s.verifyAdmin(QStringLiteral("nope"), &err);

        QVERIFY2(s.lockoutRemainingSeconds() > 0, "반복 실패 후 잠겨야 한다");
        // 잠긴 동안에는 올바른 비밀번호도 즉시 통과시키지 않는다.
        QVERIFY(!s.verifyAdmin(QStringLiteral("pit-inspect-2026"), &err));

        s.setAdminPassword(QStringLiteral("pit-inspect-2026"));            // 잠금 해제
        QCOMPARE(s.lockoutRemainingSeconds(), 0);
    }

    /// 성공하면 실패 카운터가 초기화되어야 한다. 그러지 않으면 오타 몇 번에
    /// 정상 사용 중에도 잠긴다.
    void successClearsFailureCount()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QString err;
        s.verifyAdmin(QStringLiteral("nope"), &err);
        s.verifyAdmin(QStringLiteral("nope"), &err);
        QVERIFY(s.verifyAdmin(QStringLiteral("pit-inspect-2026")));

        for (int i = 0; i < 4; ++i)
            s.verifyAdmin(QStringLiteral("nope"), &err);
        QCOMPARE(s.lockoutRemainingSeconds(), 0);
    }

    void signIn_requiresNameForAuditTrail()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QString err;
        QVERIFY(!s.signIn(QStringLiteral("   "), Role::Operator, {}, &err));
        QVERIFY(!err.isEmpty());
    }

    void signIn_operatorNeedsNoPassword()
    {
        auto &s = Session::instance();
        QVERIFY(s.signIn(QStringLiteral("김검수"), Role::Operator, {}));
        QVERIFY(s.isSignedIn());
        QCOMPARE(s.role(), Role::Operator);
        QCOMPARE(s.displayName(), QStringLiteral("김검수"));
        s.signOut();
        QVERIFY(!s.isSignedIn());
    }

    void signIn_adminNeedsPassword()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QVERIFY(!s.signIn(QStringLiteral("관리자"), Role::Admin, QStringLiteral("wrong")));
        QVERIFY(!s.isSignedIn());
        QVERIFY(s.signIn(QStringLiteral("관리자"), Role::Admin,
                         QStringLiteral("pit-inspect-2026")));
        QCOMPARE(s.role(), Role::Admin);
        s.signOut();
    }

    /// 인증 시도는 성공·실패 모두 기록 대상이다.
    void authAttempts_areReported()
    {
        auto &s = Session::instance();
        QVERIFY(s.setAdminPassword(QStringLiteral("pit-inspect-2026")));
        QSignalSpy spy(&s, &Session::authAttempt);
        s.verifyAdmin(QStringLiteral("nope"));
        s.verifyAdmin(QStringLiteral("pit-inspect-2026"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
        QCOMPARE(spy.at(1).at(0).toBool(), true);
    }

    void cleanupTestCase()
    {
        QSettings store(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("WEGO Robotics"), QStringLiteral("SHALOM GCS"));
        store.clear();
    }
};

QTEST_MAIN(TestSession)
#include "test_session.moc"
