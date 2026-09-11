#include "views/SettingsDialog.h"

#include <QButtonGroup>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QListWidget>
#include <QSpinBox>
#include <QNetworkInterface>
#include <QTabBar>
#include <QTcpSocket>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtMath>

#include "BuildInfo.h"
#include "Config.h"
#include "RobotDef.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;
using hmi::Config;

namespace {

/// 읽기 전용 값 한 줄. 로봇측이 강제하는 항목을 보여줄 때 쓴다.
QWidget *readOnlyRow(const QString &label, const QString &value, const QString &note)
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, metrics::s2);
    lay->setSpacing(2);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(label));
    top->addStretch(1);
    top->addWidget(readout(value));
    lay->addLayout(top);

    auto *hint = new QLabel(note);
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);
    return host;
}

/// 경로 한 줄. 입력칸 옆에 찾아보기를 붙인다.
///
/// 검수고에서 NAS 경로를 손으로 받아 적는 일이 흔한데, 한 글자만 틀려도
/// 증상은 "사진이 안 보인다" 뿐이라 원인을 짚기 어렵다.
QWidget *pathRow(const QString &label, QLineEdit *edit, QPushButton **browseOut)
{
    auto *host = new QWidget;
    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s2);

    auto *lbl = sectionLabel(label);
    lbl->setFixedWidth(84);
    lay->addWidget(lbl);
    lay->addWidget(edit, 1);

    auto *browse = new QPushButton(QStringLiteral("찾아보기"));
    browse->setProperty("size", "sm");
    lay->addWidget(browse);
    *browseOut = browse;
    return host;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("설정"));
    resize(520, 560);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s4, metrics::s4, metrics::s4, metrics::s4);
    lay->setSpacing(metrics::s3);

    tabs_ = new QTabWidget;
    auto *tabs = tabs_;
    // 탭이 폭에 맞춰 늘어나면 항목 수가 바뀔 때마다 위치가 흔들린다.
    tabs->tabBar()->setExpanding(false);
    tabs->addTab(buildAppearanceTab(), QStringLiteral("표시"));
    tabs->addTab(buildConnectionTab(), QStringLiteral("연결"));
    tabs->addTab(buildOperationTab(), QStringLiteral("조작"));
    tabs->addTab(buildPowerTab(), QStringLiteral("전원"));
    tabs->addTab(buildStorageTab(), QStringLiteral("저장"));
    tabs->addTab(buildSafetyTab(), QStringLiteral("안전"));
    tabs->addTab(buildAboutTab(), QStringLiteral("정보"));
    lay->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(QStringLiteral("기본값으로"));
    auto *close = new QPushButton(QStringLiteral("닫기"));
    close->setProperty("variant", "primary");
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(reset, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("기본값으로 되돌리기"),
            QStringLiteral("모든 설정을 기본값으로 되돌립니다."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        Config::instance().resetToDefaults();
        load();
    });

    load();
}

