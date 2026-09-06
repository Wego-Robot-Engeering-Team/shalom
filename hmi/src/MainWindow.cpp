#include "MainWindow.h"

#include <QApplication>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QCursor>
#include <QInputDialog>
#include <QToolTip>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include "Config.h"
#include "diag/CodeCatalog.h"
#include "diag/LogStore.h"
#include "mapview/MapRender.h"
#include "net/BridgeClient.h"
#include "mapview/MapView.h"
#include "panels/ArmPanel.h"
#include "panels/CapturePanel.h"
#include "data/InspectionRecord.h"
#include "panels/DataPanel.h"
#include "panels/DiagnosticsPanel.h"
#include "panels/EventLogPanel.h"
#include "panels/MissionPanel.h"
#include "panels/StatusPanel.h"
#include "panels/TeleopPanel.h"
#include "panels/WaypointPanel.h"
#include "auth/Session.h"
#include "theme/Style.h"
#include "views/SettingsDialog.h"
#include "theme/Tokens.h"
#include "widgets/BrandMark.h"
#include "widgets/EStopButton.h"
#include "widgets/Gauges.h"
#include "widgets/IconButton.h"
#include "widgets/NotificationCenter.h"
#include "widgets/MapCard.h"
#include "widgets/MapLegend.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;
using hmi::map::MapMode;
using hmi::map::MapView;
using hmi::robot::DriveMode;
using hmi::robot::MissionState;
using hmi::robot::Telemetry;

// ============================ MainWindow ============================

MainWindow::MainWindow(hmi::robot::RobotLink *link, QWidget *parent)
    : QMainWindow(parent), robot_(link)
{
    Q_ASSERT(robot_);
    robot_->setParent(this);

    setWindowTitle(QStringLiteral("철도차량 하부점검 관제 시스템"));
    resize(1720, 990);

    log_ = new diag::LogStore(this);

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("Root"));
    setCentralWidget(root);

    // 레일이 창 왼쪽을 위에서 아래까지 차지하고, 상단 바는 그 오른쪽에서
    // 시작한다. 상단 바를 창 전체 폭으로 깔면 왼쪽 끝이 본문 어느 열과도
    // 맞지 않아 혼자 뻗어 나온 것처럼 보인다.
    auto *outer = new QHBoxLayout(root);
    outer->setContentsMargins(metrics::s3, metrics::s3, metrics::s3, metrics::s3);
    outer->setSpacing(metrics::s3);

    nav_ = new NavRail;
    outer->addWidget(nav_);

    auto *right = new QVBoxLayout;
    right->setSpacing(metrics::s3);
    right->addWidget(buildTopBar());

    auto *body = new QHBoxLayout;
    body->setSpacing(metrics::s3);

    map_ = new MapCard;
    map_->addModeButtons(autoBtn_, manualBtn_);
    context_ = qobject_cast<QStackedWidget *>(buildContextColumn());

    // 레일 바로 옆에 그 레일이 바꾸는 열을 둔다. 레일은 왼쪽 끝인데
    // 눌러서 바뀌는 화면이 오른쪽 끝에 있으면, 조작자는 1500 px 떨어진
    // 두 곳을 번갈아 봐야 한다. 지도는 남는 폭 전부를 가져간다.
    auto *upper = new QSplitter(Qt::Horizontal);
    upper->setChildrenCollapsible(false);
    upper->setHandleWidth(metrics::s2);
    upper->addWidget(context_);
    upper->addWidget(map_);
    upper->setSizes({440, 1060});
    upper->setStretchFactor(1, 1);

    body->addWidget(upper, 1);
    right->addLayout(body, 1);
    outer->addLayout(right, 1);

    // E-Stop 발동 시 창 전체를 감싸는 경고 테두리 (지시서 2.2.7 [5]).
    alert_ = new AlertFrame(root);
    alert_->setGeometry(root->rect());
    toasts_ = new ToastHost(root);
    // 알림 종 아래에 띄운다. 어차피 종의 목록에 쌓이는 내용이므로,
    // 나온 자리와 쌓이는 자리가 같아야 둘이 같은 것임이 드러난다.
    toasts_->setAnchorWidget(bell_);
    connect(bell_, &NotificationBell::opened, toasts_, &ToastHost::dismissAll);
    root->installEventFilter(this);

    wireSignals();
    startSession();
}

