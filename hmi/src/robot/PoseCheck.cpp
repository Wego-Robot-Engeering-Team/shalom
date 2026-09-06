#include "robot/PoseCheck.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>

#include <cmath>

#include "RobotDef.h"

namespace hmi::robot {

namespace {

/// 가동 한계에서 이만큼 안쪽까지는 경고한다. 약 5도.
constexpr double kLimitMargin = 0.09;

/// 축이 이만큼 안에서 정렬되면 자유도 하나를 잃는다. 약 6도.
constexpr double kAlignMargin = 0.10;

/// B2 몸통 윗면 높이와 반경. Robot3DView 의 형상과 같은 값을 쓴다 —
/// 화면에서 팔이 몸통을 뚫고 들어가 보이는데 경고가 없으면 둘 중 하나가
/// 틀린 것이고, 조작자는 어느 쪽인지 알 수 없다.
constexpr double kBodyTopZ = 0.10;    ///< 팔 밑동 기준. 이보다 낮으면 몸통 안이다.
constexpr double kBodyHalfX = 0.33;
constexpr double kBodyHalfY = 0.18;

/// 팔 밑동 기준 링크 원점. Robot3DView 와 같은 DH 를 쓴다.
QList<QVector3D> linkOrigins(const QList<double> &q);

}  // namespace

PoseWarning checkArmPose(const QList<double> &joints)
{
    if (joints.size() < 7)
        return {};

    // 1) 가동 한계. 가장 확실하고, 넘기면 로봇이 그냥 거부한다.
    for (int i = 0; i < 7; ++i) {
        const auto &j = kFr3Joints[size_t(i)];
        const double v = joints.at(i);
        if (v <= j.lo + kLimitMargin || v >= j.hi - kLimitMargin) {
            return {QStringLiteral("danger"),
                    QStringLiteral("%1 축이 가동 한계에 닿았습니다. 더 보내면 거부됩니다.")
                        .arg(j.label)};
        }
    }

    // 2) 몸통 간섭. 정확한 충돌 검사는 로봇이 한다. 여기서는 팔 마디가
    //    B2 몸통 위에 겹쳐 내려앉는 명백한 경우만 잡는다.
    const auto origins = linkOrigins(joints);
    for (const auto &o : origins) {
        if (o.z() < kBodyTopZ && std::abs(o.x()) < kBodyHalfX
            && std::abs(o.y()) < kBodyHalfY) {
            return {QStringLiteral("danger"),
                    QStringLiteral("팔이 로봇 몸통과 겹칩니다. 이 자세로는 보낼 수 없습니다.")};
        }
    }

    // 3) FR3 가 자유도를 잃는 두 자세. 로봇이 특이자세를 판정하지만, 보내기
    //    전에 알면 다시 시도할 필요가 없다.
    if (std::abs(joints.at(4)) < kAlignMargin) {
        return {QStringLiteral("warn"),
                QStringLiteral("손목 축이 일직선입니다. 이 자세에서는 한 방향으로 못 움직입니다.")};
    }
    if (joints.at(3) > kFr3Joints[3].hi - 0.25) {
        return {QStringLiteral("warn"),
                QStringLiteral("팔이 거의 다 펴졌습니다. 더 뻗으면 움직임이 막힙니다.")};
    }

    return {};
}

namespace {

QList<QVector3D> linkOrigins(const QList<double> &q)
{
    // FR3 수정 DH. Robot3DView 와 같은 표를 쓴다 — 두 곳이 갈라지면 화면과
    // 경고가 서로 다른 팔을 말하게 된다.
    struct Dh { double a, d, alpha; };
    static const Dh kDh[8] = {
        {0.0, 0.333, 0.0},        {0.0, 0.0, -M_PI_2},     {0.0, 0.316, M_PI_2},
        {0.0825, 0.0, M_PI_2},    {-0.0825, 0.384, -M_PI_2},
        {0.0, 0.0, M_PI_2},       {0.088, 0.0, M_PI_2},    {0.0, 0.107, 0.0},
    };

    QList<QVector3D> out;
    QMatrix4x4 t;
    out << t.map(QVector3D(0, 0, 0));
    for (int i = 0; i < 8; ++i) {
        const double th = i < 7 ? q.value(i) : 0.0;
        const Dh &d = kDh[i];

        QMatrix4x4 step;
        step(0, 0) = float(std::cos(th));
        step(0, 1) = float(-std::sin(th));
        step(0, 3) = float(d.a);
        step(1, 0) = float(std::sin(th) * std::cos(d.alpha));
        step(1, 1) = float(std::cos(th) * std::cos(d.alpha));
        step(1, 2) = float(-std::sin(d.alpha));
        step(1, 3) = float(-d.d * std::sin(d.alpha));
        step(2, 0) = float(std::sin(th) * std::sin(d.alpha));
        step(2, 1) = float(std::cos(th) * std::sin(d.alpha));
        step(2, 2) = float(std::cos(d.alpha));
        step(2, 3) = float(d.d * std::cos(d.alpha));

        t *= step;
        out << t.map(QVector3D(0, 0, 0));
    }
    return out;
}

}  // namespace

}  // namespace hmi::robot
