#include "MainWindow.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include "diag/LogStore.h"
#include "mapview/MapRender.h"
#include "mapview/MapView.h"
#include "panels/ArmPanel.h"
#include "panels/EventLogPanel.h"
#include "panels/StatusPanel.h"
#include "panels/TeleopPanel.h"
#include "panels/WaypointPanel.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/BrandMark.h"
#include "widgets/EStopButton.h"
#include "widgets/Gauges.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::map::MapMode;
using gcs::map::MapView;

// ============================ MapCard ============================

MapCard::MapCard(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Card"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(1, 1, 1, 1);
    view_ = new MapView;
    lay->addWidget(view_);

    // 지도 위에 떠 있는 툴바. 레이아웃에 넣지 않고 직접 배치하므로
    // 지도 면적을 잡아먹지 않는다.
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("MapOverlay"));
    auto *tb = new QHBoxLayout(toolbar_);
    tb->setContentsMargins(metrics::s2, metrics::s2, metrics::s2, metrics::s2);
    tb->setSpacing(metrics::s2);

    goal_ = new QPushButton(QStringLiteral("목표 지정"));
    waypoint_ = new QPushButton(QStringLiteral("포인트 추가"));
    fit_ = new QPushButton(QStringLiteral("전체 보기"));
    for (auto *b : {goal_, waypoint_, fit_}) {
        b->setProperty("size", "sm");
        tb->addWidget(b);
    }
    goal_->setCheckable(true);
    waypoint_->setCheckable(true);

    tb->addSpacing(metrics::s2);
    mapLabel_ = sectionLabel(QStringLiteral("맵 없음"));
    tb->addWidget(mapLabel_);

    readout_ = new QLabel(QStringLiteral("—"), this);
    readout_->setObjectName(QStringLiteral("MapReadout"));
    readout_->setAlignment(Qt::AlignCenter);
    readout_->setMinimumWidth(150);

    connect(fit_, &QPushButton::clicked, view_, &MapView::fitMap);
    connect(view_, &MapView::cursorMoved, this, [this](double x, double y) {
        readout_->setText(QStringLiteral("%1, %2").arg(x, 7, 'f', 2).arg(y, 7, 'f', 2));
    });
}

void MapCard::setMapLabel(const QString &mapId, const QString &extent)
{
    mapLabel_->setText(QStringLiteral("%1 · %2").arg(mapId, extent));
    toolbar_->adjustSize();
}

void MapCard::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    toolbar_->adjustSize();
    toolbar_->move(metrics::s3, metrics::s3);
    readout_->adjustSize();
    readout_->move(width() - readout_->width() - metrics::s3, metrics::s3);
}

// ============================ MainWindow ============================

MainWindow::MainWindow(bool demoMode, QWidget *parent)
    : QMainWindow(parent), demoMode_(demoMode)
{
    setWindowTitle(QStringLiteral("SHALOM 관제 · 철도차량 하부 점검시스템"));
    resize(1720, 990);

    log_ = new diag::LogStore(this);

    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("Root"));
    setCentralWidget(root);

    auto *outer = new QVBoxLayout(root);
    outer->setContentsMargins(metrics::s3, metrics::s3, metrics::s3, metrics::s3);
    outer->setSpacing(metrics::s3);
    outer->addWidget(buildTopBar());

    auto *split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    split->setHandleWidth(metrics::s2);
    split->addWidget(buildLeftColumn());
    map_ = new MapCard;
    split->addWidget(map_);
    split->addWidget(buildRightColumn());
    split->setSizes({340, 1020, 400});
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    // E-Stop 발동 시 창 전체를 감싸는 경고 테두리 (지시서 2.2.7 [5]).
    alert_ = new AlertFrame(root);
    alert_->setGeometry(root->rect());
    root->installEventFilter(this);

    wireSignals();
    if (demoMode_)
        startDemo();
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

QWidget *MainWindow::buildLeftColumn()
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    status_ = new StatusPanel;
    teleop_ = new TeleopPanel;
    events_ = new EventLogPanel(log_);
    lay->addWidget(status_);
    lay->addWidget(teleop_);
    lay->addWidget(events_, 1);

    host->setMinimumWidth(320);
    host->setMaximumWidth(420);
    return host;
}

QWidget *MainWindow::buildRightColumn()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, metrics::s2, 0);
    lay->setSpacing(metrics::s3);

    waypoints_ = new WaypointPanel;
    waypoints_->setMinimumHeight(420);
    arm_ = new ArmPanel;
    lay->addWidget(waypoints_);
    lay->addWidget(arm_);

    // 로봇팔 패널은 7 축 슬라이더 때문에 세로로 길다. 스크롤을 붙여
    // 작은 화면에서도 지도 폭을 뺏기지 않게 한다.
    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(380);
    scroll->setMaximumWidth(460);
    return scroll;
}

