#include "MainWindow.h"

#include <QApplication>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include "Config.h"
#include "diag/LogStore.h"
#include "mapview/MapRender.h"
#include "mapview/MapView.h"
#include "panels/ArmPanel.h"
#include "panels/DiagnosticsPanel.h"
#include "panels/EventLogPanel.h"
#include "panels/StatusPanel.h"
#include "panels/TeleopPanel.h"
#include "panels/WaypointPanel.h"
#include "auth/Session.h"
#include "theme/Style.h"
#include "views/SettingsDialog.h"
#include "theme/Tokens.h"
#include "widgets/BrandMark.h"
#include "widgets/EStopButton.h"
#include "widgets/MapCard.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::map::MapMode;
using gcs::map::MapView;
using gcs::sim::DriveMode;
using gcs::sim::MissionState;

// ============================ MainWindow ============================

MainWindow::MainWindow(bool simMode, QWidget *parent)
    : QMainWindow(parent), simMode_(simMode)
{
    setWindowTitle(QStringLiteral("SHALOM 관제 · 철도차량 하부 점검시스템"));
    resize(1720, 990);

    log_ = new diag::LogStore(this);
    robot_ = new sim::SimRobot(this);

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("Root"));
    setCentralWidget(root);

    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(metrics::s3, metrics::s3, metrics::s3, metrics::s3);
    outer->setSpacing(metrics::s3);
    outer->addWidget(buildTopBar());

    // ---- 본문: 레일 | (지도 | 컨텍스트) 위에 로그 ----
    auto *body = new QHBoxLayout;
    body->setSpacing(metrics::s3);

    nav_ = new NavRail;
    body->addWidget(nav_);

    map_ = new MapCard;
    context_ = qobject_cast<QStackedWidget *>(buildContextColumn());

    auto *upper = new QSplitter(Qt::Horizontal);
    upper->setChildrenCollapsible(false);
    upper->setHandleWidth(metrics::s2);
    upper->addWidget(map_);
    upper->addWidget(context_);
    upper->setSizes({1080, 420});
    upper->setStretchFactor(0, 1);

    events_ = new EventLogPanel(log_);

    // 로그는 항상 보이되 높이를 조절할 수 있어야 한다. 평소엔 몇 줄만
    // 보다가, 문제가 생기면 끌어올려 넓게 본다.
    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->setChildrenCollapsible(false);
    vertical->setHandleWidth(metrics::s2);
    vertical->addWidget(upper);
    vertical->addWidget(events_);
    vertical->setSizes({700, 230});
    vertical->setStretchFactor(0, 1);

    body->addWidget(vertical, 1);
    outer->addLayout(body, 1);

    // E-Stop 발동 시 창 전체를 감싸는 경고 테두리 (지시서 2.2.7 [5]).
    alert_ = new AlertFrame(root);
    alert_->setGeometry(root->rect());
    root->installEventFilter(this);

    wireSignals();
    if (simMode_)
        startSimulation();
}

QWidget *MainWindow::buildTopBar()
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("TopBar"));
    bar->setFixedHeight(66);

    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(metrics::s4, 0, metrics::s3, 0);
    lay->setSpacing(metrics::s3);

    lay->addWidget(new BrandMark(nullptr, 30), 0, Qt::AlignVCenter);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto *t = new QLabel(QStringLiteral("SHALOM 관제"));
    t->setObjectName(QStringLiteral("AppTitle"));
    auto *s = new QLabel(QStringLiteral("Unitree B2 + FR3 · 철도차량 하부 점검"));
    s->setObjectName(QStringLiteral("AppSubtitle"));
    titles->addWidget(t);
    titles->addWidget(s);
    lay->addLayout(titles);

    lay->addSpacing(metrics::s4);
    // 배지는 '변하는' 상태에만 쓴다. 맵 이름 같은 고정 정보는 지도 툴바로 뺐다.
    linkBadge_ = new Badge(QStringLiteral("연결 끊김"), QStringLiteral("danger"));
    missionBadge_ = new Badge(QStringLiteral("미션 대기"), QStringLiteral("neutral"));
    lay->addWidget(linkBadge_);
    lay->addWidget(missionBadge_);

    lay->addStretch(1);

    autoBtn_ = new QPushButton(QStringLiteral("자율"));
    manualBtn_ = new QPushButton(QStringLiteral("수동"));
    for (auto *b : {autoBtn_, manualBtn_}) {
        b->setCheckable(true);
        b->setFixedWidth(84);
        lay->addWidget(b);
    }
    autoBtn_->setChecked(true);

    userBadge_ = new Badge({}, QStringLiteral("neutral"));
    userBadge_->hide();
    lay->addWidget(userBadge_);

    settingsBtn_ = new QPushButton(QStringLiteral("설정"));
    settingsBtn_->setProperty("size", "sm");
    settingsBtn_->setFixedWidth(52);
    lay->addWidget(settingsBtn_);

    themeBtn_ = new QPushButton(QStringLiteral("다크"));
    themeBtn_->setProperty("size", "sm");
    themeBtn_->setFixedWidth(58);
    themeBtn_->setToolTip(QStringLiteral("다크 / 라이트 전환"));
    lay->addWidget(themeBtn_);

    lay->addSpacing(metrics::s3);
    estop_ = new EStopButton(nullptr, 52);
    lay->addWidget(estop_);
    return bar;
}

