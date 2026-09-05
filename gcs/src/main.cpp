// SHALOM control station - application entry point.

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QHash>
#include <QTimer>

#include "Config.h"
#include "MainWindow.h"
#include "auth/Session.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"

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
    app.setApplicationName(QStringLiteral("SHALOM GCS"));
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
        else if (which.startsWith(QLatin1String("settings"))) {
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
    // --no-login 은 개발·캡처 전용이며 납품 빌드에서 제거한다.
    if (!args.contains(QStringLiteral("--no-login"))) {
        gcs::ui::WelcomeDialog welcome;
        if (welcome.exec() != QDialog::Accepted)
            return 0;
    }

    // --live 는 브릿지에 접속한다. 아직 BridgeClient 가 없으므로 현재는
    // 내장 시뮬레이터만 동작한다.
    gcs::ui::MainWindow window(!args.contains(QStringLiteral("--live")));
    window.show();

    const int viewIdx = args.indexOf(QStringLiteral("--view"));
    if (viewIdx >= 0 && viewIdx + 1 < args.size()) {
        static const QHash<QString, gcs::ui::NavItem> kViews{
            {QStringLiteral("drive"), gcs::ui::NavItem::Drive},
            {QStringLiteral("locations"), gcs::ui::NavItem::Locations},
            {QStringLiteral("arm"), gcs::ui::NavItem::Arm},
            {QStringLiteral("capture"), gcs::ui::NavItem::Capture},
            {QStringLiteral("diagnostics"), gcs::ui::NavItem::Diagnostics},
        };
        const auto it = kViews.constFind(args.at(viewIdx + 1));
        if (it != kViews.constEnd())
            window.showView(*it);
    }

    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        captureAndQuit(&window, args.at(shotIdx + 1));

    return app.exec();
}
