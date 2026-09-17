// Copyright (c) 2026 WeGo Robotics. All rights reserved.

// Undercarriage inspection control station - application entry point.

#include <QApplication>
#include <QAbstractButton>
#include <QEvent>
#include <QIcon>
#include <QLabel>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>

#include "Config.h"
#include "MainWindow.h"
#include "net/BridgeClient.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "auth/Session.h"
#include "views/WelcomeDialog.h"

namespace {

/// 화면의 상태값·경고문·설명문은 조작 대상이 아니므로 드래그해서 복사할 수
/// 있어야 한다. QLabel을 만드는 모든 지점에 같은 플래그를 빼먹지 않도록
/// 애플리케이션 단계에서 한 번 적용한다. 버튼 내부 텍스트는 제외한다.
class CopyableLabelFilter final : public QObject {
public:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Polish)
            return QObject::eventFilter(watched, event);
        auto *label = qobject_cast<QLabel *>(watched);
        if (!label || qobject_cast<QAbstractButton *>(label->parentWidget()))
            return QObject::eventFilter(watched, event);
        label->setTextInteractionFlags(label->textInteractionFlags() | Qt::TextSelectableByMouse);
        return QObject::eventFilter(watched, event);
    }
};

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

}  // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Inspection HMI"));
    app.setOrganizationName(QStringLiteral("WEGO Robotics"));

    // 플랫폼 네이티브 스타일 대신 Fusion 으로 고정한다.
    //
    // 네이티브 스타일은 macOS·Windows·Linux 에서 위젯 메트릭과 그리기 방식이
    // 제각각이고, 일부는 스타일시트를 부분적으로만 존중한다(탭 바 배경, 정렬 등).
    // 납품물이 Windows 와 Ubuntu 양쪽에서 같아야 하므로, 한 곳에서 검수한 화면이
    // 다른 곳에서 달라지지 않도록 스타일을 통일한다.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    CopyableLabelFilter copyableLabelFilter;
    app.installEventFilter(&copyableLabelFilter);

    // 창·작업 표시줄·독 아이콘. 윈도우 탐색기가 보는 .exe 아이콘은 리소스에
    // 박혀 있고(resources/brand/app.rc), 이건 실행 중에 보이는 쪽이다.
    // 리눅스는 .desktop 이 가리키는 PNG 가 런처용, 이것이 창용이다.
    app.setWindowIcon(QIcon(QStringLiteral(":/brand/logo.svg")));

    loadBundledFonts();
    applyUiFont(app);

    // 저장된 표시 설정을 먼저 적용한 뒤 스타일시트를 만든다.
    auto &cfg = hmi::Config::instance();
    hmi::theme::setTheme(cfg.theme());
    hmi::theme::setUiScale(cfg.uiScale());
    app.setStyleSheet(hmi::theme::buildQss());

    // 조작자 확인. 여기서 입력한 이름이 이후 모든 조작 이력에 남는다.
    //
    // 끌 수 있는 길을 두지 않는다. 빌드 옵션으로 두었더니 개발 빌드와 납품
    // 빌드가 서로 다른 경로로 뜨게 되고, 어느 쪽을 시험한 것인지 흐려졌다.
    // 자격증명 자체가 자리표시라 여기서 막는 것은 아무것도 없다 — 목적은
    // 이력에 이름을 남기는 것이다(auth/Session.h).
    {
        hmi::ui::WelcomeDialog welcome;
        if (welcome.exec() != QDialog::Accepted)
            return 0;
    }

    // 기본은 빈 관제 화면이다. 실기와 시뮬레이터는 모두 설정에서 명시적으로
    // 선택한 브릿지 주소로만 연결한다. 127.0.0.1을 기본으로 붙이면 첫 화면의
    // "연결 안 됨"과 진짜 연결 실패를 구별할 수 없다.
    auto *bridge = new hmi::net::BridgeClient({}, 9090);
    hmi::robot::RobotLink *link = bridge;

    hmi::ui::MainWindow window(link);
    window.show();

    return app.exec();
}
