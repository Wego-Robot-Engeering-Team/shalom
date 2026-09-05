// Simulator behaviour tests.
//
// These encode the safety-relevant rules the statement of work requires, so
// that a later refactor cannot quietly weaken them:
//
//   - the emergency stop halts motion immediately and rejects commands
//   - manual jog is latched to zero when commands stop arriving (deadman)
//   - switching to manual aborts autonomous driving
//   - releasing the emergency stop does not resume anything on its own
//
// The simulator stands in for the robot until the bridge exists, so these are
// also the acceptance criteria the real BridgeClient will have to meet.

#include <QSignalSpy>
#include <QTest>

#include "sim/SimRobot.h"

using namespace gcs::sim;

namespace {

/// dt 를 20 Hz 로 고정해 실제 tick 주기와 같은 조건에서 돌린다.
constexpr double kDt = 0.05;

Telemetry run(SimRobot &r, double seconds)
{
    Telemetry tm;
    for (int i = 0; i < int(seconds / kDt); ++i)
        tm = r.step(kDt);
    return tm;
}

}  // namespace

class TestSim : public QObject {
    Q_OBJECT

private slots:

    // ---- 수동 조작 -------------------------------------------------------

    void manualJog_movesRobot()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        const Telemetry before = r.step(kDt);

        // 조작이 계속되는 동안에만 움직인다.
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.3, 0, 0);
            r.step(kDt);
        }
        const Telemetry after = r.step(kDt);

        QVERIFY2(after.x > before.x + 0.15, "전진 명령으로 실제 이동해야 한다");
    }

    /// 명령이 끊기면 브릿지가 300 ms 안에 0 을 래치한다 (명세 §3.1).
    /// 관제가 멈추거나 링크가 끊겨도 로봇이 계속 달리면 안 된다.
    void manualJog_deadmanLatchesZero()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        for (int i = 0; i < 10; ++i) {
            r.setCmdVel(0.4, 0, 0);
            r.step(kDt);
        }
        QVERIFY(r.step(kDt).speed > 0.1);

        // 명령을 끊는다. 데드맨 시간이 지나면 정지해야 한다.
        const Telemetry stopped = run(r, 0.5);
        QVERIFY2(qFuzzyIsNull(stopped.speed), "명령 중단 후 속도가 0 이어야 한다");
    }

    void manualJog_ignoredInAutoMode()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        const double x0 = r.step(kDt).x;
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.5, 0, 0);
            r.step(kDt);
        }
        QCOMPARE(r.step(kDt).x, x0);
    }

    // ---- 목표 이동 -------------------------------------------------------

    void goal_drivesToTarget()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        const Telemetry start = r.step(kDt);
        r.requestGoal(start.x + 3.0, start.y, 0.0);

        const Telemetry end = run(r, 40.0);
        QVERIFY2(qAbs(end.x - (start.x + 3.0)) < 0.15,
                 qPrintable(QStringLiteral("목표 도달 실패: x=%1").arg(end.x)));
    }

    void goal_rejectedWhileStopped()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        r.engageEstop();

        QSignalSpy spy(&r, &SimRobot::robotEvent);
        const Telemetry before = r.step(kDt);
        r.requestGoal(before.x + 3.0, before.y, 0.0);
        const Telemetry after = run(r, 3.0);

        QCOMPARE(after.x, before.x);
        QVERIFY2(!spy.isEmpty(), "거부 사유가 이벤트로 보고되어야 한다");
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("E_ESTOP_ENGAGED"));
    }

    void goal_rejectedInManualMode()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        QSignalSpy spy(&r, &SimRobot::robotEvent);
        const Telemetry before = r.step(kDt);
        r.requestGoal(before.x + 2.0, before.y, 0.0);
        run(r, 2.0);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("E_MODE"));
    }

    // ---- 비상정지 --------------------------------------------------------

    void estop_stopsMotionImmediately()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.5, 0, 0);
            r.step(kDt);
        }
        QVERIFY(r.step(kDt).speed > 0.1);

        r.engageEstop();
        // 감속 곡선 없이 곧바로 0 이어야 한다 — 1초 이내 완전 정지 요구다.
        QCOMPARE(r.step(kDt).speed, 0.0);
    }

    void estop_ignoresJogWhileEngaged()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        r.engageEstop();
        const double x0 = r.step(kDt).x;
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.5, 0, 0);
            r.step(kDt);
        }
        QCOMPARE(r.step(kDt).x, x0);
    }

    /// 정지 중에도 조작 명령을 받아두면, 해제하는 순간 그 속도로 튀어나간다.
    /// 실제 위험 시나리오다: 조작자가 조이스틱을 쥔 채 정지를 걸고,
    /// 안전을 확인한 뒤 해제하면 로봇이 즉시 움직인다.
    /// setCmdVel 이 정지 상태에서 명령을 거부해야 막힌다.
    void estop_doesNotLungeOnRelease()
    {
        SimRobot r;
        r.setMode(DriveMode::Manual);
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.5, 0, 0);
            r.step(kDt);
        }
        r.engageEstop();

        // 조작자가 계속 누르고 있는 상황.
        for (int i = 0; i < 20; ++i) {
            r.setCmdVel(0.5, 0, 0);
            r.step(kDt);
        }

        const double xBefore = r.step(kDt).x;
        r.releaseEstop();
        const Telemetry justAfter = r.step(kDt);

        QCOMPARE(justAfter.speed, 0.0);
        QVERIFY2(qAbs(justAfter.x - xBefore) < 1e-9,
                 "해제 직후 이전 조작 속도로 움직이면 안 된다");
    }

    void estop_pausesMissionAndDoesNotAutoResume()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        r.missionStart();
        QCOMPARE(r.missionState(), MissionState::Running);
        run(r, 1.0);

        r.engageEstop();
        QCOMPARE(r.missionState(), MissionState::Paused);

        r.releaseEstop();
        // 해제만으로 재개되면 지시서 2.2.5 위반이다.
        QCOMPARE(r.missionState(), MissionState::Paused);
        const Telemetry after = run(r, 2.0);
        QVERIFY2(qFuzzyIsNull(after.speed), "해제 직후 스스로 움직이면 안 된다");

        r.missionResume();
        QCOMPARE(r.missionState(), MissionState::Running);
    }

    void estop_freezesArm()
    {
        SimRobot r;
        r.setArmPreset(QStringLiteral("stow"));
        run(r, 0.5);
        const QList<double> mid = r.step(kDt).joints;

        r.engageEstop();
        const QList<double> after = run(r, 2.0).joints;
        for (int i = 0; i < mid.size(); ++i)
            QVERIFY2(qAbs(after.at(i) - mid.at(i)) < 1e-9, "정지 중 관절이 움직이면 안 된다");
    }

    // ---- 모드 ------------------------------------------------------------

    /// 수동 명령은 자율주행보다 항상 우선한다 (지시서 2.2.5).
    void manualMode_abortsMission()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        r.missionStart();
        run(r, 1.0);
        QCOMPARE(r.missionState(), MissionState::Running);

        r.setMode(DriveMode::Manual);
        QCOMPARE(r.missionState(), MissionState::Idle);
    }

    void manualMode_cancelsActiveGoal()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        const Telemetry start = r.step(kDt);
        r.requestGoal(start.x + 5.0, start.y, 0.0);
        run(r, 1.0);

        r.setMode(DriveMode::Manual);
        const Telemetry a = r.step(kDt);
        const Telemetry b = run(r, 2.0);
        QVERIFY2(qAbs(b.x - a.x) < 1e-6, "모드 전환 후 목표를 향해 계속 가면 안 된다");
    }

    // ---- 미션 ------------------------------------------------------------

    void mission_completesWaypointsInOrder()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        r.missionStart();

        // 12 포인트 × (이동 + 3 초 체류). 넉넉히 돌린다.
        run(r, 400.0);

        int done = 0;
        for (const auto &w : r.waypoints())
            if (w.value(QStringLiteral("status")).toString() == QLatin1String("done"))
                ++done;
        QCOMPARE(done, r.waypoints().size());
        QCOMPARE(r.missionState(), MissionState::Idle);
    }

    void mission_emitsCarTimingCodes()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        QSignalSpy spy(&r, &SimRobot::robotEvent);
        r.missionStart();
        run(r, 400.0);

        int starts = 0, completes = 0;
        for (const auto &sig : spy) {
            const QString code = sig.at(0).toString();
            if (code == QLatin1String("CAR_START"))
                ++starts;
            else if (code == QLatin1String("CAR_COMPLETE"))
                ++completes;
        }
        // 1량 촬영 시간 보고서가 이 구간으로 산출된다. 량마다 한 쌍이어야 한다.
        QCOMPARE(starts, 3);
        QCOMPARE(completes, 3);
    }

    void mission_pauseHaltsProgress()
    {
        SimRobot r;
        r.setMode(DriveMode::Auto);
        r.missionStart();
        run(r, 2.0);
        r.missionPause();

        const Telemetry a = r.step(kDt);
        const Telemetry b = run(r, 3.0);
        QVERIFY(qAbs(b.x - a.x) < 1e-6);
    }

    // ---- 로봇팔 ----------------------------------------------------------

    void arm_movesTowardPreset()
    {
        SimRobot r;
        const QList<double> home = r.step(kDt).joints;
        r.setArmPreset(QStringLiteral("stow"));
        const QList<double> stowed = run(r, 6.0).joints;
        QVERIFY2(qAbs(stowed.at(1) - home.at(1)) > 0.5, "관절이 프리셋을 향해 움직여야 한다");
    }

    void arm_stopHoldsCurrentPosition()
    {
        SimRobot r;
        r.setArmPreset(QStringLiteral("stow"));
        run(r, 0.5);
        r.stopArm();
        const QList<double> a = r.step(kDt).joints;
        const QList<double> b = run(r, 2.0).joints;
        for (int i = 0; i < a.size(); ++i)
            QVERIFY(qAbs(a.at(i) - b.at(i)) < 1e-9);
    }
};

QTEST_APPLESS_MAIN(TestSim)
#include "test_sim.moc"
