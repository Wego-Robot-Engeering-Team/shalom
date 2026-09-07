#include "robot/PoseCheck.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>

#include <cmath>

#include "RobotDef.h"
#include "robot/Kinematics.h"

namespace hmi::robot {

namespace {

/// 가동 한계에서 이만큼 안쪽까지는 경고한다. 약 5도.
constexpr double kLimitMargin = 0.09;

/// 축이 이만큼 안에서 정렬되면 자유도 하나를 잃는다. 약 6도.
constexpr double kAlignMargin = 0.10;

/// 링크 원점이 트렁크 안으로 이 만큼 들어오면 간섭으로 본다. 원점은 점이고
/// 링크는 두께가 있으므로 상자를 조금 키운다.
constexpr double kBodyMargin = 0.03;

}  // namespace

PoseWarning checkArmPose(const QList<double> &joints)
{
    if (joints.size() < kArmJointCount)
        return {};

    // 1) 가동 한계. 가장 확실하고, 넘기면 로봇이 그냥 거부한다.
    for (int i = 0; i < kArmJointCount; ++i) {
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
    const auto origins = jointOrigins(joints);
    const Box &b = kB2TrunkInArmFrame;
    for (const auto &o : origins) {
        if (o.z() < b.maxZ + kBodyMargin
            && o.x() > b.minX - kBodyMargin && o.x() < b.maxX + kBodyMargin
            && o.y() > b.minY - kBodyMargin && o.y() < b.maxY + kBodyMargin) {
            return {QStringLiteral("danger"),
                    QStringLiteral("팔이 로봇 몸통과 겹칩니다. 이 자세로는 보낼 수 없습니다.")};
        }
    }

    // 3) 이 팔이 자유도를 잃는 두 자세. 로봇이 특이자세를 판정하지만, 보내기
    //    전에 알면 다시 시도할 필요가 없다.
    //
    //    어느 관절에서 무너지는지는 URDF 체인의 야코비안을 직접 훑어서
    //    확인했다 (117649 자세). J5 나 J3 가 0 이면 |det J| 가 그대로 0 이
    //    되고, 나머지 네 축은 0 이어도 조작성이 떨어지지 않는다. Franka 를
    //    전제한 이전 판정(J4 가 상한 근처)은 이 팔에서는 아무 의미가 없다.
    if (std::abs(joints.at(4)) < kAlignMargin) {
        return {QStringLiteral("warn"),
                QStringLiteral("손목 축이 일직선입니다. 이 자세에서는 한 방향으로 못 움직입니다.")};
    }
    if (std::abs(joints.at(2)) < kAlignMargin) {
        return {QStringLiteral("warn"),
                QStringLiteral("팔꿈치가 다 펴졌습니다. 이 자세에서는 한 방향으로 못 움직입니다.")};
    }

    return {};
}

}  // namespace hmi::robot
