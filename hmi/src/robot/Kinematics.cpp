#include "robot/Kinematics.h"

#include <QMatrix4x4>
#include <QVector3D>
#include <QtMath>

#include <array>
#include <cmath>

#include "RobotDef.h"

namespace hmi::robot {

namespace {

/// The fixed part of one chain step, then the joint's own rotation about z.
///
/// Straight out of `fairino3_v6.urdf`: each joint's `<origin xyz rpy>` followed
/// by a revolute joint whose axis is z. Written this way rather than as a
/// Denavit-Hartenberg table because that is the form FAIRINO publish, and a
/// hand-derived DH table is one transcription step away from being subtly wrong
/// with nothing to check it against.
QMatrix4x4 linkStep(const ArmLink &link, double theta)
{
    QMatrix4x4 m;
    m.translate(float(link.x), float(link.y), float(link.z));
    // Qt applies these in the order written, and MJCF/URDF rpy is R = Rz Ry Rx.
    m.rotate(qRadiansToDegrees(link.yaw), 0, 0, 1);
    m.rotate(qRadiansToDegrees(link.pitch), 0, 1, 0);
    m.rotate(qRadiansToDegrees(link.roll), 1, 0, 0);
    m.rotate(qRadiansToDegrees(theta), 0, 0, 1);
    return m;
}

QMatrix4x4 flangeTransform(const QList<double> &q)
{
    QMatrix4x4 t;
    for (int i = 0; i < kArmJointCount; ++i)
        t *= linkStep(kFr3Chain[size_t(i)], q.value(i));
    return t;
}

/// 회전행렬을 ZYX 오일러각(roll·pitch·yaw)으로. 짐벌락 근처에서는 roll 을
/// 0 으로 두고 yaw 에 몰아준다 — 두 각의 합만 정해지는 자리라 어느 한 쪽을
/// 고정하지 않으면 값이 미끄러진다.
void toRpy(const QMatrix4x4 &m, double &roll, double &pitch, double &yaw)
{
    const double r20 = m(2, 0);
    pitch = std::asin(qBound(-1.0, -r20, 1.0));

    if (std::abs(r20) > 0.99999) {
        roll = 0.0;
        yaw = std::atan2(-m(0, 1), m(1, 1));
        return;
    }
    roll = std::atan2(m(2, 1), m(2, 2));
    yaw = std::atan2(m(1, 0), m(0, 0));
}

QMatrix4x4 fromRpy(double roll, double pitch, double yaw)
{
    QMatrix4x4 m;
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);

    m(0, 0) = float(cy * cp);
    m(0, 1) = float(cy * sp * sr - sy * cr);
    m(0, 2) = float(cy * sp * cr + sy * sr);
    m(1, 0) = float(sy * cp);
    m(1, 1) = float(sy * sp * sr + cy * cr);
    m(1, 2) = float(sy * sp * cr - cy * sr);
    m(2, 0) = float(-sp);
    m(2, 1) = float(cp * sr);
    m(2, 2) = float(cp * cr);
    return m;
}

/// 두 자세 사이의 회전 오차를 축·각 벡터로. 세 각을 그냥 빼면 짐벌락
/// 근처에서 오차가 튀어 해가 발산한다.
QVector3D orientationError(const QMatrix4x4 &current, const QMatrix4x4 &target)
{
    // R_err = R_target * R_current^T
    double r[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k)
                sum += double(target(i, k)) * double(current(j, k));
            r[i][j] = sum;
        }

    const double trace = r[0][0] + r[1][1] + r[2][2];
    const double angle = std::acos(qBound(-1.0, (trace - 1.0) / 2.0, 1.0));
    if (angle < 1e-9)
        return {};

    const double s = 2.0 * std::sin(angle);
    return QVector3D(float((r[2][1] - r[1][2]) / s * angle),
                     float((r[0][2] - r[2][0]) / s * angle),
                     float((r[1][0] - r[0][1]) / s * angle));
}

/// 6×6 대칭 양정부호 계에 대한 가우스 소거. Eigen 을 들이지 않으려고
/// 직접 푼다 — 이 한 곳 때문에 임치 목록과 에어갭 재빌드에 의존성을
/// 하나 더 얹을 이유가 없다.
bool solve6(double a[6][7], double out[6])
{
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 6; ++r)
            if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                pivot = r;
        if (std::abs(a[pivot][col]) < 1e-12)
            return false;
        if (pivot != col)
            for (int c = 0; c <= 6; ++c)
                std::swap(a[col][c], a[pivot][c]);

        for (int r = 0; r < 6; ++r) {
            if (r == col)
                continue;
            const double f = a[r][col] / a[col][col];
            for (int c = col; c <= 6; ++c)
                a[r][c] -= f * a[col][c];
        }
    }
    for (int i = 0; i < 6; ++i)
        out[i] = a[i][6] / a[i][i];
    return true;
}

}  // namespace