QWidget *MainWindow::buildContextColumn()
{
    auto *stack = new QStackedWidget;
    stack->setMinimumWidth(380);
    stack->setMaximumWidth(520);

    // NavItem 순서와 페이지 인덱스가 일치해야 한다.
    stack->addWidget(buildDriveContext());
    stack->addWidget(buildLocationsContext());
    stack->addWidget(buildArmContext());
    stack->addWidget(buildCaptureContext());
    stack->addWidget(buildDiagnosticsContext());
    return stack;
}

QWidget *MainWindow::buildDriveContext()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    status_ = new StatusPanel;
    lay->addWidget(status_);

    // 수동 조작은 수동 모드에서만 나타난다. 자율 주행 중에는 의미가 없고,
    // 비활성 컨트롤을 띄워두면 화면만 차지한다.
    teleop_ = new TeleopPanel;
    teleopHost_ = teleop_;
    teleopHost_->setVisible(false);
    lay->addWidget(teleopHost_);

    lay->addStretch(1);
    return page;
}

QWidget *MainWindow::buildLocationsContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, metrics::s2, 0);
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
    lay->setContentsMargins(0, 0, metrics::s2, 0);
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
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto *card = new Card(QStringLiteral("촬영 제어"));
    auto *note = new QLabel(QStringLiteral(
        "촬영 트리거, 2D/3D 미리보기, 메타데이터 입력이 이 자리에 들어갑니다.\n"
        "카메라 라이브뷰는 제어 소켓이 아니라 별도 HTTP MJPEG 경로로 받습니다."));
    note->setObjectName(QStringLiteral("Hint"));
    note->setWordWrap(true);
    card->body()->addWidget(note);
    card->body()->addStretch(1);

    lay->addWidget(card);
    return page;
}

QWidget *MainWindow::buildDiagnosticsContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, metrics::s2, 0);
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
        settings_ = new SettingsDialog(this);
        // 모달이 아니다. 로봇을 보면서 글자 크기를 조정할 수 있어야 한다.
        connect(settings_, &SettingsDialog::appearanceChanged, this, [this] {
            applyTheme(colors().name);
        });
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

// ================= 배선 =================

