#include "panels/ArmPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtMath>

#include "RobotDef.h"
#include "robot/Kinematics.h"
#include "robot/PoseCheck.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/ValueSlider.h"
#include "widgets/Robot3DView.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using namespace gcs::robot;

namespace {
/// 각도를 기준값과 같은 바퀴로 옮긴다.
///
/// -π 과 π 은 같은 자세다. 정기구학은 그 경계에서 관절이 눈금 하나만
/// 달라져도 부호를 뒤집어 내놓는데, 그대로 두면 슬라이더에서는 손잡이와
/// 기준 고리가 양 끝으로 벌어진다 — 아무것도 만지지 않았는데 보내지 않은
/// 편집이 있는 것처럼 보인다.
double wrapNear(double v, double ref)
{
    while (v - ref > M_PI)
        v -= 2 * M_PI;
    while (ref - v > M_PI)
        v += 2 * M_PI;
    return v;
}

}  // namespace

ArmPanel::ArmPanel(QWidget *parent) : QWidget(parent)
{
    actual_ = QList<double>(int(kFr3Joints.size()), 0.0);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("로봇팔 제어 · FR3"));
    state_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(state_);
    outer->addWidget(card_);

    // 조작성 지수 게이지는 뺐다. 숫자든 게이지든 조작자가 그것을 보고
    // 할 일이 정해지지 않았다. 여유가 줄었을 때 무엇을 하라는 한 줄만
    // 남기고, 그마저도 여유가 있을 때는 숨긴다.
    advice_ = new QLabel;
    advice_->setObjectName(QStringLiteral("Hint"));
    advice_->setWordWrap(true);
    advice_->hide();
    card_->body()->addWidget(advice_);

    // 3D 뷰가 남는 세로 공간을 전부 가져간다. 자세를 눈으로 보는 것이
    // 이 화면의 주된 용도다.
    build3DSection();
    card_->body()->addWidget(new HLine);
    buildPresetSection();
    card_->body()->addWidget(new HLine);
    buildCommandTabs();

    // 정지는 프리셋이 아니다. 자세를 고르는 버튼들과 같은 줄에 두면 네 번째
    // 자세처럼 보인다. 위의 모든 조작을 되돌리는 것이므로 맨 아래에 둔다.
    // 크기는 다른 조작과 같게 — 창 전체를 덮는 비상정지와 달리 이건
    // 로봇팔 하나를 멈추는 보통의 조작이다.
    auto *stop = new QPushButton(QStringLiteral("로봇팔 정지"));
    stop->setProperty("variant", "danger");
    stop->setProperty("size", "sm");
    connect(stop, &QPushButton::clicked, this, &ArmPanel::stopRequested);
    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(stop);
}

void ArmPanel::build3DSection()
{
    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(QStringLiteral("현재 자세")));

    // 문제가 있을 때만 나타나는 표시. 문장을 늘 띄워 두면 자리를 차지하고,
    // 정작 문제가 생겼을 때 다른 문장과 구분되지 않는다. 마우스를 올리면
    // 무엇이 문제인지 나온다.
    poseWarning_ = new QLabel(QStringLiteral("!"));
    poseWarning_->setObjectName(QStringLiteral("PoseWarning"));
    poseWarning_->setAlignment(Qt::AlignCenter);
    poseWarning_->setFixedSize(18, 18);
    poseWarning_->hide();
    head->addSpacing(metrics::s2);
    head->addWidget(poseWarning_, 0, Qt::AlignVCenter);

    head->addStretch(1);
    auto *reset = new QPushButton(QStringLiteral("시점 초기화"));
    reset->setProperty("size", "sm");
    head->addWidget(reset);
    card_->body()->addLayout(head);

    view3d_ = new Robot3DView;
    view3d_->setMinimumHeight(320);
    card_->body()->addWidget(view3d_, 1);
    connect(reset, &QPushButton::clicked, view3d_, &Robot3DView::resetCamera);
}