QWidget *SettingsDialog::buildAboutTab()
{
    // 이 탭이 있는 이유는 셋이다.
    //
    //  1. 과업지시서 4장이 "실제 사용된 각 컴포넌트의 정확한 버전(커밋 해시
    //     포함)" 을 요구하고 7.3 절 임치 대상에도 같은 항목이 있다. 검수
    //     자리에서 화면만 열면 확인되는 편이, 설계서를 뒤지는 것보다 낫다.
    //  2. Qt 를 LGPL 로 동적 링크해 쓴다. 고지와 소스 입수 경로를 제품 안에
    //     두는 것이 그 의무를 가장 확실하게 지키는 방법이다.
    //  3. 과업지시서 7.2 절이 "ROS2 오픈소스 관련 IP 는 각 원저작권자에
    //     귀속" 이라고 못박고 있다. 귀속을 적어 둘 자리가 필요하다.
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s4, metrics::s4, metrics::s4, metrics::s4);
    lay->setSpacing(metrics::s2);

    using namespace hmi::build;

    lay->addWidget(sectionLabel(QStringLiteral("제품")));
    lay->addWidget(readOnlyRow(QStringLiteral("이름"), productName(), QString()));
    lay->addWidget(readOnlyRow(QStringLiteral("버전"), version(), QString()));

    // 커밋되지 않은 변경이 섞인 빌드는 임치본과 산출물이 달라진다. 그
    // 사실이 화면에 남아야 검수 자리에서 바로 걸린다.
    const QString hash = gitHash() + gitDirty();
    lay->addWidget(readOnlyRow(
        QStringLiteral("빌드"), hash,
        isDirty() ? QStringLiteral("커밋되지 않은 변경이 섞인 빌드입니다. "
                                   "납품본으로 쓰지 마십시오.")
                  : QStringLiteral("소스 저장소의 커밋 해시입니다.")));
    lay->addWidget(readOnlyRow(QStringLiteral("빌드 일시"),
                               buildDate() + QStringLiteral(" UTC"), QString()));
    lay->addWidget(readOnlyRow(QStringLiteral("통신 규약"),
                               QStringLiteral("v") + protocolVersion(),
                               QStringLiteral("로봇과 맞아야 하는 값입니다. "
                                              "다르면 연결 시 거부됩니다.")));

    lay->addWidget(new HLine);
    lay->addWidget(sectionLabel(QStringLiteral("공급")));
    lay->addWidget(readOnlyRow(QStringLiteral("개발"), vendor(), QString()));
    lay->addWidget(readOnlyRow(QStringLiteral("발주"), client(), QString()));

    lay->addWidget(new HLine);
    lay->addWidget(sectionLabel(QStringLiteral("오픈소스 고지")));

    auto *notice = new QLabel(QStringLiteral(
        "이 제품은 아래 오픈소스 구성요소를 사용합니다. 각 구성요소의 "
        "저작권은 원저작권자에게 있습니다.\n\n"
        "  • Qt 6 — LGPL-3.0 (동적 링크)\n"
        "  • ROS 2, Nav2, CycloneDDS — Apache-2.0\n"
        "  • SLAM Toolbox — LGPL-2.1 (별도 프로세스)\n"
        "  • KISS-ICP — MIT\n"
        "  • ground_segmentation 외 — BSD-3-Clause\n\n"
        "LGPL 구성요소의 소스는 납품 매체의 licenses/ 아래에 함께 제공됩니다. "
        "전체 설치 패키지 목록과 판본은 시스템 설계서의 의존성 목록을 "
        "보십시오."));
    notice->setObjectName(QStringLiteral("Hint"));
    notice->setWordWrap(true);
    notice->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(notice);

    lay->addStretch(1);

    // 고지문이 길어 대화상자 높이를 넘는다. 잘린 채 두면 고지를 안 한
    // 것과 같으므로 스크롤에 넣는다.
    auto *scroll = new QScrollArea;
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    lay->addWidget(sectionLabel(QStringLiteral("글자 크기")));

    auto *row = new QHBoxLayout;
    scale_ = new QSlider(Qt::Horizontal);
    // 80 % ~ 160 %. 이보다 작으면 한글 가독성이 무너지고, 크면 패널이 잘린다.
    scale_->setRange(80, 160);
    scale_->setSingleStep(5);
    scale_->setPageStep(10);
    scaleValue_ = readout(QStringLiteral("100%"));
    row->addWidget(scale_, 1);
    row->addWidget(scaleValue_);
    lay->addLayout(row);

    auto *hint = new QLabel(QStringLiteral(
        "화면을 멀리 두고 보거나 글자가 작게 느껴지면 키우십시오."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // 값만 쓴다. 화면에 반영하는 것은 창을 띄운 쪽의 일이다 — 여기서 같이
    // 하면 순서에 따라 결과가 달라지고, 실제로 그것 때문에 테마 전환이
    // 한 번 조용히 죽었다.
    connect(scale_, &QSlider::valueChanged, this, [this](int v) {
        scaleValue_->setText(QStringLiteral("%1%").arg(v));
        Config::instance().setUiScale(v / 100.0);
    });

    lay->addSpacing(metrics::s3);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);
    lay->addWidget(sectionLabel(QStringLiteral("테마")));

    auto *themeRow = new QHBoxLayout;
    lightBtn_ = new QPushButton(QStringLiteral("라이트"));
    darkBtn_ = new QPushButton(QStringLiteral("다크"));
    // 버튼 그룹으로 묶어 한쪽을 누르면 다른 쪽이 풀리게 한다. 손으로
    // 맞추면 "기본값으로" 같은 다른 경로에서 둘 다 눌린 채로 남는다.
    auto *themeGroup = new QButtonGroup(this);
    themeGroup->setExclusive(true);
    for (auto *b : {lightBtn_, darkBtn_}) {
        b->setCheckable(true);
        themeGroup->addButton(b);
        themeRow->addWidget(b, 1);
    }
    lay->addLayout(themeRow);

    // 눌린 표시는 load() 가 맞춘다. 여기서만 맞추면 "기본값으로" 로 테마가
    // 라이트로 돌아가도 다크 쪽이 눌린 채로 남는다.
    connect(lightBtn_, &QPushButton::clicked, this,
            [] { Config::instance().setTheme(QStringLiteral("light")); });
    connect(darkBtn_, &QPushButton::clicked, this,
            [] { Config::instance().setTheme(QStringLiteral("dark")); });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildConnectionTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    // ---- 로봇 목록 ----
    //
    // 관제 한 대가 여러 로봇을 다룬다. 주소를 매번 고쳐 쓰게 하면 오타가
    // 나고, 오타가 난 주소는 "연결 안 됨" 으로만 보인다.
    //
    // 로컬에서 도는 시뮬레이터(127.0.0.1)도 여기 한 줄로 들어간다. 관제
    // 입장에서 주고받는 것이 같으므로 특별 취급할 이유가 없다.
    lay->addWidget(sectionLabel(QStringLiteral("로봇")));

    robotList_ = new QListWidget;
    robotList_->setObjectName(QStringLiteral("PickList"));
    robotList_->setMinimumHeight(110);
    lay->addWidget(robotList_);

    auto *listButtons = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("추가"));
    auto *removeBtn = new QPushButton(QStringLiteral("삭제"));
    listButtons->addWidget(addBtn);
    listButtons->addWidget(removeBtn);
    listButtons->addStretch(1);
    lay->addLayout(listButtons);

    robotName_ = new QLineEdit;
    host_ = new QLineEdit;
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    // 포트는 규약이 정한 값이라 현장에서 바꿀 것이 아니다(9090, 통신 규약
    // 1.1). 그래도 감춰 두지는 않는다 — 방화벽 규칙을 적거나 연결이 안 될 때
    // 확인해야 하는 값이고, 화면에 없으면 문서를 뒤지게 된다.
    port_->setEnabled(false);
    port_->setToolTip(QStringLiteral("통신 규약이 정한 값입니다 (9090)."));
    lay->addWidget(fieldRow(QStringLiteral("이름"), robotName_, 96));
    lay->addWidget(fieldRow(QStringLiteral("주소"), host_, 96));
    lay->addWidget(fieldRow(QStringLiteral("제어 포트"), port_, 96));

    auto *hint = new QLabel(QStringLiteral(
        "목록에서 고른 로봇에 연결합니다. 상단 바의 연결 배지를 눌러도 바꿀 수 "
        "있습니다. 촬영한 사진은 이 경로를 거치지 않고 로봇에서 저장 장치로 바로 "
        "올라갑니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(robotList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        Config::instance().setCurrentRobot(row);
        showSelectedRobot();
        refreshNetworkInfo();
    });

    connect(addBtn, &QPushButton::clicked, this, [this] {
        auto list = Config::instance().robots();
        list.append({QStringLiteral("새 로봇"), QStringLiteral("192.168.0.10"), 9090});
        Config::instance().setRobots(list);
        Config::instance().setCurrentRobot(int(list.size()) - 1);
        reloadRobotList();
    });

    connect(removeBtn, &QPushButton::clicked, this, [this] {
        auto list = Config::instance().robots();
        // 마지막 한 대는 남긴다. 목록이 비면 붙을 곳이 사라지고, 화면에는
        // 그 사실이 "연결 안 됨" 으로만 보인다.
        if (list.size() <= 1)
            return;
        list.removeAt(Config::instance().currentRobot());
        Config::instance().setRobots(list);
        reloadRobotList();
    });

    for (auto *edit : {robotName_, host_})
        connect(edit, &QLineEdit::editingFinished, this,
                &SettingsDialog::applyRobotEdits);
    connect(port_, &QSpinBox::editingFinished, this,
            &SettingsDialog::applyRobotEdits);

    // ---- 연결 확인 ----
    auto *testRow = new QHBoxLayout;
    testButton_ = new QPushButton(QStringLiteral("연결 확인"));
    testResult_ = new QLabel;
    testResult_->setObjectName(QStringLiteral("Hint"));
    testResult_->setWordWrap(true);
    testRow->addWidget(testButton_);
    testRow->addWidget(testResult_, 1);
    lay->addLayout(testRow);
    connect(testButton_, &QPushButton::clicked, this, &SettingsDialog::testConnection);

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);

    // ---- 관제 PC 네트워크 ----
    lay->addWidget(sectionLabel(QStringLiteral("관제 PC 네트워크")));
    interfaces_ = readout();
    interfaces_->setWordWrap(true);
    lay->addWidget(interfaces_);

    subnetWarning_ = new QLabel;
    subnetWarning_->setObjectName(QStringLiteral("Hint"));
    subnetWarning_->setWordWrap(true);
    lay->addWidget(subnetWarning_);

    refreshNetworkInfo();
    connect(host_, &QLineEdit::textChanged, this, [this] { refreshNetworkInfo(); });

    reloadRobotList();

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildOperationTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    linear_ = new QDoubleSpinBox;
    linear_->setRange(0.05, robot::kVxMax);
    linear_->setSingleStep(0.05);
    linear_->setDecimals(2);
    linear_->setSuffix(QStringLiteral(" m/s"));

    angular_ = new QDoubleSpinBox;
    angular_->setRange(qRadiansToDegrees(0.05), qRadiansToDegrees(robot::kWzMax));
    angular_->setSingleStep(5.0);
    angular_->setDecimals(0);
    angular_->setSuffix(QStringLiteral(" °/s"));

    lay->addWidget(sectionLabel(QStringLiteral("수동 조작 기본 속도")));
    lay->addWidget(fieldRow(QStringLiteral("선속도"), linear_, 84));
    lay->addWidget(fieldRow(QStringLiteral("각속도"), angular_, 84));

    auto *hint = new QLabel(QStringLiteral(
        "수동 조작 화면을 열 때 처음 적용되는 속도입니다. "
        "최대값은 로봇의 주행 한계(%1 m/s, %2 °/s)라 그 위로는 올릴 수 없습니다.\n\n"
        "지도에 없는 장애물이 가까워지면 로봇이 스스로 %3 m/s 까지 늦춥니다. "
        "이 설정과는 관계없이 동작합니다.")
            .arg(robot::kVxMax, 0, 'f', 2)
            .arg(qRadiansToDegrees(robot::kWzMax), 0, 'f', 0)
            .arg(robot::kVxCaution, 0, 'f', 2));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(linear_, &QDoubleSpinBox::valueChanged, this,
            [](double v) { Config::instance().setDefaultLinearSpeed(v); });
    connect(angular_, &QDoubleSpinBox::valueChanged, this, [](double deg) {
        Config::instance().setDefaultAngularSpeed(qDegreesToRadians(deg));
    });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildPowerTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    returnPct_ = new QSpinBox;
    returnPct_->setRange(5, 90);
    returnPct_->setSuffix(QStringLiteral(" %"));

    departPct_ = new QSpinBox;
    departPct_->setRange(10, 100);
    departPct_->setSuffix(QStringLiteral(" %"));

    lay->addWidget(sectionLabel(QStringLiteral("배터리")));
    lay->addWidget(fieldRow(QStringLiteral("복귀 시작"), returnPct_, 96));
    lay->addWidget(fieldRow(QStringLiteral("출발 최소"), departPct_, 96));

    auto *hint = new QLabel(QStringLiteral(
        "복귀 시작 — 점검 중 잔량이 이 값 아래로 내려가면 로봇이 점검을 멈추고 "
        "충전 스테이션으로 돌아갑니다.\n\n"
        "출발 최소 — 충전이 이 값에 이르기 전에는 점검을 시작하지 않습니다. "
        "부족한 잔량으로 나갔다가 차량 아래에서 서면, 꺼내기 위해 열차를 "
        "움직여야 합니다.\n\n"
        "출발 최소는 복귀 시작보다 높아야 합니다. 낮으면 나가자마자 되돌아옵니다.\n\n"
        "이 두 값은 로봇이 지킵니다. 관제 화면이 꺼져 있어도 그대로 동작합니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // 설정만 저장하고 끝나면 로봇은 예전 값으로 계속 돈다. 바뀔 때마다
    // 즉시 로봇으로 보낸다.
    connect(returnPct_, &QSpinBox::valueChanged, this, [this](int v) {
        Config::instance().setBatteryReturnPercent(v);
        applyBatteryBounds();
        emit batteryPolicyChanged();
    });
    connect(departPct_, &QSpinBox::valueChanged, this, [this](int v) {
        Config::instance().setBatteryDeparturePercent(v);
        applyBatteryBounds();
        emit batteryPolicyChanged();
    });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildStorageTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    logDir_ = new QLineEdit;
    retention_ = new QSpinBox;
    retention_->setRange(7, 3650);
    retention_->setSuffix(QStringLiteral(" 일"));
    nasPath_ = new QLineEdit;

    QPushButton *browseLog = nullptr;
    QPushButton *browseNas = nullptr;

    lay->addWidget(sectionLabel(QStringLiteral("이벤트 로그")));
    lay->addWidget(pathRow(QStringLiteral("저장 위치"), logDir_, &browseLog));
    logDirStatus_ = new QLabel;
    logDirStatus_->setObjectName(QStringLiteral("Hint"));
    logDirStatus_->setWordWrap(true);
    lay->addWidget(logDirStatus_);
    lay->addWidget(fieldRow(QStringLiteral("보관 기간"), retention_, 84));

    auto *logHint = new QLabel(QStringLiteral(
        "문제가 생겼을 때 담당자에게 보낼 수 있는 것은 내보낸 로그뿐입니다."));
    logHint->setObjectName(QStringLiteral("Hint"));
    logHint->setWordWrap(true);
    lay->addWidget(logHint);

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);
    lay->addWidget(sectionLabel(QStringLiteral("촬영 데이터")));
    lay->addWidget(pathRow(QStringLiteral("저장 장치 경로"), nasPath_, &browseNas));
    nasStatus_ = new QLabel;
    nasStatus_->setObjectName(QStringLiteral("Hint"));
    nasStatus_->setWordWrap(true);
    lay->addWidget(nasStatus_);

    auto *hint = new QLabel(QStringLiteral(
        "이력 화면에서 사진을 찾고 내려받을 때 쓰는 경로입니다. "
        "사진을 저장 장치에 올리는 것은 로봇이며, 관제 화면은 읽기만 합니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // 긴 경로는 입력칸에서 잘린다. 도구 설명으로 전체를 볼 수 있게 한다.
    const auto pick = [this](QLineEdit *edit, const QString &title) {
        const QString dir = QFileDialog::getExistingDirectory(this, title, edit->text());
        if (dir.isEmpty())
            return;
        edit->setText(QDir::toNativeSeparators(dir));
        emit edit->editingFinished();
    };
    connect(browseLog, &QPushButton::clicked, this,
            [pick, this] { pick(logDir_, QStringLiteral("이벤트 로그 저장 위치")); });
    connect(browseNas, &QPushButton::clicked, this,
            [pick, this] { pick(nasPath_, QStringLiteral("촬영 데이터 저장 장치 경로")); });

    connect(logDir_, &QLineEdit::editingFinished, this, [this] {
        Config::instance().setLogDirectory(logDir_->text());
        refreshPathStatus();
    });
    connect(retention_, &QSpinBox::valueChanged, this,
            [](int v) { Config::instance().setLogRetentionDays(v); });
    connect(nasPath_, &QLineEdit::editingFinished, this, [this] {
        Config::instance().setNasMountPath(nasPath_->text());
        refreshPathStatus();
    });

    lay->addStretch(1);
    return page;
}

void SettingsDialog::applyBatteryBounds()
{
    // 두 값의 순서를 설명으로만 부탁하면 언젠가 뒤집힌 채로 저장된다.
    // 입력 범위 자체를 서로에게 묶어 애초에 만들 수 없게 한다. 사이는
    // 10 %p 를 띄운다 — 충전 스테이션까지 돌아올 여유다.
    constexpr int kMargin = 10;
    const QSignalBlocker b1(returnPct_), b2(departPct_);
    returnPct_->setMaximum(qMax(returnPct_->minimum(), departPct_->value() - kMargin));
    departPct_->setMinimum(qMin(departPct_->maximum(), returnPct_->value() + kMargin));

    // 범위를 좁히면 스핀박스가 값을 말없이 끌어당긴다. 신호를 막아 둔
    // 참이라 그대로 두면 화면의 숫자와 저장된 값이 갈린다.
    auto &cfg = Config::instance();
    if (returnPct_->value() != int(cfg.batteryReturnPercent())
        || departPct_->value() != int(cfg.batteryDeparturePercent())) {
        cfg.setBatteryReturnPercent(returnPct_->value());
        cfg.setBatteryDeparturePercent(departPct_->value());
        emit batteryPolicyChanged();
    }
}

void SettingsDialog::refreshPathStatus()
{
    const auto describe = [](QLabel *out, const QString &path) {
        out->setToolTip(path);
        if (path.isEmpty()) {
            out->setText(QStringLiteral("경로가 비어 있습니다."));
            return;
        }
        const QFileInfo info(path);
        if (!info.exists())
            out->setText(QStringLiteral("⚠ %1 — 없는 경로입니다.").arg(path));
        else if (!info.isDir())
            out->setText(QStringLiteral("⚠ %1 — 폴더가 아닙니다.").arg(path));
        else if (!info.isWritable())
            out->setText(QStringLiteral("⚠ %1 — 쓸 수 없습니다.").arg(path));
        else
            out->setText(path);
    };
    if (logDirStatus_)
        describe(logDirStatus_, logDir_->text());
    // 촬영 데이터는 관제 화면이 읽기만 하므로 쓰기 권한까지 따지지 않는다.
    if (nasStatus_) {
        const QString path = nasPath_->text();
        nasStatus_->setToolTip(path);
        const QFileInfo info(path);
        nasStatus_->setText(path.isEmpty() ? QStringLiteral("경로가 비어 있습니다.")
                            : !info.isDir()
                                ? QStringLiteral("⚠ %1 — 지금은 연결되어 있지 않습니다.").arg(path)
                                : path);
    }
}

QWidget *SettingsDialog::buildSafetyTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s2);

    auto *intro = new QLabel(QStringLiteral(
        "아래 값은 로봇이 직접 지킵니다. 관제 화면에서는 바꿀 수 없습니다."));
    intro->setObjectName(QStringLiteral("Hint"));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    lay->addSpacing(metrics::s2);

    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 응답"),
        QStringLiteral("%1 초 이내").arg(robot::kEstopResponseSec),
        QStringLiteral("정지 명령과 주행 명령 차단이 동시에 걸립니다. "
                       "최종 권한은 하드웨어 정지 버튼에 있습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("통신 두절 정지"),
        QStringLiteral("%1 초").arg(robot::kLinkLossStopSec),
        QStringLiteral("관제 화면이 꺼지거나 연결이 끊겨도 로봇이 스스로 멈춥니다. "
                       "다시 연결되어도 자율주행은 자동으로 이어지지 않습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("조작 중단 시 정지"),
        QStringLiteral("%1 ms").arg(robot::kDeadmanMs),
        QStringLiteral("조작 버튼에서 손을 떼면 로봇이 즉시 멈춥니다. 관제 화면이 멈추거나 "
                       "연결이 끊겨도 마찬가지입니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 해제"), QStringLiteral("관리자 인증 필요"),
        QStringLiteral("스스로 풀리지 않습니다. 반대로 정지시킬 때는 인증 없이 바로 됩니다.")));

    lay->addStretch(1);
    return page;
}

