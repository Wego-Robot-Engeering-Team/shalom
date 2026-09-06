// Documentation drift checks.
//
// The protocol document is a contractual deliverable (the "ROS2 interface
// specification"), and it is the only thing the robot-side team builds
// against. A channel that exists in the code but not in the document, or the
// reverse, means one of the two teams is working from something wrong.
//
// These checks are deliberately crude - substring presence - because the goal
// is to catch a forgotten edit, not to parse Markdown. A crude check that
// runs on every build is worth more than a precise one that nobody runs.

#include <QFile>
#include <QIODevice>
#include <QRegularExpression>
#include <QTest>

#include "RobotDef.h"
#include "net/Channels.h"
#include "net/Envelope.h"

class TestDocs : public QObject {
    Q_OBJECT

private:
    static QString protocolDoc()
    {
        // 문서는 레포 루트의 docs/ 아래에 있다.
        QFile f(QStringLiteral(HMI_SOURCE_DIR "/../docs/bridge_protocol.md"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private slots:

    void documentExists()
    {
        QVERIFY2(!protocolDoc().isEmpty(),
                 "docs/bridge_protocol.md 를 읽지 못했다 — 경로를 확인할 것");
    }

    /// 코드에 있는 채널이 명세에 전부 적혀 있어야 한다.
    ///
    /// 목록을 여기에 손으로 적어 두었더니, 새 채널을 헤더에만 넣고 이 목록에
    /// 넣지 않는 일이 생겼다 — 그러면 검사는 통과하고 명세서에는 채널이
    /// 빠진 채로 남는다. 로봇 팀이 보는 것은 명세서뿐이라, 그 채널은
    /// 아무도 구현하지 않는다. 실제로 cmd/power/policy 가 그랬다.
    ///
    /// 그래서 헤더를 읽는다. 목록을 두 곳에 두지 않으면 어긋날 수도 없다.
    void everyChannelIsDocumented()
    {
        const QString doc = protocolDoc();
        QVERIFY(!doc.isEmpty());

        QFile header(QStringLiteral(HMI_SOURCE_DIR "/src/net/Channels.h"));
        QVERIFY2(header.open(QIODevice::ReadOnly), "Channels.h 를 읽지 못했다");
        const QString src = QString::fromUtf8(header.readAll());

        // 원시 문자열을 쓰면 moc 의 전처리기가 괄호를 세다 막힌다.
        static const QRegularExpression decl(
            QStringLiteral("inline constexpr auto \\w+ = \"([^\"]+)\""));
        QStringList channels;
        for (auto it = decl.globalMatch(src); it.hasNext();)
            channels << it.next().captured(1);
        QVERIFY2(channels.size() > 20,
                 qPrintable(QStringLiteral("채널을 %1 개밖에 못 찾았다 — "
                                           "헤더 형식이 바뀌었는지 확인할 것")
                                .arg(channels.size())));

        QStringList missing;
        for (const auto &c : channels)
            if (!doc.contains(c))
                missing << c;
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("명세서에 없는 채널: %1")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// 프로토콜 오류 코드도 마찬가지.
    void everyErrorCodeIsDocumented()
    {
        const QString doc = protocolDoc();
        using namespace hmi::net::err;
        const QStringList codes{
            QLatin1String(kVersion),     QLatin1String(kUnknownChannel),
            QLatin1String(kBadPayload),  QLatin1String(kEstopEngaged),
            QLatin1String(kMode),        QLatin1String(kBusy),
            QLatin1String(kUnreachable), QLatin1String(kLimit),
            QLatin1String(kHardware),
        };
        QStringList missing;
        for (const auto &c : codes)
            if (!doc.contains(c))
                missing << c;
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("명세서에 없는 오류 코드: %1")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// 프레이밍 상수가 코드와 문서에서 같아야 한다. 여기가 어긋나면
    /// 브릿지와 관제가 서로 다른 프레임을 기대하게 된다.
    void framingConstantsMatchDocument()
    {
        const QString doc = protocolDoc();
        QVERIFY2(doc.contains(QStringLiteral("SHLM")), "magic 값이 문서에 없다");
        QVERIFY2(doc.contains(QStringLiteral("32 MiB")), "프레임 상한이 문서에 없다");
        QVERIFY2(doc.contains(QStringLiteral("TCP_NODELAY")),
                 "TCP_NODELAY 요구가 문서에 없다");
    }

    /// 안전 시간값은 설정 화면이 조작자에게 그대로 보여 주는 숫자다.
    /// 로봇이 지키는 값이고 규격이 그 근거이므로, 셋이 어긋나면 화면이
    /// 지키지도 않는 값을 약속하는 셈이 된다.
    void safetyTimingsMatchDocument()
    {
        const QString doc = protocolDoc();
        QVERIFY2(doc.contains(QStringLiteral("%1 ms").arg(hmi::robot::kDeadmanMs)),
                 "데드맨 시간이 문서에 없다");
        QVERIFY2(doc.contains(QStringLiteral("**%1초**").arg(hmi::robot::kLinkLossStopSec))
                     || doc.contains(QStringLiteral("통신 두절 %1초")
                                         .arg(hmi::robot::kLinkLossStopSec)),
                 "통신 두절 정지 시간이 문서에 없다");
    }

    /// 개발 중에 쓰던 이름은 납품물에 남지 않는다.
    ///
    /// 이 문서는 패키지에 함께 설치되는 납품 성과물이라 제목부터 눈에 띈다.
    /// 지금 남은 것은 로봇측 ROS 패키지 이름 하나뿐이고, 그것은 로봇 코드와
    /// 같이 바꿔야 문서가 사실과 어긋나지 않는다. 그 하나만 예외로 둔다 —
    /// 로봇 코드가 합쳐지면 이 예외도 같이 없앤다.
    void codenameIsNotInTheDeliveredDocument()
    {
        QString doc = protocolDoc();
        QVERIFY(!doc.isEmpty());
        doc.remove(QStringLiteral("shalom_bridge"));

        QVERIFY2(!doc.contains(QStringLiteral("shalom"), Qt::CaseInsensitive),
                 "납품 문서에 개발 중 이름이 남아 있다");
    }

    /// 문서가 선언한 프로토콜 버전과 코드가 같아야 한다.
    void protocolVersionMatches()
    {
        const QString doc = protocolDoc();
        QVERIFY(doc.contains(QStringLiteral("프로토콜 통신 규약 v%1")
                                 .arg(hmi::net::kProtocolVersion))
                || doc.contains(QStringLiteral("현재 `%1`").arg(hmi::net::kProtocolVersion)));
    }
};

QTEST_APPLESS_MAIN(TestDocs)
#include "test_docs.moc"
