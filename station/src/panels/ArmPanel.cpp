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
/// 슬라이더는 0.1도 해상도의 정수 눈금으로 다룬다.
constexpr double kSliderScale = 10.0;

int toTicks(double rad) { return int(qRadiansToDegrees(rad) * kSliderScale); }
double fromTicks(int ticks) { return qDegreesToRadians(ticks / kSliderScale); }
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

    // 두 탭은 같은 목표를 정하는 두 가지 방법이다. 한쪽을 만지다 다른 쪽으로
    // 넘어가면, 보이지 않는 탭에 보내지 않은 값이 남아 나중에 무엇이 갈지
    // 알 수 없다. 떠나는 탭은 기준값으로 되돌린다.
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 0) {
            for (auto *e : std::as_const(ee_))
                e->setCommand(e->actual());
        } else {
            syncSlidersToActual();
        }
        refreshPreview();
    });
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

    auto *hint = new QLabel(QStringLiteral(
        "팔 끝을 보낼 자리입니다. 로봇 기준 좌표이며, 숫자를 누르면 직접 "
        "입력할 수 있습니다.\n빈 점은 마지막으로 보낸 값입니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // 관절과 같은 조작으로 통일한다. 한쪽은 슬라이더, 한쪽은 스핀박스면
    // 같은 성격의 값을 다루는 방법을 두 번 배워야 한다.
    struct Spec { const char *key; const char *label; double lo, hi; const char *unit;
                  int decimals; double scale; double def; };
    // 축 이름은 X/Y/Z/Roll/Pitch/Yaw 를 그대로 쓴다. 이 탭을 쓰는 사람은
    // 좌표계를 아는 사람이고, "숙임" 같은 말로 바꾸면 어느 축인지 오히려
    // 되짚어야 한다. 조작자용 표현이 필요한 곳은 프리셋 쪽이다.
    static const Spec specs[] = {
        {"x", "X", -1.0, 1.0, " m", 2, 1.0, 0.40},
        {"y", "Y", -1.0, 1.0, " m", 2, 1.0, 0.00},
        {"z", "Z", -0.5, 1.5, " m", 2, 1.0, 0.50},
        {"roll", "Roll", -M_PI, M_PI, "°", 0, 180.0 / M_PI, M_PI},
        {"pitch", "Pitch", -M_PI, M_PI, "°", 0, 180.0 / M_PI, 0.0},
        {"yaw", "Yaw", -M_PI, M_PI, "°", 0, 180.0 / M_PI, 0.0},
    };

    for (const auto &sp : specs) {
        auto *slider = new ValueSlider(QString::fromUtf8(sp.label), sp.lo, sp.hi,
                                       QString::fromUtf8(sp.unit), sp.decimals, sp.scale);
        slider->setCommand(sp.def);
        // 로봇은 끝단 좌표를 따로 보고하지 않는다. 대신 마지막으로 보낸 값을
        // 비교 기준으로 둔다 — 관절 탭과 같은 모양으로 "만졌지만 아직 안
        // 보낸" 구간이 보인다. 아무 기준도 없으면 무엇을 바꿨는지 알 수 없다.
        slider->setActual(sp.def);
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
        // 보낸 순간이 새 기준이 된다. 편집 표시가 사라져 "보냈다" 가 보인다.
        for (auto *s : std::as_const(ee_))
            s->setActual(s->command());
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
    actual_ = positions;
    for (int i = 0; i < sliders_.size() && i < positions.size(); ++i)
        sliders_[i]->setActual(positions.at(i));
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
    for (auto *s : std::as_const(sliders_))
        s->update();
    refreshPreview();
}

void ArmPanel::refreshPreview()
{
    // 3D 뷰에 보낼 자세를 겹쳐 보여준다. 화면에만 반영되고 로봇은 움직이지
    // 않는다 — 실제 명령은 "보내기" 를 눌러야 나간다.
    // 관절 탭에서만 미리보기를 띄운다. 끝단 좌표에서 자세를 얻으려면 역기구학이
    // 필요한데 그것은 로봇이 푼다. 관제가 임의로 풀어 보여주면 실제로 나올
    // 자세와 다를 수 있고, 그 차이를 조작자가 확인할 방법이 없다.
    const bool onJointTab = !tabs_ || tabs_->currentIndex() == 0;

    QList<double> q;
    bool differs = false;
    for (int i = 0; i < sliders_.size(); ++i) {
        q << sliders_.at(i)->command();
        if (sliders_.at(i)->diverged())
            differs = true;
    }
    view3d_->setPreviewJoints(onJointTab && differs ? q : QList<double>{});

    // 보내기 전에 조용히 알린다. 로봇이 최종 판정을 하지만, 눌러 본 뒤에야
    // 거부 코드로 알게 되는 것보다 낫다. 요란하게 막지는 않는다 — 조작자가
    // 의도해서 그 자세로 가는 경우도 있다.
    if (onJointTab && differs) {
        const auto warning = robot::checkArmPose(q);
        poseWarning_->setToolTip(warning.text);
        poseWarning_->setProperty("tone", warning.severity);
        poseWarning_->setVisible(!warning.isEmpty());
        theme::repolish(poseWarning_);
    } else {
        poseWarning_->hide();
    }
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
