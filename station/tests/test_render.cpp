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

#include <QLabel>

#include "MainWindow.h"
#include "panels/ArmPanel.h"
#include "sim/SimRobot.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"
#include "widgets/NotificationCenter.h"

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