void MainWindow::wireSignals()
{
    connect(nav_, &NavRail::navigated, this, &MainWindow::navigate);

    // 미션 상태와 로봇 이벤트의 진실 원천은 로봇쪽이다. UI 는 따라간다.
    connect(robot_, &sim::SimRobot::missionStateChanged,
            this, &MainWindow::onMissionStateChanged);
    connect(robot_, &sim::SimRobot::robotEvent, this,
            [this](const QString &code, const QVariantMap &detail) {
                log_->log(code, QJsonObject::fromVariantMap(detail));
            });
    connect(themeBtn_, &QPushButton::clicked, this, [this] {
        const auto &c = toggleTheme();
        Config::instance().setTheme(c.name);
        applyTheme(c.name);
    });
    connect(settingsBtn_, &QPushButton::clicked, this, &MainWindow::openSettings);

    connect(estop_, &EStopButton::engageRequested, this, &MainWindow::engageEstop);
    connect(estop_, &EStopButton::releaseRequested, this, &MainWindow::releaseEstop);

    connect(autoBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("auto")); });
    connect(manualBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("manual")); });

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

    connect(view, &MapView::waypointClicked, this, [this](const QString &id) {
        log_->note(diag::Severity::Info, QStringLiteral("포인트 선택: %1").arg(id));
    });

    connect(locations_, &LocationPanel::captureFromRobot, this, &MainWindow::captureLocation);
    connect(locations_, &LocationPanel::captureFromMap, this, [this](const QString &kind) {
        pendingPlacementKind_ = kind;
        map_->goalButton()->setChecked(false);
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 위치를 지정하고, 드래그해 방향을 정하십시오"));
    });
    connect(locations_, &LocationPanel::gotoRequested, this, [this](const QString &kind) {
        const QVariantMap &loc = kind == QLatin1String("dock") ? dock_ : home_;
        if (loc.isEmpty())
            return;
        log_->note(diag::Severity::Info,
                   QStringLiteral("%1 로 이동")
                       .arg(kind == QLatin1String("dock") ? QStringLiteral("충전 스테이션")
                                                          : QStringLiteral("시작 위치")),
                   QJsonObject{{"channel", QStringLiteral("cmd/goto")},
                               {"x", loc.value(QStringLiteral("x")).toDouble()},
                               {"y", loc.value(QStringLiteral("y")).toDouble()}});
    });

    // 20 Hz 로 흘려보낸다. 시뮬레이터가 데드맨을 그대로 구현하므로,
    // 발행이 멈추면 로봇도 멈춘다.
    connect(teleop_, &TeleopPanel::cmdVel, robot_, &sim::SimRobot::setCmdVel);

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

    connect(waypoints_, &WaypointPanel::addRequested, this, [this] {
        pendingPlacementKind_ = QStringLiteral("inspection");
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 점검포인트를 추가하십시오"));
    });
    connect(waypoints_, &WaypointPanel::orderChanged, this, [this](const QStringList &ids) {
        robot_->setWaypoints(waypoints_->waypoints());
        log_->note(diag::Severity::Info,
                   QStringLiteral("점검 순서 변경 (%1개)").arg(ids.size()),
                   QJsonObject{{"channel", QStringLiteral("cmd/waypoints/set")}});
    });
    connect(waypoints_, &WaypointPanel::missionStart, this, [this] {
        robot_->setWaypoints(waypoints_->waypoints());
        robot_->missionStart();
        log_->log(QStringLiteral("MISSION_START"));
    });
    connect(waypoints_, &WaypointPanel::missionPause, this, [this] {
        robot_->setWaypoints(waypoints_->waypoints());
        robot_->missionPause();
        log_->log(QStringLiteral("MISSION_PAUSE"));
    });
    connect(waypoints_, &WaypointPanel::missionResume, this, [this] {
        robot_->setWaypoints(waypoints_->waypoints());
        robot_->missionResume();
        log_->log(QStringLiteral("MISSION_RESUME"));
    });
    connect(waypoints_, &WaypointPanel::missionStop, this, [this] {
        robot_->setWaypoints(waypoints_->waypoints());
        robot_->missionStop();
        log_->log(QStringLiteral("MISSION_STOP"));
    });
}

void MainWindow::onMissionStateChanged(MissionState state)
{
    const bool running = state != MissionState::Idle;
    const bool paused = state == MissionState::Paused;
    waypoints_->setMissionState(running, paused);

    if (!running)
        missionBadge_->set(QStringLiteral("미션 대기"), QStringLiteral("neutral"));
    else if (paused)
        missionBadge_->set(QStringLiteral("일시정지"), QStringLiteral("warn"));
    else
        missionBadge_->set(QStringLiteral("자율주행 중"), QStringLiteral("info"));
}