QWidget *MainWindow::buildTopBar()
{
    // 상단 바는 성격이 다른 것을 한 줄에 담는다. 아무 구분 없이 늘어놓으면
    // 주행 모드 전환과 테마 토글이 같은 무게로 보인다. 세 덩어리로 나눈다.
    //
    //   [ 무엇인가 ]      로고 · 이름
    //   [ 장비가 어떤가 ] 연결 · 배터리
    //   [ 무엇을 하는가 ] 주행 모드
    //   [ 부수적인 것 ]   사용자 · 설정 · 테마 · 알림
    //   [ 멈춤 ]          비상정지
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setFixedHeight(metrics::topBarH);

    auto *lay = new QHBoxLayout(bar);
    // 오른쪽 끝은 비상정지다. 그 위젯은 맥동 링이 잘리지 않도록 사방에
    // 투명한 여백을 달고 있어서, 좌우에 같은 값을 주면 보이는 판은 그만큼
    // 더 안쪽에 선다. 눈에 보이는 것끼리 맞춘다.
    lay->setContentsMargins(metrics::s4, 0, metrics::s4 - EStopButton::kVisualInset, 0);
    lay->setSpacing(metrics::s3);

    // ---- 장비가 어떤가 ----
    // 배지에 이름을 붙인다. "시뮬레이터" 만 떠 있으면 그것이 연결 상태를
    // 말하는 것인지 알 수 없다.
    lay->addWidget(captionLabel(QStringLiteral("연결")), 0, Qt::AlignVCenter);
    linkBadge_ = new Badge(QStringLiteral("끊김"), QStringLiteral("danger"));
    lay->addWidget(linkBadge_);

    lay->addSpacing(metrics::s3);
    lay->addWidget(captionLabel(QStringLiteral("배터리")), 0, Qt::AlignVCenter);
    headerBattery_ = new BatteryPill(nullptr, 25.0);
    lay->addWidget(headerBattery_, 0, Qt::AlignVCenter);

    lay->addStretch(1);

    // 주행 모드 버튼은 여기서 만들되 상단 바에 두지 않는다. 지도 툴바로
    // 보내 로봇이 움직이는 면 위에 얹는다. 설정·테마와 나란히 두면
    // 화면에서 두 번째로 무거운 조작이 도구처럼 보인다.
    autoBtn_ = new QPushButton(QStringLiteral("자율"));
    manualBtn_ = new QPushButton(QStringLiteral("수동"));
    for (auto *b : {autoBtn_, manualBtn_}) {
        b->setCheckable(true);
        b->setProperty("size", "sm");
        b->setFixedWidth(70);
    }
    autoBtn_->setChecked(true);

    // ---- 부수적인 것 ----
    // 테두리 없는 버튼으로 낮춘다. 조작이 아니라 도구다.
    userBadge_ = new Badge({}, QStringLiteral("neutral"));
    userBadge_->hide();
    lay->addWidget(userBadge_);

    settingsBtn_ = new IconButton(IconButton::Glyph::Sliders);
    settingsBtn_->setToolTip(QStringLiteral("설정"));
    lay->addWidget(settingsBtn_, 0, Qt::AlignVCenter);

    // 라벨은 "지금 상태"가 아니라 "누르면 갈 곳"이다. 다크에서는 해,
    // 라이트에서는 달을 보여준다.
    themeBtn_ = new IconButton(colors().isDark() ? IconButton::Glyph::Sun
                                                 : IconButton::Glyph::Moon);
    themeBtn_->setToolTip(QStringLiteral("다크 / 라이트 전환"));
    lay->addWidget(themeBtn_, 0, Qt::AlignVCenter);

    bell_ = new NotificationBell;
    lay->addWidget(bell_, 0, Qt::AlignVCenter);

    // ---- 멈춤 ----
    // 다른 조작과 붙여두면 손이 잘못 간다. 선과 여백으로 떼어 놓는다.
    lay->addSpacing(metrics::s3);
    lay->addWidget(new VLine(nullptr, metrics::s3), 0);
    lay->addSpacing(metrics::s3);

    estop_ = new EStopButton(nullptr, 60);
    lay->addWidget(estop_);
    return bar;
}

QWidget *MainWindow::buildContextColumn()
{
    auto *stack = new QStackedWidget;
    stack->setMinimumWidth(380);

    // NavItem 순서와 페이지 인덱스가 일치해야 한다.
    stack->addWidget(buildDriveContext());
    stack->addWidget(buildLocationsContext());
    stack->addWidget(buildArmContext());
    stack->addWidget(buildCaptureContext());
    stack->addWidget(buildDiagnosticsContext());
    stack->addWidget(buildDataContext());
    stack->addWidget(buildEventsContext());
    return stack;
}

QWidget *MainWindow::buildEventsContext()
{
    // 로그는 예전에 컨텍스트 열 아래에 고정으로 붙어 있었다. 늘 보이는
    // 대신 서너 줄만 보이는 자리였고, 조작자가 놓치면 안 되는 것은 어차피
    // 토스트로 뜨고 알림함에 쌓인다. 화면 하나를 온전히 준다.
    events_ = new EventLogPanel(log_);
    return events_;
}