void SettingsDialog::setCurrentTab(int index)
{
    tabs_->setCurrentIndex(index);
}

int SettingsDialog::tabCount() const
{
    return tabs_->count();
}

void SettingsDialog::refreshNetworkInfo()
{
    QStringList lines;
    QList<QPair<QHostAddress, int>> local;   // 주소와 프리픽스 길이

    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        // IsRunning 까지 요구한다. IsUp 만 보면 도커 브리지나 케이블이 빠진
        // 랜카드까지 올라오는데, 그중 172.18.0.1/16 같은 것은 대역이 넓어서
        // 로봇 주소가 우연히 그 안에 들면 "같은 네트워크" 라고 잘못 말한다.
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            lines << QStringLiteral("%1   %2/%3")
                         .arg(iface.humanReadableName(), entry.ip().toString())
                         .arg(entry.prefixLength());
            local.append({entry.ip(), entry.prefixLength()});
        }
    }

    interfaces_->setText(lines.isEmpty() ? QStringLiteral("사용 가능한 IPv4 인터페이스 없음")
                                         : lines.join(QLatin1Char('\n')));

    // 브릿지 주소가 어느 인터페이스와도 같은 서브넷에 없으면 알린다.
    // 에어갭 설치에서 가장 흔한 연결 실패 원인이고, 증상은 그냥 "연결 안 됨"
    // 이라 원인을 짚기 어렵다.
    const QHostAddress target(Config::instance().bridgeHost());
    if (target.isNull() || target.protocol() != QAbstractSocket::IPv4Protocol) {
        subnetWarning_->clear();
        subnetWarning_->hide();
        return;
    }

    // 루프백은 인터페이스 목록에 없다(위에서 IsLoopBack 을 걸렀다). 그대로
    // 두면 이 기계에서 도는 브릿지 — 시뮬레이터를 띄운 경우가 그렇다 — 를
    // "같은 네트워크가 아니다" 라고 잘못 말한다. 늘 닿는 주소다.
    if (target.isLoopback()) {
        subnetWarning_->clear();
        subnetWarning_->hide();
        return;
    }

    bool sameSubnet = false;
    for (const auto &[ip, prefix] : local) {
        if (target.isInSubnet(ip, prefix)) {
            sameSubnet = true;
            break;
        }
    }

    subnetWarning_->setVisible(!sameSubnet && !local.isEmpty());
    if (!sameSubnet && !local.isEmpty()) {
        subnetWarning_->setText(
            QStringLiteral("⚠ 로봇 주소 %1 은 위 대역 어디에도 들지 않습니다. "
                           "같은 네트워크가 아니면 연결되지 않습니다.")
                .arg(target.toString()));
    }
}