void ArmPanel::buildCommandTabs()
{
    // 관절과 끝단은 같은 일을 하는 두 방법이다. 둘 다 늘어놓으면 열이
    // 어떤 화면보다 길어지고, 정작 조작자는 한 번에 하나만 쓴다.
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(buildJointTab(), QStringLiteral("관절"));
    tabs_->addTab(buildEeTab(), QStringLiteral("끝단 위치"));
    card_->body()->addWidget(tabs_);

    // 두 탭은 같은 목표를 다르게 적은 것이다. 한쪽을 만지면 다른 쪽도 따라
    // 바뀌므로, 예전처럼 탭을 떠날 때 값을 되돌릴 필요가 없다 — 보이지 않는
    // 탭에 다른 값이 남아 있을 수가 없다.
    //
    // 화면이 열리는 순간에도 마찬가지여야 한다. 첫 텔레메트리가 오기 전에
    // 끝단 탭을 열어 보는 것만으로도 두 탭이 어긋나 보이면 안 된다.
    syncEeFromJoints();
}

QWidget *ArmPanel::buildJointTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    // 설명 한 줄은 지웠다. 한 번 읽으면 그만인 문장이 세로 공간을 계속
    // 차지하고, 그만큼 3D 뷰가 줄어든다. 같은 내용은 슬라이더 도구 설명에 있다.
    lay->setContentsMargins(0, metrics::s2, 0, 0);
    lay->setSpacing(0);

    for (const auto &j : kFr3Joints) {
        auto *slider = new ValueSlider(QString::fromUtf8(j.label), j.lo, j.hi,
                                       QStringLiteral("°"), 1, 180.0 / M_PI);
        slider->setObjectName(QStringLiteral("Joint%1").arg(sliders_.size() + 1));
        connect(slider, &QSlider::valueChanged, this, &ArmPanel::onSliderMoved);
        lay->addWidget(slider);
        sliders_ << slider;
    }

    lay->addSpacing(metrics::s1);
    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);
    auto *send = new QPushButton(QStringLiteral("보내기"));
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");
    auto *sync = new QPushButton(QStringLiteral("되돌리기"));
    sync->setToolTip(QStringLiteral("슬라이더를 로봇의 지금 각도로 되돌립니다"));
    sync->setProperty("size", "sm");
    row->addWidget(send, 1);
    row->addWidget(sync, 1);
    lay->addLayout(row);
    commandButtons_ << send << sync;

    connect(send, &QPushButton::clicked, this, [this] {
        QList<double> q;
        for (auto *s : std::as_const(sliders_))
            q << s->command();
        emit jointGoal(q);
    });
    connect(sync, &QPushButton::clicked, this, &ArmPanel::syncSlidersToActual);
    return page;
}