QWidget *MainWindow::buildDriveContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    status_ = new StatusPanel;
    lay->addWidget(status_);

    // 수동 조작은 수동 모드에서만 나타난다. 자율 주행 중에는 의미가 없고,
    // 비활성 컨트롤을 띄워두면 화면만 차지한다.
    //
    // 점검 목록보다 위에 둔다. 수동으로 바꿨다는 것은 지금 조작자가 직접
    // 몰겠다는 뜻이므로, 그 순간 눌러야 할 것이 스크롤 아래에 있으면 안 된다.
    teleop_ = new TeleopPanel;
    teleopHost_ = teleop_;
    teleopHost_->setVisible(false);
    lay->addWidget(teleopHost_);

    // 점검 목록과 시작·정지는 위치 화면에 있다. 하지만 운용 중에는
    // 지도를 띄운 이 화면에 머무르므로, 진행 상황만이라도 여기서 읽히게 한다.
    // 카드는 내용만큼만 차지한다. 남는 세로는 아래 여백으로 흘린다.
    mission_ = new MissionPanel;
    lay->addWidget(mission_);
    lay->addStretch(1);

    // 스크롤로 감싸지 않으면 이 열의 최소 높이가 카드 높이의 합이 된다.
    // 수동 모드로 바꿔 조작 카드가 나타나는 순간 세로 스플리터가 밀려
    // 지도와 로그의 높이가 같이 변한다. 나머지 다섯 화면과 같은 방식이다.
    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildLocationsContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    locations_ = new LocationPanel;
    waypoints_ = new WaypointPanel;
    waypoints_->setMinimumHeight(360);
    lay->addWidget(locations_);
    lay->addWidget(waypoints_, 1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildArmContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    arm_ = new ArmPanel;
    lay->addWidget(arm_);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildCaptureContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    capture_ = new CapturePanel;
    lay->addWidget(capture_);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildDiagnosticsContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    diagnostics_ = new DiagnosticsPanel;
    lay->addWidget(diagnostics_);
    lay->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildDataContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    data_ = new DataPanel;
    lay->addWidget(data_);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

void MainWindow::logAction(const QString &code, QVariantMap detail)
{
    auto &session = auth::Session::instance();
    if (session.isSignedIn()) {
        detail[QStringLiteral("by")] = session.displayName();
        detail[QStringLiteral("role")] = auth::roleLabel(session.role());
    }
    log_->log(code, QJsonObject::fromVariantMap(detail));
}

void MainWindow::openSettings()
{
    if (!settings_) {
        // 모달이 아니다. 로봇을 보면서 글자 크기를 조정할 수 있어야 한다.
        settings_ = new SettingsDialog(this);
        connect(settings_, &SettingsDialog::batteryPolicyChanged, this,
                &MainWindow::pushBatteryPolicy);
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

// ================= 배선 =================

void MainWindow::wireSignals()
{
    wireRobotSignals();
    wireChromeSignals();
    wireMapSignals();
    wireLocationSignals();
    wirePanelSignals();
    wireMissionSignals();
}

void MainWindow::wireRobotSignals()
{
    connect(nav_, &NavRail::navigated, this, &MainWindow::navigate);
    connect(log_, &diag::LogStore::appended, this, &MainWindow::onLogAppended);
    connect(robot_, &robot::RobotLink::telemetry, this, &MainWindow::onTelemetry);
    if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_)) {
        connect(bridge, &net::BridgeClient::mapReceived, this,
                [this](const QByteArray &png, const QJsonObject &meta) {
                    QImage img;
                    if (!img.loadFromData(png, "PNG")) {
                        log_->note(diag::Severity::Error,
                                   QStringLiteral("지도 이미지를 읽지 못했습니다"));
                        return;
                    }
                    const auto info = hmi::map::MapInfo::create(
                        meta.value(QStringLiteral("width")).toInt(),
                        meta.value(QStringLiteral("height")).toInt(),
                        meta.value(QStringLiteral("resolution")).toDouble(),
                        meta.value(QStringLiteral("origin")).toObject()
                            .value(QStringLiteral("x")).toDouble(),
                        meta.value(QStringLiteral("origin")).toObject()
                            .value(QStringLiteral("y")).toDouble(),
                        0.0, meta.value(QStringLiteral("map_id")).toString());
                    if (!info) {
                        log_->note(diag::Severity::Error,
                                   QStringLiteral("지도 정보가 올바르지 않습니다"));
                        return;
                    }
                    map_->view()->setMap(*info, img);
                    map_->setMapLabel(info->mapId,
                                      QStringLiteral("%1×%2 m")
                                          .arg(info->extentXMeters(), 0, 'f', 0)
                                          .arg(info->extentYMeters(), 0, 'f', 0));
                    log_->note(diag::Severity::Ok, QStringLiteral("지도 수신"),
                               QJsonObject{{"map_id", info->mapId}});
                });
    }

    connect(robot_, &robot::RobotLink::connectionChanged, this, [this](bool ok) {
        status_->setConnected(ok);
        linkBadge_->set(ok ? robot_->describe() : QStringLiteral("연결 끊김"),
                        ok ? QStringLiteral("warn") : QStringLiteral("danger"));
    });

    // 미션 상태와 로봇 이벤트의 진실 원천은 로봇쪽이다. UI 는 따라간다.
    connect(robot_, &robot::RobotLink::missionStateChanged,
            this, &MainWindow::onMissionStateChanged);
    connect(robot_, &robot::RobotLink::robotEvent, this,
            [this](const QString &code, const QVariantMap &detail) {
                log_->log(code, QJsonObject::fromVariantMap(detail));
            });
}

void MainWindow::wireChromeSignals()
{
    // 여기서도 값만 쓴다. 아래의 Config 구독이 화면을 칠한다 — 두 곳에서
    // 칠하면 어느 쪽이 이겼는지에 따라 결과가 달라진다.
    connect(themeBtn_, &QPushButton::clicked, this, [] {
        auto &cfg = Config::instance();
        cfg.setTheme(cfg.theme() == QLatin1String("dark") ? QStringLiteral("light")
                                                          : QStringLiteral("dark"));
    });
    connect(settingsBtn_, &QPushButton::clicked, this, &MainWindow::openSettings);

    // 설정 창이 아니라 설정 자체를 듣는다. 창이 신호를 내게 해 두면 값을
    // 바꾸는 다른 경로 — 기본값 복원이나 나중에 생길 무엇이든 — 는 화면을
    // 갱신하지 못한다.
    //
    // 이름은 반드시 Config 에서 읽는다. colors() 는 지금 칠해져 있는 테마라,
    // 이 신호가 오는 시점에는 아직 바뀌기 전 값이다 — 그것으로 다시 칠하면
    // 아무 일도 일어나지 않는다.
    connect(&Config::instance(), &Config::appearanceChanged, this, [this] {
        auto &cfg = Config::instance();
        setUiScale(cfg.uiScale());
        applyTheme(cfg.theme());
    });

    // 이름과 권한은 상단바에 떠 있다. 세션이 바뀌었는데 그대로 두면 화면이
    // 지난 사람의 이름으로 기록되는 것처럼 보인다.
    auto &session = auth::Session::instance();
    connect(&session, &auth::Session::signedInChanged, this,
            &MainWindow::refreshUserBadge);

    // 관리자 인증 시도는 성공이든 실패든 남는다. 오류 코드 목록의
    // ESTOP_RELEASE_DENIED 가 "시도 이력은 로그에 남습니다" 라고 안내하는데,
    // 그 이력을 실제로 적는 곳이 없었다.
    connect(&session, &auth::Session::authAttempt, this,
            [this](bool accepted, const QString &detail) {
                log_->note(accepted ? diag::Severity::Info : diag::Severity::Warn,
                           QStringLiteral("관리자 인증 — %1").arg(detail),
                           QJsonObject{{"accepted", accepted}});
            });

    connect(estop_, &EStopButton::engageRequested, this, &MainWindow::engageEstop);
    connect(estop_, &EStopButton::releaseRequested, this, &MainWindow::releaseEstop);

    connect(autoBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("auto")); });
    connect(manualBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("manual")); });
}

