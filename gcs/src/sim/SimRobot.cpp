#include "sim/SimRobot.h"

#include <QRandomGenerator>
#include <QtMath>

#include "RobotDef.h"

namespace gcs::sim {

using gcs::map::MapInfo;
using gcs::map::normalizeAngle;

namespace {

constexpr double kResolution = 0.05;   // 5 cm/셀
constexpr int kW = 720;                // 36 m
constexpr int kH = 420;                // 21 m

/// 수동 조작 데드맨. 브릿지와 같은 값을 쓴다 (명세 §3.1).
/// UI 가 멈춰도 로봇이 계속 달리지 않게 하는 것이 목적이다.
constexpr double kDeadmanSec = 0.3;

/// 목표 도달 판정. 과업지시서의 주행 정밀도 요구(±10 cm)와 같은 크기로 둔다.
constexpr double kGoalTolerance = 0.10;
constexpr double kHeadingTolerance = 0.09;   // 약 5도 — 방향오차 요구와 동일

/// 자율주행 속도. 미등록 물체 접근 시 감속 기준(30 cm/s)보다 조금 높게 잡되,
/// B2 상한은 넘지 않는다.
constexpr double kAutoLinear = 0.45;
constexpr double kAutoAngular = 0.7;

/// 촬영 포인트에서 머무는 시간. 로봇팔 포지셔닝과 촬영을 합친 값이다.
constexpr double kDwellSec = 3.0;

/// 관절이 목표를 따라가는 속도. 실제 FR3 한계보다 보수적으로 둔다.
constexpr double kJointRate = 0.9;   // rad/s

inline void fillRect(QList<qint8> &g, int x0, int y0, int x1, int y1, qint8 v)
{
    for (int y = qMax(0, y0); y < qMin(kH, y1); ++y)
        for (int x = qMax(0, x0); x < qMin(kW, x1); ++x)
            g[qsizetype(y) * kW + x] = v;
}

}  // namespace

// ============================ 맵 / 초기 데이터 ============================

MapData buildMap()
{
    QList<qint8> g(qsizetype(kW) * kH, qint8(-1));   // 미탐색으로 시작

    fillRect(g, 30, 30, kW - 30, kH - 30, 0);        // 검수고 내부

    fillRect(g, 30, 30, kW - 30, 38, 100);           // 외벽
    fillRect(g, 30, kH - 38, kW - 30, kH - 30, 100);
    fillRect(g, 30, 30, 38, kH - 30, 100);
    fillRect(g, kW - 38, 30, kW - 30, kH - 30, 100);

    // 열차 하부 점검 피트 — 좌우 벽 두 줄. 그 사이가 로봇 주행로.
    const int pitTop = 150, pitBottom = 270;
    fillRect(g, 90, pitTop, kW - 90, pitTop + 6, 100);
    fillRect(g, 90, pitBottom, kW - 90, pitBottom + 6, 100);

    for (int cx = 120; cx < kW - 100; cx += 118) {   // 량 구분 기둥
        fillRect(g, cx, pitTop - 26, cx + 10, pitTop, 100);
        fillRect(g, cx, pitBottom + 6, cx + 10, pitBottom + 32, 100);
    }

    fillRect(g, 60, 60, 120, 96, 100);               // 충전 스테이션
    fillRect(g, 66, 66, 114, 90, 0);

    auto *rng = QRandomGenerator::global();
    for (qsizetype i = 0; i < g.size(); ++i)
        if (g[i] == 0 && rng->generateDouble() < 0.0012)
            g[i] = qint8(rng->bounded(25, 70));

    const auto info = MapInfo::create(kW, kH, kResolution,
                                      -kW * kResolution / 2, -kH * kResolution / 2,
                                      0.0, QStringLiteral("gtxa_pit_demo"));
    return {*info, g};
}

QList<QVariantMap> buildWaypoints()
{
    QList<QVariantMap> wps;
    int n = 0;
    for (int car = 1; car <= 3; ++car) {
        for (int pt = 1; pt <= 4; ++pt) {
            QVariantMap w;
            w["id"] = QStringLiteral("C%1-P%2")
                          .arg(car, 2, 10, QLatin1Char('0'))
                          .arg(pt, 2, 10, QLatin1Char('0'));
            w["name"] = QStringLiteral("%1량 P%2").arg(car).arg(pt);
            w["car"] = car;
            w["x"] = -14.0 + n * 2.35;
            w["y"] = 0.0;
            w["theta"] = 0.0;
            w["tag_id"] = 10 + n;
            w["status"] = QStringLiteral("todo");
            wps << w;
            ++n;
        }
    }
    return wps;
}

QList<QVariantMap> buildTags(const QList<QVariantMap> &waypoints)
{
    QList<QVariantMap> tags;
    for (const auto &w : waypoints) {
        const int id = w.value("tag_id").toInt();
        // 각 포인트 양옆 벽에 마커가 붙는다 (지시서 2.2.2 로봇암 제어 항목).
        tags << QVariantMap{{"id", id}, {"x", w.value("x")}, {"y", 3.0}};
        tags << QVariantMap{{"id", id + 100}, {"x", w.value("x")}, {"y", -3.0}};
    }
    return tags;
}

// ============================ SimRobot ============================

SimRobot::SimRobot(QObject *parent) : QObject(parent)
{
    joints_ = {robot::kArmHome.begin(), robot::kArmHome.end()};
    jointTarget_ = joints_;
    waypoints_ = buildWaypoints();
}

void SimRobot::setWaypoints(const QList<QVariantMap> &waypoints)
{
    waypoints_ = waypoints;
    activeIndex_ = -1;
}

// ---- 명령 --------------------------------------------------------------

void SimRobot::setCmdVel(double vx, double vy, double wz)
{
    if (estop_ || mode_ != DriveMode::Manual)
        return;
    cmdVx_ = vx;
    cmdVy_ = vy;
    cmdWz_ = wz;
    sinceCmdVel_ = 0.0;
}

void SimRobot::requestGoal(double x, double y, double theta)
{
    if (estop_) {
        emit robotEvent(QStringLiteral("E_ESTOP_ENGAGED"), {});
        return;
    }
    if (mode_ != DriveMode::Auto) {
        emit robotEvent(QStringLiteral("E_MODE"),
                   {{"reason", QStringLiteral("수동 모드에서는 목표 이동을 실행하지 않습니다")}});
        return;
    }
    hasGoal_ = true;
    goalX_ = x;
    goalY_ = y;
    goalTheta_ = theta;
    navStatus_ = QStringLiteral("driving");
    // 목표 이동과 미션 순회는 같은 주행기를 쓰므로 동시에 돌 수 없다.
    if (mission_ == MissionState::Running)
        mission_ = MissionState::Paused;
}

void SimRobot::cancelNav()
{
    hasGoal_ = false;
    navStatus_ = QStringLiteral("idle");
}

void SimRobot::missionStart()
{
    if (estop_ || mode_ != DriveMode::Auto)
        return;
    for (int i = 0; i < waypoints_.size(); ++i)
        setWaypointStatus(i, QStringLiteral("todo"));
    activeIndex_ = -1;
    currentCar_ = -1;
    hasGoal_ = false;
    mission_ = MissionState::Running;
    emit missionStateChanged(mission_);
}

void SimRobot::missionPause()
{
    if (mission_ != MissionState::Running)
        return;
    mission_ = MissionState::Paused;
    emit missionStateChanged(mission_);
}

void SimRobot::missionResume()
{
    if (mission_ != MissionState::Paused || estop_)
        return;
    mission_ = MissionState::Running;
    emit missionStateChanged(mission_);
}

void SimRobot::missionStop()
{
    mission_ = MissionState::Idle;
    activeIndex_ = -1;
    emit missionStateChanged(mission_);
}

void SimRobot::engageEstop()
{
    estop_ = true;
    // 즉시 정지. 감속 곡선을 그리지 않는다 — 1초 이내 완전 정지 요구다.
    cmdVx_ = cmdVy_ = cmdWz_ = 0.0;
    speed_ = 0.0;
    hasGoal_ = false;
    navStatus_ = QStringLiteral("idle");
    jointTarget_ = joints_;          // 로봇팔도 그 자리에 선다
    if (mission_ == MissionState::Running) {
        mission_ = MissionState::Paused;
        emit missionStateChanged(mission_);
    }
}

void SimRobot::releaseEstop()
{
    estop_ = false;
    // 자율주행은 자동 재개하지 않는다. 명시적 재개 명령이 필요하다.
}

void SimRobot::setMode(DriveMode mode)
{
    mode_ = mode;
    cmdVx_ = cmdVy_ = cmdWz_ = 0.0;
    if (mode_ == DriveMode::Manual) {
        // 수동 조작이 자율주행보다 우선한다 (지시서 2.2.5).
        hasGoal_ = false;
        navStatus_ = QStringLiteral("idle");
        if (mission_ != MissionState::Idle) {
            mission_ = MissionState::Idle;
            activeIndex_ = -1;
            emit missionStateChanged(mission_);
        }
    }
}

void SimRobot::setArmJointGoal(const QList<double> &q)
{
    if (estop_) {
        emit robotEvent(QStringLiteral("E_ESTOP_ENGAGED"), {});
        return;
    }
    if (q.size() == jointTarget_.size())
        jointTarget_ = q;
}

void SimRobot::setArmPreset(const QString &name)
{
    if (estop_) {
        emit robotEvent(QStringLiteral("E_ESTOP_ENGAGED"), {});
        return;
    }
    const std::array<double, 7> *preset = nullptr;
    if (name == QLatin1String("home"))
        preset = &robot::kArmHome;
    else if (name == QLatin1String("standby"))
        preset = &robot::kArmStandby;
    else if (name == QLatin1String("stow"))
        preset = &robot::kArmStow;
    if (preset)
        jointTarget_ = {preset->begin(), preset->end()};
}

void SimRobot::stopArm()
{
    jointTarget_ = joints_;
}

// ---- 적분 --------------------------------------------------------------

void SimRobot::integrate(double dt, double vx, double vy, double wz)
{
    // 유니사이클 + 횡이동. B2 는 게걸음이 가능하므로 vy 를 살려둔다.
    const double c = std::cos(theta_);
    const double s = std::sin(theta_);
    x_ += (vx * c - vy * s) * dt;
    y_ += (vx * s + vy * c) * dt;
    theta_ = normalizeAngle(theta_ + wz * dt);
    speed_ = std::hypot(vx, vy);
}

bool SimRobot::driveToward(double dt, double tx, double ty, bool alignHeading,
                           double targetTheta)
{
    const double dx = tx - x_;
    const double dy = ty - y_;
    const double dist = std::hypot(dx, dy);

    if (dist > kGoalTolerance) {
        // 먼저 목표 방향으로 돌고, 어느 정도 정렬되면 전진한다.
        // 방향이 크게 어긋난 채 전진하면 호를 그리며 벽에 붙는다.
        const double bearing = normalizeAngle(std::atan2(dy, dx) - theta_);
        const double wz = qBound(-kAutoAngular, bearing * 1.8, kAutoAngular);
        const double vx = qAbs(bearing) < 0.5
                              ? qMin(kAutoLinear, dist * 1.2)
                              : 0.0;
        integrate(dt, vx, 0.0, wz);
        return false;
    }

    if (alignHeading) {
        const double err = normalizeAngle(targetTheta - theta_);
        if (qAbs(err) > kHeadingTolerance) {
            integrate(dt, 0.0, 0.0, qBound(-kAutoAngular, err * 1.8, kAutoAngular));
            return false;
        }
    }

    speed_ = 0.0;
    return true;
}

int SimRobot::nextPendingWaypoint() const
{
    for (int i = 0; i < waypoints_.size(); ++i)
        if (waypoints_.at(i).value(QStringLiteral("status")).toString()
            != QLatin1String("done"))
            return i;
    return -1;
}

void SimRobot::setWaypointStatus(int index, const QString &status)
{
    if (index < 0 || index >= waypoints_.size())
        return;
    waypoints_[index][QStringLiteral("status")] = status;
}

void SimRobot::stepMission(double dt)
{
    if (activeIndex_ < 0) {
        activeIndex_ = nextPendingWaypoint();
        if (activeIndex_ < 0) {
            mission_ = MissionState::Idle;
            emit missionStateChanged(mission_);
            emit robotEvent(QStringLiteral("RETURN_TO_DOCK"), {});
            return;
        }
        setWaypointStatus(activeIndex_, QStringLiteral("current"));
        dwell_ = 0.0;

        const int car = waypoints_.at(activeIndex_).value(QStringLiteral("car")).toInt();
        if (car != currentCar_) {
            // 1량 촬영 시간(7분) 측정 구간의 시작점이다.
            currentCar_ = car;
            emit robotEvent(QStringLiteral("CAR_START"), {{"car", car}});
        }
    }

    const QVariantMap &wp = waypoints_.at(activeIndex_);
    const double tx = wp.value(QStringLiteral("x")).toDouble();
    const double ty = wp.value(QStringLiteral("y")).toDouble();

    if (!driveToward(dt, tx, ty, true, wp.value(QStringLiteral("theta")).toDouble()))
        return;

    // 도착 후 정지 상태에서 촬영한다 (동적 촬영 불가).
    if (dwell_ == 0.0) {
        emit robotEvent(QStringLiteral("WAYPOINT_REACHED"),
                   {{"point_id", wp.value(QStringLiteral("id"))}});
        setArmPreset(QStringLiteral("standby"));
    }
    dwell_ += dt;

    if (dwell_ >= kDwellSec) {
        emit robotEvent(QStringLiteral("CAPTURE_OK"),
                   {{"point_id", wp.value(QStringLiteral("id"))}});
        // 촬영 원본은 로봇 로컬 스풀에 쌓였다가 NAS 로 올라간다 (명세 §6.3).
        pendingUploads_ += 2;   // 2D + 3D
        setWaypointStatus(activeIndex_, QStringLiteral("done"));

        const int car = wp.value(QStringLiteral("car")).toInt();
        const int next = nextPendingWaypoint();
        const bool carDone =
            next < 0 || waypoints_.at(next).value(QStringLiteral("car")).toInt() != car;
        if (carDone)
            emit robotEvent(QStringLiteral("CAR_COMPLETE"), {{"car", car}});

        activeIndex_ = -1;
        dwell_ = 0.0;
    }
}

void SimRobot::stepArm(double dt)
{
    const double maxStep = kJointRate * dt;
    for (int i = 0; i < joints_.size() && i < jointTarget_.size(); ++i) {
        const double err = jointTarget_.at(i) - joints_.at(i);
        joints_[i] += qBound(-maxStep, err, maxStep);
    }
}

// ---- 한 스텝 ------------------------------------------------------------

Telemetry SimRobot::step(double dt)
{
    t_ += dt;
    sinceCmdVel_ += dt;

    if (estop_) {
        speed_ = 0.0;
    } else if (mode_ == DriveMode::Manual) {
        // 데드맨: 명령이 끊기면 즉시 0 으로 래치한다.
        if (sinceCmdVel_ > kDeadmanSec)
            cmdVx_ = cmdVy_ = cmdWz_ = 0.0;
        integrate(dt, cmdVx_, cmdVy_, cmdWz_);
    } else if (hasGoal_) {
        if (driveToward(dt, goalX_, goalY_, true, goalTheta_)) {
            hasGoal_ = false;
            navStatus_ = QStringLiteral("arrived");
            emit robotEvent(QStringLiteral("WAYPOINT_REACHED"),
                       {{"point_id", QStringLiteral("목표 지점")}});
        }
    } else if (mission_ == MissionState::Running) {
        stepMission(dt);
    } else {
        speed_ = 0.0;
    }

    if (!estop_)
        stepArm(dt);

    trail_ << QPointF(x_, y_);
    if (trail_.size() > 1200)
        trail_.removeFirst();

    // 배터리는 활동량에 비례해 줄어든다. 정지 중에도 대기 전력은 있다.
    soc_ = qMax(0.0, soc_ - dt * (0.02 + speed_ * 0.05));

    // 업로드는 주행과 무관하게 진행된다.
    uploadTimer_ += dt;
    if (uploadTimer_ > 1.5 && pendingUploads_ > 0) {
        uploadTimer_ = 0.0;
        if (--pendingUploads_ == 0)
            emit robotEvent(QStringLiteral("UPLOAD_COMPLETE"), {});
    }

    Telemetry tm;
    tm.x = x_;
    tm.y = y_;
    tm.theta = theta_;
    tm.speed = speed_;
    tm.trail = trail_;
    tm.soc = soc_;
    tm.joints = joints_;
    tm.estop = estop_;
    tm.navStatus = navStatus_;

    // 계획 경로: 현재 위치에서 남은 포인트들
    if (mission_ == MissionState::Running) {
        tm.plan << QPointF(x_, y_);
        for (const auto &w : waypoints_)
            if (w.value(QStringLiteral("status")).toString() != QLatin1String("done"))
                tm.plan << QPointF(w.value(QStringLiteral("x")).toDouble(),
                                   w.value(QStringLiteral("y")).toDouble());
    } else if (hasGoal_) {
        tm.plan << QPointF(x_, y_) << QPointF(goalX_, goalY_);
    }

    // 팔꿈치(J4)가 펴질수록 조작성이 급감하는 특이자세를 모사한다.
    const double elbow = qAbs(joints_.value(3) + 0.1518) / 2.89;
    tm.manipulability = qMax(0.004, 0.115 * std::pow(elbow, 0.7));
    tm.sigmaMin = qMax(0.002, 0.085 * std::pow(elbow, 0.8));

    // 정지해 있고 포인트 근처일 때만 마커가 잡힌다고 본다.
    if (speed_ < 0.05) {
        for (const auto &w : waypoints_) {
            const double d = std::hypot(w.value(QStringLiteral("x")).toDouble() - x_,
                                        w.value(QStringLiteral("y")).toDouble() - y_);
            if (d < 1.2)
                tm.seenTags.insert(w.value(QStringLiteral("tag_id")).toInt());
        }
    }

    auto *rng = QRandomGenerator::global();
    tm.cpu = 34 + 22 * qAbs(std::sin(t_ * 0.3)) + speed_ * 20;
    tm.mem = 51 + 8 * qAbs(std::sin(t_ * 0.17));
    tm.cpuTemp = 58 + 14 * qAbs(std::sin(t_ * 0.11));
    tm.gpuTemp = 62 + 16 * qAbs(std::sin(t_ * 0.13));
    tm.rtt = 18 + 14 * rng->generateDouble();

    // ---- 센서·링크 건강 상태 ----
    // 판정(state)은 브릿지가 내려주는 값이다(명세 §9.2). 여기서는 그 형태를
    // 그대로 흉내 내, UI 가 판정 로직을 갖지 않도록 한다.
    auto sensor = [](const char *id, const char *name, double hz, double actual,
                     const QString &state, const QString &detail = {}) {
        gcs::ui::SensorHealth h;
        h.id = QString::fromLatin1(id);
        h.name = QString::fromUtf8(name);
        h.expectedHz = hz;
        h.actualHz = actual;
        h.state = state;
        h.detail = detail;
        return h;
    };

    const QString ok = QStringLiteral("ok");
    const double jitter = 1.0 - 0.02 * rng->generateDouble();
    tm.sensors = {
        sensor("lidar", "LiDAR", 10, 10 * jitter, ok,
               QStringLiteral("%1k pts").arg(38.4 + rng->generateDouble(), 0, 'f', 1)),
        sensor("imu", "IMU", 200, 200 * jitter, ok),
        sensor("aurora", "Aurora S", 20, 20 * jitter, ok),
        sensor("cam_body", "본체 카메라", 15, 15 * jitter, ok),
        // 촬영 카메라는 이동 중에 프레임을 흘리는 상황을 재현한다.
        sensor("cam_arm_2d", "암 2D 카메라", 15,
               speed_ > 0.2 ? 7.5 : 15 * jitter,
               speed_ > 0.2 ? QStringLiteral("degraded") : ok,
               speed_ > 0.2 ? QStringLiteral("프레임 드롭") : QString()),
        sensor("cam_arm_3d", "암 3D 카메라", 10, 10 * jitter, ok),
        sensor("joints_b2", "B2 관절", 50, 50 * jitter, ok),
        sensor("joints_fr3", "FR3 관절", 100, 100 * jitter, ok),
    };

    tm.link.connected = true;
    tm.link.rttMs = tm.rtt;
    tm.link.rssiDbm = -52 - 8 * rng->generateDouble();
    tm.link.rxBytesPerS = 38000 + 6000 * rng->generateDouble();
    tm.link.txBytesPerS = 1500 + 600 * rng->generateDouble();
    tm.link.seqGaps = seqGaps_;
    tm.link.decodeErrors = 0;
    tm.link.reconnects = 0;

    tm.nasOnline = true;
    tm.pendingUploads = pendingUploads_;
    tm.spoolFreeMb = 412000;
    return tm;
}

}  // namespace gcs::sim