void MainWindow::wireSignals()
{
    connect(themeBtn_, &QPushButton::clicked, this,
            [this] { applyTheme(toggleTheme().name); });

    connect(estop_, &EStopButton::engageRequested, this, &MainWindow::engageEstop);
    connect(estop_, &EStopButton::releaseRequested, this, &MainWindow::releaseEstop);

    connect(autoBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("auto")); });
    connect(manualBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("manual")); });

    auto *view = map_->view();
    connect(map_->goalButton(), &QPushButton::toggled, this, [view](bool on) {
        view->setMode(on ? MapMode::SetGoal : MapMode::View);
    });
    connect(map_->waypointButton(), &QPushButton::toggled, this, [view](bool on) {
        view->setMode(on ? MapMode::AddWaypoint : MapMode::View);
    });

    connect(view, &MapView::goalRequested, this, [this](double x, double y, double th) {
        map_->goalButton()->setChecked(false);
        log_->note(diag::Severity::Info,
                   QStringLiteral("목표 지정"),
                   {{"x", x}, {"y", y}, {"theta_deg", qRadiansToDegrees(th)},
                    {"channel", QStringLiteral("cmd/goto")}});
    });
    connect(view, &MapView::waypointPlaced, this, [this](double x, double y, double th) {
        map_->waypointButton()->setChecked(false);
        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        wps << QVariantMap{{"id", QStringLiteral("NEW-%1").arg(n, 2, 10, QLatin1Char('0'))},
                           {"name", QStringLiteral("신규 포인트 %1").arg(n)},
                           {"x", x}, {"y", y}, {"theta", th},
                           {"status", QStringLiteral("todo")}};
        waypoints_->setWaypoints(wps);
        map_->view()->setWaypoints(wps);
        log_->note(diag::Severity::Ok, QStringLiteral("점검포인트 추가"),
                   {{"x", x}, {"y", y}});
    });
    connect(view, &MapView::waypointClicked, this, [this](const QString &id) {
        log_->note(diag::Severity::Info, QStringLiteral("포인트 선택: %1").arg(id));
    });

    connect(teleop_, &TeleopPanel::cmdVel, this, [](double, double, double) {
        // 브릿지 연결 시 cmd/cmd_vel 로 20 Hz 발행. 데모에서는 소비하지 않는다.
    });

    connect(arm_, &ArmPanel::presetRequested, this, [this](const QString &name) {
        arm_->applyPresetToSliders(name);
        log_->log(QStringLiteral("ARM_PRESET"), {{"preset", name}});
    });
    connect(arm_, &ArmPanel::jointGoal, this, [this](const QList<double> &) {
        log_->note(diag::Severity::Info, QStringLiteral("관절 목표 전송"),
                   {{"channel", QStringLiteral("cmd/arm/joint_goal")}});
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
        log_->note(diag::Severity::Warn, QStringLiteral("로봇팔 정지 요청"),
                   {{"channel", QStringLiteral("cmd/arm/stop")}});
    });

    connect(waypoints_, &WaypointPanel::addRequested, this,
            [this] { map_->waypointButton()->setChecked(true); });
    connect(waypoints_, &WaypointPanel::orderChanged, this, [this](const QStringList &ids) {
        log_->note(diag::Severity::Info,
                   QStringLiteral("점검 순서 변경 (%1개)").arg(ids.size()),
                   {{"channel", QStringLiteral("cmd/waypoints/set")}});
    });
    connect(waypoints_, &WaypointPanel::missionStart, this, [this] {
        waypoints_->setMissionState(true, false);
        missionBadge_->set(QStringLiteral("자율주행 중"), QStringLiteral("info"));
        log_->log(QStringLiteral("MISSION_START"));
    });
    connect(waypoints_, &WaypointPanel::missionPause, this, [this] {
        waypoints_->setMissionState(true, true);
        missionBadge_->set(QStringLiteral("일시정지"), QStringLiteral("warn"));
        log_->log(QStringLiteral("MISSION_PAUSE"));
    });
    connect(waypoints_, &WaypointPanel::missionResume, this, [this] {
        waypoints_->setMissionState(true, false);
        missionBadge_->set(QStringLiteral("자율주행 중"), QStringLiteral("info"));
        log_->log(QStringLiteral("MISSION_RESUME"));
    });
    connect(waypoints_, &WaypointPanel::missionStop, this, [this] {
        waypoints_->setMissionState(false, false);
        missionBadge_->set(QStringLiteral("미션 대기"), QStringLiteral("neutral"));
        log_->log(QStringLiteral("MISSION_STOP"));
    });
}

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

// ================= 안전 =================

void MainWindow::engageEstop()
{
    estop_->setEngaged(true);
    alert_->setActive(true);
    status_->setMode({}, true);
    teleop_->setJogEnabled(false);
    arm_->setControlsEnabled(false);
    autoBtn_->setChecked(false);
    manualBtn_->setChecked(false);
    log_->log(QStringLiteral("ESTOP_ENGAGED"));
}

