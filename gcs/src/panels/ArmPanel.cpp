#include "panels/ArmPanel.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtMath>

#include "RobotDef.h"
#include "theme/Tokens.h"
#include "widgets/Gauges.h"
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

    // ---- 조작성 게이지 ----
    auto *gaugeRow = new QHBoxLayout;
    gaugeRow->setSpacing(metrics::s3);
    manip_ = new ArcGauge(nullptr, QStringLiteral("조작성 지수 w"),
                          kManipWarn, kManipDanger);
    gaugeRow->addWidget(manip_, 1);

    auto *side = new QVBoxLayout;
    side->setSpacing(metrics::s1);
    side->addWidget(sectionLabel(QStringLiteral("σ min")));
    sigma_ = readout(QStringLiteral("—"), true);
    side->addWidget(sigma_);
    singular_ = new Badge(QStringLiteral("정상"), QStringLiteral("ok"));
    side->addWidget(singular_, 0, Qt::AlignLeft);
    side->addStretch(1);
    gaugeRow->addLayout(side);
    card_->body()->addLayout(gaugeRow);
    card_->body()->addWidget(new HLine);

    buildJointSection();
    card_->body()->addWidget(new HLine);
    buildEeSection();
    card_->body()->addWidget(new HLine);
    buildPresetSection();
    card_->body()->addStretch(1);
}

void ArmPanel::buildJointSection()
{
    card_->body()->addWidget(sectionLabel(QStringLiteral("관절 (7축)")));

    for (const auto &j : kFr3Joints) {
        auto *row = new QWidget;
        auto *lay = new QVBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);

        auto *bar = new JointBar(j.label, j.lo, j.hi);
        lay->addWidget(bar);
        bars_ << bar;

        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(toTicks(j.lo), toTicks(j.hi));
        // 가동 범위가 0 을 포함하지 않는 축(J4, J6)은 중앙에서 시작한다.
        slider->setValue(j.lo < 0 && j.hi > 0 ? 0 : toTicks((j.lo + j.hi) / 2));
        connect(slider, &QSlider::valueChanged, this, &ArmPanel::onSliderMoved);
        lay->addWidget(slider);
        sliders_ << slider;

        card_->body()->addWidget(row);
    }

    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);
    auto *send = new QPushButton(QStringLiteral("관절 목표 전송"));
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");
    auto *sync = new QPushButton(QStringLiteral("현재값 동기화"));
    sync->setProperty("size", "sm");
    row->addWidget(send, 1);
    row->addWidget(sync);
    card_->body()->addLayout(row);
    commandButtons_ << send << sync;

    connect(send, &QPushButton::clicked, this, [this] {
        QList<double> q;
        for (auto *s : std::as_const(sliders_))
            q << fromTicks(s->value());
        emit jointGoal(q);
    });
    connect(sync, &QPushButton::clicked, this, &ArmPanel::syncSlidersToActual);
}

void ArmPanel::buildEeSection()
{
    card_->body()->addWidget(sectionLabel(QStringLiteral("End-Effector 목표 (MoveIt2)")));

    auto *grid = new QGridLayout;
    grid->setSpacing(metrics::s2);

    struct Spec { const char *key; const char *label; double lo, hi, step, def; };
    static const Spec specs[] = {
        {"x", "X [m]", -1.0, 1.0, 0.01, 0.40},
        {"y", "Y [m]", -1.0, 1.0, 0.01, 0.00},
        {"z", "Z [m]", -0.5, 1.5, 0.01, 0.50},
        {"roll", "R [°]", -180, 180, 1.0, 180.0},
        {"pitch", "P [°]", -180, 180, 1.0, 0.0},
        {"yaw", "Y [°]", -180, 180, 1.0, 0.0},
    };

    int i = 0;
    for (const auto &s : specs) {
        auto *lbl = sectionLabel(QString::fromUtf8(s.label));
        auto *box = new QDoubleSpinBox;
        box->setRange(s.lo, s.hi);
        box->setSingleStep(s.step);
        box->setValue(s.def);
        box->setDecimals(s.step < 1 ? 2 : 1);
        box->setAlignment(Qt::AlignRight);
        grid->addWidget(lbl, i / 3 * 2, i % 3);
        grid->addWidget(box, i / 3 * 2 + 1, i % 3);
        ee_.insert(QString::fromLatin1(s.key), box);
        ++i;
    }
    card_->body()->addLayout(grid);

    auto *send = new QPushButton(QStringLiteral("EE 목표 실행"));
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");
    card_->body()->addWidget(send);
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

    auto *stop = new QPushButton(QStringLiteral("로봇팔 정지"));
    stop->setProperty("variant", "danger");
    connect(stop, &QPushButton::clicked, this, &ArmPanel::stopRequested);
    card_->body()->addWidget(stop);
}

void ArmPanel::setArmState(const QList<double> &positions, double manipulability,
                           double sigmaMin, const QString &moveitState)
{
    actual_ = positions;
    for (int i = 0; i < bars_.size() && i < positions.size(); ++i)
        bars_[i]->setActual(positions.at(i));

    const double norm = qBound(0.0, manipulability / kManipNominal, 1.0);
    manip_->setState(norm, QString::number(manipulability, 'f', 4));
    sigma_->setText(QString::number(sigmaMin, 'f', 4));

    if (norm <= kManipDanger)
        singular_->set(QStringLiteral("특이자세 근접"), QStringLiteral("danger"));
    else if (norm <= kManipWarn)
        singular_->set(QStringLiteral("주의"), QStringLiteral("warn"));
    else
        singular_->set(QStringLiteral("정상"), QStringLiteral("ok"));

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
    // 명령값 고스트를 갱신한다. 실제값과 갈라진 정도가 곧 미전송 편차다.
    for (int i = 0; i < bars_.size() && i < sliders_.size(); ++i)
        bars_[i]->setCommand(fromTicks(sliders_[i]->value()));
}

void ArmPanel::syncSlidersToActual()
{
    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < actual_.size(); ++i)
        sliders_[i]->setValue(toTicks(actual_.at(i)));
    syncing_ = false;
    for (auto *b : std::as_const(bars_))
        b->clearCommand();
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
        sliders_[i]->setValue(toTicks((*preset)[i]));
    syncing_ = false;
    onSliderMoved();
}

}  // namespace gcs::ui