void MainWindow::navigate(NavItem item)
{
    context_->setCurrentIndex(int(item));
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
        locations_->setHome(home_);
    } else {
        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        loc[QStringLiteral("id")] = QStringLiteral("TP-%1").arg(n, 2, 10, QLatin1Char('0'));
        loc[QStringLiteral("name")] = QStringLiteral("교시 포인트 %1").arg(n);
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
        this, QStringLiteral("E-Stop 해제 확인"),
        QStringLiteral("E-Stop 을 해제합니다.\n\n"
                       "로봇 주변에 사람이 없고 안전이 확보되었는지 확인하십시오.\n"
                       "해제 후에도 자율주행은 자동 재개되지 않으며,\n"
                       "명시적 재개 명령이 필요합니다."),
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
    if (obj == centralWidget() && ev->type() == QEvent::Resize)
        alert_->setGeometry(centralWidget()->rect());
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::applyTheme(const QString &name)
{
    const Colors &c = setTheme(name);
    if (auto *app = qobject_cast<QApplication *>(QApplication::instance()))
        app->setStyleSheet(buildQss());

    themeBtn_->setText(c.isDark() ? QStringLiteral("라이트") : QStringLiteral("다크"));

    // QSS 로 칠해지는 위젯은 스타일시트 재적용만으로 따라온다.
    // 씬 아이템의 펜 색은 아이템에 박혀 있어 별도로 다시 지정해야 한다.
    map_->view()->retheme();
    for (auto *w : findChildren<QWidget *>())
        w->update();

    // 맵 이미지는 팔레트 색으로 구워져 있으므로 다시 렌더한다.
    if (!mapData_.grid.isEmpty()) {
        map_->view()->setMap(mapData_.info,
                             gcs::map::occupancyToImage(mapData_.grid, mapData_.info.width,
                                                        mapData_.info.height));
    }
}

// ================= 데모 =================

void MainWindow::startSimulation()
{
    mapData_ = sim::buildMap();
    map_->view()->setMap(mapData_.info,
                         gcs::map::occupancyToImage(mapData_.grid, mapData_.info.width,
                                                    mapData_.info.height));
    map_->setMapLabel(mapData_.info.mapId,
                      QStringLiteral("%1×%2 m")
                          .arg(mapData_.info.extentXMeters(), 0, 'f', 0)
                          .arg(mapData_.info.extentYMeters(), 0, 'f', 0));

    const auto wps = robot_->waypoints();
    waypoints_->setWaypoints(wps);
    map_->view()->setWaypoints(wps);
    map_->view()->setTags(sim::buildTags(wps));

    // 충전 스테이션은 맵의 좌상단 구조물 위치에 맞춘다.
    dock_ = QVariantMap{{"kind", QStringLiteral("dock")}, {"x", -13.5}, {"y", 6.6},
                        {"theta", 0.0}, {"captured_from", QStringLiteral("map")}};
    locations_->setDock(dock_);
    locations_->setHome({});

    status_->setConnected(true);
    linkBadge_->set(QStringLiteral("시뮬레이터"), QStringLiteral("warn"));

    auto &session = auth::Session::instance();
    if (session.isSignedIn()) {
        userBadge_->set(QStringLiteral("%1 · %2")
                            .arg(session.displayName(), auth::roleLabel(session.role())),
                        session.role() == auth::Role::Admin ? QStringLiteral("info")
                                                            : QStringLiteral("neutral"));
        userBadge_->show();
    }
    setMode(QStringLiteral("auto"));
    nav_->setCurrent(NavItem::Drive);
    navigate(NavItem::Drive);

    log_->note(diag::Severity::Ok, QStringLiteral("SLAM 맵 로드 완료"),
               QJsonObject{{"map_id", mapData_.info.mapId}});
    log_->note(diag::Severity::Info,
               QStringLiteral("브릿지 미연결 — 내장 시뮬레이터로 구동 중"));

    timer_ = new QTimer(this);
    timer_->setInterval(50);           // 20 Hz. 수동 조작 발행 주기와 맞춘다.
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start();
}

void MainWindow::tick()
{
    const sim::Telemetry tm = robot_->step(0.05);
    auto *view = map_->view();

    view->setRobotPose(tm.x, tm.y, tm.theta);
    view->setTrail(tm.trail);
    view->setPlan(tm.plan);
    view->setTagsSeen(tm.seenTags);

    for (const auto &w : robot_->waypoints()) {
        const QString id = w.value(QStringLiteral("id")).toString();
        const QString st = w.value(QStringLiteral("status")).toString();
        view->setWaypointStatus(id, st);
        waypoints_->setStatus(id, st);
    }

    status_->setPose(tm.x, tm.y, qRadiansToDegrees(tm.theta));
    status_->setBattery(tm.soc);
    status_->setSystem(tm.cpu, tm.mem, tm.cpuTemp, tm.gpuTemp, tm.rtt);
    status_->setTagsSeen(int(tm.seenTags.size()));
    arm_->setArmState(tm.joints, tm.manipulability, tm.sigmaMin,
                      tm.speed > 0.01 ? QStringLiteral("idle")
                                      : QStringLiteral("executing"));

    nav_->setBattery(tm.soc);
    nav_->setPoseText(QStringLiteral("%1, %2\nθ %3°")
                          .arg(tm.x, 0, 'f', 1)
                          .arg(tm.y, 0, 'f', 1)
                          .arg(qRadiansToDegrees(tm.theta), 0, 'f', 0));
    nav_->setDiagnosticsAlerts(log_->countAtOrAbove(diag::Severity::Error));

    // 위치 등록 가능 여부는 실제 속력으로 판정한다. 시뮬레이터가 속력을
    // 직접 알려주므로 UI 가 궤적을 미분할 필요가 없다.
    snapshot_.x = tm.x;
    snapshot_.y = tm.y;
    snapshot_.theta = tm.theta;
    snapshot_.speed = tm.speed;
    snapshot_.poseFresh = true;
    snapshot_.localizationOk = true;
    snapshot_.visibleTagId = tm.seenTags.isEmpty() ? -1 : *tm.seenTags.cbegin();
    locations_->setSnapshot(snapshot_);

    diagnostics_->setSensors(tm.sensors);
    diagnostics_->setLink(tm.link);
    diagnostics_->setStorage(tm.nasOnline, tm.pendingUploads, tm.spoolFreeMb);
}

}  // namespace gcs::ui
