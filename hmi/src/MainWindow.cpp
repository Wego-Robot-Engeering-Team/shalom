// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "MainWindow.h"

#include <QApplication>
#include <QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QToolTip>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QIcon>
#include <QMenu>
#include <QWidgetAction>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTcpSocket>
#include <QUuid>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "auth/Session.h"
#include "diag/CodeCatalog.h"
#include "diag/LogStore.h"
#include "mapview/MapRender.h"
#include "net/BridgeClient.h"
#include "net/Channels.h"
#include "mapview/MapView.h"
#include "panels/ArmPanel.h"
#include "panels/CapturePanel.h"
#include "data/InspectionRecord.h"
#include "panels/DataPanel.h"
#include "panels/DiagnosticsPanel.h"
#include "panels/EventLogPanel.h"
#include "panels/MissionPanel.h"
#include "panels/MissionLibraryPanel.h"
#include "panels/StatusPanel.h"
#include "panels/TeleopPanel.h"
#include "panels/NavigationSpeedPanel.h"
#include "panels/WaypointPanel.h"
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

namespace {

constexpr int kPresenceProbeIntervalMs = 3000;
constexpr int kPresenceProbeTimeoutMs = 800;
constexpr char kPresenceReply[] = "INSPECTION-PRESENCE/1\n";

QVariantMap waypointDefinition(QVariantMap point)
{
    for (const auto *field : {"status", "kind", "captured_from", "localization_ok", "tag_id"})
        point.remove(QLatin1String(field));
    if (!point.contains(QStringLiteral("name")))
        point.insert(QStringLiteral("name"), point.value(QStringLiteral("id")));
    if (!point.contains(QStringLiteral("theta")))
        point.insert(QStringLiteral("theta"), 0.0);
    return point;
}

QIcon presenceIcon(bool reachable)
{
    constexpr int size = 12;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QString(reachable ? colors().success : colors().danger)));
    painter.drawEllipse(1, 1, size - 2, size - 2);
    return QIcon(pixmap);
}

// theta는 로봇과 저장 파일에서 라디안이다. 조작자에게는 도 단위로
// 보여주되, 저장할 때만 다시 라디안으로 변환한다.
bool editWaypoint(QWidget *parent, QVariantMap &point, const QString &title)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("WaypointEditor"));
    dialog.setWindowTitle(title);
    auto *form = new QFormLayout(&dialog);

    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("WaypointName"));
    name->setText(point.value(QStringLiteral("name"),
                              point.value(QStringLiteral("id"))).toString());
    name->setPlaceholderText(QStringLiteral("예: 차량 앞쪽 점검 위치"));
    form->addRow(QStringLiteral("이름"), name);

    const auto coordinate = [&dialog](const QString &objectName, double value) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setObjectName(objectName);
        spin->setRange(-1000000.0, 1000000.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setSuffix(QStringLiteral(" m"));
        spin->setValue(value);
        return spin;
    };
    auto *x = coordinate(QStringLiteral("WaypointX"),
                         point.value(QStringLiteral("x")).toDouble());
    auto *y = coordinate(QStringLiteral("WaypointY"),
                         point.value(QStringLiteral("y")).toDouble());
    form->addRow(QStringLiteral("X"), x);
    form->addRow(QStringLiteral("Y"), y);

    auto *yaw = new QDoubleSpinBox(&dialog);
    yaw->setObjectName(QStringLiteral("WaypointYaw"));
    yaw->setRange(-180.0, 180.0);
    yaw->setDecimals(1);
    yaw->setSingleStep(5.0);
    yaw->setSuffix(QStringLiteral("°"));
    yaw->setValue(std::remainder(
        qRadiansToDegrees(point.value(QStringLiteral("theta")).toDouble()), 360.0));
    form->addRow(QStringLiteral("도착 방향 (yaw)"), yaw);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("저장"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    form->addRow(buttons);
    const auto updateSave = [name, buttons] {
        const QByteArray utf8 = name->text().trimmed().toUtf8();
        buttons->button(QDialogButtonBox::Save)->setEnabled(!utf8.isEmpty() && utf8.size() <= 120);
    };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, updateSave);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSave();
    name->selectAll();
    name->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;

    point[QStringLiteral("name")] = name->text().trimmed();
    point[QStringLiteral("x")] = x->value();
    point[QStringLiteral("y")] = y->value();
    point[QStringLiteral("theta")] = qDegreesToRadians(yaw->value());
    return true;
}

// 벽에 수직으로 설치한 태그: yaw는 지도 +X에서 태그 앞면 바깥쪽으로
// 향하는 수평 법선의 각도다. 높이와 방향을 모르면 0으로 추정하지 않는다.
bool editMarker(QWidget *parent, QVariantMap &marker, bool headingProvided = false)
{
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("MarkerEditor"));
    dialog.setWindowTitle(QStringLiteral("마커 등록"));
    auto *form = new QFormLayout(&dialog);

    auto *id = new QSpinBox(&dialog);
    id->setObjectName(QStringLiteral("MarkerId"));
    id->setRange(0, 100000);
    id->setValue(marker.value(QStringLiteral("id")).toInt());
    form->addRow(QStringLiteral("태그 ID"), id);

    const auto coordinate = [&dialog](const QString &objectName, double value) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setObjectName(objectName);
        spin->setRange(-1000000.0, 1000000.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        spin->setSuffix(QStringLiteral(" m"));
        spin->setValue(value);
        return spin;
    };
    auto *x = coordinate(QStringLiteral("MarkerX"), marker.value(QStringLiteral("x")).toDouble());
    auto *y = coordinate(QStringLiteral("MarkerY"), marker.value(QStringLiteral("y")).toDouble());
    form->addRow(QStringLiteral("지도 X"), x);
    form->addRow(QStringLiteral("지도 Y"), y);

    auto *z = new QDoubleSpinBox(&dialog);
    z->setObjectName(QStringLiteral("MarkerZ"));
    z->setRange(-1000001.0, 1000000.0);
    z->setDecimals(3);
    z->setSingleStep(0.1);
    z->setSuffix(QStringLiteral(" m"));
    z->setSpecialValueText(QStringLiteral("높이 입력"));
    z->setValue(marker.contains(QStringLiteral("z"))
                    ? marker.value(QStringLiteral("z")).toDouble() : z->minimum());
    form->addRow(QStringLiteral("지도 Z (태그 중심)"), z);

    auto *yaw = new QDoubleSpinBox(&dialog);
    yaw->setObjectName(QStringLiteral("MarkerYaw"));
    yaw->setRange(-181.0, 180.0);
    yaw->setDecimals(1);
    yaw->setSingleStep(5.0);
    yaw->setSuffix(QStringLiteral("°"));
    yaw->setSpecialValueText(QStringLiteral("방향 입력"));
    yaw->setValue(marker.contains(QStringLiteral("yaw")) || headingProvided
                      ? std::remainder(qRadiansToDegrees(
                            marker.value(QStringLiteral("yaw")).toDouble()), 360.0)
                      : yaw->minimum());
    form->addRow(QStringLiteral("태그 앞면 방향"), yaw);

    auto *hint = new QLabel(QStringLiteral("태그를 정면에서 볼 때 앞면이 향하는 지도 방향입니다."),
                            &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("저장"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    form->addRow(buttons);
    const auto updateSave = [z, yaw, buttons] {
        buttons->button(QDialogButtonBox::Save)->setEnabled(
            z->value() > z->minimum() && yaw->value() > yaw->minimum());
    };
    QObject::connect(z, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, updateSave);
    QObject::connect(yaw, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog, updateSave);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSave();
    if (dialog.exec() != QDialog::Accepted)
        return false;

    marker[QStringLiteral("id")] = id->value();
    marker[QStringLiteral("x")] = x->value();
    marker[QStringLiteral("y")] = y->value();
    marker[QStringLiteral("z")] = z->value();
    marker[QStringLiteral("yaw")] = qDegreesToRadians(yaw->value());
    return true;
}

}  // namespace

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
    context_ = qobject_cast<QStackedWidget *>(buildContextColumn());
    status_->goalButton()->setEnabled(false);
    waypoints_->setEditingEnabled(false);

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

    // ---- 어느 로봇인가 ----
    //
    // 관제 한 대가 여러 로봇을 다루므로, 지금 무엇을 조작하고 있는지가
    // 화면에서 가장 먼저 보여야 한다. 주소를 잘못 적어 옆 로봇에 붙어도
    // 나머지 화면은 정상으로 보이고, 그 상태로 비상정지를 누르면 아무도
    // 보고 있지 않은 로봇이 선다.
    //
    // 이름과 연결 상태를 한 덩어리로 둔다. 예전에는 이름표와 "연결" 배지가
    // 따로 있었는데, 붙고 나면 둘이 같은 것을 말해 자리만 차지했다. 상태는
    // 점 하나로 말한다 — 정상을 경고색 상자로 감싸면 읽는 사람이 매번
    // 무엇이 잘못됐는지 확인하게 된다.
    // 점 · 이름 · 주소 · 펼침 표시를 한 덩어리로 둔다. 버튼 안에 배치를
    // 넣고 자식 라벨은 마우스를 통과시킨다 — 어디를 눌러도 열린다.
    robotButton_ = new QPushButton;
    robotButton_->setObjectName(QStringLiteral("RobotPicker"));
    robotButton_->setCursor(Qt::PointingHandCursor);
    connect(robotButton_, &QPushButton::clicked, this, &MainWindow::showRobotPicker);

    auto *pick = new QHBoxLayout(robotButton_);
    pick->setContentsMargins(metrics::s2, 4, metrics::s2, 4);
    pick->setSpacing(metrics::s2);

    linkDot_ = new QLabel;
    linkDot_->setObjectName(QStringLiteral("LinkDot"));
    linkDot_->setFixedSize(8, 8);
    pick->addWidget(linkDot_, 0, Qt::AlignVCenter);

    // 연결 후에는 로봇이 보낸 ID가 주 표시다. 별칭과 주소는 그 아래에 둔다.
    auto *stack = new QVBoxLayout;
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    robotNameLabel_ = new QLabel(QStringLiteral("로봇 선택"));
    robotNameLabel_->setObjectName(QStringLiteral("RobotPickerName"));
    robotAddrLabel_ = new QLabel;
    robotAddrLabel_->setObjectName(QStringLiteral("RobotPickerAddr"));
    stack->addWidget(robotNameLabel_);
    stack->addWidget(robotAddrLabel_);
    pick->addLayout(stack);

    auto *chevron = new QLabel(QStringLiteral("⌄"));
    chevron->setObjectName(QStringLiteral("RobotPickerChevron"));
    pick->addWidget(chevron, 0, Qt::AlignVCenter);

    for (QWidget *child : {static_cast<QWidget *>(linkDot_),
                           static_cast<QWidget *>(robotNameLabel_),
                           static_cast<QWidget *>(robotAddrLabel_),
                           static_cast<QWidget *>(chevron)})
        child->setAttribute(Qt::WA_TransparentForMouseEvents);

    lay->addWidget(robotButton_, 0, Qt::AlignVCenter);

    setLinkTone(QStringLiteral("danger"));

    lay->addSpacing(metrics::s3);
    lay->addWidget(captionLabel(QStringLiteral("배터리")), 0, Qt::AlignVCenter);
    headerBattery_ = new BatteryPill(nullptr, 25.0);
    headerBattery_->setUnavailable();
    lay->addWidget(headerBattery_, 0, Qt::AlignVCenter);

    lay->addStretch(1);

    // 주행 모드 버튼은 여기서 만들고 주행 운용 패널에 배치한다.
    autoBtn_ = new QPushButton(QStringLiteral("자율"));
    manualBtn_ = new QPushButton(QStringLiteral("수동"));
    for (auto *b : {autoBtn_, manualBtn_}) {
        b->setCheckable(true);
        b->setProperty("size", "sm");
        b->setFixedWidth(70);
    }
    autoBtn_->setEnabled(false);
    manualBtn_->setEnabled(false);

    // ---- 부수적인 것 ----
    // 테두리 없는 버튼으로 낮춘다. 조작이 아니라 도구다.

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
    stack->addWidget(buildMissionContext());
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
    driveTabs_ = new QTabWidget;
    driveTabs_->setObjectName(QStringLiteral("DriveTabs"));

    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    auto *modes = new QHBoxLayout;
    for (auto *button : {manualBtn_, autoBtn_}) {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        modes->addWidget(button);
    }
    lay->addLayout(modes);
    status_ = new StatusPanel;
    status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    lay->addWidget(status_);


    // 본체의 수동 이동과 자세는 주행의 한 방식이다. 여러 장치를 쓰는
    // 미션의 진행·제어는 미션 탭에서 맡는다.
    teleop_ = new TeleopPanel;
    teleop_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    lay->addWidget(teleop_);
    teleop_->hide();
    navigationSpeed_ = new NavigationSpeedPanel;
    navigationSpeed_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    lay->addWidget(navigationSpeed_);
    lay->addStretch(1);

    // 스크롤로 감싸지 않으면 이 열의 최소 높이가 카드 높이의 합이 된다.
    // 나머지 화면과 같은 방식이다.
    auto *operationScroll = new QScrollArea;
    operationScroll->setWidget(inner);
    operationScroll->setWidgetResizable(true);
    operationScroll->setFrameShape(QFrame::NoFrame);
    operationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lay->setSizeConstraint(QLayout::SetMinAndMaxSize);
    driveTabs_->addTab(operationScroll, QStringLiteral("운용"));
    driveTabs_->addTab(buildLocationsContext(), QStringLiteral("위치 관리"));
    connect(driveTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        const bool operation = index == 0;
        refreshGoalAvailability();
        if (!operation)
            teleop_->cancelJog();
        // 위치를 지도에 찍는 도중 운용 화면으로 돌아오면 다음 지도 클릭이
        // 의도치 않게 위치를 추가하지 않도록 편집 상태를 끝낸다.
        pendingPlacementKind_.clear();
        status_->goalButton()->setChecked(false);
        map_->poseEstimateButton()->setChecked(false);
        map_->view()->setMode(MapMode::View);
        map_->setPlacementHint({});
    });
    return driveTabs_;
}