void MainWindow::wireMapSignals()
{
    auto *view = map_->view();

    connect(map_->goalButton(), &QPushButton::toggled, this, [this, view](bool on) {
        pendingPlacementKind_.clear();
        view->setMode(on ? MapMode::SetGoal : MapMode::View);
        map_->setPlacementHint(on ? QStringLiteral("지도를 클릭해 목표를 지정하고, "
                                                   "드래그해 방향을 정하십시오")
                                  : QString());
    });

    connect(view, &MapView::goalRequested, this, [this](double x, double y, double th) {
        map_->goalButton()->setChecked(false);
        map_->setPlacementHint({});
        robot_->requestGoal(x, y, th);
        log_->note(diag::Severity::Info, QStringLiteral("목표 지정"),
                   QJsonObject{{"x", x}, {"y", y}, {"theta_deg", qRadiansToDegrees(th)},
                               {"channel", QStringLiteral("cmd/goto")}});
    });

    connect(view, &MapView::waypointPlaced, this, [this](double x, double y, double th) {
        const QString kind = pendingPlacementKind_.isEmpty()
                                 ? QStringLiteral("inspection")
                                 : pendingPlacementKind_;
        pendingPlacementKind_.clear();
        map_->setPlacementHint({});

        QVariantMap loc{{"x", x}, {"y", y}, {"theta", th},
                        {"captured_from", QStringLiteral("map")}};

        if (kind == QLatin1String("dock") || kind == QLatin1String("home")) {
            loc[QStringLiteral("kind")] = kind;
            (kind == QLatin1String("dock") ? dock_ : home_) = loc;
            locations_->setDock(dock_);
            mission_->setDockKnown(!dock_.isEmpty());
            locations_->setHome(home_);
            log_->log(QStringLiteral("LOC_CAPTURED"), QJsonObject::fromVariantMap(loc));
            return;
        }

        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        loc[QStringLiteral("id")] = QStringLiteral("NEW-%1").arg(n, 2, 10, QLatin1Char('0'));
        loc[QStringLiteral("name")] = QStringLiteral("신규 포인트 %1").arg(n);
        loc[QStringLiteral("status")] = QStringLiteral("todo");
        wps << loc;
        waypoints_->setWaypoints(wps);
        map_->view()->setWaypoints(wps);
        log_->log(QStringLiteral("LOC_CAPTURED"), QJsonObject::fromVariantMap(loc));
    });

    connect(map_->legend(), &MapLegend::manageRequested, this,
            [this](MapLegend::Item item) {
                // "이게 뭐지" 다음은 대개 "바꾸고 싶다" 이다. 설명을 읽은
                // 자리에서 관리 화면으로 바로 넘어가게 한다.
                if (item == MapLegend::Item::Waypoint)
                    showView(NavItem::Locations);
            });

    connect(view, &MapView::waypointClicked, this, [this](const QString &id) {
        showWaypointInfo(id, QCursor::pos());
    });
}

void MainWindow::wireLocationSignals()
{
    connect(locations_, &LocationPanel::captureFromRobot, this, &MainWindow::captureLocation);
    connect(locations_, &LocationPanel::captureFromMap, this, [this](const QString &kind) {
        pendingPlacementKind_ = kind;
        map_->goalButton()->setChecked(false);
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 위치를 지정하고, 드래그해 방향을 정하십시오"));
    });
    connect(locations_, &LocationPanel::gotoRequested, this, [this](const QString &kind) {
        const bool isDock = kind == QLatin1String("dock");
        driveTo(isDock ? dock_ : home_,
                isDock ? QStringLiteral("충전 스테이션") : QStringLiteral("시작 위치"));
    });

    // 20 Hz 로 흘려보낸다. 시뮬레이터가 데드맨을 그대로 구현하므로,
    // 발행이 멈추면 로봇도 멈춘다.
}

void MainWindow::wirePanelSignals()
{
    connect(teleop_, &TeleopPanel::cmdVel, robot_, &robot::RobotLink::setCmdVel);

    connect(arm_, &ArmPanel::presetRequested, this, [this](const QString &name) {
        arm_->applyPresetToSliders(name);
        robot_->setArmPreset(name);
        log_->log(QStringLiteral("ARM_PRESET"), QJsonObject{{"preset", name}});
    });
    connect(arm_, &ArmPanel::jointGoal, this, [this](const QList<double> &q) {
        robot_->setArmJointGoal(q);
        log_->note(diag::Severity::Info, QStringLiteral("관절 목표 전송"),
                   QJsonObject{{"channel", QStringLiteral("cmd/arm/joint_goal")}});
    });
    connect(arm_, &ArmPanel::eeGoal, this, [this](const QVariantMap &g) {
        log_->note(diag::Severity::Info,
                   QStringLiteral("EE 목표 X%1 Y%2 Z%3")
                       .arg(g.value("x").toDouble(), 0, 'f', 2)
                       .arg(g.value("y").toDouble(), 0, 'f', 2)
                       .arg(g.value("z").toDouble(), 0, 'f', 2),
                   {{"channel", QStringLiteral("cmd/arm/ee_goal")}});
    });
    connect(arm_, &ArmPanel::stopRequested, this, [this] {
        robot_->stopArm();
        log_->note(diag::Severity::Warn, QStringLiteral("로봇팔 정지 요청"),
                   QJsonObject{{"channel", QStringLiteral("cmd/arm/stop")}});
    });

    connect(data_, &DataPanel::notice, this,
            [this](const QString &severity, const QString &message) {
                log_->note(severity == QLatin1String("ok")   ? diag::Severity::Ok
                           : severity == QLatin1String("warn") ? diag::Severity::Warn
                                                               : diag::Severity::Info,
                           message);
            });

    connect(capture_, &CapturePanel::captureRequested, this, [this] {
        logAction(QStringLiteral("CAPTURE_OK"),
                  {{"point_id", capture_ ? QStringLiteral("수동 촬영") : QString()}});
    });
    connect(capture_, &CapturePanel::saveRequested, this,
            [this](const hmi::capture::CaptureMetadata &meta) {
                // 저장은 로봇측이 수행한다. 관제는 메타데이터를 붙여 요청만 한다
                // — 원본이 관제를 경유하지 않는 것과 같은 이유다.
                logAction(QStringLiteral("CAPTURE_OK"),
                          QJsonObject::fromVariantMap(meta.toJson().toVariantMap())
                              .toVariantMap());
            });
}

