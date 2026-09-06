// Undercarriage inspection control station - application entry point.

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QHash>
#include <QTimer>

#include "Config.h"
#include "MainWindow.h"
#include "net/BridgeClient.h"
#ifdef GCS_WITH_TESTBED
#    include "sim/SimRobot.h"
#endif
#include "auth/Session.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"
#include "widgets/NotificationCenter.h"

namespace {

/// Registers bundled fonts.
///
/// The Windows default Korean face (Malgun Gothic) noticeably dates the
/// interface; bundling Pretendard keeps rendering identical across Windows and
/// Ubuntu. Falls back to system fonts silently when the resource is absent.
void loadBundledFonts()
{
    const QDir dir(QStringLiteral(":/fonts"));
    if (!dir.exists())
        return;
    for (const auto &f : dir.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
                                       QDir::Files))
        QFontDatabase::addApplicationFont(dir.filePath(f));
}

void applyUiFont(QApplication &app)
{
    const auto families = QFontDatabase::families();
    for (const auto &name : {QStringLiteral("Pretendard Variable"), QStringLiteral("Pretendard"),
                             QStringLiteral("Inter"), QStringLiteral("Helvetica Neue")}) {
        if (families.contains(name)) {
            app.setFont(QFont(name, 10));
            return;
        }
    }
}

/// Grabs the window and exits. Used for layout review during development and
/// for collecting the on-screen state during on-site support.
void captureAndQuit(QWidget *window, const QString &path)
{
    // 시뮬레이터가 궤적과 게이지를 채울 시간을 준다.
    QTimer::singleShot(3000, window, [window, path] {
        window->grab().save(path);
        QApplication::quit();
    });
}

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Inspection GCS"));
    app.setOrganizationName(QStringLiteral("WEGO Robotics"));

    // 플랫폼 네이티브 스타일 대신 Fusion 으로 고정한다.
    //
    // 네이티브 스타일은 macOS·Windows·Linux 에서 위젯 메트릭과 그리기 방식이
    // 제각각이고, 일부는 스타일시트를 부분적으로만 존중한다(탭 바 배경, 정렬 등).
    // 납품물이 Windows 와 Ubuntu 양쪽에서 같아야 하므로, 한 곳에서 검수한 화면이
    // 다른 곳에서 달라지지 않도록 스타일을 통일한다.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    loadBundledFonts();
    applyUiFont(app);

    const QStringList args = QApplication::arguments();

    // 저장된 표시 설정을 먼저 적용한 뒤 스타일시트를 만든다.
    auto &cfg = gcs::Config::instance();
    gcs::theme::setTheme(args.contains(QStringLiteral("--dark")) ? QStringLiteral("dark")
                                                                 : cfg.theme());
    gcs::theme::setUiScale(cfg.uiScale());
    app.setStyleSheet(gcs::theme::buildQss());

    // 개발 중 대화상자 레이아웃 확인용. 납품 빌드에서 --shot 계열과 함께 제거한다.
    const int dialogIdx = args.indexOf(QStringLiteral("--shot-dialog"));
    if (dialogIdx >= 0 && dialogIdx + 2 < args.size()) {
        const QString which = args.at(dialogIdx + 1);
        const QString path = args.at(dialogIdx + 2);
        QWidget *dialog = nullptr;
        if (which == QLatin1String("welcome"))
            dialog = new gcs::ui::WelcomeDialog;
        else if (which == QLatin1String("notifications")) {
            using gcs::ui::Notification;
            const QDateTime now = QDateTime::currentDateTime();
            dialog = new gcs::ui::NotificationPopup({
                {now, QStringLiteral("사람 접근 감지 — 일시정지"),
                 QStringLiteral("작업 구역에서 인원을 이격시키십시오(1m 이상)"),
                 QStringLiteral("warn")},
                {now.addSecs(-42), QStringLiteral("우회 경로 없음 — 정지"),
                 QStringLiteral("경로상 장애물을 제거하십시오"), QStringLiteral("error")},
                {now.addSecs(-95), QStringLiteral("지도 불러오기 완료"), {},
                 QStringLiteral("ok")},
                {now.addSecs(-140), QStringLiteral("저장 위치를 찾을 수 없습니다"),
                 QStringLiteral("/mnt/nas/inspection"), QStringLiteral("warn")},
            });
        } else if (which.startsWith(QLatin1String("settings"))) {
            auto *sd = new gcs::ui::SettingsDialog;
            // "settings:2" 형태로 탭을 지정한다.
            const auto parts = which.split(QLatin1Char(':'));
            if (parts.size() > 1)
                sd->setCurrentTab(parts.at(1).toInt());
            dialog = sd;
        }
        if (!dialog)
            return 2;
        dialog->show();
        QTimer::singleShot(600, dialog, [dialog, path] {
            dialog->grab().save(path);
            QApplication::quit();
        });
        return app.exec();
    }

    // 조작자 확인. 여기서 입력한 이름이 이후 모든 권한 동작의 이력에 남는다.
    //
    // 로그인 생략은 빌드 옵션(GCS_REQUIRE_LOGIN=OFF)으로만 가능하다. 실행 인자로
    // 끌 수 있게 두면 납품 빌드에서도 꺼진 채 나갈 수 있고, 아무도 눈치채지 못한다.
    // --no-login 은 요구가 켜져 있을 때는 무시된다(스크린샷 경로용 잔재 방지).