QWidget *MainWindow::buildMissionContext()
{
    auto *inner = new QWidget;
    auto *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s3);

    mission_ = new MissionPanel;
    lay->addWidget(mission_);
    missionLibrary_ = new MissionLibraryPanel;
    lay->addWidget(missionLibrary_);

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
    lay->addWidget(waypoints_, 1);
    lay->addWidget(locations_);

    auto *scroll = new QScrollArea;
    scroll->setWidget(inner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

QWidget *MainWindow::buildArmContext()
{
    arm_ = new ArmPanel;
    return arm_;
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
    // 누가 시켰는지가 이력의 핵심이다. 무엇이 언제 일어났는지만 남으면
    // 사후에 책임 소재를 가릴 수 없다.
    const QString by = auth::Session::instance().displayName();
    if (!by.isEmpty())
        detail[QStringLiteral("by")] = by;
    log_->log(code, QJsonObject::fromVariantMap(detail));
}

void MainWindow::openSettings()
{
    if (!settings_) {
        // 모달이 아니다. 로봇을 보면서 글자 크기를 조정할 수 있어야 한다.
        settings_ = new SettingsDialog(this);
        connect(settings_, &SettingsDialog::navigationSpeedRangesRequested,
                robot_, &robot::RobotLink::setNavigationSpeedRanges);
        connect(robot_, &robot::RobotLink::commandResult,
                settings_, &SettingsDialog::handleCommandResult);
        connect(settings_, &SettingsDialog::batteryPolicyChanged, this,
                &MainWindow::pushBatteryPolicy);
        connect(settings_, &SettingsDialog::robotProfilesChanged, this, [this] {
            // 목록에서 현재 로봇까지 지웠다면 Config 만 비우고 TCP 연결을
            // 살려 두면, "로봇 선택" 화면이 실제로는 지운 로봇을 계속
            // 조작하는 위험한 상태가 된다. 삭제는 연결 해제까지 한 동작이다.
            auto &cfg = Config::instance();
            robotPresence_.clear();
            autoConnectOnPresence_ = true;
            pollRobotPresence();

            if (cfg.currentRobot() >= 0) {
                const auto list = cfg.robots();
                const auto &entry = list.at(cfg.currentRobot());
                if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                    bridge->setEndpoint(entry.host, quint16(entry.port));
                refreshRobotButton();
                return;
            }

            if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                bridge->disconnectFromBridge();
            saidId_.clear();
            saidName_.clear();
            maps_.clear();
            map_->setMapListEnabled(false);
            status_->goalButton()->setChecked(false);
            status_->goalButton()->setEnabled(false);
            map_->poseEstimateButton()->setChecked(false);
            map_->poseEstimateButton()->setEnabled(false);
            waypoints_->setEditingEnabled(false);
            map_->setMapLabel(QStringLiteral("지도 없음"), QString());
            setLinkTone(QStringLiteral("danger"));
            headerBattery_->setUnavailable();
            arm_->setControlsEnabled(false);
            arm_->setExecutionAvailable(false);
            arm_->clearReportedState();
            teleop_->setJogEnabled(false);
            refreshRobotButton();
        });
        // 표시 설정은 설정 창 안에서 먼저 미리 본다. Config 는 저장을 누를
        // 때만 바뀌므로, 이 경로가 없으면 슬라이더를 움직여도 확인할 수 없다.
        connect(settings_, &SettingsDialog::appearancePreviewChanged, this,
                [this](const QString &theme, double scale) {
                    setUiScale(scale);
                    applyTheme(theme);
                });
    }
    settings_->reload();
    if (navigationSpeedLimits_) {
        const auto &limits = *navigationSpeedLimits_;
        settings_->setNavigationSpeedState(limits[0], limits[1], limits[2], limits[3], limits[4], limits[5],
                                           navigationSpeedApplied_);
    } else {
        settings_->resetNavigationSpeed();
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

void MainWindow::openConnectionSettings()
{
    openSettings();
    // 로봇 선택 메뉴에서 온 사람은 표시·전원 설정을 보려는 것이 아니라
    // 주소를 추가·수정하려는 것이다. 첫 탭부터 다시 찾게 하지 않는다.
    settings_->setCurrentTab(1);
}

// ================= 배선 =================

void MainWindow::wireSignals()
{
    wireRobotSignals();
    wireChromeSignals();
    wireMapSignals();
    // 촬영 결과는 로봇이 저장한 바이트를 그대로 되돌려 준다.
    if (auto *bridge = qobject_cast<hmi::net::BridgeClient *>(robot_)) {
        connect(bridge, &hmi::net::BridgeClient::previewReceived, this,
                [this](const QByteArray &bytes, const QJsonObject &meta) {
                    const QString file = meta.value(QStringLiteral("file")).toString();
                    if (!pendingCaptureMetadata_.isEmpty() ||
                        (!lastCaptureFile_.isEmpty() && file != lastCaptureFile_))
                        return;  // 이전 촬영의 늦은 미리보기
                    capture_->setSavedFileName(file);
                    QImage img;
                    if (!img.loadFromData(bytes)) {
                        log_->note(diag::Severity::Warn,
                                   QStringLiteral("촬영 미리보기를 읽지 못했습니다"));
                        return;
                    }
                    capture_->showPreview2d(img);
                    log_->note(diag::Severity::Ok, QStringLiteral("촬영 미리보기 수신"), meta);
                });
    }

    wireLocationSignals();
    wirePanelSignals();
    wireMissionSignals();
}

void MainWindow::wireRobotSignals()
{
    connect(nav_, &NavRail::navigated, this, &MainWindow::navigate);
    connect(log_, &diag::LogStore::appended, this, &MainWindow::onLogAppended);
    connect(bell_, &NotificationBell::unreadChanged, this,
            [this](int count) { nav_->setEventAlerts(count); });
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
                    const bool liveFrame = info->mapId.isEmpty() ||
                                           info->mapId == QLatin1String("live");
                    const bool sameActiveMap = info->mapId == activeMapId_ ||
                        (activeMapId_ == QLatin1String("live") && liveFrame);
                    if ((!requestedMapId_.isEmpty() && info->mapId != requestedMapId_) ||
                        (requestedMapId_.isEmpty() && !activeMapId_.isEmpty() &&
                         !sameActiveMap))
                        return;  // 지도 전환 중 도착한 이전 지도의 프레임
                    if (const auto *previous = map_->view()->mapInfo();
                        previous && previous->mapId != info->mapId)
                        clearDraftGoal();
                    map_->view()->setMap(*info, img);
                    // 현재 화면 목록만 새 좌표계에 다시 놓는다. 지도 전환 중
                    // RobotLink에 남아 있는 이전 지도 지점을 재표시하면 안 된다.
                    map_->view()->setWaypoints(waypoints_->waypoints());
                    mapExtent_ = QStringLiteral("%1×%2 m")
                                     .arg(info->extentXMeters(), 0, 'f', 0)
                                     .arg(info->extentYMeters(), 0, 'f', 0);
                    map_->setMapLabel(sameActiveMap && !activeMapName_.isEmpty()
                                          ? activeMapName_
                                          : liveFrame ? QStringLiteral("live") : info->mapId,
                                      mapExtent_);
                    const bool ready = requestedMapId_.isEmpty() && sameActiveMap &&
                                       !activeMapId_.isEmpty() &&
                                       activeMapId_ != QLatin1String("live");
                    refreshGoalAvailability();
                    map_->poseEstimateButton()->setEnabled(robot_->isConnected() && ready);
                    waypoints_->setEditingEnabled(ready && !waypointWritePending_);
                    log_->note(diag::Severity::Ok, QStringLiteral("지도 수신"),
                               QJsonObject{{"map_id", info->mapId}});
                });
        connect(bridge, &net::BridgeClient::mapsReceived, this,
                [this](const QList<QVariantMap> &maps) {
                    maps_ = maps;
                    map_->setMapListEnabled(robot_->isConnected() && requestedMapId_.isEmpty());
                });
        connect(bridge, &net::BridgeClient::activeMapReceived, this,
                [this](const QVariantMap &map) {
                    const QString mapId = map.value(QStringLiteral("id")).toString();
                    const QString name = map.value(QStringLiteral("name")).toString();
                    const bool wasLive = activeMapId_ == QLatin1String("live");
                    if (activeMapId_ != mapId) {
                        clearDraftGoal();
                        goalStartPending_ = false;
                        navigationError_.clear();
                        missionDefinitions_.clear();
                        refreshMissionProgress();
                    }
                    activeMapId_ = mapId;
                    activeMapName_ = name.isEmpty() ? mapId : name;
                    if (mapId == QLatin1String("live")) {
                        waypoints_->setSaveStatus(QStringLiteral(
                            "실시간 지도 · 웨이포인트 저장 불가"));
                    } else if (wasLive) {
                        waypoints_->setSaveStatus({});
                    }
                    missionLibrary_->setMapId(mapId);
                    const auto *shown = map_->view()->mapInfo();
                    if (shown && shown->mapId != mapId) {
                        waypoints_->setWaypoints({});
                        map_->view()->setWaypoints({});
                    }
                    const bool ready = requestedMapId_.isEmpty() && shown &&
                                       shown->mapId == mapId && !mapId.isEmpty() &&
                                       mapId != QLatin1String("live");
                    map_->poseEstimateButton()->setEnabled(
                        robot_->isConnected() && ready);
                    refreshGoalAvailability();
                    waypoints_->setEditingEnabled(ready && !waypointWritePending_);
                    map_->setMapLabel(activeMapName_, shown && shown->mapId == mapId
                                                         ? mapExtent_ : QStringLiteral("불러오는 중"));
                    map_->setMapListEnabled(robot_->isConnected() && requestedMapId_.isEmpty());
                });
    }

    connect(robot_, &robot::RobotLink::connectionChanged, this, [this](bool ok) {
        setLinkTone(ok ? QStringLiteral("ok") : QStringLiteral("danger"));
        // 끊기면 로봇이 말하던 값을 놓는다. 화면에는 저장해 둔 이름이 남는다 —
        // 한 번 붙어 본 로봇이라면 그 이름이 그 주소의 로봇 이름이다.
        // 다시 붙을 때 로봇이 다시 말하므로, 그 사이에 로봇이 바뀌었다면
        // 그때 이름이 바뀌어 드러난다.
        if (!ok) {
            if (!pendingCaptureMetadata_.isEmpty())
                log_->note(diag::Severity::Warn,
                           QStringLiteral("로봇 연결이 끊어져 촬영 결과를 확인하지 못했습니다."));
            pendingCaptureMetadata_.clear();
            lastCaptureFile_.clear();
            capture_->resetCapture();
            driveModeConfirmed_ = false;
            requestedDriveMode_.reset();
            showReportedDriveMode();
            autoBtn_->setEnabled(false);
            manualBtn_->setEnabled(false);
            saidId_.clear();
            saidName_.clear();
            maps_.clear();
            activeMapId_.clear();
            navigationStatus_.clear();
            clearDraftGoal();
            goalStartPending_ = false;
            navigationError_.clear();
            navigationTelemetry_ = {};
            status_->setTelemetry({}, false);
            refreshGoalAvailability();
            activeMapName_.clear();
            mapExtent_.clear();
            requestedMapId_.clear();
            missionDefinitions_.clear();
            activeMissionId_.clear();
            activeMissionIndex_ = -1;
            activeMissionTotal_ = 0;
            missionLibrary_->setMissions({});
            missionLibrary_->setArmPosePresets({});
            missionLibrary_->setMapId({});
            refreshMissionProgress();
            mission_->setMissionState(QStringLiteral("disconnected"));
            waypointWritePending_ = false;
            waypointWriteAccepted_ = false;
            pendingWaypoints_.clear();
            waypoints_->setEditingEnabled(false);
            waypoints_->setSaveStatus({});
            waypoints_->setWaypoints({});
            map_->view()->clearMap();
            dock_.clear();
            home_.clear();
            showFixedLocations();
            locations_->setMarkers({});
            map_->view()->setTags({});
            map_->setMapListEnabled(false);
            status_->goalButton()->setChecked(false);
            status_->goalButton()->setEnabled(false);
            map_->poseEstimateButton()->setChecked(false);
            map_->poseEstimateButton()->setEnabled(false);
            map_->setMapLabel(QStringLiteral("지도 없음"), {});
            headerBattery_->setUnavailable();
            nav_->setDiagnosticsAlerts(0);
            teleop_->setJogEnabled(false);
            arm_->setControlsEnabled(false);
            arm_->clearReportedState();
            arm_->setPosePresets({});
            refreshRobotButton();
        } else if (!estop_->isEngaged()) {
            driveModeConfirmed_ = false;
            requestedDriveMode_.reset();
            showReportedDriveMode();
            autoBtn_->setEnabled(true);
            manualBtn_->setEnabled(true);
            map_->setMapListEnabled(true);
            refreshGoalAvailability();
            // 연결 자체는 제어 권한이 아니다. 다만 기존의 빈 화면에서처럼
            // 조작계를 계속 비활성으로 두면 새로 고른 로봇을 조작할 수 없다.
            arm_->setControlsEnabled(true);
            // 본체 조작도 같이 돌아와야 한다. 연결이 끊겼을 때 잠가 둔 것을
            // 다시 풀지 않으면, 로봇이 붙어 있는데 조작만 죽은 채로 남는다.
            teleop_->setJogEnabled(true);
        }
    });

    connect(robot_, &robot::RobotLink::driveModeReported, this,
            [this](DriveMode mode) {
                driveModeConfirmed_ = true;
                if (requestedDriveMode_ && *requestedDriveMode_ == mode && !robot_->modeChangePending())
                    requestedDriveMode_.reset();
                showReportedDriveMode();
                refreshGoalAvailability();
            });
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &, const QString &) {
                if (channel != QLatin1String(hmi::ch::kCmdMode) || ok)
                    return;
                requestedDriveMode_.reset();
                showReportedDriveMode();
                refreshGoalAvailability();
            });

    connect(robot_, &robot::RobotLink::robotIdentity, this,
            [this](const QString &id, const QString &name) {
                // 로봇의 ID와 내부 이름은 로봇이 보고한다. HMI에 저장한 별칭은
                // 운영자가 붙인 값이므로, 내부 이름으로 자동 덮어쓰지 않는다.
                saidId_ = id;
                saidName_ = name;
                refreshRobotButton();
                // 이력에는 식별자를 남긴다. 사람용 이름은 로봇이 바꿀 수
                // 있지만 식별자는 그 기계를 가리킨다.
                if (!id.isEmpty())
                    log_->note(diag::Severity::Info,
                               QStringLiteral("로봇 %1 에 연결").arg(id),
                               QJsonObject{{"name", name}});
            });

    // 미션 상태와 로봇 이벤트의 진실 원천은 로봇쪽이다. UI 는 따라간다.
    connect(robot_, &robot::RobotLink::missionStateChanged,
            this, &MainWindow::onMissionStateChanged);
    connect(robot_, &robot::RobotLink::missionsChanged, this,
            [this](const QList<QVariantMap> &missions) {
                missionDefinitions_ = missions;
                missionLibrary_->setMissions(missions);
                refreshMissionProgress();
            });
    connect(robot_, &robot::RobotLink::missionProgressChanged, this,
            [this](const QString &id, int index, int total) {
                activeMissionId_ = id;
                activeMissionIndex_ = index;
                activeMissionTotal_ = total;
                refreshMissionProgress();
            });
    connect(robot_, &robot::RobotLink::armPosePresetsChanged,
            arm_, &ArmPanel::setPosePresets);
    connect(robot_, &robot::RobotLink::armPosePresetsChanged,
            missionLibrary_, &MissionLibraryPanel::setArmPosePresets);
    connect(robot_, &robot::RobotLink::fixedLocationsChanged, this, [this] {
        if (!requestedMapId_.isEmpty())
            return;
        dock_ = robot_->dockPose();
        home_ = robot_->homePose();
        showFixedLocations();
    });
    connect(robot_, &robot::RobotLink::mapMarkersChanged, this, [this] {
        if (!requestedMapId_.isEmpty())
            return;
        const auto markers = robot_->markers();
        locations_->setMarkers(markers);
        map_->view()->setTags(markers);
    });
    connect(robot_, &robot::RobotLink::waypointsChanged, this,
            [this](const QList<QVariantMap> &points) {
                applyWaypointCatalog(points);
            });
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &code, const QString &message) {
                if (channel == QLatin1String(hmi::ch::kCmdWaypointsSet) && ok && waypointWritePending_) {
                    waypointWriteAccepted_ = true;
                    applyWaypointCatalog(robot_->waypoints());
                }
                if (channel == QLatin1String(hmi::ch::kCmdWaypointsSet) && !ok) {
                    waypointWritePending_ = false;
                    waypointWriteAccepted_ = false;
                    pendingWaypoints_.clear();
                    applyWaypointCatalog(robot_->waypoints());
                    const auto *shown = map_->view()->mapInfo();
                    waypoints_->setEditingEnabled(robot_->isConnected() && shown &&
                                                  shown->mapId == activeMapId_ &&
                                                  activeMapId_ != QLatin1String("live") &&
                                                  requestedMapId_.isEmpty());
                    waypoints_->setSaveStatus(
                        QStringLiteral("저장 실패 · %1 %2").arg(code, message), true);
                }
                if (channel == QLatin1String(hmi::ch::kCmdLocationsSet) && !ok) {
                    dock_ = robot_->dockPose();
                    home_ = robot_->homePose();
                    showFixedLocations();
                    log_->note(diag::Severity::Error, QStringLiteral("위치 저장 실패 · %1 %2").arg(code, message));
                }
                if (channel == QLatin1String(hmi::ch::kCmdMarkersSet) && !ok) {
                    log_->note(diag::Severity::Error,
                               QStringLiteral("마커 저장 실패 · %1 %2").arg(code, message));
                    if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                        bridge->requestMapCatalog();
                    else {
                        locations_->setMarkers(robot_->markers());
                        map_->view()->setTags(robot_->markers());
                    }
                }
            });
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &code, const QString &message) {
                if (channel == QLatin1String(hmi::ch::kCmdMapsSelect)) {
                    requestedMapId_.clear();
                    waypoints_->setWaypoints(robot_->waypoints());
                    map_->view()->setWaypoints(waypoints_->waypoints());
                    dock_ = robot_->dockPose();
                    home_ = robot_->homePose();
                    showFixedLocations();
                    locations_->setMarkers(robot_->markers());
                    map_->view()->setTags(robot_->markers());
                    map_->setMapListEnabled(robot_->isConnected());
                    const auto *shown = map_->view()->mapInfo();
                    const bool ready = shown && shown->mapId == activeMapId_;
                    refreshGoalAvailability();
                    map_->poseEstimateButton()->setEnabled(
                        robot_->isConnected() && ready &&
                        activeMapId_ != QLatin1String("live"));
                    waypoints_->setEditingEnabled(ready && activeMapId_ != QLatin1String("live") &&
                                                  !waypointWritePending_);
                    map_->setMapLabel(activeMapName_.isEmpty() ? activeMapId_ : activeMapName_,
                                      ready ? mapExtent_ : QStringLiteral("불러오는 중"));
                }
                if (!ok && (channel == QLatin1String(hmi::ch::kCmdMapsSelect) ||
                            channel == QLatin1String(hmi::ch::kCmdMapsRename) ||
                            channel == QLatin1String(hmi::ch::kCmdMapsSetDefault) ||
                            channel == QLatin1String(hmi::ch::kCmdMapsDelete)))
                    log_->note(diag::Severity::Error,
                               QStringLiteral("지도 작업 실패 · %1 %2").arg(code, message));
            });
    connect(robot_, &robot::RobotLink::commandResult, missionLibrary_,
            &MissionLibraryPanel::handleCommandResult);
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &, const QString &) {
                if (ok && channel == QLatin1String(hmi::ch::kCmdMissionStart))
                    showView(NavItem::Mission);
            });
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

    connect(estop_, &EStopButton::engageRequested, this, &MainWindow::engageEstop);
    connect(estop_, &EStopButton::releaseRequested, this, &MainWindow::releaseEstop);

    connect(autoBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("auto")); });
    connect(manualBtn_, &QPushButton::clicked, this, [this] { setMode(QStringLiteral("manual")); });
}