void MainWindow::wireMissionSignals()
{
    connect(waypoints_, &WaypointPanel::addRequested, this, [this] {
        pendingPlacementKind_ = QStringLiteral("inspection");
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 점검포인트를 추가하십시오"));
    });
    connect(waypoints_, &WaypointPanel::deleteRequested, this, [this](const QString &id) {
        auto wps = waypoints_->waypoints();
        const auto removed = std::remove_if(wps.begin(), wps.end(), [&id](const QVariantMap &w) {
            return w.value(QStringLiteral("id")).toString() == id;
        });
        if (removed == wps.end())
            return;
        wps.erase(removed, wps.end());
        waypoints_->setWaypoints(wps);
        map_->view()->setWaypoints(wps);
        robot_->setWaypoints(wps);
        log_->note(diag::Severity::Info, QStringLiteral("점검포인트 삭제 (%1)").arg(id),
                   QJsonObject{{"id", id}});
    });

    // 목록에서 고른 포인트를 지도에서도 짚어 준다. 목록과 지도를 눈으로
    // 대응시키지 못하면 좌표만 보고 어디인지 알아내야 한다.
    connect(waypoints_, &WaypointPanel::waypointSelected, this, [this](const QString &id) {
        map_->view()->focusWaypoint(id);
    });

    connect(waypoints_, &WaypointPanel::orderChanged, this, [this](const QStringList &ids) {
        robot_->setWaypoints(waypoints_->waypoints());
        log_->note(diag::Severity::Info,
                   QStringLiteral("점검 순서 변경 (%1개)").arg(ids.size()),
                   QJsonObject{{"channel", QStringLiteral("cmd/waypoints/set")}});
    });
    connect(mission_, &MissionPanel::missionStart, this, [this] {
        // 로봇도 같은 값으로 거부하지만, 여기서 먼저 막아야 조작자가 이유를
        // 안다. 로봇만 거부하면 화면에서는 "눌렀는데 아무 일도 안 났다" 가 된다.
        const double departAt = Config::instance().batteryDeparturePercent();
        if (lastSoc_ < departAt) {
            QMessageBox::warning(
                this, QStringLiteral("점검을 시작할 수 없습니다"),
                QStringLiteral("배터리가 %1%% 입니다. 출발 최소 기준 %2%% 이상 "
                               "충전한 뒤에 시작하십시오.\n\n"
                               "기준은 설정 · 전원에서 바꿀 수 있습니다.")
                    .arg(lastSoc_, 0, 'f', 0)
                    .arg(departAt, 0, 'f', 0));
            return;
        }
        robot_->setWaypoints(waypoints_->waypoints());
        robot_->missionStart();
        log_->log(QStringLiteral("MISSION_START"));
    });
    // 목록을 다시 보내는 것은 시작할 때뿐이다. 일시정지·재개·중단에서도
    // 보내면 진행 중인 점검 도중에 목록을 갈아 끼우는 셈이고, 로봇은 그때
    // 어디까지 했는지를 잃는다 — 재개가 "이어서" 가 아니게 된다.
    connect(mission_, &MissionPanel::missionPause, this, [this] {
        robot_->missionPause();
        log_->log(QStringLiteral("MISSION_PAUSE"));
    });
    connect(mission_, &MissionPanel::missionResume, this, [this] {
        robot_->missionResume();
        log_->log(QStringLiteral("MISSION_RESUME"));
    });
    connect(waypoints_, &WaypointPanel::waypointsChanged, mission_,
            &MissionPanel::setWaypoints);

    connect(mission_, &MissionPanel::missionStop, this, [this] {
        robot_->missionStop();
        log_->log(QStringLiteral("MISSION_STOP"));
    });
    connect(mission_, &MissionPanel::returnToDock, this, [this] {
        // 점검 중이면 먼저 세운다. 목표만 걸면 로봇이 충전소로 갔다가
        // 남은 점검을 저 혼자 다시 시작한다 — 조작자는 세운 줄 안다.
        // 취소가 아니라 일시정지인 이유는, 충전하고 이어서 하는 것이
        // 이 버튼을 누르는 거의 모든 이유이기 때문이다.
        if (robot_->missionState() != MissionState::Idle) {
            robot_->missionPause();
            log_->log(QStringLiteral("MISSION_PAUSE"));
        }
        driveTo(dock_, QStringLiteral("충전 스테이션"));
    });
}

void MainWindow::driveTo(const QVariantMap &pose, const QString &label)
{
    if (pose.isEmpty())
        return;
    if (estop_->isEngaged()) {
        QMessageBox::warning(this, QStringLiteral("이동할 수 없습니다"),
                             QStringLiteral("비상정지 상태입니다. 해제한 뒤에 다시 "
                                            "시도하십시오."));
        return;
    }
    // 목적지로 데려가는 것은 자율주행이다. 수동에서 눌렀다고 거절하면
    // 조작자는 목적지를 말한 것뿐인데 모드를 탓하는 창을 보게 된다.
    if (robot_->mode() != DriveMode::Auto)
        setMode(QStringLiteral("auto"));

    robot_->requestGoal(pose.value(QStringLiteral("x")).toDouble(),
                        pose.value(QStringLiteral("y")).toDouble(),
                        pose.value(QStringLiteral("theta")).toDouble());
    log_->note(diag::Severity::Info, QStringLiteral("%1 로 이동").arg(label),
               QJsonObject{{"channel", QStringLiteral("cmd/goto")},
                           {"x", pose.value(QStringLiteral("x")).toDouble()},
                           {"y", pose.value(QStringLiteral("y")).toDouble()}});
}

void MainWindow::onMissionStateChanged(MissionState state)
{
    const bool running = state != MissionState::Idle;
    const bool paused = state == MissionState::Paused;

    mission_->setMissionState(paused   ? QStringLiteral("paused")
                              : running ? QStringLiteral("running")
                                        : QStringLiteral("idle"));

    // 상단 바의 미션 배지는 점검 진행 카드와 중복이라 뺐다. 그 배지를
    // 갱신하던 코드가 남아 널 포인터를 건드렸고, 자율주행을 시작하는 순간
    // 프로그램이 죽었다.
}

void MainWindow::onLogAppended(const diag::LogEntry &entry)
{
    // 정보성 항목까지 띄우면 화면이 알림으로 덮이고, 정작 중요한 것이 묻힌다.
    // 조작자가 조치해야 하는 등급만 올린다.
    if (int(entry.severity) < int(diag::Severity::Warn))
        return;

    const auto *code = diag::CodeCatalog::instance().find(entry.code);
    const QString title = code ? code->title : entry.message;

    // 첫 번째 조치를 함께 보여준다. 무슨 일이 났는지만 알리고 무엇을 해야
    // 하는지 말하지 않으면, 조작자는 결국 로그를 다시 열어야 한다.
    QString detail;
    if (code && !code->actions.isEmpty())
        detail = code->actions.first();
    else if (code && title != code->cause)
        detail = code->cause;

    const QString severity = diag::severityToString(entry.severity);

    // 토스트는 몇 초 뒤 스스로 사라진다. 자리를 비운 사이에 지나간 알림을
    // 확인할 수 있도록 같은 내용을 알림함에도 넣는다.
    toasts_->show(title, detail, severity);
    bell_->add({QDateTime::fromMSecsSinceEpoch(entry.timestampMs), title, detail, severity});
}