#if GCS_REQUIRE_LOGIN
    {
        gcs::ui::WelcomeDialog welcome;
        if (welcome.exec() != QDialog::Accepted)
            return 0;
    }
#else
    // 개발 빌드. 권한 동작 이력이 비지 않도록 자리표시 조작자로 서명해 둔다.
    gcs::auth::Session::instance().signInAsDeveloper();
#endif

    // --live 는 실제 브릿지에, 그 외에는 내장 시뮬레이터에 붙는다.
    // 창은 어느 쪽인지 알지 못한다 — 둘 다 RobotLink 를 구현한다.
    gcs::robot::RobotLink *link = nullptr;
    if (args.contains(QStringLiteral("--live"))) {
        // 설정값을 실행 인자로 덮어쓸 수 있게 한다. 현장 지원에서 설정 창을
        // 열지 않고 다른 주소로 붙여봐야 하는 경우가 있다.
        QString host = cfg.bridgeHost();
        int port = cfg.bridgePort();
        const int hostIdx = args.indexOf(QStringLiteral("--host"));
        if (hostIdx >= 0 && hostIdx + 1 < args.size())
            host = args.at(hostIdx + 1);
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        if (portIdx >= 0 && portIdx + 1 < args.size())
            port = args.at(portIdx + 1).toInt();

        auto *bridge = new gcs::net::BridgeClient(host, quint16(port));
        bridge->connectToBridge();
        link = bridge;
    } else {
#ifdef GCS_WITH_TESTBED
        link = new gcs::sim::SimRobot;
#else
        // 납품 빌드에는 시뮬레이터가 없다. 로봇이 없으면 화면이 뜨지 않는
        // 편이, 가짜 데이터로 도는 화면을 현장에서 진짜로 오해하는 것보다
        // 낫다.
        qCritical("로봇 주소를 지정해야 합니다: --live --host <주소> --port <포트>");
        return 2;
#endif
    }

    gcs::ui::MainWindow window(link);
    window.show();

    const int viewIdx = args.indexOf(QStringLiteral("--view"));
    if (viewIdx >= 0 && viewIdx + 1 < args.size()) {
        static const QHash<QString, gcs::ui::NavItem> kViews{
            {QStringLiteral("drive"), gcs::ui::NavItem::Drive},
            {QStringLiteral("locations"), gcs::ui::NavItem::Locations},
            {QStringLiteral("arm"), gcs::ui::NavItem::Arm},
            {QStringLiteral("capture"), gcs::ui::NavItem::Capture},
            {QStringLiteral("diagnostics"), gcs::ui::NavItem::Diagnostics},
            {QStringLiteral("data"), gcs::ui::NavItem::Data},
            {QStringLiteral("events"), gcs::ui::NavItem::Events},
        };
        const auto it = kViews.constFind(args.at(viewIdx + 1));
        if (it != kViews.constEnd())
            window.showView(*it);
    }

    // 창 크기별 레이아웃 확인용. "--size 1280x760" 형태.
    const int sizeIdx = args.indexOf(QStringLiteral("--size"));
    if (sizeIdx >= 0 && sizeIdx + 1 < args.size()) {
        const auto wh = args.at(sizeIdx + 1).split(QLatin1Char('x'));
        if (wh.size() == 2)
            window.resize(wh.at(0).toInt(), wh.at(1).toInt());
    }

    // 수동 모드에서만 나타나는 조작 패널을 확인하기 위한 개발용 옵션.
    if (args.contains(QStringLiteral("--manual")))
        window.setDriveMode(QStringLiteral("manual"));

    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        captureAndQuit(&window, args.at(shotIdx + 1));

    return app.exec();
}
