// SHALOM control station - application entry point.

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QPixmap>
#include <QTimer>

#include "MainWindow.h"
#include "theme/Style.h"
#include "theme/Tokens.h"

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
    // 데모 피드가 궤적과 게이지를 채울 시간을 준다.
    QTimer::singleShot(5000, window, [window, path] {
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

    loadBundledFonts();
    applyUiFont(app);

    const QStringList args = QApplication::arguments();
    if (args.contains(QStringLiteral("--dark")))
        gcs::theme::setTheme(QStringLiteral("dark"));
    app.setStyleSheet(gcs::theme::buildQss());

    // --live 는 브릿지에 접속한다. 아직 BridgeClient 가 없으므로 현재는
    // 데모 모드만 동작한다.
    gcs::ui::MainWindow window(!args.contains(QStringLiteral("--live")));
    window.show();

    const int shotIdx = args.indexOf(QStringLiteral("--shot"));
    if (shotIdx >= 0 && shotIdx + 1 < args.size())
        captureAndQuit(&window, args.at(shotIdx + 1));

    return app.exec();
}
