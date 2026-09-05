#include "panels/TeleopPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include "RobotDef.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {
constexpr int kPublishHz = 20;
}

TeleopPanel::TeleopPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("수동 조작"));
    outer->addWidget(card_);

    auto *holder = new QHBoxLayout;
    holder->addStretch(1);
    holder->addWidget(buildPad());
    holder->addStretch(1);
    card_->body()->addLayout(holder);

    card_->body()->addSpacing(metrics::s2);
    linear_ = addSpeedRow(QStringLiteral("선속도"), robot::kVxMax, 0.30,
                          QStringLiteral("m/s"), robot::kVxCaution);
    angular_ = addSpeedRow(QStringLiteral("각속도"), robot::kWzMax, 0.50,
                           QStringLiteral("rad/s"), -1);

    auto *note = new QLabel(QStringLiteral("버튼을 누르는 동안만 발행 · %1 Hz · 데드맨 300 ms")
                                .arg(kPublishHz));
    note->setObjectName(QStringLiteral("Hint"));
    note->setWordWrap(true);
    card_->body()->addWidget(note);
    card_->body()->addStretch(1);

    timer_ = new QTimer(this);
    timer_->setInterval(1000 / kPublishHz);
    connect(timer_, &QTimer::timeout, this, &TeleopPanel::publish);

    setJogEnabled(false);
}

QWidget *TeleopPanel::buildPad()
{
    auto *pad = new QWidget;
    auto *grid = new QGridLayout(pad);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(metrics::s1);

    struct Cell { const char *key; int row, col; const char *glyph; };
    static const Cell cells[] = {
        {"rot_l", 0, 0, "↺"}, {"fwd", 0, 1, "▲"}, {"rot_r", 0, 2, "↻"},
        {"left", 1, 0, "◀"},  {"stop", 1, 1, "■"}, {"right", 1, 2, "▶"},
        {"back", 2, 1, "▼"},
    };

    for (const auto &c : cells) {
        auto *b = new QPushButton(QString::fromUtf8(c.glyph));
        b->setFixedSize(46, 34);
        const QString key = QString::fromLatin1(c.key);
        if (key == QLatin1String("stop")) {
            b->setProperty("variant", "danger");
            connect(b, &QPushButton::clicked, this, [this] {
                release();
                emit cmdVel(0, 0, 0);
            });
        } else {
            // pressed/released 를 쓴다. clicked 는 버튼을 떼야 발생해서
            // 누르고 있는 동안 계속 보내는 동작을 만들 수 없다.
            connect(b, &QPushButton::pressed, this, [this, key] { press(key); });
            connect(b, &QPushButton::released, this, &TeleopPanel::release);
        }
        grid->addWidget(b, c.row, c.col);
        buttons_.insert(key, b);
    }
    return pad;
}

QSlider *TeleopPanel::addSpeedRow(const QString &label, double vmax, double def,
                                  const QString &unit, double caution)
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s1);

    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(label));
    head->addStretch(1);
    auto *value = readout(QStringLiteral("%1 %2").arg(def, 0, 'f', 2).arg(unit));
    head->addWidget(value);
    lay->addLayout(head);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(5, int(vmax * 100));
    slider->setValue(int(def * 100));

    connect(slider, &QSlider::valueChanged, this, [value, unit, caution, slider](int v) {
        value->setText(QStringLiteral("%1 %2").arg(v / 100.0, 0, 'f', 2).arg(unit));
        if (caution < 0)
            return;
        // 지시서 2.2.5 는 미등록 물체 접근 시 30 cm/s 감속을 요구한다.
        // 그 기준을 넘겨 설정하면 슬라이더를 경고색으로 바꿔 알린다.
        const QString warn = v / 100.0 > caution ? QStringLiteral("true")
                                                 : QStringLiteral("false");
        if (slider->property("warn").toString() != warn) {
            slider->setProperty("warn", warn);
            repolish(slider);
        }
    });
    lay->addWidget(slider);
    card_->body()->addWidget(host);
    return slider;
}

void TeleopPanel::setJogEnabled(bool on)
{
    enabled_ = on;
    for (auto it = buttons_.cbegin(); it != buttons_.cend(); ++it)
        it.value()->setEnabled(on || it.key() == QLatin1String("stop"));
    linear_->setEnabled(on);
    angular_->setEnabled(on);
    if (!on)
        release();
}

void TeleopPanel::press(const QString &key)
{
    if (!enabled_)
        return;
    const double lin = linear_->value() / 100.0;
    const double ang = angular_->value() / 100.0;

    vx_ = key == QLatin1String("fwd") ? lin : key == QLatin1String("back") ? -lin : 0.0;
    // 횡이동은 전진보다 느리게 건다. 사족보행에서 게걸음은 안정성이 낮다.
    vy_ = key == QLatin1String("left") ? lin * 0.7
          : key == QLatin1String("right") ? -lin * 0.7 : 0.0;
    wz_ = key == QLatin1String("rot_l") ? ang : key == QLatin1String("rot_r") ? -ang : 0.0;

    if (!timer_->isActive())
        timer_->start();
    publish();
}

void TeleopPanel::release()
{
    vx_ = vy_ = wz_ = 0.0;
    publish();        // 즉시 0 을 한 번 보낸다. 데드맨을 기다리지 않는다.
    timer_->stop();
}

void TeleopPanel::publish()
{
    emit cmdVel(vx_, vy_, wz_);
}

}  // namespace gcs::ui