void MainWindow::releaseEstop()
{
    // 자동 해제 금지 (지시서 2.2.5). 반드시 사람이 확인한다.
    const auto answer = QMessageBox::question(
        this, QStringLiteral("E-Stop 해제 확인"),
        QStringLiteral("E-Stop 을 해제합니다.\n\n"
                       "로봇 주변에 사람이 없고 안전이 확보되었는지 확인하십시오.\n"
                       "해제 후에도 자율주행은 자동 재개되지 않으며,\n"
                       "명시적 재개 명령이 필요합니다."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    estop_->setEngaged(false);
    alert_->setActive(false);
    log_->log(QStringLiteral("ESTOP_RELEASED"));
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
    autoBtn_->setChecked(isAuto);
    manualBtn_->setChecked(!isAuto);
    status_->setMode(mode, false);
    teleop_->setJogEnabled(!isAuto);
    arm_->setControlsEnabled(true);

    if (isAuto) {
        log_->log(QStringLiteral("MODE_AUTO"));
    } else {
        // 수동 전환 시 자율주행 즉시 중단 (지시서 2.2.5 수동 조작 우선권).
        waypoints_->setMissionState(false, false);
        missionBadge_->set(QStringLiteral("미션 대기"), QStringLiteral("neutral"));
        log_->log(QStringLiteral("MODE_MANUAL"));
    }
}

// ================= 테마 =================

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
    if (demoMode_ && !mapData_.grid.isEmpty()) {
        map_->view()->setMap(mapData_.info,
                             gcs::map::occupancyToImage(mapData_.grid,
                                                        mapData_.info.width,
                                                        mapData_.info.height));
    }
}

// ================= 데모 =================

void MainWindow::startDemo()
{
    mapData_ = demo::buildMap();
    map_->view()->setMap(mapData_.info,
                         gcs::map::occupancyToImage(mapData_.grid, mapData_.info.width,
                                                    mapData_.info.height));
    map_->setMapLabel(mapData_.info.mapId,
                      QStringLiteral("%1×%2 m")
                          .arg(mapData_.info.extentXMeters(), 0, 'f', 0)
                          .arg(mapData_.info.extentYMeters(), 0, 'f', 0));

    auto wps = demo::buildWaypoints();
    waypoints_->setWaypoints(wps);
    map_->view()->setWaypoints(wps);
    map_->view()->setTags(demo::buildTags(wps));

    status_->setConnected(true);
    linkBadge_->set(QStringLiteral("데모 데이터"), QStringLiteral("warn"));
    setMode(QStringLiteral("auto"));

    waypoints_->setMissionState(true, false);
    missionBadge_->set(QStringLiteral("자율주행 중"), QStringLiteral("info"));

    log_->note(diag::Severity::Ok, QStringLiteral("SLAM 맵 로드 완료"),
               {{"map_id", mapData_.info.mapId}});
    log_->note(diag::Severity::Info, QStringLiteral("브릿지 미연결 — 데모 데이터로 구동 중"));
    log_->log(QStringLiteral("MISSION_START"));
    log_->log(QStringLiteral("CAR_START"), {{"car", 1}});
    log_->log(QStringLiteral("SAFE_SLOW"));
    log_->log(QStringLiteral("OBSTACLE_UNKNOWN"), {{"x", 3.21}, {"y", -1.04}});
    log_->log(QStringLiteral("ARM_SINGULAR"), {{"manipulability", 0.021}});
    log_->log(QStringLiteral("TAG_LOST"), {{"point_id", QStringLiteral("C02-P04")}});

    feed_ = std::make_unique<demo::Feed>(wps);
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, &MainWindow::demoTick);
    timer_->start();
}

void MainWindow::demoTick()
{
    const demo::Frame f = feed_->step(0.1);
    auto *view = map_->view();

    view->setRobotPose(f.x, f.y, f.theta);
    view->setTrail(f.trail);
    view->setPlan(f.plan);
    view->setTagsSeen(f.seenTags);

    for (const auto &w : feed_->waypoints()) {
        const QString id = w.value(QStringLiteral("id")).toString();
        const QString st = w.value(QStringLiteral("status")).toString();
        view->setWaypointStatus(id, st);
        waypoints_->setStatus(id, st);
    }

    status_->setPose(f.x, f.y, qRadiansToDegrees(f.theta));
    status_->setBattery(f.soc);
    status_->setSystem(f.cpu, f.mem, f.cpuTemp, f.gpuTemp, f.rtt);
    status_->setTagsSeen(int(f.seenTags.size()));
    arm_->setArmState(f.joints, f.manipulability, f.sigmaMin,
                      f.manipulability > 0.02 ? QStringLiteral("executing")
                                              : QStringLiteral("planning"));
}

}  // namespace gcs::ui