void MainWindow::showWaypointInfo(const QString &id, const QPoint &globalPos)
{
    QVariantMap found;
    int index = -1;
    const auto points = waypoints_->waypoints();
    for (int i = 0; i < points.size(); ++i) {
        if (points.at(i).value(QStringLiteral("id")).toString() == id) {
            found = points.at(i);
            index = i;
            break;
        }
    }
    if (found.isEmpty())
        return;

    // 이 포인트로 이미 찍힌 사진을 함께 보여준다. 지도에서 포인트를 눌렀을 때
    // 알고 싶은 것은 "여기 찍었나, 언제 찍었나" 이지 좌표가 아니다.
    const auto scan = hmi::data::scanDirectory(Config::instance().nasMountPath());
    QList<hmi::data::InspectionRecord> forPoint;
    for (const auto &r : scan.records)
        if (r.pointId == id)
            forPoint << r;

    QStringList lines;
    lines << QStringLiteral("<b>%1</b>  ·  %2")
                 .arg(found.value(QStringLiteral("name"), id).toString(),
                      QStringLiteral("%1번째").arg(index + 1));
    lines << QStringLiteral("위치  %1, %2")
                 .arg(found.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                 .arg(found.value(QStringLiteral("y")).toDouble(), 0, 'f', 2);
    if (found.contains(QStringLiteral("tag_id")))
        lines << QStringLiteral("마커  %1").arg(found.value(QStringLiteral("tag_id")).toInt());

    if (forPoint.isEmpty()) {
        lines << QStringLiteral("<i>저장된 촬영 없음</i>");
    } else {
        lines << QStringLiteral("촬영 %1건, 최근 %2")
                     .arg(forPoint.size())
                     .arg(forPoint.first().capturedAt.toString(
                         QStringLiteral("yyyy-MM-dd HH:mm")));
    }

    QToolTip::showText(globalPos, lines.join(QStringLiteral("<br/>")), this);
}

void MainWindow::navigate(NavItem item)
{
    context_->setCurrentIndex(int(item));
}

void MainWindow::setInspectionDirectory(const QString &path)
{
    data_->setDirectory(path);
    log_->note(diag::Severity::Info, QStringLiteral("표본 촬영 폴더 사용 — %1").arg(path));
}

void MainWindow::showView(NavItem item)
{
    nav_->setCurrent(item);
    navigate(item);
}

// ================= 위치 등록 =================

void MainWindow::captureLocation(const QString &kind)
{
    const CaptureCheck check = LocationPanel::checkCapture(snapshot_, kind);

    if (!check.allowed) {
        log_->log(check.code, QJsonObject{{"kind", kind}, {"reason", check.reason}});
        QMessageBox::warning(this, QStringLiteral("위치 등록 불가"), check.reason);
        return;
    }

    QVariantMap loc{
        {"kind", kind},
        {"x", snapshot_.x},
        {"y", snapshot_.y},
        {"theta", snapshot_.theta},
        {"captured_from", QStringLiteral("robot")},
        {"localization_ok", snapshot_.localizationOk},
    };
    if (snapshot_.visibleTagId >= 0)
        loc[QStringLiteral("tag_id")] = snapshot_.visibleTagId;

    if (kind == QLatin1String("dock") || kind == QLatin1String("home")) {
        (kind == QLatin1String("dock") ? dock_ : home_) = loc;
        locations_->setDock(dock_);
        mission_->setDockKnown(!dock_.isEmpty());
        locations_->setHome(home_);
    } else {
        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        loc[QStringLiteral("id")] = QStringLiteral("TP-%1").arg(n, 2, 10, QLatin1Char('0'));
        loc[QStringLiteral("name")] = QStringLiteral("점검 위치 %1").arg(n);
        loc[QStringLiteral("status")] = QStringLiteral("todo");
        wps << loc;
        waypoints_->setWaypoints(wps);
        map_->view()->setWaypoints(wps);
    }

    // 신뢰도가 낮은 채로 저장된 위치는 별도 코드로 남긴다.
    // 나중에 "이 포인트는 어떻게 잡았나"를 로그로 추적할 수 있어야 한다.
    log_->log(check.degraded ? QStringLiteral("LOC_CAPTURE_DEGRADED")
                             : QStringLiteral("LOC_CAPTURED"),
              QJsonObject::fromVariantMap(loc));
}

// ================= 안전 =================

void MainWindow::engageEstop()
{
    robot_->engageEstop();
    estop_->setEngaged(true);
    alert_->setActive(true);
    // 지정해 둔 목표는 이 시점에 무효다. 지도에 남겨 두면 해제 후에도
    // 로봇이 그리로 갈 것처럼 읽힌다.
    map_->view()->clearGoal();
    status_->setMode({}, true);
    teleop_->setJogEnabled(false);
    teleopHost_->setVisible(false);
    arm_->setControlsEnabled(false);
    autoBtn_->setChecked(false);
    manualBtn_->setChecked(false);
    logAction(QStringLiteral("ESTOP_ENGAGED"));
}

void MainWindow::releaseEstop()
{
    // 자동 해제 금지 (지시서 2.2.5). 사람이 확인하고, 관리자 권한을 요구한다.
    // 발동에는 어떤 인증도 걸지 않는다 — 급할 때 인증하다 못 누르면 안 된다.
    const auto answer = QMessageBox::question(
        this, QStringLiteral("비상정지 해제 확인"),
        QStringLiteral("비상정지를 해제합니다.\n\n"
                       "로봇 주변에 사람이 없고 안전이 확보되었는지 확인하십시오.\n"
                       "해제해도 자율주행은 저절로 이어지지 않습니다.\n"
                       "다시 시작하려면 재개를 눌러야 합니다."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    auto &session = auth::Session::instance();
    if (session.role() != auth::Role::Admin) {
        bool ok = false;
        const QString pw = QInputDialog::getText(
            this, QStringLiteral("관리자 인증"),
            QStringLiteral("비상정지 해제에는 관리자 비밀번호가 필요합니다."),
            QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;

        QString err;
        if (!session.verifyAdmin(pw, &err)) {
            logAction(QStringLiteral("ESTOP_RELEASE_DENIED"), {{"reason", err}});
            QMessageBox::warning(this, QStringLiteral("인증 실패"), err);
            return;
        }
    }

    robot_->releaseEstop();
    estop_->setEngaged(false);
    alert_->setActive(false);
    logAction(QStringLiteral("ESTOP_RELEASED"));
    // 해제 후에는 수동 모드로 떨어뜨린다. 바로 자율로 복귀시키면
    // "명시적 재개" 요건을 UI 가 우회하는 셈이 된다.
    setMode(QStringLiteral("manual"));
}

void MainWindow::refreshUserBadge()
{
    auto &session = auth::Session::instance();
    userBadge_->setVisible(session.isSignedIn());
    if (!session.isSignedIn())
        return;
    userBadge_->set(QStringLiteral("%1 · %2")
                        .arg(session.displayName(), auth::roleLabel(session.role())),
                    session.role() == auth::Role::Admin ? QStringLiteral("info")
                                                        : QStringLiteral("neutral"));
}

void MainWindow::setMode(const QString &mode)
{
    if (estop_->isEngaged()) {
        autoBtn_->setChecked(false);
        manualBtn_->setChecked(false);
        return;
    }

    const bool isAuto = mode == QLatin1String("auto");
    robot_->setMode(isAuto ? DriveMode::Auto : DriveMode::Manual);
    autoBtn_->setChecked(isAuto);
    manualBtn_->setChecked(!isAuto);
    status_->setMode(mode, false);

    teleop_->setJogEnabled(!isAuto);
    teleopHost_->setVisible(!isAuto);
    if (!isAuto)
        map_->view()->clearGoal();
    arm_->setControlsEnabled(true);

    if (isAuto) {
        log_->log(QStringLiteral("MODE_AUTO"));
    } else {
        // 수동 전환 시 자율주행 즉시 중단 (지시서 2.2.5 수동 조작 우선권).
        // 중단은 시뮬레이터가 수행하고 missionStateChanged 로 통보한다.
        log_->log(QStringLiteral("MODE_MANUAL"));
        // 수동 조작을 하려면 조작계가 보여야 한다.
        nav_->setCurrent(NavItem::Drive);
        navigate(NavItem::Drive);
    }
}

// ================= 창 =================

void MainWindow::showEvent(QShowEvent *ev)
{
    QMainWindow::showEvent(ev);
    if (didInitialFit_)
        return;
    didInitialFit_ = true;
    // 이 시점에도 레이아웃이 완전히 끝나지 않았을 수 있어 다음 이벤트 루프로
    // 미룬다. 곧바로 부르면 여전히 이전 뷰포트 크기를 쓴다.
    QTimer::singleShot(0, this, [this] { map_->view()->fitMap(); });
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == centralWidget() && ev->type() == QEvent::Resize) {
        alert_->setGeometry(centralWidget()->rect());
        toasts_->setBottomAnchor(metrics::s3);
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::applyTheme(const QString &name)
{
    const Colors &c = setTheme(name);
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance()))
        app->setStyleSheet(buildQss());

    themeBtn_->setGlyph(c.isDark() ? IconButton::Glyph::Sun : IconButton::Glyph::Moon);

    // QSS 로 칠해지는 위젯은 스타일시트 재적용만으로 따라온다.
    // 씬 아이템의 펜 색은 아이템에 박혀 있어 별도로 다시 지정해야 한다.
    map_->view()->retheme();
    for (auto *w : findChildren<QWidget *>())
        w->update();

    // 맵 이미지는 팔레트 색으로 구워져 있으므로 다시 렌더한다.
    if (!mapData_.grid.isEmpty()) {
        map_->view()->setMap(mapData_.info,
                             hmi::map::occupancyToImage(mapData_.grid, mapData_.info.width,
                                                        mapData_.info.height));
    }
}

// ================= 로그 파일 =================

void MainWindow::startLogFile()
{
    // 설정 화면에 저장 위치와 보관 기간이 있는데도 파일이 열리지 않고
    // 있었다. 문제가 생긴 뒤 담당자에게 보낼 수 있는 것이 로그뿐인데
    // 그 로그가 창을 닫는 순간 사라지고 있었다는 뜻이다.
    const QString dir = Config::instance().logDirectory();
    if (!QDir().mkpath(dir)) {
        log_->note(diag::Severity::Warn,
                   QStringLiteral("로그 저장 폴더를 만들 수 없습니다: %1").arg(dir));
        return;
    }

    pruneOldLogs(dir, Config::instance().logRetentionDays());

    const QString path =
        QStringLiteral("%1/inspection-%2.jsonl")
            .arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd")));

    QString err;
    if (!log_->startFileSink(path, &err))
        log_->note(diag::Severity::Warn, err);
}

void MainWindow::pruneOldLogs(const QString &dir, int retentionDays)
{
    if (retentionDays <= 0)
        return;

    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-retentionDays);
    const auto files = QDir(dir).entryInfoList({QStringLiteral("inspection-*.jsonl")}, QDir::Files);
    for (const auto &fi : files)
        if (fi.lastModified() < cutoff)
            QFile::remove(fi.absoluteFilePath());
}

// ================= 데모 =================

void MainWindow::pushBatteryPolicy()
{
    const auto &cfg = Config::instance();
    robot_->setBatteryPolicy(cfg.batteryReturnPercent(), cfg.batteryDeparturePercent());
    log_->note(diag::Severity::Info,
               QStringLiteral("배터리 기준 전송 — 복귀 %1%, 출발 %2%")
                   .arg(cfg.batteryReturnPercent(), 0, 'f', 0)
                   .arg(cfg.batteryDeparturePercent(), 0, 'f', 0));
}

void MainWindow::startSession()
{
    startLogFile();
    // 로봇이 지킬 값이므로 시작하자마자 보낸다. 설정 화면을 한 번도 열지
    // 않은 채로 운용하면 로봇은 아무 기준도 못 받는다.
    pushBatteryPolicy();

    // 로봇이 지도를 보내오기 전까지는 비어 있다. 테스트베드로 돌 때는
    // 링크가 대역 지도를 들고 온다 — MainWindow 는 어느 쪽인지 모른다.
    const auto initial = robot_->initialMap();
    if (initial) {
        mapData_ = *initial;
        map_->view()->setMap(mapData_.info,
                             hmi::map::occupancyToImage(mapData_.grid, mapData_.info.width,
                                                        mapData_.info.height));
        map_->setMapLabel(mapData_.info.mapId,
                          QStringLiteral("%1×%2 m")
                              .arg(mapData_.info.extentXMeters(), 0, 'f', 0)
                              .arg(mapData_.info.extentYMeters(), 0, 'f', 0));
    } else {
        map_->setMapLabel(QStringLiteral("지도 수신 대기"), QString());
    }

    const auto wps = robot_->waypoints();
    waypoints_->setWaypoints(wps);
    map_->view()->setWaypoints(wps);
    map_->view()->setTags(robot_->markers());

    dock_ = robot_->dockPose();
    locations_->setDock(dock_);
    mission_->setDockKnown(!dock_.isEmpty());
    locations_->setHome({});

    // 이력은 저장 장치의 공유 폴더를 직접 읽는다. 로봇을 거치지 않는다.
    data_->setDirectory(Config::instance().nasMountPath());

    status_->setConnected(robot_->isConnected());
    linkBadge_->set(robot_->describe(),
                    robot_->isConnected() ? QStringLiteral("warn")
                                          : QStringLiteral("danger"));

    refreshUserBadge();

    // 로그인 화면은 이 창보다 먼저 돈다. 그때의 인증 시도를 지금 옮겨 적는다.
    for (const auto &a : auth::Session::instance().takePendingAttempts()) {
        log_->note(a.accepted ? diag::Severity::Info : diag::Severity::Warn,
                   QStringLiteral("관리자 인증 — %1").arg(a.detail),
                   QJsonObject{{"accepted", a.accepted},
                               {"at", a.at.toString(Qt::ISODate)}});
    }

    setMode(QStringLiteral("auto"));
    nav_->setCurrent(NavItem::Drive);
    navigate(NavItem::Drive);

    if (initial) {
        log_->note(diag::Severity::Ok, QStringLiteral("지도 불러오기 완료"),
                   QJsonObject{{"map_id", mapData_.info.mapId}});
        log_->note(diag::Severity::Info,
                   QStringLiteral("로봇 미연결 — 내장 시뮬레이터로 구동 중"));
    } else {
        log_->note(diag::Severity::Info,
                   QStringLiteral("로봇 연결 시도 — %1").arg(robot_->describe()));
    }

    // 텔레메트리 주기는 링크가 정한다. 창이 자체 타이머를 돌리면
    // 시뮬레이터와 브릿지에서 갱신 속도가 달라진다.
    robot_->start();
}

void MainWindow::onTelemetry(const Telemetry &tm)
{
    auto *view = map_->view();

    view->setRobotPose(tm.x, tm.y, tm.theta, !tm.poseFresh);
    view->setTrail(tm.trail);
    view->setPlan(tm.plan);
    view->setTagsSeen(tm.seenTags);

    for (const auto &w : robot_->waypoints()) {
        const QString id = w.value(QStringLiteral("id")).toString();
        const QString st = w.value(QStringLiteral("status")).toString();
        view->setWaypointStatus(id, st);
        waypoints_->setStatus(id, st);
    }

    // 배터리와 좌표는 내비게이션 레일이, 시스템 지표는 진단 화면이 맡는다.
    // 여기서 또 그리면 한 화면에 같은 숫자가 두 번 뜬다.
    status_->setMotion(tm.speed);
    status_->setPose(tm.x, tm.y, qRadiansToDegrees(tm.theta));
    status_->setArmState(tm.armState);
    status_->setTagsSeen(int(tm.seenTags.size()));
    arm_->setArmState(tm.joints, tm.manipulability, tm.sigmaMin, tm.armState);

    lastSoc_ = tm.soc;
    headerBattery_->setState(tm.soc);
    nav_->setDiagnosticsAlerts(log_->countAtOrAbove(diag::Severity::Error));
    nav_->setEventAlerts(log_->countAtOrAbove(diag::Severity::Warn));

    // 위치 등록 가능 여부는 실제 속력으로 판정한다. 시뮬레이터가 속력을
    // 직접 알려주므로 UI 가 궤적을 미분할 필요가 없다.
    snapshot_.x = tm.x;
    snapshot_.y = tm.y;
    snapshot_.theta = tm.theta;
    snapshot_.speed = tm.speed;
    snapshot_.poseFresh = tm.poseFresh;
    snapshot_.localizationOk = tm.localizationOk;
    snapshot_.visibleTagId = tm.seenTags.isEmpty() ? -1 : *tm.seenTags.cbegin();
    locations_->setSnapshot(snapshot_);

    // 촬영은 정지 상태에서만 허용한다 (지시서 2.2.4 동적 촬영 불가).
    const bool stationary = tm.speed < 0.05;
    capture_->setContext(tm.x, tm.y, tm.theta,
                         tm.seenTags.isEmpty() ? -1 : *tm.seenTags.cbegin());
    capture_->setCaptureAllowed(stationary && !tm.estop && tm.poseFresh,
                                tm.estop ? QStringLiteral("비상정지가 걸려 있습니다.")
                                : !tm.poseFresh
                                    ? QStringLiteral("위치 정보가 오래되었습니다.")
                                    : QStringLiteral("로봇이 움직이는 중입니다. 멈춘 뒤에 촬영할 수 있습니다."));

    diagnostics_->setSystem(tm.cpu, tm.gpu, tm.mem, tm.cpuTemp, tm.gpuTemp);
    diagnostics_->setSensors(tm.sensors);
    diagnostics_->setLink(tm.link);
    diagnostics_->setStorage(tm.nasOnline, tm.pendingUploads, tm.spoolFreeMb);
}

}  // namespace hmi::ui