void SettingsDialog::testConnection()
{
    auto &cfg = Config::instance();
    testButton_->setEnabled(false);
    testResult_->setText(QStringLiteral("확인 중…"));

    // 소켓을 열었다 닫는 것으로 끝낸다. 프로토콜 핸드셰이크는 하지 않는다 —
    // 여기서 확인하려는 것은 "포트에 닿는가" 뿐이다.
    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(3000);

    const auto finish = [this, socket, timer](const QString &text) {
        timer->stop();
        socket->abort();
        socket->deleteLater();
        timer->deleteLater();
        testResult_->setText(text);
        testButton_->setEnabled(true);
    };

    connect(socket, &QTcpSocket::connected, this, [finish, &cfg] {
        finish(QStringLiteral("연결됨 — %1:%2 에 응답이 있습니다.")
                   .arg(cfg.bridgeHost())
                   .arg(cfg.bridgePort()));
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [finish, socket](QAbstractSocket::SocketError) {
                finish(QStringLiteral("연결 실패 — %1").arg(socket->errorString()));
            });
    connect(timer, &QTimer::timeout, this, [finish] {
        finish(QStringLiteral("연결 실패 — 시간 초과(3초). 주소·포트와 방화벽을 확인하십시오."));
    });

    timer->start();
    socket->connectToHost(cfg.bridgeHost(), quint16(cfg.bridgePort()));
}

void SettingsDialog::reloadRobotList()
{
    auto &cfg = Config::instance();
    const QSignalBlocker block(robotList_);
    robotList_->clear();
    const auto list = cfg.robots();
    for (const auto &e : list) {
        robotList_->addItem(QStringLiteral("%1      %2:%3")
                                .arg(e.name.isEmpty() ? QStringLiteral("(이름 없음)") : e.name)
                                .arg(e.host)
                                .arg(e.port));
    }
    robotList_->setCurrentRow(cfg.currentRobot());
    showSelectedRobot();
}

void SettingsDialog::showSelectedRobot()
{
    auto &cfg = Config::instance();
    const auto list = cfg.robots();
    if (list.isEmpty())
        return;
    const auto &e = list.at(cfg.currentRobot());
    const QSignalBlocker b1(robotName_), b2(host_), b3(port_);
    robotName_->setText(e.name);
    host_->setText(e.host);
    port_->setValue(e.port);
}

void SettingsDialog::applyRobotEdits()
{
    auto &cfg = Config::instance();
    auto list = cfg.robots();
    if (list.isEmpty())
        return;
    const int i = cfg.currentRobot();
    // 주소가 비면 저장하지 않는다. 빈 주소는 목록에서 한 줄을 차지하면서
    // 아무 데도 붙지 못하는, 눈으로는 멀쩡해 보이는 항목이 된다.
    if (host_->text().trimmed().isEmpty()) {
        showSelectedRobot();
        return;
    }
    list[i].name = robotName_->text().trimmed();
    list[i].host = host_->text().trimmed();
    list[i].port = port_->value();
    cfg.setRobots(list);
    reloadRobotList();
}

void SettingsDialog::load()
{
    auto &cfg = Config::instance();
    const QSignalBlocker b1(scale_), b2(host_), b3(port_);
    const QSignalBlocker bp1(returnPct_), bp2(departPct_);
    const QSignalBlocker b5(linear_), b6(angular_), b7(logDir_), b8(retention_), b9(nasPath_);

    scale_->setValue(int(qRound(cfg.uiScale() * 100)));
    scaleValue_->setText(QStringLiteral("%1%").arg(scale_->value()));
    reloadRobotList();
    returnPct_->setValue(int(cfg.batteryReturnPercent()));
    departPct_->setValue(int(cfg.batteryDeparturePercent()));
    linear_->setValue(cfg.defaultLinearSpeed());
    angular_->setValue(qRadiansToDegrees(cfg.defaultAngularSpeed()));
    logDir_->setText(cfg.logDirectory());
    retention_->setValue(cfg.logRetentionDays());
    nasPath_->setText(cfg.nasMountPath());

    const bool isDark = cfg.theme() == QLatin1String("dark");
    lightBtn_->setChecked(!isDark);
    darkBtn_->setChecked(isDark);

    applyBatteryBounds();
    refreshPathStatus();
}


}  // namespace hmi::ui
