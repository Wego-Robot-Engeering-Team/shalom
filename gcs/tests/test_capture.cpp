// Location capture validation tests.
//
// A waypoint recorded from a bad pose looks perfectly fine on screen and only
// reveals itself during the acceptance run, at which point correcting it means
// putting the robot back under the train. These rules are what prevent that,
// so they are pinned here rather than left to the panel's UI state.

#include <QTest>

#include "panels/LocationPanel.h"

using namespace gcs::ui;

namespace {

RobotSnapshot healthy()
{
    RobotSnapshot s;
    s.x = 3.0;
    s.y = -1.0;
    s.theta = 0.4;
    s.speed = 0.0;
    s.poseFresh = true;
    s.localizationOk = true;
    s.visibleTagId = 14;
    return s;
}

}  // namespace

class TestCapture : public QObject {
    Q_OBJECT

private slots:

    void healthySnapshot_isAllowed()
    {
        const auto r = LocationPanel::checkCapture(healthy(), QStringLiteral("inspection"));
        QVERIFY(r.allowed);
        QVERIFY(!r.degraded);
        QCOMPARE(r.code, QStringLiteral("LOC_CAPTURED"));
    }

    /// 이동 중 좌표는 뭉개진다. 10 Hz 갱신에 0.2 m/s 면 프레임 사이 2 cm 다.
    void movingRobot_isBlocked()
    {
        auto s = healthy();
        s.speed = 0.2;
        const auto r = LocationPanel::checkCapture(s, QStringLiteral("inspection"));
        QVERIFY(!r.allowed);
        QCOMPARE(r.code, QStringLiteral("LOC_CAPTURE_BLOCKED"));
        QVERIFY2(r.reason.contains(QStringLiteral("이동")), "사유에 원인이 드러나야 한다");
    }

    /// 아주 느린 잔여 움직임까지 막으면 현장에서 등록이 안 된다.
    void negligibleDrift_isAllowed()
    {
        auto s = healthy();
        s.speed = 0.02;
        QVERIFY(LocationPanel::checkCapture(s, QStringLiteral("inspection")).allowed);
    }

    /// 링크가 끊긴 동안의 마지막 좌표를 저장하면 실제와 무관한 값이 남는다.
    void stalePose_isBlocked()
    {
        auto s = healthy();
        s.poseFresh = false;
        const auto r = LocationPanel::checkCapture(s, QStringLiteral("inspection"));
        QVERIFY(!r.allowed);
        QCOMPARE(r.code, QStringLiteral("LOC_CAPTURE_BLOCKED"));
    }

    /// 정지해 있어도 위치 정보가 오래되었으면 막아야 한다.
    /// 두 조건은 독립이며, 하나만 보면 놓친다.
    void stalePoseWhileStationary_isStillBlocked()
    {
        auto s = healthy();
        s.speed = 0.0;
        s.poseFresh = false;
        QVERIFY(!LocationPanel::checkCapture(s, QStringLiteral("inspection")).allowed);
    }

    /// 신뢰도 저하는 차단이 아니라 경고다. 현장에서 아예 등록을 못 하게 하면
    /// 조작자가 우회 방법을 찾게 된다.
    void degradedLocalization_warnsButAllows()
    {
        auto s = healthy();
        s.localizationOk = false;
        const auto r = LocationPanel::checkCapture(s, QStringLiteral("inspection"));
        QVERIFY(r.allowed);
        QVERIFY(r.degraded);
        QCOMPARE(r.code, QStringLiteral("LOC_CAPTURE_DEGRADED"));
    }

    /// 점검포인트는 마커 연결이 없으면 2차 정밀 보정을 쓸 수 없다.
    void inspectionWithoutTag_warns()
    {
        auto s = healthy();
        s.visibleTagId = -1;
        const auto r = LocationPanel::checkCapture(s, QStringLiteral("inspection"));
        QVERIFY(r.allowed);
        QVERIFY(r.degraded);
        QVERIFY(r.reason.contains(QStringLiteral("Apriltag")));
    }

    /// 충전 스테이션과 시작 위치는 마커 보정을 쓰지 않으므로 경고 대상이 아니다.
    void dockWithoutTag_isClean()
    {
        auto s = healthy();
        s.visibleTagId = -1;
        for (const auto &kind : {QStringLiteral("dock"), QStringLiteral("home")}) {
            const auto r = LocationPanel::checkCapture(s, kind);
            QVERIFY2(r.allowed, qPrintable(kind));
            QVERIFY2(!r.degraded, qPrintable(kind + QStringLiteral(" 는 마커가 필요 없다")));
        }
    }

    /// 차단 조건이 경고 조건보다 우선해야 한다. 순서가 뒤집히면
    /// 이동 중인데도 "주의" 로만 표시되어 등록이 통과한다.
    void blockingBeatsWarning()
    {
        auto s = healthy();
        s.speed = 0.5;
        s.localizationOk = false;
        s.visibleTagId = -1;
        const auto r = LocationPanel::checkCapture(s, QStringLiteral("inspection"));
        QVERIFY(!r.allowed);
    }
};

QTEST_APPLESS_MAIN(TestCapture)
#include "test_capture.moc"