void MainWindow::wireMapSignals()
{
    auto *view = map_->view();
    connect(map_->mapButton(), &QPushButton::clicked, this, &MainWindow::showMapPicker);
    connect(status_->navPauseButton(), &QPushButton::clicked, this, [this] {
        if (!robot_->isConnected())
            return;
        if (navigationStatus_ == QLatin1String("paused"))
            robot_->resumeNav();
        else
            robot_->pauseNav();
    });
    connect(status_->startButton(), &QPushButton::clicked, this, &MainWindow::startDraftGoal);
    connect(status_, &StatusPanel::missionRequested, this, [this] { showView(NavItem::Mission); });
    connect(status_->navCancelButton(), &QPushButton::clicked, this, [this] {
        if (!draftGoal_.isEmpty() && !goalStartPending_) {
            status_->goalButton()->setChecked(false);
            clearDraftGoal();
            navigationError_.clear();
            refreshGoalAvailability();
        } else {
            robot_->cancelNav();
        }
    });
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &code, const QString &message) {
        if (channel != QLatin1String(ch::kCmdGoto) && channel != QLatin1String(ch::kCmdNavResume) &&
            channel != QLatin1String(ch::kCmdNavPause) && channel != QLatin1String(ch::kCmdNavCancel))
            return;
        if (!ok) {
            if (channel == QLatin1String(ch::kCmdGoto)) goalStartPending_ = false;
            navigationError_ = message.isEmpty() ? code : message;
        } else {
            navigationError_.clear();
        }
        refreshGoalAvailability();
    });
    connect(map_->refreshButton(), &QPushButton::clicked, this, [this] {
        if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
            bridge->requestMapCatalog();
    });

    connect(status_->goalButton(), &QPushButton::toggled, this, [this, view](bool on) {
        if (on)
            map_->poseEstimateButton()->setChecked(false);
        pendingPlacementKind_.clear();
        view->setMode(on ? MapMode::SetGoal : MapMode::View);
        map_->setPlacementHint(on ? QStringLiteral("지도를 클릭해 목표를 지정하고, "
                                                   "드래그해 방향을 정하십시오")
                                  : QString());
    });

    connect(map_->poseEstimateButton(), &QPushButton::toggled, this,
            [this, view](bool on) {
        if (on) {
            status_->goalButton()->setChecked(false);
            pendingPlacementKind_.clear();
            view->setMode(MapMode::EstimatePose);
            map_->setPlacementHint(
                QStringLiteral("지도를 클릭하고 드래그해 로봇의 초기 위치와 방향을 지정하십시오"));
        } else if (view->mode() == MapMode::EstimatePose) {
            view->setMode(MapMode::View);
            map_->setPlacementHint({});
        }
    });

    connect(view, &MapView::goalRequested, this, [this](double x, double y, double th) {
        status_->goalButton()->setChecked(false);
        map_->setPlacementHint({});
        if (!canPlaceGoal())
            return;  // 모드나 지도가 바뀐 뒤 도착한 지도 클릭은 명령이 아니다.
        setDraftGoal({{"x", x}, {"y", y}, {"theta", th}});
    });

    connect(view, &MapView::poseEstimateRequested, this,
            [this](double x, double y, double theta) {
        map_->poseEstimateButton()->setChecked(false);
        map_->setPlacementHint({});
        robot_->setInitialPose(x, y, theta);
        log_->note(diag::Severity::Info, QStringLiteral("초기 위치 추정 요청"),
                   QJsonObject{{"x", x}, {"y", y}, {"theta", theta},
                               {"channel", QStringLiteral("cmd/localization/initial_pose")}});
    });

    connect(view, &MapView::waypointPlaced, this, [this](double x, double y, double th) {
        const QString kind = pendingPlacementKind_.isEmpty()
                                 ? QStringLiteral("inspection")
                                 : pendingPlacementKind_;
        pendingPlacementKind_.clear();
        map_->setPlacementHint({});

        QVariantMap loc{{"x", x}, {"y", y}, {"theta", th}};

        if (kind == QLatin1String("dock") || kind == QLatin1String("home")) {
            loc[QStringLiteral("kind")] = kind;
            (kind == QLatin1String("dock") ? dock_ : home_) = loc;
            applyFixedLocations();
            log_->log(QStringLiteral("SETUP_LOC_CAPTURED"), QJsonObject::fromVariantMap(loc));
            return;
        }

        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        loc[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        loc[QStringLiteral("name")] = QStringLiteral("신규 포인트 %1").arg(n);
        if (!editWaypoint(this, loc, QStringLiteral("웨이포인트 추가")))
            return;
        wps << loc;
        if (submitWaypoints(wps))
            log_->log(QStringLiteral("SETUP_LOC_CAPTURED"), QJsonObject::fromVariantMap(loc));
    });

    connect(map_->legend(), &MapLegend::manageRequested, this,
            [this](MapLegend::Item item) {
                // "이게 뭐지" 다음은 대개 "바꾸고 싶다" 이다. 설명을 읽은
                // 자리에서 관리 화면으로 바로 넘어가게 한다.
                if (item == MapLegend::Item::Waypoint
                    || item == MapLegend::Item::Dock
                    || item == MapLegend::Item::Home)
                    showLocationAssets();
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
        status_->goalButton()->setChecked(false);
        map_->poseEstimateButton()->setChecked(false);
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 위치를 지정하고, 드래그해 방향을 정하십시오"));
    });
    // 지도에서 태그 중심을 찍고, 드래그 방향을 앞면의 수평 법선으로 쓴다.
    // 클릭만 한 경우 방향은 비워 두고 등록 창에서 직접 입력받는다.
    connect(locations_, &LocationPanel::addMarkerFromMap, this, [this] {
        status_->goalButton()->setChecked(false);
        map_->view()->setMode(MapMode::AddTag);
        map_->setPlacementHint(
            QStringLiteral("마커 중심을 클릭하고 앞면 방향으로 드래그하십시오"));
    });
    connect(map_->view(), &MapView::tagPlaced, this,
            [this](double x, double y, double yaw, bool headingProvided) {
        map_->setPlacementHint({});

        QList<QVariantMap> ms = locations_->markers();
        int suggested = 0;
        for (const auto &m : std::as_const(ms))
            suggested = qMax(suggested, m.value(QStringLiteral("id")).toInt() + 1);

        QVariantMap marker{{QStringLiteral("id"), suggested},
                           {QStringLiteral("x"), x}, {QStringLiteral("y"), y}};
        if (headingProvided)
            marker[QStringLiteral("yaw")] = yaw;
        if (!editMarker(this, marker, headingProvided))
            return;

        const int id = marker.value(QStringLiteral("id")).toInt();

        // 같은 번호가 이미 있으면 자리를 옮긴 것으로 본다. 같은 ID 를 둘
        // 두면 로봇이 어느 쪽으로 보정할지 알 수 없다.
        for (int i = 0; i < ms.size(); ++i) {
            if (ms.at(i).value(QStringLiteral("id")).toInt() != id)
                continue;
            if (QMessageBox::question(
                    this, QStringLiteral("이미 등록된 마커입니다"),
                    QStringLiteral("마커 #%1 은 이미 등록되어 있습니다.\n"
                                   "새 자리로 옮기시겠습니까?").arg(id))
                != QMessageBox::Yes)
                return;
            ms.removeAt(i);
            break;
        }

        ms << marker;
        locations_->setMarkers(ms);
        robot_->setMarkers(ms);
        map_->view()->setTags(ms);
        log_->note(diag::Severity::Info, QStringLiteral("마커 #%1 등록").arg(id),
                   QJsonObject{{"channel", QStringLiteral("cmd/markers/set")},
                               {"x", marker.value(QStringLiteral("x")).toDouble()},
                               {"y", marker.value(QStringLiteral("y")).toDouble()},
                               {"z", marker.value(QStringLiteral("z")).toDouble()},
                               {"yaw", marker.value(QStringLiteral("yaw")).toDouble()}});
    });
    connect(locations_, &LocationPanel::editMarkerRequested, this, [this](int row) {
        QList<QVariantMap> ms = locations_->markers();
        if (row < 0 || row >= ms.size())
            return;
        QVariantMap marker = ms.at(row);
        if (!editMarker(this, marker))
            return;
        const int id = marker.value(QStringLiteral("id")).toInt();
        for (int i = 0; i < ms.size(); ++i) {
            if (i != row && ms.at(i).value(QStringLiteral("id")).toInt() == id) {
                QMessageBox::warning(this, QStringLiteral("마커 ID 중복"),
                                     QStringLiteral("마커 #%1이 이미 등록되어 있습니다.").arg(id));
                return;
            }
        }
        ms[row] = marker;
        locations_->setMarkers(ms);
        robot_->setMarkers(ms);
        map_->view()->setTags(ms);
    });
    connect(locations_, &LocationPanel::markersChanged, this,
            [this](const QList<QVariantMap> &ms) {
                robot_->setMarkers(ms);
                map_->view()->setTags(ms);
                log_->note(diag::Severity::Info,
                           QStringLiteral("마커 목록 변경 (%1개)").arg(ms.size()),
                           QJsonObject{{"channel", QStringLiteral("cmd/markers/set")}});
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
    connect(navigationSpeed_, &NavigationSpeedPanel::speedLimitsRequested,
            robot_, &robot::RobotLink::setNavigationSpeedLimits);
    connect(robot_, &robot::RobotLink::navigationSpeedLimitsChanged,
            navigationSpeed_, &NavigationSpeedPanel::setReportedLimits);
    connect(robot_, &robot::RobotLink::navigationSpeedLimitsChanged, teleop_,
            [this](double linear, double, double, double angular, double, double, bool) {
                teleop_->setSpeedLimits(linear, angular);
            });
    connect(robot_, &robot::RobotLink::navigationSpeedLimitsChanged, this,
            [this](double linear, double minimum, double maximum,
                   double angular, double angularMinimum, double angularMaximum, bool applied) {
                navigationSpeedLimits_ = std::array<double, 6>{linear, minimum, maximum,
                                                              angular, angularMinimum, angularMaximum};
                navigationSpeedApplied_ = applied;
                refreshGoalAvailability();
                if (settings_)
                    settings_->setNavigationSpeedState(linear, minimum, maximum, angular,
                                                       angularMinimum, angularMaximum, applied);
            });
    connect(robot_, &robot::RobotLink::commandResult,
            navigationSpeed_, &NavigationSpeedPanel::handleCommandResult);
    connect(robot_, &robot::RobotLink::connectionChanged, navigationSpeed_,
            [this](bool) {
                navigationSpeed_->reset();
                navigationSpeedLimits_.reset();
                navigationSpeedApplied_ = false;
                if (settings_)
                    settings_->resetNavigationSpeed();
                teleop_->setSpeedLimits(0.30, 0.50);
            });
    connect(teleop_, &TeleopPanel::cmdVel, robot_, &robot::RobotLink::setCmdVel);
    connect(teleop_, &TeleopPanel::basePosture, robot_,
            [this](const QString &posture, bool confirm) {
                robot_->setBasePosture(posture, confirm);
            });
    // 자세는 로봇이 알려 준 값만 표시한다. 버튼을 눌렀다고 화면을 먼저 바꾸면
    // 로봇이 거절했을 때 조작자는 앉은 줄 알고 다음 동작을 시킨다.
    connect(robot_, &robot::RobotLink::baseStateChanged, teleop_,
            [this](const QString &posture, const QString &) {
                teleop_->setBasePosture(posture);
            });

    connect(arm_, &ArmPanel::jointGoal, this, [this](const QList<double> &q) {
        robot_->setArmJointGoal(q);
        log_->note(diag::Severity::Info, QStringLiteral("관절 목표 전송"),
                   QJsonObject{{"channel", QStringLiteral("cmd/arm/joint_goal")}});
    });
    connect(arm_, &ArmPanel::eeGoal, this, [this](const QVariantMap &g) {
        robot_->setArmEeGoal(g);
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
    connect(arm_, &ArmPanel::savePosePresetRequested,
            robot_, &robot::RobotLink::saveArmPosePreset);
    connect(arm_, &ArmPanel::updatePosePresetRequested,
            robot_, &robot::RobotLink::updateArmPosePreset);
    connect(arm_, &ArmPanel::archivePosePresetRequested,
            robot_, &robot::RobotLink::archiveArmPosePreset);
    connect(robot_, &robot::RobotLink::commandResult, arm_,
            &ArmPanel::setCommandResult);

    connect(data_, &DataPanel::notice, this,
            [this](const QString &severity, const QString &message) {
                log_->note(severity == QLatin1String("ok")   ? diag::Severity::Ok
                           : severity == QLatin1String("warn") ? diag::Severity::Warn
                                                               : diag::Severity::Info,
                           message);
            });

    // 촬영은 로봇이 한다. 원본이 관제를 거치지 않는 것과 같은 이유이고,
    // 정지 상태 여부도 로봇이 판단한다 — 화면만 막으면 다른 경로로 들어온
    // 요청은 그대로 통과한다.
    connect(capture_, &CapturePanel::captureRequested, this, [this] {
        lastCaptureFile_.clear();
        pendingCaptureMetadata_ = capture_->currentMetadata().toJson().toVariantMap();
        pendingCaptureMetadata_[QStringLiteral("tag_id")] = snapshot_.visibleTagId;
        robot_->triggerCapture(pendingCaptureMetadata_);
        log_->note(diag::Severity::Info, QStringLiteral("촬영 요청"),
                   QJsonObject{{"channel", QStringLiteral("cmd/capture/trigger")}});
    });
    connect(robot_, &robot::RobotLink::commandResult, this,
            [this](const QString &channel, bool ok, const QString &code, const QString &message) {
                if (channel != QLatin1String(hmi::ch::kCmdCapture))
                    return;
                if (ok) {
                    capture_->captureStored();
                    lastCaptureFile_ = message;
                    capture_->setSavedFileName(message);
                    pendingCaptureMetadata_[QStringLiteral("file")] = message;
                    logAction(QStringLiteral("CAPTURE_OK"), pendingCaptureMetadata_);
                } else {
                    capture_->captureFailed(QStringLiteral("%1 %2").arg(code, message).trimmed());
                }
                pendingCaptureMetadata_.clear();
            });
}

void MainWindow::wireMissionSignals()
{
    connect(missionLibrary_, &MissionLibraryPanel::missionsRequested,
            robot_, &robot::RobotLink::requestMissions);
    connect(missionLibrary_, &MissionLibraryPanel::saveRequested,
            robot_, &robot::RobotLink::saveMission);
    connect(missionLibrary_, &MissionLibraryPanel::archiveRequested,
            robot_, &robot::RobotLink::archiveMission);
    connect(missionLibrary_, &MissionLibraryPanel::runRequested, this,
            [this](const QString &id) {
                const double departAt = Config::instance().batteryDeparturePercent();
                if (lastSoc_ < departAt) {
                    QMessageBox::warning(this, QStringLiteral("미션을 시작할 수 없습니다"),
                        QStringLiteral("배터리가 %1%% 입니다. 출발 최소 기준 %2%% 이상 충전하십시오.")
                            .arg(lastSoc_, 0, 'f', 0).arg(departAt, 0, 'f', 0));
                    return;
                }
                robot_->startMission(id);
            });
    connect(waypoints_, &WaypointPanel::addRequested, this, [this] {
        pendingPlacementKind_ = QStringLiteral("inspection");
        status_->goalButton()->setChecked(false);
        map_->poseEstimateButton()->setChecked(false);
        map_->view()->setMode(MapMode::AddWaypoint);
        map_->setPlacementHint(
            QStringLiteral("지도를 클릭해 위치를 정하고 드래그해 방향을 정하십시오"));
    });
    connect(waypoints_, &WaypointPanel::captureFromRobotRequested, this,
            [this] { captureLocation(QStringLiteral("inspection")); });
    connect(waypoints_, &WaypointPanel::updateRequested, this,
            [this](const QString &id, const QVariantMap &edited) {
        auto wps = waypoints_->waypoints();
        const auto it = std::find_if(wps.begin(), wps.end(), [&id](const QVariantMap &w) {
            return w.value(QStringLiteral("id")).toString() == id;
        });
        if (it == wps.end())
            return;
        QVariantMap value = edited;
        const auto base = value.take(QStringLiteral("_expected_point")).toMap();
        if (!base.isEmpty() && waypointDefinition(base) != waypointDefinition(*it)) {
            waypoints_->setSaveStatus(QStringLiteral("다른 곳에서 수정된 웨이포인트입니다. 취소 후 다시 편집하십시오."), true);
            return;
        }
        *it = value;
        if (submitWaypoints(wps))
            log_->note(diag::Severity::Info, QStringLiteral("웨이포인트 수정 요청 (%1)").arg(id),
                       QJsonObject::fromVariantMap(edited));
    });
    connect(waypoints_, &WaypointPanel::deleteRequested, this, [this](const QString &id) {
        auto wps = waypoints_->waypoints();
        const auto removed = std::remove_if(wps.begin(), wps.end(), [&id](const QVariantMap &w) {
            return w.value(QStringLiteral("id")).toString() == id;
        });
        if (removed == wps.end())
            return;
        wps.erase(removed, wps.end());
        if (submitWaypoints(wps))
            log_->note(diag::Severity::Info, QStringLiteral("점검포인트 삭제 요청 (%1)").arg(id),
                       QJsonObject{{"id", id}});
    });

    // 목록에서 고른 포인트를 지도에서도 짚어 준다. 목록과 지도를 눈으로
    // 대응시키지 못하면 좌표만 보고 어디인지 알아내야 한다.
    connect(waypoints_, &WaypointPanel::waypointSelected, this, [this](const QString &id) {
        map_->view()->setSelectedWaypoint(id);
        map_->view()->focusWaypoint(id);
    });
    connect(waypoints_, &WaypointPanel::gotoRequested, this, [this](const QString &id) {
        const auto points = waypoints_->waypoints();
        const auto it = std::find_if(points.cbegin(), points.cend(), [&id](const QVariantMap &point) {
            return point.value(QStringLiteral("id")).toString() == id;
        });
        if (it != points.cend())
            driveTo(*it, it->value(QStringLiteral("name"), id).toString());
    });
    connect(mission_, &MissionPanel::missionPause, this, [this] {
        robot_->missionPause();
        log_->log(QStringLiteral("MISSION_PAUSE"));
    });
    connect(mission_, &MissionPanel::missionResume, this, [this] {
        robot_->missionResume();
        log_->log(QStringLiteral("MISSION_RESUME"));
    });
    connect(waypoints_, &WaypointPanel::waypointsChanged, this,
            [this](const QList<QVariantMap> &points) {
                missionLibrary_->setWaypoints(points);
                refreshMissionProgress();
            });

    connect(mission_, &MissionPanel::missionStop, this, [this] {
        robot_->missionStop();
        log_->log(QStringLiteral("MISSION_STOP"));
    });
    connect(mission_, &MissionPanel::returnToDock, this, [this] {
        robot_->returnToDock();
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
    if (!robot_->isConnected() || !requestedMapId_.isEmpty() || goalStartPending_)
        return;
    const auto mission = robot_->missionState();
    if ((mission != MissionState::Idle && mission != MissionState::Completed && mission != MissionState::Failed) ||
        navigationStatus_ == QLatin1String("accepting") || navigationStatus_ == QLatin1String("navigating") ||
        navigationStatus_ == QLatin1String("paused") || navigationStatus_ == QLatin1String("pausing") ||
        navigationStatus_ == QLatin1String("canceling"))
        return;
    showView(NavItem::Drive);
    driveTabs_->setCurrentIndex(0);
    QVariantMap target = pose;
    target["name"] = label;
    setDraftGoal(target);
}

void MainWindow::onMissionStateChanged(MissionState state)
{
    QString displayState;
    switch (state) {
    case MissionState::Idle:             displayState = QStringLiteral("idle"); break;
    case MissionState::Ready:            displayState = QStringLiteral("ready"); break;
    case MissionState::Running:          displayState = QStringLiteral("running"); break;
    case MissionState::Pausing:          displayState = QStringLiteral("pausing"); break;
    case MissionState::Paused:           displayState = QStringLiteral("paused"); break;
    case MissionState::Recovering:       displayState = QStringLiteral("recovering"); break;
    case MissionState::Returning:        displayState = QStringLiteral("returning"); break;
    case MissionState::Completed:        displayState = QStringLiteral("completed"); break;
    case MissionState::Failed:           displayState = QStringLiteral("failed"); break;
    case MissionState::Fault:            displayState = QStringLiteral("fault"); break;
    case MissionState::EmergencyStopped: displayState = QStringLiteral("emergency_stopped"); break;
    }
    mission_->setMissionState(displayState);
    missionLibrary_->setMissionState(state);
    if (state != MissionState::Idle && state != MissionState::Completed && state != MissionState::Failed)
        clearDraftGoal();
    refreshGoalAvailability();
}

void MainWindow::refreshMissionProgress()
{
    QString name;
    QStringList labels;
    const auto found = std::find_if(missionDefinitions_.cbegin(), missionDefinitions_.cend(),
        [this](const QVariantMap &mission) {
            return mission.value(QStringLiteral("id")).toString() == activeMissionId_;
        });
    if (found != missionDefinitions_.cend()) {
        name = found->value(QStringLiteral("name")).toString();
        const auto points = waypoints_->waypoints();
        for (const auto &value : found->value(QStringLiteral("steps")).toList()) {
            const QVariantMap step = value.toMap();
            const QString type = step.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("navigate")) {
                const QString locationId = step.value(QStringLiteral("location_id")).toString();
                const auto point = std::find_if(points.cbegin(), points.cend(),
                    [&locationId](const QVariantMap &candidate) {
                        return candidate.value(QStringLiteral("id")).toString() == locationId;
                    });
                const QString location = point != points.cend()
                    ? point->value(QStringLiteral("name"), locationId).toString() : locationId;
                labels << QStringLiteral("이동 · %1").arg(location);
            } else if (type == QLatin1String("capture")) {
                labels << QStringLiteral("촬영 · %1").arg(step.value(QStringLiteral("preset")).toString());
            } else if (type == QLatin1String("arm_move")) {
                labels << QStringLiteral("팔 자세 · %1").arg(step.value(QStringLiteral("pose")).toString());
            } else if (type == QLatin1String("dock")) {
                labels << QStringLiteral("충전소 복귀");
            } else {
                labels << step.value(QStringLiteral("id"), QStringLiteral("알 수 없는 단계")).toString();
            }
        }
    } else if (!activeMissionId_.isEmpty()) {
        // An external client may start a legacy plan. Never present the
        // waypoint catalog order as that plan's execution order.
        name = QStringLiteral("실행 중인 경로");
    }
    mission_->setProgress(name, activeMissionIndex_, activeMissionTotal_, labels);
    missionNavigationLabel_ = name;
    if (activeMissionIndex_ >= 0 && activeMissionIndex_ < labels.size())
        missionNavigationLabel_ += QStringLiteral("\n%1").arg(labels.at(activeMissionIndex_));
    refreshGoalAvailability();
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
    const auto points = waypoints_->waypoints();
    for (int i = 0; i < points.size(); ++i) {
        if (points.at(i).value(QStringLiteral("id")).toString() == id) {
            found = points.at(i);
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
    lines << QStringLiteral("<b>%1</b>")
                 .arg(found.value(QStringLiteral("name"), id).toString().toHtmlEscaped());
    lines << QStringLiteral("위치  x %1 · y %2")
                 .arg(found.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                 .arg(found.value(QStringLiteral("y")).toDouble(), 0, 'f', 2);
    lines << QStringLiteral("도착 방향  %1°")
                 .arg(qRadiansToDegrees(found.value(QStringLiteral("theta")).toDouble()),
                      0, 'f', 1);
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
    if (item != NavItem::Drive) {
        // 지도는 탭 전환 뒤에도 계속 보인다. 편집 모드를 남기면 사용자가
        // 다른 화면을 보는 사이 지도 클릭이 숨은 편집 명령이 된다.
        pendingPlacementKind_.clear();
        status_->goalButton()->setChecked(false);
        map_->poseEstimateButton()->setChecked(false);
        map_->view()->setMode(MapMode::View);
        map_->setPlacementHint({});
        teleop_->cancelJog();
    }
    context_->setCurrentIndex(int(item));
    refreshGoalAvailability();
}

void MainWindow::showLocationAssets()
{
    showView(NavItem::Drive);
    if (driveTabs_)
        driveTabs_->setCurrentIndex(1);
}

void MainWindow::clearDraftGoal()
{
    draftGoal_.clear();
    draftGoalMapId_.clear();
    map_->view()->setDraftGoal({});
}

void MainWindow::setDraftGoal(const QVariantMap &goal)
{
    const auto *info = map_->view()->mapInfo();
    if (!info || goalStartPending_ || !goal.contains("x") || !goal.contains("y") ||
        !goal.contains("theta") || !std::isfinite(goal.value("x").toDouble()) ||
        !std::isfinite(goal.value("y").toDouble()) || !std::isfinite(goal.value("theta").toDouble()))
        return;
    draftGoal_ = goal;
    draftGoalMapId_ = info->mapId;
    navigationError_.clear();
    map_->view()->setDraftGoal(draftGoal_);
    refreshGoalAvailability();
}

void MainWindow::startDraftGoal()
{
    const auto *info = map_->view()->mapInfo();
    if (!canStartGoal() || !info || info->mapId != draftGoalMapId_)
        return;
    status_->goalButton()->setChecked(false);
    goalStartPending_ = true;
    navigationError_.clear();
    refreshGoalAvailability();
    const QVariantMap target = draftGoal_;
    robot_->requestGoal(target.value("x").toDouble(), target.value("y").toDouble(),
                        target.value("theta").toDouble());
    log_->note(diag::Severity::Info, QStringLiteral("주행 시작"),
        QJsonObject::fromVariantMap(target));
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

bool MainWindow::canPlaceGoal() const
{
    const bool navigating = navigationStatus_ == QLatin1String("accepting") ||
        navigationStatus_ == QLatin1String("navigating") || navigationStatus_ == QLatin1String("pausing") ||
        navigationStatus_ == QLatin1String("paused") || navigationStatus_ == QLatin1String("canceling");
    const auto mission = robot_->missionState();
    const bool missionBusy = mission != MissionState::Idle && mission != MissionState::Completed &&
                             mission != MissionState::Failed;
    const auto *shown = map_->view()->mapInfo();
    // SLAM 중에는 저장 지도 ID가 없지만 지도 자체는 유효하다. 반대로
    // 저장 지도 전환 중의 이전 프레임에는 목표를 찍으면 안 된다.
    const bool matchingMap = shown &&
        ((activeMapId_.isEmpty() || activeMapId_ == QLatin1String("live"))
             ? (shown->mapId.isEmpty() || shown->mapId == QLatin1String("live"))
             : shown->mapId == activeMapId_);
    return robot_->isConnected() && !estop_->isEngaged() &&
           driveModeConfirmed_ && !requestedDriveMode_ &&
           robot_->mode() == DriveMode::Auto && matchingMap &&
           requestedMapId_.isEmpty() && !goalStartPending_ && !navigating && !missionBusy;
}

bool MainWindow::canStartGoal() const
{
    return canPlaceGoal() && !draftGoal_.isEmpty() && navigationTelemetry_.navFresh &&
        (!navigationSpeedLimits_ || navigationSpeedApplied_) &&
        (navigationTelemetry_.navigationLifecycle.isEmpty() ||
         navigationTelemetry_.navigationLifecycle == QLatin1String("active"));
}

void MainWindow::refreshGoalAvailability()
{
    const auto mission = robot_->missionState();
    const bool missionBusy = mission != MissionState::Idle && mission != MissionState::Completed &&
                             mission != MissionState::Failed;
    const bool available = canPlaceGoal();
    if (!available)
        status_->goalButton()->setChecked(false);
    status_->goalButton()->setEnabled(available);
    QString reason;
    if (available)
        reason.clear();
    else if (!robot_->isConnected())
        reason = QStringLiteral("로봇 연결 후 사용할 수 있습니다");
    else if (estop_->isEngaged())
        reason = QStringLiteral("비상정지 해제 후 사용할 수 있습니다");
    else if (!driveModeConfirmed_)
        reason = QStringLiteral("로봇의 주행 모드를 확인하는 중입니다");
    else if (requestedDriveMode_)
        reason = QStringLiteral("주행 모드 전환을 기다리는 중입니다");
    else if (robot_->mode() != DriveMode::Auto)
        reason = QStringLiteral("자율 모드에서 사용할 수 있습니다");
    else if (!requestedMapId_.isEmpty())
        reason = QStringLiteral("지도 전환을 기다리는 중입니다");
    else if (missionBusy)
        reason.clear();
    else if (goalStartPending_)
        reason = QStringLiteral("로봇 응답 대기 중");
    else if (navigationStatus_ == QLatin1String("accepting") ||
             navigationStatus_ == QLatin1String("navigating") ||
             navigationStatus_ == QLatin1String("pausing") ||
             navigationStatus_ == QLatin1String("paused") ||
             navigationStatus_ == QLatin1String("canceling"))
        reason = QStringLiteral("현재 목표 주행을 취소한 뒤 새 목표를 지정하십시오");
    else
        reason = QStringLiteral("현재 지도를 불러오는 중입니다");
    const bool paused = navigationStatus_ == QLatin1String("paused");
    const bool moving = navigationStatus_ == QLatin1String("accepting") ||
                        navigationStatus_ == QLatin1String("navigating");
    const bool stopping = navigationStatus_ == QLatin1String("pausing") ||
                          navigationStatus_ == QLatin1String("canceling");
    if (paused || moving || stopping)
        reason.clear();
    const bool resume = !estop_->isEngaged() && driveModeConfirmed_ && !requestedDriveMode_ &&
                        robot_->mode() == DriveMode::Auto && requestedMapId_.isEmpty() && navigationTelemetry_.navFresh;
    QString missionLabel = robot::missionStateLabel(mission);
    if (activeMissionIndex_ >= 0)
        missionLabel += QStringLiteral(" · 단계 %1 / %2").arg(activeMissionIndex_ + 1).arg(activeMissionTotal_);
    if (!missionNavigationLabel_.isEmpty()) missionLabel += '\n' + missionNavigationLabel_;
    status_->setTelemetry(navigationTelemetry_, robot_->isConnected());
    status_->setGoalState(robot_->isConnected() ? navigationStatus_ : QStringLiteral("disconnected"),
        draftGoal_.isEmpty() ? navigationTelemetry_.navGoal : draftGoal_,
        !draftGoal_.isEmpty(), goalStartPending_, missionBusy, missionLabel,
        missionBusy ? QString{} : !navigationError_.isEmpty() ? navigationError_ :
        draftGoal_.isEmpty() ? navigationTelemetry_.navError : QString{});
    if (available && !draftGoal_.isEmpty()) {
        if (!navigationTelemetry_.navFresh) reason = QStringLiteral("주행 상태 수신 대기");
        else if (!navigationTelemetry_.navigationLifecycle.isEmpty() &&
                 navigationTelemetry_.navigationLifecycle != QLatin1String("active"))
            reason = QStringLiteral("내비게이션 준비 중");
        else if (navigationSpeedLimits_ && !navigationSpeedApplied_)
            reason = QStringLiteral("속도 제한 적용 대기 중");
    }
    status_->setActions(available, canStartGoal(),
        robot_->isConnected() && !missionBusy && (paused ? resume : moving),
        robot_->isConnected() && !missionBusy && (goalStartPending_ || paused || moving || stopping || !draftGoal_.isEmpty()),
        reason);
}

void MainWindow::showReportedDriveMode()
{
    teleop_->setVisible(driveModeConfirmed_ && robot_->mode() == DriveMode::Manual &&
                        !requestedDriveMode_ && !estop_->isEngaged());
    if (!teleop_->isVisible()) teleop_->cancelJog();
    const bool modeSelectable = robot_->isConnected() && !estop_->isEngaged() &&
                                !requestedDriveMode_;
    autoBtn_->setEnabled(modeSelectable);
    manualBtn_->setEnabled(modeSelectable);
    if (estop_->isEngaged()) {
        autoBtn_->setChecked(false);
        manualBtn_->setChecked(false);
        status_->setMode({}, true);
    } else if (requestedDriveMode_) {
        // 요청 직후 들어온 이전 모드의 상태 보고로 버튼을 되돌리지 않는다.
        status_->setMode(QStringLiteral("전환 중"), false);
    } else if (driveModeConfirmed_) {
        const bool isAuto = robot_->mode() == DriveMode::Auto;
        autoBtn_->setChecked(isAuto);
        manualBtn_->setChecked(!isAuto);
        status_->setMode(isAuto ? QStringLiteral("auto") : QStringLiteral("manual"), false);
    } else {
        autoBtn_->setChecked(false);
        manualBtn_->setChecked(false);
        status_->setMode({}, false);
    }
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
    };

    if (kind == QLatin1String("dock") || kind == QLatin1String("home")) {
        (kind == QLatin1String("dock") ? dock_ : home_) = loc;
        applyFixedLocations();
    } else {
        loc.remove(QStringLiteral("kind"));
        auto wps = waypoints_->waypoints();
        const int n = wps.size() + 1;
        loc[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        loc[QStringLiteral("name")] = QStringLiteral("점검 위치 %1").arg(n);
        if (!editWaypoint(this, loc, QStringLiteral("웨이포인트 추가")))
            return;
        wps << loc;
        if (!submitWaypoints(wps))
            return;
    }

    // 신뢰도가 낮은 채로 저장된 위치는 별도 코드로 남긴다.
    // 나중에 "이 포인트는 어떻게 잡았나"를 로그로 추적할 수 있어야 한다.
    log_->log(check.degraded ? QStringLiteral("SETUP_LOC_DEGRADED")
                             : QStringLiteral("SETUP_LOC_CAPTURED"),
              QJsonObject::fromVariantMap(loc));
}

void MainWindow::applyWaypointCatalog(const QList<QVariantMap> &points)
{
    if (!requestedMapId_.isEmpty())
        return;
    const bool saved = waypointWritePending_;
    if (saved) {
        if (!waypointWriteAccepted_ || points.size() != pendingWaypoints_.size())
            return;
        for (int i = 0; i < points.size(); ++i)
            if (waypointDefinition(points.at(i)) != waypointDefinition(pendingWaypoints_.at(i)))
                return;
        waypointWritePending_ = waypointWriteAccepted_ = false;
        pendingWaypoints_.clear();
    }
    waypoints_->setWaypoints(points);
    const auto *shown = map_->view()->mapInfo();
    const bool ready = robot_->isConnected() && shown && shown->mapId == activeMapId_;
    map_->view()->setWaypoints(ready ? points : QList<QVariantMap>{});
    waypoints_->setEditingEnabled(ready && activeMapId_ != QLatin1String("live"));
    if (saved)
        waypoints_->setSaveStatus(QStringLiteral("웨이포인트 저장됨"));
}

bool MainWindow::submitWaypoints(const QList<QVariantMap> &points)
{
    const auto *shown = map_->view()->mapInfo();
    if (activeMapId_.isEmpty() || activeMapId_ == QLatin1String("live") ||
        !requestedMapId_.isEmpty() || !shown || shown->mapId != activeMapId_) {
        QMessageBox::information(this, QStringLiteral("웨이포인트 저장"),
                                 QStringLiteral("지도를 불러온 뒤 웨이포인트를 등록하십시오."));
        return false;
    }
    if (waypointWritePending_ || !robot_->isConnected()) {
        QMessageBox::information(this, QStringLiteral("웨이포인트 저장"),
            waypointWritePending_ ? QStringLiteral("이전 저장 결과를 기다리는 중입니다.")
                                  : QStringLiteral("로봇에 연결한 뒤 저장하십시오."));
        return false;
    }
    waypointWritePending_ = true;
    waypointWriteAccepted_ = false;
    pendingWaypoints_ = points;
    waypoints_->setEditingEnabled(false);
    waypoints_->setSaveStatus(QStringLiteral("웨이포인트 저장 중…"));
    robot_->setWaypoints(points, waypoints_->waypoints(), activeMapId_);
    const auto generation = ++waypointWriteGeneration_;
    QTimer::singleShot(7000, this, [this, generation] {
        if (!waypointWritePending_ || waypointWriteGeneration_ != generation)
            return;
        waypointWritePending_ = false;
        waypointWriteAccepted_ = false;
        pendingWaypoints_.clear();
        applyWaypointCatalog(robot_->waypoints());
        const auto *shown = map_->view()->mapInfo();
        waypoints_->setEditingEnabled(robot_->isConnected() && shown && shown->mapId == activeMapId_ &&
                                      requestedMapId_.isEmpty() && activeMapId_ != QLatin1String("live"));
        waypoints_->setSaveStatus(QStringLiteral("저장 결과를 확인하지 못했습니다."), true);
    });
    return true;
}

// ================= 안전 =================

void MainWindow::engageEstop()
{
    robot_->engageEstop();
    // 빨간 래치와 테두리는 클릭 사실이 아니라 로봇이 state/safety로 확인한
    // 사실을 표시한다. 연결이 막 끊긴 순간에도 화면만 "발동"으로 바뀌면
    // 조작자는 정지됐다고 오해할 수 있다.
    // 지정해 둔 목표는 이 시점에 무효다. 지도에 남겨 두면 해제 후에도
    // 로봇이 그리로 갈 것처럼 읽힌다.
    map_->view()->clearGoal();
    status_->setMode({}, true);
    teleop_->setJogEnabled(false);
    arm_->setControlsEnabled(false);
    autoBtn_->setChecked(false);
    manualBtn_->setChecked(false);
    logAction(QStringLiteral("SAFETY_ESTOP_ENGAGED"));
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

    robot_->releaseEstop();
    // 해제도 safety_manager의 실제 상태가 내려온 뒤에만 화면에 반영한다.
    // 물리 E-Stop이 여전히 눌려 있으면 HMI 해제 요청으로 바뀌면 안 된다.
    logAction(QStringLiteral("SAFETY_ESTOP_RELEASED"));
}

/// 두 자리를 화면 곳곳에 반영한다. 로봇에게는 보내지 않는다.
///
/// 로봇이 알려 준 값을 받았을 때와 조작자가 새로 지정했을 때가 화면 입장에서
/// 같아야 한다. 예전에는 기동 경로가 패널만 직접 갱신하고 이 함수를 타지
/// 않아서, 로봇이 알려 준 충전소가 지도에는 영영 뜨지 않았다.
void MainWindow::showFixedLocations()
{
    locations_->setDock(dock_);
    locations_->setHome(home_);
    mission_->setDockKnown(!dock_.isEmpty());

    // 지도에도 올린다. 두 자리는 순회 목록 밖이라 웨이포인트 오버레이로는
    // 그려지지 않는데, 정작 조작자가 "충전소가 어느 쪽이더라" 를 묻는 곳은
    // 목록이 아니라 지도다.
    map_->view()->setDock(dock_);
    map_->view()->setHome(home_);
}

void MainWindow::applyFixedLocations()
{
    showFixedLocations();

    // 로봇에게도 보낸다. 화면에만 적어 두면 배터리 복귀와 점검 종료 복귀는
    // 로봇이 예전부터 알고 있던 자리로 간다 — 조작자는 방금 옮겨 놓은 줄
    // 알고 있고, 그 차이는 로봇이 엉뚱한 데로 갈 때에야 드러난다.
    QList<QVariantMap> fixed;
    if (!dock_.isEmpty())
        fixed << dock_;
    if (!home_.isEmpty())
        fixed << home_;
    if (!fixed.isEmpty())
        robot_->setLocations(fixed);
}

void MainWindow::setMode(const QString &mode)
{
    if (estop_->isEngaged()) {
        autoBtn_->setChecked(false);
        manualBtn_->setChecked(false);
        refreshGoalAvailability();
        return;
    }

    const bool isAuto = mode == QLatin1String("auto");
    if (!robot_->isConnected())
        return;
    const DriveMode desired = isAuto ? DriveMode::Auto : DriveMode::Manual;
    if (driveModeConfirmed_ && !requestedDriveMode_ && robot_->mode() == desired) {
        showReportedDriveMode();
        refreshGoalAvailability();
        return;
    }
    requestedDriveMode_ = desired;
    autoBtn_->setChecked(isAuto);
    manualBtn_->setChecked(!isAuto);
    showReportedDriveMode();
    refreshGoalAvailability();
    robot_->setMode(*requestedDriveMode_);

    // 조작 가능 여부는 연결에만 달렸다. 주행 모드로 잠그지 않는다 —
    // 자율 주행 중에도 조작자가 잡으면 그 동안은 수동이 앞서야 하고, 그
    // 중재는 로봇의 twist_mux 가 한다(수동 모드에서는 브릿지가 제자리
    // 명령을 계속 내보내 자율 출력이 나가지 못하게 잡아 둔다).
    const bool controlsAvailable = robot_->isConnected();
    teleop_->setJogEnabled(controlsAvailable);
    arm_->setControlsEnabled(controlsAvailable);

    // 미연결 상태의 "자동"은 초기 화면의 선택값일 뿐 로봇 모드 전환이
    // 아니다. 실제 로봇에 전달됐을 때만 안전 이력을 남긴다.
    if (isAuto) {
        log_->log(QStringLiteral("SAFETY_MODE_AUTO"));
    } else {
        // 수동 전환은 자율주행을 취소하지 않는다. 지시서 2.2.5 가 요구하는
        // 것은 수동이 우선한다는 것이지 자율을 버리라는 것이 아니고,
        // 취소해 버리면 잠깐 비켜 세우려던 조작자가 목표까지 잃는다.
        // 수동 모드인 동안 자율 출력은 로봇의 twist_mux 에서 막힌다.
        log_->log(QStringLiteral("SAFETY_MODE_MANUAL"));
        // 수동으로 바꿨다는 것은 지금 직접 몰겠다는 뜻이다. 조작계가 있는
        // 주행 화면으로 데려간다.
        nav_->setCurrent(NavItem::Drive);
        navigate(NavItem::Drive);
        driveTabs_->setCurrentIndex(0);
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

/// 연결 배지 아래에 로봇 목록을 펼친다.
/// 연결 점의 색을 바꾼다.
void MainWindow::setLinkTone(const QString &tone)
{
    linkDot_->setProperty("tone", tone);
    // 속성으로 고른 스타일은 다시 계산해 줘야 바뀐다.
    linkDot_->style()->unpolish(linkDot_);
    linkDot_->style()->polish(linkDot_);
}

/// 버튼에 지금 붙어 있는 로봇을 적는다.
///
/// 연결 후에는 로봇이 실제로 보낸 ID가 윗줄이다. HMI의 별칭은 운영자 메모일
/// 뿐 로봇 식별자가 아니다. 연결 전에는 아직 ID를 모르므로 별칭/주소를 쓴다.
void MainWindow::refreshRobotButton()
{
    auto &cfg = Config::instance();
    const auto list = cfg.robots();
    const int current = cfg.currentRobot();
    const QString alias = current < 0 || current >= list.size()
        ? QString() : list.at(current).name.trimmed();

    const QString address = current < 0 || current >= list.size()
        ? QString()
        : QStringLiteral("%1:%2").arg(list.at(current).host).arg(list.at(current).port);

    const bool identified = robot_->isConnected() && !saidId_.isEmpty();
    robotNameLabel_->setText(identified ? saidId_
        : robot_->isConnected() && current >= 0 ? QStringLiteral("로봇 ID 확인 중")
        : !alias.isEmpty() ? alias
        : !address.isEmpty() ? address : QStringLiteral("로봇 선택"));
    robotAddrLabel_->setText(identified && !alias.isEmpty()
        ? QStringLiteral("%1 · %2").arg(alias, address)
        : identified || !alias.isEmpty() ? address : QString());

    QStringList tip;
    if (!alias.isEmpty())
        tip << QStringLiteral("별칭  %1").arg(alias);
    if (!saidName_.isEmpty())
        tip << QStringLiteral("로봇이 말한 이름  %1").arg(saidName_);
    if (identified)
        tip << QStringLiteral("로봇 ID  %1").arg(saidId_);
    if (!address.isEmpty())
        tip << QStringLiteral("주소  %1").arg(address);
    tip << QStringLiteral("눌러서 연결할 로봇을 고릅니다");
    robotButton_->setToolTip(tip.join(QLatin1Char('\n')));

    // 버튼 크기를 안의 배치가 정하게 한다. QPushButton 은 자기 글자를 기준으로
    // 크기를 답하는데 이 버튼에는 글자가 없어서, 그대로 두면 자식 라벨이
    // 1 px 로 눌린다 — 점만 보이고 이름과 주소가 사라진다.
    if (auto *inner = robotButton_->layout()) {
        inner->activate();
        robotButton_->setMinimumSize(inner->minimumSize());
    }
}

void MainWindow::showRobotPicker()
{
    auto &cfg = Config::instance();
    const auto list = cfg.robots();
    const int current = cfg.currentRobot();

    // exec() 가 아니라 popup() 이다. exec() 는 자기 이벤트 루프를 돌려서,
    // 메뉴가 떠 있는 동안 로봇에서 오는 자세·안전 상태가 화면에 반영되지
    // 않는다. 목록을 열어 둔 채로 값이 멎으면 그것을 통신 끊김으로 읽는다.
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (int i = 0; i < list.size(); ++i) {
        const auto &e = list.at(i);
        // 현재 연결된 로봇의 확인된 ID만 보여준다. 다른 프로필에는 별칭과
        // 주소만 있으므로, 아직 읽지 않은 ID를 추측하거나 기억하지 않는다.
        const QString verifiedId = i == current && robot_->isConnected() ? saidId_ : QString();
        const QString alias = e.name.trimmed();
        const QString address = QStringLiteral("%1:%2").arg(e.host).arg(e.port);
        auto *action = menu->addAction(
            !verifiedId.isEmpty()
                ? (alias.isEmpty() ? QStringLiteral("%1      %2").arg(verifiedId, address)
                                   : QStringLiteral("%1 · %2      %3").arg(verifiedId, alias, address))
                : (alias.isEmpty() ? address
                                   : QStringLiteral("%1      %2").arg(alias, address)));
        // 현재 제어 중인 로봇은 실제 제어 연결을, 나머지는 별도 상태 확인
        // 포트의 응답을 표시한다. 목록을 여는 행위 자체는 제어권을 얻지 않는다.
        const bool reachable = (i == current && robot_->isConnected())
            || robotPresence_.value(robotProfileKey(e.host, e.port), false);
        action->setIcon(presenceIcon(reachable));
        action->setToolTip(reachable ? QStringLiteral("브릿지 응답 가능")
                                     : QStringLiteral("브릿지 응답 없음"));
        // 고른 것에 표시를 남긴다. 목록만 보여 주면 지금 어디에 붙어 있는지
        // 배지를 다시 읽어야 한다.
        action->setCheckable(true);
        action->setChecked(i == current);
        connect(action, &QAction::triggered, this, [this, i] { selectRobot(i); });
    }

    menu->addSeparator();
    auto *manage = menu->addAction(QStringLiteral("연결 설정…"));
    connect(manage, &QAction::triggered, this, &MainWindow::openConnectionSettings);

    menu->popup(robotButton_->mapToGlobal(QPoint(0, robotButton_->height() + 4)));
}

void MainWindow::showMapPicker()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    if (activeMapId_ == QLatin1String("live")) {
        auto *live = menu->addAction(QStringLiteral("실시간 지도 · 웨이포인트 저장 불가"));
        live->setEnabled(false);
        menu->addSeparator();
    }
    if (maps_.isEmpty()) {
        menu->addAction(QStringLiteral("저장된 지도 없음"))->setEnabled(false);
    }
    for (const auto &map : maps_) {
        const QString id = map.value(QStringLiteral("id")).toString();
        const QString name = map.value(QStringLiteral("name")).toString();
        const int points = map.value(QStringLiteral("waypoint_count")).toInt();
        const bool isDefault = map.value(QStringLiteral("default")).toBool();
        const QString label = name.isEmpty() ? id : name;
        auto *row = new QWidget(menu);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(metrics::s1, 1, metrics::s1, 1);
        layout->setSpacing(metrics::s1);
        auto *select = new QPushButton(row);
        select->setObjectName(QStringLiteral("MapSelect_%1").arg(id));
        select->setProperty("variant", "ghost");
        select->setProperty("size", "sm");
        select->setText(QStringLiteral("%1%2  ·  %3개")
                            .arg(id == activeMapId_ ? QStringLiteral("✓  ") : QString())
                            .arg(select->fontMetrics().elidedText(label, Qt::ElideRight, 165))
                            .arg(points));
        select->setToolTip(QStringLiteral("%1 (%2) · 웨이포인트 %3개")
                               .arg(label, id).arg(points));
        select->setMinimumWidth(205);
        layout->addWidget(select, 1);

        auto *setDefault = new QPushButton(isDefault ? QStringLiteral("기본 ✓")
                                                 : QStringLiteral("기본 지정"), row);
        setDefault->setObjectName(QStringLiteral("MapDefault_%1").arg(id));
        setDefault->setProperty("variant", "ghost");
        setDefault->setProperty("size", "sm");
        setDefault->setFixedSize(62, 28);
        setDefault->setToolTip(isDefault ? QStringLiteral("기본 지도 해제")
                                      : QStringLiteral("다음 실행부터 기본 지도로 사용"));
        layout->addWidget(setDefault);

        auto *rename = new IconButton(IconButton::Glyph::Edit, row);
        rename->setObjectName(QStringLiteral("MapRename_%1").arg(id));
        rename->setFixedSize(28, 28);
        rename->setAccessibleName(QStringLiteral("%1 이름 변경").arg(label));
        rename->setToolTip(QStringLiteral("%1 이름 변경").arg(label));
        layout->addWidget(rename);

        auto *remove = new IconButton(IconButton::Glyph::Trash, row);
        remove->setObjectName(QStringLiteral("MapDelete_%1").arg(id));
        remove->setFixedSize(28, 28);
        remove->setToolTip(isDefault ? QStringLiteral("기본 지도를 해제한 뒤 삭제할 수 있습니다")
                                     : QStringLiteral("%1 삭제").arg(label));
        remove->setEnabled(id != activeMapId_ && !isDefault);
        layout->addWidget(remove);

        auto *action = new QWidgetAction(menu);
        action->setDefaultWidget(row);
        menu->addAction(action);

        connect(select, &QPushButton::clicked, this, [this, menu, id] {
            menu->close();
            if (id == activeMapId_)
                return;
            if (waypointWritePending_) {
                QMessageBox::information(this, QStringLiteral("지도 전환"),
                                         QStringLiteral("웨이포인트 저장이 끝난 뒤 지도를 바꾸십시오."));
                return;
            }
            if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_)) {
                clearDraftGoal();
                requestedMapId_ = id;
                map_->setMapListEnabled(false);
                status_->goalButton()->setChecked(false);
                status_->goalButton()->setEnabled(false);
                map_->poseEstimateButton()->setChecked(false);
                map_->poseEstimateButton()->setEnabled(false);
                waypoints_->setEditingEnabled(false);
                waypoints_->setWaypoints({});
                map_->view()->setWaypoints({});
                dock_.clear();
                home_.clear();
                showFixedLocations();
                locations_->setMarkers({});
                map_->view()->setTags({});
                map_->setMapLabel(QStringLiteral("지도 전환 중"), QString());
                bridge->selectMap(id);
            }
        });
        connect(rename, &QPushButton::clicked, this, [this, menu, id, label] {
            menu->close();
            bool accepted = false;
            const QString name = QInputDialog::getText(this, QStringLiteral("지도 이름 변경"),
                                                       QStringLiteral("새 지도 폴더 이름"),
                                                       QLineEdit::Normal, label,
                                                       &accepted).trimmed();
            if (!accepted || name.isEmpty() || name == label)
                return;
            if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                bridge->renameMap(id, name);
        });
        connect(setDefault, &QPushButton::clicked, this, [this, menu, id, isDefault] {
            menu->close();
            if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                bridge->setDefaultMap(isDefault ? QString() : id);
        });
        connect(remove, &QPushButton::clicked, this, [this, menu, id, label] {
            menu->close();
            if (QMessageBox::question(this, QStringLiteral("지도 삭제"),
                    QStringLiteral("‘%1’ 지도와 안의 웨이포인트·미션을 로봇의 보관 폴더로 옮길까요?\n"
                                   "현재 사용하는 지도와 시작 지도는 삭제할 수 없습니다.")
                        .arg(label), QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No) != QMessageBox::Yes)
                return;
            if (auto *bridge = qobject_cast<net::BridgeClient *>(robot_))
                bridge->deleteMap(id);
        });
    }
    menu->popup(map_->mapButton()->mapToGlobal(QPoint(0, map_->mapButton()->height() + 4)));
}

void MainWindow::selectRobot(int index)
{
    auto &cfg = Config::instance();
    const auto list = cfg.robots();
    if (index < 0 || index >= list.size())
        return;

    // 자동 선택은 기동 시 마지막으로 고른 한 대에만 적용한다. 여기부터는
    // 조작자가 명시적으로 고른 대상이며 BridgeClient가 재연결을 관리한다.
    autoConnectOnPresence_ = false;

    cfg.setCurrentRobot(index);
    const auto &e = list.at(index);

    if (auto *bridge = qobject_cast<hmi::net::BridgeClient *>(robot_)) {
        bridge->setEndpoint(e.host, quint16(e.port));
        bridge->connectToBridge();
    }

    saidId_.clear();
    saidName_.clear();
    refreshRobotButton();
    headerBattery_->setUnavailable();
    logAction(QStringLiteral("ROBOT_SELECTED"),
              {{"name", e.name}, {"host", e.host}, {"port", e.port}});
}

QString MainWindow::robotProfileKey(const QString &host, int controlPort)
{
    // IPv6 주소에도 ':'가 들어가므로 사람이 읽는 host:port 대신 구분자를
    // 명시한다. 이 키는 화면 수명 동안만 쓰며 설정 파일 형식은 바꾸지 않는다.
    return host.trimmed().toLower() + QChar(0x1f) + QString::number(controlPort);
}

void MainWindow::startRobotPresencePolling()
{
    if (robotPresenceTimer_)
        return;
    robotPresenceTimer_ = new QTimer(this);
    robotPresenceTimer_->setInterval(kPresenceProbeIntervalMs);
    connect(robotPresenceTimer_, &QTimer::timeout, this, &MainWindow::pollRobotPresence);
    pollRobotPresence();
    robotPresenceTimer_->start();
}

void MainWindow::pollRobotPresence()
{
    const auto list = Config::instance().robots();
    QHash<QString, bool> currentProfiles;
    for (const auto &entry : list) {
        const QString key = robotProfileKey(entry.host, entry.port);
        currentProfiles.insert(key, robotPresence_.value(key, false));
        probeRobotPresence(entry.host, entry.port);
    }
    robotPresence_ = std::move(currentProfiles);
    refreshRobotButton();
}

void MainWindow::probeRobotPresence(const QString &host, int controlPort)
{
    const QString key = robotProfileKey(host, controlPort);
    if (host.trimmed().isEmpty() || controlPort <= 0 || controlPort >= 65535
        || activePresenceProbes_.contains(key))
        return;

    auto *socket = new QTcpSocket(this);
    activePresenceProbes_.insert(key, socket);

    const auto finish = [this, key, socket](bool reachable) {
        if (activePresenceProbes_.value(key) != socket)
            return;
        activePresenceProbes_.remove(key);
        socket->abort();
        socket->deleteLater();

        const auto profiles = Config::instance().robots();
        const auto found = std::find_if(profiles.cbegin(), profiles.cend(),
            [&key](const hmi::RobotEntry &entry) {
                return robotProfileKey(entry.host, entry.port) == key;
            });
        if (found == profiles.cend())
            return;  // 설정에서 지운 대상의 늦은 응답이다.

        setRobotPresence(found->host, found->port, reachable);
    };

    connect(socket, &QTcpSocket::connected, this, [socket] {
        socket->write(kPresenceReply);
    });
    connect(socket, &QTcpSocket::readyRead, this, [socket, finish] {
        finish(socket->readAll().startsWith(kPresenceReply));
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [finish](QAbstractSocket::SocketError) { finish(false); });
    QTimer::singleShot(kPresenceProbeTimeoutMs, socket, [finish] { finish(false); });
    socket->connectToHost(host, quint16(controlPort));
}

void MainWindow::setRobotPresence(const QString &host, int controlPort, bool reachable)
{
    const QString key = robotProfileKey(host, controlPort);
    const bool changed = robotPresence_.value(key, false) != reachable;
    robotPresence_.insert(key, reachable);
    if (changed)
        refreshRobotButton();

    // 마지막으로 선택했던 로봇만 자동으로 제어 연결을 연다. 목록의 다른
    // 초록 점은 "운용 가능" 표시일 뿐, 관제권을 임의로 옮기지 않는다.
    const auto &cfg = Config::instance();
    const int current = cfg.currentRobot();
    const auto profiles = cfg.robots();
    if (!reachable || !autoConnectOnPresence_ || current < 0 || current >= profiles.size()
        || robotProfileKey(profiles.at(current).host, profiles.at(current).port) != key)
        return;

    autoConnectOnPresence_ = false;
    QTimer::singleShot(0, this, [this, current] { selectRobot(current); });
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
    // 최초 실행은 아직 어떤 로봇에도 명령을 내리지 않은 상태다. 이력에는
    // 실제로 전송된 일만 남기고, 보관만 한 정책은 연결 직후 BridgeClient가
    // 보낸다.
    if (robot_->isConnected()) {
        log_->note(diag::Severity::Info,
                   QStringLiteral("배터리 기준 전송 — 복귀 %1%, 출발 %2%")
                       .arg(cfg.batteryReturnPercent(), 0, 'f', 0)
                       .arg(cfg.batteryDeparturePercent(), 0, 'f', 0));
    }
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
        map_->setMapLabel(QStringLiteral("지도 없음"), QString());
    }

    const auto wps = robot_->waypoints();
    waypoints_->setWaypoints(wps);
    map_->view()->setWaypoints(wps);
    locations_->setMarkers(robot_->markers());
    map_->view()->setTags(robot_->markers());

    // 로봇이 알려 준 값이 출발점이다. 여기서 다시 보내지는 않는다 —
    // 방금 받은 것을 그대로 돌려주는 셈이라 의미가 없다.
    dock_ = robot_->dockPose();
    home_ = robot_->homePose();
    showFixedLocations();

    // 이력은 저장 장치의 공유 폴더를 직접 읽는다. 로봇을 거치지 않는다.
    data_->setDirectory(Config::instance().nasMountPath());

    setLinkTone(robot_->isConnected() ? QStringLiteral("ok")
                                      : QStringLiteral("danger"));
    saidId_.clear();
    saidName_.clear();
    refreshRobotButton();

    // 등록된 로봇은 제어 포트가 아닌 상태 확인 포트만 주기적으로 확인한다.
    // 마지막으로 선택했던 한 대가 응답하면 그때만 실제 제어 연결을 연다.
    startRobotPresencePolling();

    showReportedDriveMode();
    nav_->setCurrent(NavItem::Drive);
    navigate(NavItem::Drive);

    if (initial) {
        log_->note(diag::Severity::Ok, QStringLiteral("지도 불러오기 완료"),
                   QJsonObject{{"map_id", mapData_.info.mapId}});
    }

    // 텔레메트리 갱신은 브릿지 링크가 정한다. HMI가 별도 상태 타이머로
    // 로봇을 흉내 내지 않는다.
    robot_->start();
}

void MainWindow::onTelemetry(const Telemetry &tm)
{
    navigationTelemetry_ = tm;
    navigationStatus_ = tm.navStatus;
    if (tm.navFresh && (tm.navStatus == QLatin1String("accepting") ||
                       tm.navStatus == QLatin1String("navigating"))) {
        goalStartPending_ = false;
        clearDraftGoal();
    }
    if (!draftGoal_.isEmpty()) map_->view()->setDraftGoal(draftGoal_);
    const bool estopChanged = estop_->isEngaged() != tm.estop;
    if (estopChanged) {
        estop_->setEngaged(tm.estop);
        alert_->setActive(tm.estop);
        if (tm.estop) {
            requestedDriveMode_.reset();
            clearDraftGoal();
            map_->view()->clearGoal();
            status_->setMode({}, true);
            teleop_->setJogEnabled(false);
            arm_->setControlsEnabled(false);
            autoBtn_->setChecked(false);
            manualBtn_->setChecked(false);
        } else if (robot_->isConnected()) {
            // 해제는 주행 재개가 아니다. 조작계만 다시 열고 실제 이동은 다음
            // 명시적 수동 입력 또는 미션 재개가 safety_manager에 요청한다.
            teleop_->setJogEnabled(true);
            arm_->setControlsEnabled(true);
        }
        autoBtn_->setEnabled(robot_->isConnected() && !tm.estop);
        manualBtn_->setEnabled(robot_->isConnected() && !tm.estop);
        showReportedDriveMode();
        refreshGoalAvailability();
    }

    refreshGoalAvailability();
    auto *view = map_->view();

    view->setRobotPose(tm.x, tm.y, tm.theta, !tm.poseFresh);
    view->setTrail(requestedMapId_.isEmpty() ? tm.trail : QList<QPointF>{});
    view->setPlan(requestedMapId_.isEmpty() ? tm.plan : QList<QPointF>{});
    if (requestedMapId_.isEmpty() && robot_->isConnected() &&
        tm.navGoal.contains(QStringLiteral("x")) &&
        tm.navGoal.contains(QStringLiteral("y"))) {
        view->setGoal(tm.navGoal.value(QStringLiteral("x")).toDouble(),
                      tm.navGoal.value(QStringLiteral("y")).toDouble(),
                      tm.navGoal.value(QStringLiteral("theta")).toDouble());
    } else if (requestedMapId_.isEmpty() && robot_->isConnected() && !tm.plan.isEmpty() &&
               (robot_->missionState() == MissionState::Running ||
                robot_->missionState() == MissionState::Recovering ||
                robot_->missionState() == MissionState::Returning ||
                robot_->missionState() == MissionState::Paused)) {
        const auto end = tm.plan.last();
        const auto before = tm.plan.size() > 1 ? tm.plan.at(tm.plan.size() - 2) : end;
        view->setGoal(end.x(), end.y(),
                      std::atan2(end.y() - before.y(), end.x() - before.x()));
    } else {
        view->clearGoal();
    }
    view->setTagsSeen(tm.seenTags);

    for (const auto &w : robot_->waypoints()) {
        const QString id = w.value(QStringLiteral("id")).toString();
        const QString st = w.value(QStringLiteral("status")).toString();
        view->setWaypointStatus(id, st);
        waypoints_->setStatus(id, st);
    }

    // 배터리와 좌표는 내비게이션 레일이, 시스템 지표는 진단 화면이 맡는다.
    // 여기서 또 그리면 한 화면에 같은 숫자가 두 번 뜬다.

    arm_->setFeedbackFresh(tm.armFresh);
    if (tm.armFresh)
        arm_->setArmState(tm.joints, tm.manipulability, tm.sigmaMin, tm.armState);
    arm_->setExecutionAvailable(tm.armExecutionEnabled);

    lastSoc_ = tm.soc;
    // BridgeClient 는 연결이 없을 때도 화면의 신선도·타임아웃을 갱신하려고
    // 기본 Telemetry(초깃값 soc=0)를 내보낸다. 그것은 배터리 측정이 아니므로
    // 상단에 방전으로 그리면 안 된다.
    if (robot_->isConnected())
        headerBattery_->setState(tm.soc);
    else
        headerBattery_->setUnavailable();
    // 진단 배지는 과거 이벤트 수가 아니라 현재 고장 난 장치 수다. 누적
    // 로그를 넣으면 고친 뒤에도 "2" 같은 숫자가 영구히 남는다.
    int diagnosticAlerts = 0;
    for (const auto &sensor : tm.sensors) {
        if (sensor.state == QLatin1String("degraded")
            || sensor.state == QLatin1String("lost")
            || sensor.state == QLatin1String("fault"))
            ++diagnosticAlerts;
    }
    nav_->setDiagnosticsAlerts(diagnosticAlerts);

    // 웨이포인트는 최신 위치를 사용하며, 고정 위치 등록은 속력도 확인한다.
    snapshot_.x = tm.x;
    snapshot_.y = tm.y;
    snapshot_.theta = tm.theta;
    snapshot_.speed = tm.speed;
    snapshot_.poseFresh = tm.poseFresh;
    snapshot_.localizationOk = tm.localizationOk;
    snapshot_.visibleTagId = tm.seenTags.isEmpty() ? -1 : *tm.seenTags.cbegin();
    locations_->setSnapshot(snapshot_);
    const auto capture = LocationPanel::checkCapture(snapshot_, QStringLiteral("inspection"));
    waypoints_->setRobotPoseAvailable(capture.allowed, capture.reason);

    // 촬영은 정지 상태에서만 허용한다 (지시서 2.2.4 동적 촬영 불가).
    const bool stationary = tm.speed < 0.05;
    capture_->setContext(tm.x, tm.y, tm.theta,
                         tm.seenTags.isEmpty() ? -1 : *tm.seenTags.cbegin());
    capture_->setCaptureAllowed(tm.captureEnabled && tm.nasOnline &&
                                stationary && !tm.estop && tm.poseFresh,
                                !tm.captureEnabled
                                    ? QStringLiteral("이 로봇은 촬영 기능이 비활성입니다.")
                                : !tm.nasOnline
                                    ? QStringLiteral("촬영 저장소가 연결되지 않았습니다.")
                                : tm.estop ? QStringLiteral("비상정지가 걸려 있습니다.")
                                : !tm.poseFresh
                                    ? QStringLiteral("위치 정보가 오래되었습니다.")
                                    : QStringLiteral("주행 중 · 정지 후 촬영 가능"));

    diagnostics_->setSystem(tm.cpu, tm.gpu, tm.mem, tm.cpuTemp, tm.gpuTemp);
    diagnostics_->setSensors(tm.sensors);
    diagnostics_->setLink(tm.link);
    diagnostics_->setStorage(tm.nasOnline, tm.pendingUploads, tm.spoolFreeMb);
}

}  // namespace hmi::ui
