// 정·역기구학 테스트.
//
// 이 계산은 관절 탭과 끝단 탭을 서로 맞추는 데만 쓰인다. 그래도 틀리면
// 조작자가 보는 숫자가 실제로 보낼 자세와 달라지므로, 왕복이 맞는지는
// 기계로 확인해야 한다.

#include <QTest>
#include <QtMath>

#include "RobotDef.h"
#include "robot/Kinematics.h"

using namespace hmi::robot;

namespace {

/// 두 자세가 같은가. 관제 화면이 보여주는 자릿수(mm, 0.1도)보다 촘촘하게 본다.
bool near(const EePose &a, const EePose &b, double posTol = 2e-3, double rotTol = 8e-3)
{
    return std::abs(a.x - b.x) < posTol && std::abs(a.y - b.y) < posTol
           && std::abs(a.z - b.z) < posTol && std::abs(a.roll - b.roll) < rotTol
           && std::abs(a.pitch - b.pitch) < rotTol && std::abs(a.yaw - b.yaw) < rotTol;
}

QList<double> home()
{
    return {kArmHome.begin(), kArmHome.end()};
}

}  // namespace

class TestKinematics : public QObject {
    Q_OBJECT

private slots:

    /// 홈 자세는 밑동 앞쪽 위에 있어야 한다. 부호가 뒤집혀 있으면 화면에서는
    /// 그럴듯해 보이지만 조작자가 반대쪽으로 보내게 된다.
    ///
    /// 좌우 치우침은 검사하지 않는다. FR3 는 UR 계열처럼 손목 두 마디가
    /// 옆으로 물려 있어서 플랜지가 J1 축 위에 오는 일이 없다 — 어떤 자세든
    /// y 는 0.102 m 근처다. 그걸 0 으로 기대하는 검사는 팔이 아니라 검사가
    /// 틀린 것이다.
    void forward_homeIsInFront()
    {
        const EePose p = forwardKinematics(home());
        QVERIFY2(p.z > 0.3, "홈 자세의 끝단이 밑동보다 위에 있어야 한다");
        QVERIFY2(p.x > 0.1, "홈 자세의 끝단이 밑동보다 앞에 있어야 한다");
        QVERIFY2(std::abs(p.y) < 0.2, "손목 옵셋 이상으로 옆으로 벌어지면 안 된다");
    }

    /// 정기구학이 FAIRINO 의 URDF 와 같은 팔을 말하는가.
    ///
    /// 나머지 검사들은 전부 자기 자신과의 일관성만 본다 — 링크 표를 잘못
    /// 옮겨 적어도 FK 와 IK 는 사이좋게 같은 오답을 내고 왕복 검사는 통과한다.
    /// 그래서 여기서만 바깥 기준을 쓴다: `fairino3_v6.urdf`
    /// (FAIR-INNOVATION/frcobot_ros2) 의 joint origin 을 그대로 곱해서 얻은
    /// 플랜지 위치다. 이 값이 틀리면 화면이 실제와 다른 팔을 그린다.
    void forward_matchesUrdf_data()
    {
        QTest::addColumn<QList<double>>("joints");
        QTest::addColumn<double>("x");
        QTest::addColumn<double>("y");
        QTest::addColumn<double>("z");

        QTest::newRow("home") << home() << 0.24757 << -0.10200 << 0.66268;
        QTest::newRow("standby") << QList<double>{kArmStandby.begin(), kArmStandby.end()}
                                 << 0.22069 << -0.10200 << 0.29185;
        QTest::newRow("stow") << QList<double>{kArmStow.begin(), kArmStow.end()}
                              << 0.27320 << -0.10200 << 0.06563;
    }

    void forward_matchesUrdf()
    {
        QFETCH(QList<double>, joints);
        QFETCH(double, x);
        QFETCH(double, y);
        QFETCH(double, z);

        const EePose p = forwardKinematics(joints);
        // 0.5 mm. 화면이 읽히는 자릿수보다 촘촘하고, float 로 도는 QMatrix4x4
        // 의 누적 오차보다는 넉넉하다.
        constexpr double kTol = 5e-4;
        QVERIFY2(std::abs(p.x - x) < kTol && std::abs(p.y - y) < kTol
                     && std::abs(p.z - z) < kTol,
                 qPrintable(QStringLiteral("URDF %1,%2,%3 → 계산 %4,%5,%6")
                                .arg(x).arg(y).arg(z).arg(p.x).arg(p.y).arg(p.z)));
    }