QWidget *ArmPanel::buildEeTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, metrics::s2, 0, 0);
    lay->setSpacing(metrics::s1);

    // 관절과 같은 조작으로 통일한다. 한쪽은 슬라이더, 한쪽은 스핀박스면
    // 같은 성격의 값을 다루는 방법을 두 번 배워야 한다.
    struct Spec { const char *key; const char *label; double lo, hi; const char *unit;
                  int decimals; double scale; };
    // 축 이름은 X/Y/Z/Roll/Pitch/Yaw 를 그대로 쓴다. 이 탭을 쓰는 사람은
    // 좌표계를 아는 사람이고, "숙임" 같은 말로 바꾸면 어느 축인지 오히려
    // 되짚어야 한다. 조작자용 표현이 필요한 곳은 프리셋 쪽이다.
    static const Spec specs[] = {
        {"x", "X", -1.0, 1.0, " m", 2, 1.0},
        {"y", "Y", -1.0, 1.0, " m", 2, 1.0},
        {"z", "Z", -0.5, 1.5, " m", 2, 1.0},
        {"roll", "Roll", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
        {"pitch", "Pitch", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
        {"yaw", "Yaw", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
    };

    for (const auto &sp : specs) {
        auto *slider = new ValueSlider(QString::fromUtf8(sp.label), sp.lo, sp.hi,
                                       QString::fromUtf8(sp.unit), sp.decimals, sp.scale);
        slider->setObjectName(QStringLiteral("Ee_%1").arg(QString::fromLatin1(sp.key)));
        slider->setCyclic(qFuzzyCompare(sp.hi - sp.lo, 2 * M_PI));
        // 값은 채우지 않는다. 명령값은 관절에서 정기구학으로(buildCommandTabs
        // 끝), 기준값은 실제 관절에서 온다. 여기에 그럴듯한 상수를 박아 두면
        // 두 탭이 서로 다른 자세를 말하는 채로 화면이 열린다.
        connect(slider, &QSlider::valueChanged, this, &ArmPanel::syncJointsFromEe);
        lay->addWidget(slider);
        ee_.insert(QString::fromLatin1(sp.key), slider);
    }

    lay->addSpacing(metrics::s1);
    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);

    auto *send = new QPushButton(QStringLiteral("보내기"));
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");

    // 관절 탭과 같은 자리에 같은 이름으로 둔다. 되돌릴 방법이 한쪽에만
    // 있으면, 잘못 만졌을 때 무엇을 눌러야 하는지가 탭마다 달라진다.
    auto *revert = new QPushButton(QStringLiteral("되돌리기"));
    revert->setProperty("size", "sm");
    revert->setToolTip(QStringLiteral("마지막으로 보낸 값으로 되돌립니다"));
    connect(revert, &QPushButton::clicked, this, [this] {
        for (auto *e : std::as_const(ee_))
            e->setCommand(e->actual());
    });

    row->addWidget(send, 1);
    row->addWidget(revert, 1);
    lay->addLayout(row);
    commandButtons_ << send;

    connect(send, &QPushButton::clicked, this, [this] {
        emit eeGoal(QVariantMap{
            {"x", ee_[QStringLiteral("x")]->command()},
            {"y", ee_[QStringLiteral("y")]->command()},
            {"z", ee_[QStringLiteral("z")]->command()},
            {"roll", ee_[QStringLiteral("roll")]->command()},
            {"pitch", ee_[QStringLiteral("pitch")]->command()},
            {"yaw", ee_[QStringLiteral("yaw")]->command()},
            {"frame", QStringLiteral("fr3_link0")},
        });
        // 기준값은 건드리지 않는다. 팔이 실제로 그 자세에 닿을 때까지
        // 편집 표시가 남아 있는 것이 맞다 — 관절 탭도 그렇게 동작한다.
    });

    lay->addStretch(1);
    return page;
}

void ArmPanel::buildPresetSection()
{
    card_->body()->addWidget(sectionLabel(QStringLiteral("프리셋")));

    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);
    struct Preset { const char *key; const char *label; };
    static const Preset presets[] = {
        {"home", "홈"}, {"standby", "촬영대기"}, {"stow", "수납"}};

    for (const auto &p : presets) {
        auto *b = new QPushButton(QString::fromUtf8(p.label));
        b->setProperty("size", "sm");
        const QString key = QString::fromLatin1(p.key);
        connect(b, &QPushButton::clicked, this, [this, key] { emit presetRequested(key); });
        row->addWidget(b, 1);
    }
    card_->body()->addLayout(row);
}

void ArmPanel::setArmState(const QList<double> &positions, double manipulability,
                           double sigmaMin, const QString &moveitState)
{
    // 생성자가 actual_ 을 0 으로 채워 두므로 비어 있는지로는 첫 보고를
    // 가릴 수 없다. 받았는지 여부를 따로 들고 있는다.
    const bool first = !hadArmState_;
    hadArmState_ = true;
    actual_ = positions;
    for (int i = 0; i < sliders_.size() && i < positions.size(); ++i)
        sliders_[i]->setActual(positions.at(i));

    // 아직 아무것도 지시하지 않았는데 슬라이더가 기본값에 서 있으면, 화면이
    // 열리자마자 "보내지 않은 편집" 이 있다고 말하게 된다. 첫 보고를 받은
    // 순간의 명령값은 지금 자세다.
    if (first)
        syncSlidersToActual();

    syncEeActualFromJoints(positions);
    view3d_->setArmJoints(positions);
    refreshPreview();

    const double norm = qBound(0.0, manipulability / kManipNominal, 1.0);

    // 여유가 있을 때는 아무 말도 하지 않는다. "정상입니다" 를 늘 띄워두면
    // 정말 문제가 생겼을 때의 한 줄이 똑같이 생긴 한 줄로 보인다.
    if (norm <= kManipDanger) {
        advice_->setText(QStringLiteral(
            "팔이 거의 다 펴졌거나 접혔습니다. 이 자세에서는 어떤 방향으로는 "
            "아예 움직이지 못합니다. 팔을 조금 되돌리거나 로봇을 옮기십시오."));
    } else if (norm <= kManipWarn) {
        advice_->setText(QStringLiteral(
            "움직일 수 있는 여유가 줄었습니다. 더 뻗으면 멈출 수 있으니 "
            "로봇을 조금 옮겨 자세를 바꾸는 편이 낫습니다."));
    }
    advice_->setVisible(norm <= kManipWarn);
    advice_->setToolTip(QStringLiteral("조작성 지수 %1 · 최소 특이값 %2")
                            .arg(manipulability, 0, 'f', 4)
                            .arg(sigmaMin, 0, 'f', 4));
    view3d_->setSingularWarning(norm <= kManipWarn);

    if (moveitState == QLatin1String("planning"))
        state_->set(QStringLiteral("계획 중"), QStringLiteral("info"));
    else if (moveitState == QLatin1String("executing"))
        state_->set(QStringLiteral("실행 중"), QStringLiteral("info"));
    else if (moveitState == QLatin1String("error"))
        state_->set(QStringLiteral("오류"), QStringLiteral("danger"));
    else
        state_->set(QStringLiteral("대기"), QStringLiteral("neutral"));
}