QList<QMatrix4x4> jointFrames(const QList<double> &joints)
{
    QList<QMatrix4x4> frames;
    QMatrix4x4 t;
    frames << t;
    for (int i = 0; i < kArmJointCount; ++i) {
        t *= linkStep(kFr3Chain[size_t(i)], joints.value(i));
        frames << t;
    }
    return frames;
}

QList<QVector3D> jointOrigins(const QList<double> &joints)
{
    QList<QVector3D> origins;
    for (const auto &f : jointFrames(joints))
        origins << f.map(QVector3D(0, 0, 0));
    return origins;
}

EePose forwardKinematics(const QList<double> &joints)
{
    const QMatrix4x4 t = flangeTransform(joints);
    EePose p;
    p.x = t(0, 3);
    p.y = t(1, 3);
    p.z = t(2, 3);
    toRpy(t, p.roll, p.pitch, p.yaw);
    return p;
}

std::optional<QList<double>> inverseKinematics(const EePose &target, const QList<double> &seed)
{
    if (seed.size() < kArmJointCount)
        return std::nullopt;

    QMatrix4x4 goal = fromRpy(target.roll, target.pitch, target.yaw);
    goal(0, 3) = float(target.x);
    goal(1, 3) = float(target.y);
    goal(2, 3) = float(target.z);

    QList<double> q = seed;

    // 감쇠 최소자승. 특이자세 근처에서 야코비안이 나빠지는데, 감쇠가 없으면
    // 거기서 관절이 폭주한다 — 화면에서는 팔이 튕겨 나가는 것으로 보인다.
    // 6 축은 여유자유도가 없어서 7 축보다 특이자세에 자주 닿는다. 감쇠가
    // 하는 일이 그만큼 커졌다.
    constexpr double kDamping = 0.06;
    constexpr double kPosTol = 0.001;    // 1 mm
    constexpr double kRotTol = 0.005;    // 약 0.3도
    constexpr int kMaxIters = 120;
    constexpr double kDelta = 1e-5;      // 수치 야코비안 미분폭

    for (int iter = 0; iter < kMaxIters; ++iter) {
        const QMatrix4x4 cur = flangeTransform(q);

        const QVector3D dp(float(target.x - cur(0, 3)),
                           float(target.y - cur(1, 3)),
                           float(target.z - cur(2, 3)));
        const QVector3D dw = orientationError(cur, goal);

        if (dp.length() < kPosTol && dw.length() < kRotTol)
            return q;

        // 수치 야코비안. 해석 야코비안이 빠르지만, 링크 표가 바뀌면 따로
        // 고쳐야 하는 식이 하나 더 생긴다. 갱신은 슬라이더를 끄는 동안만
        // 일어나므로 이 비용은 문제가 되지 않는다.
        double J[6][kArmJointCount];
        for (int j = 0; j < kArmJointCount; ++j) {
            QList<double> qp = q;
            qp[j] += kDelta;
            const QMatrix4x4 pert = flangeTransform(qp);

            J[0][j] = (pert(0, 3) - cur(0, 3)) / kDelta;
            J[1][j] = (pert(1, 3) - cur(1, 3)) / kDelta;
            J[2][j] = (pert(2, 3) - cur(2, 3)) / kDelta;

            const QVector3D dr = orientationError(cur, pert);
            J[3][j] = dr.x() / kDelta;
            J[4][j] = dr.y() / kDelta;
            J[5][j] = dr.z() / kDelta;
        }

        // (J Jᵀ + λ²I) y = e,  dq = Jᵀ y
        double aug[6][7];
        for (int r = 0; r < 6; ++r) {
            for (int c = 0; c < 6; ++c) {
                double sum = 0.0;
                for (int k = 0; k < kArmJointCount; ++k)
                    sum += J[r][k] * J[c][k];
                aug[r][c] = sum + (r == c ? kDamping * kDamping : 0.0);
            }
        }
        const double e[6] = {dp.x(), dp.y(), dp.z(), dw.x(), dw.y(), dw.z()};
        for (int r = 0; r < 6; ++r)
            aug[r][6] = e[r];

        double y[6];
        if (!solve6(aug, y))
            return std::nullopt;

        for (int j = 0; j < kArmJointCount; ++j) {
            double dq = 0.0;
            for (int r = 0; r < 6; ++r)
                dq += J[r][j] * y[r];

            // 한 걸음을 제한한다. 크게 뛰면 가동 범위를 넘나들며 진동한다.
            q[j] = qBound(kFr3Joints[size_t(j)].lo,
                          q[j] + qBound(-0.20, dq, 0.20),
                          kFr3Joints[size_t(j)].hi);
        }
    }
    return std::nullopt;
}

}  // namespace hmi::robot
