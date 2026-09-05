// 코드 카탈로그 테스트.
//
// 핵심은 마지막 두 케이스다: 코드가 UI/브릿지/문서 세 곳에 쓰이므로
// 한 곳에만 추가하고 나머지를 빠뜨리는 실수를 기계로 잡는다.

#include <QTest>

#include "diag/CodeCatalog.h"
#include "net/Envelope.h"

using namespace gcs::diag;

class TestCatalog : public QObject {
    Q_OBJECT

private slots:

    void loads()
    {
        const auto &c = CodeCatalog::instance();
        QVERIFY2(c.isLoaded(), qPrintable(c.loadError()));
        QVERIFY(c.all().size() > 40);
    }

    /// 아래 순회 테스트들은 카탈로그가 비면 공허하게 통과한다.
    /// 매 케이스 앞에서 로드 여부를 먼저 확인한다.
    void init()
    {
        const auto &c = CodeCatalog::instance();
        QVERIFY2(c.isLoaded(), qPrintable(QStringLiteral("카탈로그 미로드: %1").arg(c.loadError())));
        QVERIFY(!c.all().isEmpty());
    }

    void noDuplicateCodes()
    {
        QStringList seen;
        for (const auto &e : CodeCatalog::instance().all()) {
            QVERIFY2(!seen.contains(e.code), qPrintable(QStringLiteral("중복 코드: %1").arg(e.code)));
            seen << e.code;
        }
    }

    void everyEntryHasRequiredText()
    {
        for (const auto &e : CodeCatalog::instance().all()) {
            QVERIFY2(!e.title.isEmpty(), qPrintable(e.code + " 제목 누락"));
            QVERIFY2(!e.cause.isEmpty(), qPrintable(e.code + " 원인 누락"));
            QVERIFY2(!e.category.isEmpty(), qPrintable(e.code + " 분류 누락"));
        }
    }

    /// 조치가 필요한 심각도(warn 이상)에는 조치 방법이 반드시 있어야 한다.
    /// 운용자는 철도 직원이지 로봇 엔지니어가 아니다 — 코드만 던지면 소용없다.
    void actionableSeveritiesHaveActions()
    {
        for (const auto &e : CodeCatalog::instance().all()) {
            if (e.severity == Severity::Warn || e.severity == Severity::Error
                || e.severity == Severity::Critical) {
                QVERIFY2(!e.actions.isEmpty(),
                         qPrintable(QStringLiteral("%1 (%2) 에 조치 방법이 없다")
                                        .arg(e.code, severityToString(e.severity))));
            }
        }
    }

    void severityRoundTrip()
    {
        for (auto s : {Severity::Info, Severity::Ok, Severity::Warn,
                       Severity::Error, Severity::Critical}) {
            QCOMPARE(severityFromString(severityToString(s)), s);
            QVERIFY(!severityLabel(s).isEmpty());
        }
    }

    void lookup_unknownCodeReturnsNull()
    {
        QVERIFY(CodeCatalog::instance().find(QStringLiteral("NO_SUCH_CODE")) == nullptr);
    }

    /// 프로토콜 오류 코드(명세 §7)가 전부 카탈로그에 있어야 한다.
    /// 브릿지가 돌려준 코드를 UI 가 설명하지 못하면 (i) 아이콘이 빈 껍데기가 된다.
    void allProtocolErrorsAreDocumented()
    {
        using namespace gcs::net::err;
        const QStringList protocolCodes{
            QString::fromLatin1(kVersion),        QString::fromLatin1(kUnknownChannel),
            QString::fromLatin1(kBadPayload),     QString::fromLatin1(kEstopEngaged),
            QString::fromLatin1(kMode),           QString::fromLatin1(kBusy),
            QString::fromLatin1(kUnreachable),    QString::fromLatin1(kLimit),
            QString::fromLatin1(kHardware),
        };
        for (const auto &code : protocolCodes) {
            QVERIFY2(CodeCatalog::instance().find(code) != nullptr,
                     qPrintable(QStringLiteral("프로토콜 코드 %1 이 카탈로그에 없다").arg(code)));
        }
    }

    /// 1량 촬영 시간(7분) 측정 보고서가 이 두 코드 구간으로 산출된다.
    /// 이름이 바뀌면 보고서 생성이 조용히 깨지므로 고정한다.
    void missionTimingCodesExist()
    {
        QVERIFY(CodeCatalog::instance().find(QStringLiteral("CAR_START")) != nullptr);
        QVERIFY(CodeCatalog::instance().find(QStringLiteral("CAR_COMPLETE")) != nullptr);
    }
};

QTEST_MAIN(TestCatalog)
#include "test_catalog.moc"