void ArmPanel::setControlsEnabled(bool on)
{
    for (auto *s : std::as_const(sliders_))
        s->setEnabled(on);
    for (auto *b : std::as_const(commandButtons_))
        b->setEnabled(on);
}

void ArmPanel::onSliderMoved()
{
    if (syncing_)
        return;
    for (auto *s : std::as_const(sliders_))
        s->update();
    syncEeFromJoints();
    refreshPreview();
}

void ArmPanel::syncEeFromJoints()
{
    if (ee_.isEmpty())
        return;

    // 두 탭은 같은 목표를 다르게 적은 것이다. 한쪽을 만지고 다른 쪽을 그대로
    // 두면 조작자가 보는 숫자가 실제로 보낼 자세와 달라진다.
    QList<double> q;
    for (auto *s : std::as_const(sliders_))
        q << s->command();

    const auto pose = robot::forwardKinematics(q);
    syncing_ = true;
    ee_[QStringLiteral("x")]->setCommand(pose.x);
    ee_[QStringLiteral("y")]->setCommand(pose.y);
    ee_[QStringLiteral("z")]->setCommand(pose.z);
    // 각도는 지금 표시된 기준값과 같은 바퀴에 올려 둔다.
    for (const auto &axis : {std::pair{"roll", pose.roll}, {"pitch", pose.pitch},
                             {"yaw", pose.yaw}}) {
        auto *s = ee_[QLatin1String(axis.first)];
        s->setCommand(wrapNear(axis.second, s->actual()));
    }
    syncing_ = false;
    eeReachable_ = true;
}

void ArmPanel::syncEeActualFromJoints(const QList<double> &joints)
{
    if (ee_.isEmpty() || joints.size() < 7)
        return;

    // 로봇은 끝단 좌표를 따로 보고하지 않는다. 그래도 관절은 보고하고 끝단은
    // 관절에서 나오므로, 정기구학을 한 번 돌리면 두 탭이 같은 하나의 자세를
    // 말한다. 예전에는 "마지막으로 보낸 값" 을 지금 자세라고 적어 두었는데,
    // 그것은 팔이 실제로 어디 있는지와 아무 상관이 없는 숫자였다.
    const auto pose = robot::forwardKinematics(joints);
    ee_[QStringLiteral("x")]->setActual(pose.x);
    ee_[QStringLiteral("y")]->setActual(pose.y);
    ee_[QStringLiteral("z")]->setActual(pose.z);
    for (const auto &axis : {std::pair{"roll", pose.roll}, {"pitch", pose.pitch},
                             {"yaw", pose.yaw}}) {
        auto *s = ee_[QLatin1String(axis.first)];
        s->setActual(wrapNear(axis.second, s->command()));
    }
}

