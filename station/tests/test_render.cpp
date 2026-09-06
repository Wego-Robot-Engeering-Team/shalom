// 모든 화면을 실제로 그려 보는 연기 테스트.
//
// 다른 테스트는 계산과 규약만 확인한다. 그리기 코드는 한 번도 실행되지
// 않았고, 그래서 페인트 경로에서만 터지는 버그를 놓쳤다. 실제로:
// 무명 네임스페이스의 monoFont 헬퍼가 같은 이름의 theme::monoFont 대신
// 자기 자신을 부르는 바람에 로봇팔 화면을 여는 순간 무한 재귀로 죽었다.
// 모든 단위 테스트가 통과한 상태였다.
//
// grab() 은 페인트 이벤트를 동기로 돌린다. 화면이 없어도 되도록
// offscreen 플랫폼에서 실행한다.

#include <QTest>
#include <QtMath>

#include <QLabel>

#include "MainWindow.h"
#include "panels/ArmPanel.h"
#include "robot/Kinematics.h"
#include "sim/SimRobot.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"
#include "widgets/NotificationCenter.h"
#include "widgets/ValueSlider.h"

using namespace gcs;

namespace {

/// 창을 만들어 한 화면씩 그려 본다. 테마마다 색을 다시 계산하는 위젯이
/// 있어 두 테마 모두 돌린다.
void paintEveryView(const QString &theme)
{
    theme::setTheme(theme);
    qApp->setStyleSheet(theme::buildQss());

    auto *robot = new sim::SimRobot;
    ui::MainWindow window(robot);
    window.resize(1400, 900);
    window.show();

    static const ui::NavItem kAll[] = {
        ui::NavItem::Drive,       ui::NavItem::Locations, ui::NavItem::Arm,
        ui::NavItem::Capture,     ui::NavItem::Diagnostics,
        ui::NavItem::Data,        ui::NavItem::Events,
    };

    for (ui::NavItem item : kAll) {
        window.showView(item);
        QVERIFY2(!window.grab().isNull(),
                 qPrintable(QStringLiteral("%1 테마에서 화면 %2 를 그리지 못했다")
                                .arg(theme)
                                .arg(int(item))));
    }

    // 수동 모드에서만 나타나는 조작 패널도 한 번 그린다.
    window.setDriveMode(QStringLiteral("manual"));
    window.showView(ui::NavItem::Drive);
    QVERIFY(!window.grab().isNull());
}

}  // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:

    void everyView_paints_light() { paintEveryView(QStringLiteral("light")); }
    void everyView_paints_dark() { paintEveryView(QStringLiteral("dark")); }

    /// 대화상자는 창 계층 밖이라 위 순회에 걸리지 않는다.
    void dialogs_paint()
    {
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());

        ui::WelcomeDialog welcome;
        QVERIFY(!welcome.grab().isNull());

        ui::SettingsDialog settings;
        for (int tab = 0; tab < 5; ++tab) {
            settings.setCurrentTab(tab);
            QVERIFY2(!settings.grab().isNull(),
                     qPrintable(QStringLiteral("설정 %1 번째 탭을 그리지 못했다").arg(tab)));
        }
    }

    /// 자세 경고는 아이콘 하나뿐이라, 설명이 도구 설명에 들어가지 않으면
    /// 화면에 "!" 만 뜨고 무엇이 문제인지 알 방법이 없어진다.
    void poseWarning_carriesItsExplanation()
    {
        ui::ArmPanel arm;

        // 로봇이 홈 자세에 있다고 알린 뒤, 슬라이더를 손목 축이 일직선이
        // 되는 자리로 옮긴다 — 경고가 떠야 하는 자세다.
        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785};
        arm.setArmState(home, 0.09, 0.06);
        arm.applyPresetToSliders(QStringLiteral("stow"));

        auto *badge = arm.findChild<QLabel *>(QStringLiteral("PoseWarning"));
        QVERIFY2(badge, "자세 경고 배지를 찾지 못했다");

        // 조건부로 검사하면 배지가 안 뜨는 채로도 통과한다. 이 자세에서는
        // 반드시 떠야 하므로 그것부터 못 박는다.
        QVERIFY2(!badge->isHidden(), "수납 자세인데 경고가 뜨지 않았다");
        QVERIFY2(!badge->toolTip().isEmpty(),
                 "경고 아이콘이 떴는데 설명이 비어 있다");
    }

    /// 관절 탭과 끝단 탭은 같은 하나의 자세를 다르게 적은 것이다. 둘이
    /// 어긋나면 조작자는 자기가 보낼 자세를 화면에서 읽을 수 없다.
    ///
    /// 예전에는 끝단 탭의 "지금" 이 마지막으로 보낸 값이었다. 팔이 실제로
    /// 어디 있는지와 아무 상관이 없는 숫자였고, 아무것도 보내지 않은
    /// 상태에서는 코드에 박아 둔 상수였다.
    void jointAndEeTabs_describeTheSamePose()
    {
        ui::ArmPanel arm;

        // 이름으로 찾는다. findChildren 의 순서는 계약이 아니다.
        QList<ui::ValueSlider *> joints;
        for (int i = 1; i <= 7; ++i) {
            auto *s = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint%1").arg(i));
            QVERIFY2(s, qPrintable(QStringLiteral("관절 %1 슬라이더가 없다").arg(i)));
            joints << s;
        }
        QList<ui::ValueSlider *> ee;
        for (const char *axis : {"x", "y", "z", "roll", "pitch", "yaw"}) {
            auto *s = arm.findChild<ui::ValueSlider *>(
                QStringLiteral("Ee_%1").arg(QLatin1String(axis)));
            QVERIFY2(s, qPrintable(QStringLiteral("끝단 %1 슬라이더가 없다")
                                       .arg(QLatin1String(axis))));
            ee << s;
        }
        const auto sliders = joints + ee;

        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785};
        arm.setArmState(home, 0.09, 0.06);

        // 끝단의 "지금" 은 실제 관절에서 나와야 한다.
        //
        // 뒤 세 축은 각도라 한 바퀴 차이는 같은 값이다. 실제로 이 자세의
        // Roll 은 정확히 ±π 자리에 있어서, 어느 부호로 나오는지는 부동소수
        // 반올림에 달렸다 — macOS 와 Ubuntu 가 서로 다르게 냈다.
        const auto pose = robot::forwardKinematics(home);
        const double want[] = {pose.x, pose.y, pose.z, pose.roll, pose.pitch, pose.yaw};
        for (int i = 0; i < 6; ++i) {
            double d = qAbs(ee.at(i)->actual() - want[i]);
            if (i >= 3)
                d = qMin(d, 2 * M_PI - d);
            QVERIFY2(d < 1e-6,
                     qPrintable(QStringLiteral("끝단 %1 번 축의 기준값이 정기구학과 다르다: "
                                               "%2 vs %3")
                                    .arg(i)
                                    .arg(ee.at(i)->actual())
                                    .arg(want[i])));
        }

        // 아직 아무것도 지시하지 않았다. 어느 탭에도 "보내지 않은 편집" 이
        // 떠 있으면 안 된다.
        for (auto *s : sliders)
            QVERIFY2(!s->diverged(),
                     qPrintable(QStringLiteral("아무것도 만지지 않았는데 %1 이 "
                                               "보내지 않은 편집으로 표시된다 "
                                               "(지금 %2, 보낼 값 %3)")
                                    .arg(s->objectName())
                                    .arg(s->actual())
                                    .arg(s->command())));

        // 관절을 만지면 끝단 명령값이 따라온다.
        joints.at(1)->setCommand(-1.2);
        QList<double> moved;
        for (auto *s : joints)
            moved << s->command();
        const auto after = robot::forwardKinematics(moved);

        // 슬라이더는 눈금 1000 칸이라 명령값이 그만큼 반올림된다. X 는 2 m
        // 범위이므로 한 칸이 2 mm 다. 그 안에 들어오면 따라온 것이다.
        const double stepX = 2.0 / 1000.0;
        QVERIFY2(qAbs(ee.at(0)->command() - after.x) <= stepX,
                 qPrintable(QStringLiteral("관절을 옮겼는데 끝단 X 가 따라오지 "
                                           "않았다: %1 vs %2")
                                .arg(ee.at(0)->command())
                                .arg(after.x)));
        QVERIFY2(qAbs(ee.at(2)->command() - after.z) <= stepX,
                 qPrintable(QStringLiteral("관절을 옮겼는데 끝단 Z 가 따라오지 "
                                           "않았다: %1 vs %2")
                                .arg(ee.at(2)->command())
                                .arg(after.z)));
    }

    /// -π 과 π 은 같은 각도다. 순환 값으로 다루지 않으면 같은 자세가 가장
    /// 크게 어긋난 것으로 나오고, 끝단 탭을 여는 순간 Roll 이 보내지 않은
    /// 편집으로 표시된다.
    void cyclicSlider_treatsBothEndsAsOneValue()
    {
        ui::ValueSlider s(QStringLiteral("Roll"), -M_PI, M_PI, QStringLiteral("°"), 0,
                          180.0 / M_PI);
        s.setCommand(M_PI);
        s.setActual(-M_PI);
        QVERIFY2(s.diverged(), "순환으로 표시하기 전에는 어긋난 것으로 보인다");

        s.setCyclic(true);
        QVERIFY2(!s.diverged(), "-π 과 π 은 같은 각도인데 어긋난 것으로 표시된다");

        // 정말로 다른 각도는 순환이어도 잡아야 한다.
        s.setActual(0.0);
        QVERIFY2(s.diverged(), "순환 값이라고 실제로 다른 각도까지 놓치면 안 된다");
    }

    /// 알림 목록은 항목이 있을 때와 없을 때 그리는 경로가 다르다.
    void notificationPopup_paints()
    {
        ui::NotificationPopup empty({});
        QVERIFY(!empty.grab().isNull());

        ui::NotificationPopup filled({
            {QDateTime::currentDateTime(), QStringLiteral("사람 접근 감지"),
             QStringLiteral("인원을 이격시키십시오"), QStringLiteral("warn")},
            {QDateTime::currentDateTime(), QStringLiteral("지도 불러오기 완료"), {},
             QStringLiteral("ok")},
        });
        QVERIFY(!filled.grab().isNull());
    }
};

QTEST_MAIN(TestRender)
#include "test_render.moc"
