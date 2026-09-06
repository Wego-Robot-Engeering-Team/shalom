#include "panels/ArmPanel.h"

#include <QDoubleSpinBox>
#include <QEvent>
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
#include "theme/Tokens.h"
#include "widgets/JointSlider.h"
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
}

void ArmPanel::build3DSection()
{
    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(QStringLiteral("현재 자세")));
    head->addStretch(1);
    auto *reset = new QPushButton(QStringLiteral("시점 초기화"));
    reset->setProperty("size", "sm");
    head->addWidget(reset);
    card_->body()->addLayout(head);

    view3d_ = new Robot3DView;
    view3d_->setMinimumHeight(220);
    card_->body()->addWidget(view3d_, 1);
    connect(reset, &QPushButton::clicked, view3d_, &Robot3DView::resetCamera);
}

void ArmPanel::buildCommandTabs()
{
    // 관절과 끝단은 같은 일을 하는 두 방법이다. 둘 다 늘어놓으면 열이
    // 어떤 화면보다 길어지고, 정작 조작자는 한 번에 하나만 쓴다.
    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->addTab(buildJointTab(), QStringLiteral("관절"));
    tabs->addTab(buildEeTab(), QStringLiteral("끝단 위치"));
    card_->body()->addWidget(tabs);
}

QWidget *ArmPanel::buildJointTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, metrics::s2, 0, 0);
    lay->setSpacing(metrics::s1);

    auto *legend = new QLabel(
        QStringLiteral("작은 점이 지금 각도, 손잡이가 보낼 각도입니다."));
    legend->setObjectName(QStringLiteral("Hint"));
    lay->addWidget(legend);

    for (const auto &j : kFr3Joints) {
        auto *slider = new JointSlider(QString::fromUtf8(j.label), j.lo, j.hi);
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
    auto *sync = new QPushButton(QStringLiteral("지금 자세로 되돌리기"));
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
    lay->setSpacing(metrics::s2);

    auto *hint = new QLabel(
        QStringLiteral("팔 끝을 보낼 자리입니다. 로봇 기준 좌표로 씁니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(metrics::s2);
    grid->setVerticalSpacing(metrics::s1);

    struct Spec { const char *key; const char *label; double lo, hi, step, def; };
    static const Spec specs[] = {
        {"x", "앞뒤 [m]", -1.0, 1.0, 0.01, 0.40},
        {"y", "좌우 [m]", -1.0, 1.0, 0.01, 0.00},
        {"z", "높이 [m]", -0.5, 1.5, 0.01, 0.50},
        {"roll", "기울기 [°]", -180, 180, 1.0, 180.0},
        {"pitch", "숙임 [°]", -180, 180, 1.0, 0.0},
        {"yaw", "회전 [°]", -180, 180, 1.0, 0.0},
    };

    int i = 0;
    for (const auto &sp : specs) {
        // 라벨과 입력칸을 한 줄에 둔다. 위아래로 쌓으면 여섯 줄이 열두 줄이 된다.
        grid->addWidget(captionLabel(QString::fromUtf8(sp.label)), i / 2, (i % 2) * 2);

        auto *box = new QDoubleSpinBox;
        box->setRange(sp.lo, sp.hi);
        box->setSingleStep(sp.step);
        box->setValue(sp.def);
        box->setDecimals(sp.step < 1 ? 2 : 1);
        box->setAlignment(Qt::AlignRight);
        // 관절 슬라이더와 같은 이유로 휠을 막는다. 스크롤하다 목표 좌표가
        // 바뀌면 팔이 엉뚱한 데로 간다.
        box->setFocusPolicy(Qt::StrongFocus);
        box->installEventFilter(this);
        grid->addWidget(box, i / 2, (i % 2) * 2 + 1);
        ee_.insert(QString::fromLatin1(sp.key), box);
        ++i;
    }
    lay->addLayout(grid);

    auto *send = new QPushButton(QStringLiteral("여기로 보내기"));
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");
    lay->addWidget(send);
    commandButtons_ << send;

    connect(send, &QPushButton::clicked, this, [this] {
        emit eeGoal(QVariantMap{
            {"x", ee_[QStringLiteral("x")]->value()},
            {"y", ee_[QStringLiteral("y")]->value()},
            {"z", ee_[QStringLiteral("z")]->value()},
            {"roll", qDegreesToRadians(ee_[QStringLiteral("roll")]->value())},
            {"pitch", qDegreesToRadians(ee_[QStringLiteral("pitch")]->value())},
            {"yaw", qDegreesToRadians(ee_[QStringLiteral("yaw")]->value())},
            {"frame", QStringLiteral("fr3_link0")},
        });
    });

    lay->addStretch(1);
    return page;
}

/// 스핀박스 위에서 휠을 돌려도 값이 바뀌지 않게 한다.
bool ArmPanel::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::Wheel && qobject_cast<QDoubleSpinBox *>(obj)) {
        ev->ignore();
        return true;
    }
    return QWidget::eventFilter(obj, ev);
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
    auto *stop = new QPushButton(QStringLiteral("정지"));
    stop->setProperty("variant", "danger");
    stop->setProperty("size", "sm");
    connect(stop, &QPushButton::clicked, this, &ArmPanel::stopRequested);
    row->addWidget(stop, 1);

    card_->body()->addLayout(row);
}

void ArmPanel::setArmState(const QList<double> &positions, double manipulability,
                           double sigmaMin, const QString &moveitState)
{
    actual_ = positions;
    for (int i = 0; i < sliders_.size() && i < positions.size(); ++i)
        sliders_[i]->setActual(positions.at(i));
    view3d_->setArmJoints(positions);

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
    // 이제 슬라이더가 지금 각도와 보낼 각도를 함께 그린다. 갱신할 별도
    // 위젯이 없다 — 다시 그리기만 하면 된다.
    for (auto *s : std::as_const(sliders_))
        s->update();
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