void ArmPanel::syncJointsFromEe()
{
    if (syncing_ || sliders_.isEmpty())
        return;

    robot::EePose target;
    target.x = ee_[QStringLiteral("x")]->command();
    target.y = ee_[QStringLiteral("y")]->command();
    target.z = ee_[QStringLiteral("z")]->command();
    target.roll = ee_[QStringLiteral("roll")]->command();
    target.pitch = ee_[QStringLiteral("pitch")]->command();
    target.yaw = ee_[QStringLiteral("yaw")]->command();

    // 지금 관절에서 출발해 가장 가까운 해를 찾는다. 팔이 갑자기 뒤집히면
    // 조작자는 자기가 그렇게 시킨 줄 안다.
    QList<double> seed;
    for (auto *s : std::as_const(sliders_))
        seed << s->command();

    const auto solved = robot::inverseKinematics(target, seed);
    if (!solved) {
        // 닿지 않는 자리다. 관절을 억지로 옮기지 않는다 — 가장 가까운 자세로
        // 슬쩍 옮겨 두면 조작자는 자기가 지정한 자리로 간다고 믿는다.
        eeReachable_ = false;
        refreshPreview();
        return;
    }

    eeReachable_ = true;
    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < solved->size(); ++i)
        sliders_[i]->setCommand(solved->at(i));
    syncing_ = false;

    for (auto *s : std::as_const(sliders_))
        s->update();
    refreshPreview();
}

void ArmPanel::refreshPreview()
{
    // 3D 뷰에 보낼 자세를 겹쳐 보여준다. 화면에만 반영되고 로봇은 움직이지
    // 않는다 — 실제 명령은 "보내기" 를 눌러야 나간다.
    QList<double> q;
    bool differs = false;
    for (int i = 0; i < sliders_.size(); ++i) {
        q << sliders_.at(i)->command();
        if (sliders_.at(i)->diverged())
            differs = true;
    }
    // 어느 탭에서 만졌든 3D 는 보낼 자세를 보여준다. 끝단 값은 역기구학을
    // 거쳐 이미 관절로 옮겨져 있다.
    view3d_->setPreviewJoints(differs ? q : QList<double>{});

    // 보내기 전에 조용히 알린다. 로봇이 최종 판정을 하지만, 눌러 본 뒤에야
    // 거부 코드로 알게 되는 것보다 낫다. 요란하게 막지는 않는다 — 조작자가
    // 의도해서 그 자세로 가는 경우도 있다.
    robot::PoseWarning warning;
    if (!eeReachable_) {
        warning = {QStringLiteral("danger"),
                   QStringLiteral("팔이 닿지 않는 자리입니다. 값을 조금 되돌리십시오.")};
    } else if (differs) {
        warning = robot::checkArmPose(q);
    }
    showPoseWarning(warning);
}

void ArmPanel::showPoseWarning(const robot::PoseWarning &warning)
{
    // 내용이 그대로면 위젯을 건드리지 않는다.
    //
    // 이 함수는 텔레메트리마다, 즉 초당 스무 번 불린다. 그때마다 repolish
    // 로 위젯을 다시 칠하면 Qt 가 도구 설명을 띄우려고 세어 두는 시간이
    // 매번 초기화되어, 아이콘에 마우스를 올려도 설명이 끝내 뜨지 않는다.
    // 화면에는 아무 문제가 없어 보이므로 원인을 찾기 어렵다.
    if (warning.text == lastWarning_.text && warning.severity == lastWarning_.severity)
        return;
    lastWarning_ = warning;

    poseWarning_->setToolTip(warning.text);
    poseWarning_->setProperty("tone", warning.severity);
    poseWarning_->setVisible(!warning.isEmpty());
    theme::repolish(poseWarning_);
}

void ArmPanel::syncSlidersToActual()
{
    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < actual_.size(); ++i)
        sliders_[i]->setCommand(actual_.at(i));
    syncing_ = false;
    onSliderMoved();
}

void ArmPanel::applyPresetToSliders(const QString &name)
{
    const std::array<double, 7> *preset = nullptr;
    if (name == QLatin1String("home"))
        preset = &kArmHome;
    else if (name == QLatin1String("standby"))
        preset = &kArmStandby;
    else if (name == QLatin1String("stow"))
        preset = &kArmStow;
    if (!preset)
        return;

    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < int(preset->size()); ++i)
        sliders_[i]->setCommand((*preset)[i]);
    syncing_ = false;
    onSliderMoved();
}

}  // namespace gcs::ui