    /// 관절을 바꾸면 자세가 바뀐다. 상수를 돌려주고 있지 않은지 본다.
    void forward_respondsToJoints()
    {
        auto q = home();
        const EePose before = forwardKinematics(q);
        q[0] += 0.5;
        const EePose after = forwardKinematics(q);
        QVERIFY(!near(before, after));
    }

    /// 왕복. FK 로 얻은 자세를 IK 에 넣으면 같은 자세로 돌아와야 한다.
    /// 관절값 자체는 달라도 된다 — 6 축도 같은 자세를 팔꿈치·손목 방향에 따라
    /// 여러 자세로 만들 수 있다.
    void inverse_roundTrips_data()
    {
        QTest::addColumn<QList<double>>("joints");
        QTest::newRow("home") << home();
        QTest::newRow("standby") << QList<double>{kArmStandby.begin(), kArmStandby.end()};
        QTest::newRow("stow") << QList<double>{kArmStow.begin(), kArmStow.end()};
        QTest::newRow("twisted")
            << QList<double>{0.4, -1.3, -1.9, -1.0, -1.2, 0.9};
    }

    void inverse_roundTrips()
    {
        QFETCH(QList<double>, joints);

        const EePose target = forwardKinematics(joints);
        const auto solved = inverseKinematics(target, home());
        QVERIFY2(solved.has_value(), "도달 가능한 자세인데 해를 못 찾았다");

        const EePose reached = forwardKinematics(*solved);
        QVERIFY2(near(target, reached),
                 qPrintable(QStringLiteral("목표 %1,%2,%3 → 도달 %4,%5,%6")
                                .arg(target.x).arg(target.y).arg(target.z)
                                .arg(reached.x).arg(reached.y).arg(reached.z)));
    }

    /// 해는 가동 범위 안이어야 한다. 범위를 넘긴 값을 보내면 로봇이 거부하고,
    /// 조작자는 왜 안 갔는지 모른다.
    void inverse_staysWithinLimits()
    {
        const EePose target = forwardKinematics({0.4, -1.3, -1.9, -1.0, -1.2, 0.9});
        const auto solved = inverseKinematics(target, home());
        QVERIFY(solved.has_value());

        for (int i = 0; i < kArmJointCount; ++i) {
            QVERIFY2(solved->at(i) >= kFr3Joints[size_t(i)].lo
                         && solved->at(i) <= kFr3Joints[size_t(i)].hi,
                     qPrintable(QStringLiteral("%1 축이 가동 범위를 벗어났다: %2")
                                    .arg(kFr3Joints[size_t(i)].label)
                                    .arg(solved->at(i))));
        }
    }

    /// 팔 길이 밖은 풀 수 없다고 말해야 한다. 억지로 가장 가까운 자세를
    /// 돌려주면 조작자는 자기가 지정한 자리로 간다고 믿는다.
    void inverse_reportsUnreachable()
    {
        EePose faraway;
        faraway.x = 3.0;
        faraway.z = 2.0;
        QVERIFY(!inverseKinematics(faraway, home()).has_value());
    }

    /// 씨앗 근처의 해를 고른다. 같은 자세라도 팔을 반대로 접는 해가 있는데,
    /// 화면에서 팔이 갑자기 뒤집히면 조작자는 자기가 그렇게 시킨 줄 안다.
    void inverse_staysNearSeed()
    {
        auto seed = home();
        auto nudged = seed;
        nudged[1] += 0.15;

        const EePose target = forwardKinematics(nudged);
        const auto solved = inverseKinematics(target, seed);
        QVERIFY(solved.has_value());

        double moved = 0.0;
        for (int i = 0; i < kArmJointCount; ++i)
            moved += std::abs(solved->at(i) - seed.at(i));
        QVERIFY2(moved < 1.0,
                 qPrintable(QStringLiteral("씨앗에서 %1 rad 이나 움직였다").arg(moved)));
    }
};

QTEST_MAIN(TestKinematics)
#include "test_kinematics.moc"
