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
#include <QRegularExpression>
#include <QTest>

#include "net/Channels.h"
#include "net/Envelope.h"

class TestDocs : public QObject {
    Q_OBJECT

private:
    static QString protocolDoc()
    {
        // 문서는 레포 루트의 docs/ 아래에 있다.
        QFile f(QStringLiteral(GCS_SOURCE_DIR "/../docs/bridge_protocol.md"));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private slots:

    void documentExists()
    {
        QVERIFY2(!protocolDoc().isEmpty(),
                 "docs/bridge_protocol.md 를 읽지 못했다 — 경로를 확인할 것");
    }

    /// 코드에 있는 채널이 명세에 전부 적혀 있어야 한다.
    void everyChannelIsDocumented()
    {
        const QString doc = protocolDoc();
        QVERIFY(!doc.isEmpty());

        const QStringList channels{
            QLatin1String(gcs::ch::kPose),        QLatin1String(gcs::ch::kBattery),
            QLatin1String(gcs::ch::kSystem),      QLatin1String(gcs::ch::kSafety),
            QLatin1String(gcs::ch::kNav),         QLatin1String(gcs::ch::kPlan),
            QLatin1String(gcs::ch::kTrail),       QLatin1String(gcs::ch::kArm),
            QLatin1String(gcs::ch::kApriltag),    QLatin1String(gcs::ch::kMission),
            QLatin1String(gcs::ch::kWaypoints),   QLatin1String(gcs::ch::kLog),
            QLatin1String(gcs::ch::kMap),         QLatin1String(gcs::ch::kPreview),
            QLatin1String(gcs::ch::kCaptureSpool),
            QLatin1String(gcs::ch::kCmdEstop),    QLatin1String(gcs::ch::kCmdEstopRelease),
            QLatin1String(gcs::ch::kCmdMode),     QLatin1String(gcs::ch::kCmdGoto),
            QLatin1String(gcs::ch::kCmdNavCancel),
            QLatin1String(gcs::ch::kCmdWaypointsSet),
            QLatin1String(gcs::ch::kCmdMissionStart),
            QLatin1String(gcs::ch::kCmdMissionPause),
            QLatin1String(gcs::ch::kCmdMissionResume),
            QLatin1String(gcs::ch::kCmdMissionStop),
            QLatin1String(gcs::ch::kCmdArmPreset),
            QLatin1String(gcs::ch::kCmdArmJointGoal),
            QLatin1String(gcs::ch::kCmdArmEeGoal),
            QLatin1String(gcs::ch::kCmdArmStop),
            QLatin1String(gcs::ch::kCmdCapture),  QLatin1String(gcs::ch::kCmdVel),
        };

        QStringList missing;
        for (const auto &ch : channels)
            if (!doc.contains(ch))
                missing << ch;

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("명세서에 없는 채널: %1")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// 프로토콜 오류 코드도 마찬가지.
    void everyErrorCodeIsDocumented()
    {
        const QString doc = protocolDoc();
        using namespace gcs::net::err;
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

    /// 문서가 선언한 프로토콜 버전과 코드가 같아야 한다.
    void protocolVersionMatches()
    {
        const QString doc = protocolDoc();
        QVERIFY(doc.contains(QStringLiteral("프로토콜 통신 규약 v%1")
                                 .arg(gcs::net::kProtocolVersion))
                || doc.contains(QStringLiteral("현재 `%1`").arg(gcs::net::kProtocolVersion)));
    }
};

QTEST_APPLESS_MAIN(TestDocs)
#include "test_docs.moc"
