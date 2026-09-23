// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/TeleopPanel.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTextEdit>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QtMath>
#include <QVBoxLayout>

#include "RobotDef.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

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
    // 단위는 도(°) 로 보여준다. 값 자체는 rad/s 로 다룬다.
    angular_ = addSpeedRow(QStringLiteral("각속도"), robot::kWzMax, 0.50,
                           QStringLiteral("°/s"), -1,
                           180.0 / M_PI, 0);

    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(buildPostureRow());

    // 첫 줄("누르고 있는 동안만 움직입니다")은 지웠다. 눌러 보면 바로 아는
    // 동작이고, 진단 화면에서 당연한 설명을 걷어낸 것과 같은 이유다.
    // 키 배치는 남긴다 — 이건 설명이 아니라 조작 방법이고 다른 출처가 없다.
    auto *note = new QLabel(QStringLiteral("↑↓←→ 이동    Q E 회전    Space 정지"));
    note->setObjectName(QStringLiteral("Hint"));
    note->setWordWrap(true);
    card_->body()->addWidget(note);
    card_->body()->addStretch(1);

    timer_ = new QTimer(this);
    timer_->setInterval(1000 / kPublishHz);
    connect(timer_, &QTimer::timeout, this, &TeleopPanel::publish);

    setJogEnabled(false);

    // 패널을 먼저 클릭해야 키가 먹으면 급할 때 못 쓴다. 창 전체를 본다.
    qApp->installEventFilter(this);
}

QString TeleopPanel::keyFor(const QKeyEvent *ev)
{
    switch (ev->key()) {
    case Qt::Key_Up:    return QStringLiteral("fwd");
    case Qt::Key_Down:  return QStringLiteral("back");
    case Qt::Key_Left:  return QStringLiteral("left");
    case Qt::Key_Right: return QStringLiteral("right");
    case Qt::Key_Q:     return QStringLiteral("rot_l");
    case Qt::Key_E:     return QStringLiteral("rot_r");
    default:            return {};
    }
}

bool TeleopPanel::typingSomewhere()
{
    const QWidget *w = QApplication::focusWidget();
    if (!w)
        return false;
    if (qobject_cast<const QLineEdit *>(w) || qobject_cast<const QTextEdit *>(w)
        || qobject_cast<const QPlainTextEdit *>(w)
        || qobject_cast<const QAbstractSpinBox *>(w))
        return true;
    const auto *combo = qobject_cast<const QComboBox *>(w);
    return combo && combo->isEditable();
}

bool TeleopPanel::eventFilter(QObject *watched, QEvent *ev)
{
    const bool down = ev->type() == QEvent::KeyPress;
    if ((!down && ev->type() != QEvent::KeyRelease) || !enabled_ || typingSomewhere())
        return QWidget::eventFilter(watched, ev);

    // 이 패널이 보이는 화면에서만 키를 받는다. 창 전체를 감시하므로, 이
    // 조건이 없으면 이력이나 진단 화면에서 방향키를 눌러도 로봇이 움직인다.
    // 예전에는 조작 카드가 수동 모드에서만 나타나 그 자체가 조건이었다.
    if (!isVisible())
        return QWidget::eventFilter(watched, ev);

    auto *ke = static_cast<QKeyEvent *>(ev);

    // 자동 반복은 버린다. 그대로 두면 누르고 있는 내내 press/release 가
    // 번갈아 들어와 로봇이 끊겼다 이어졌다 한다.
    if (ke->isAutoRepeat())
        return true;

    if (ke->key() == Qt::Key_Space) {
        if (down) {
            heldKey_.clear();
            release();
        }
        return true;
    }

    const QString key = keyFor(ke);
    if (key.isEmpty())
        return QWidget::eventFilter(watched, ev);

    if (down) {
        heldKey_ = key;
        press(key);
    } else if (heldKey_ == key) {
        // 다른 방향키로 이미 넘어간 뒤라면 이 뗌은 무시한다. 그러지 않으면
        // 두 키를 겹쳐 눌렀다 하나를 뗄 때 로봇이 멈춘다.
        heldKey_.clear();
        release();
    }

    // 눌린 방향 버튼을 같이 눌린 것처럼 보여준다.
    if (auto *b = buttons_.value(key))
        b->setDown(down);
    return true;
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
        b->setFixedSize(50, 36);
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
                                  const QString &unit, double caution,
                                  double dispScale, int decimals)
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s1);

    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(label));
    head->addStretch(1);
    auto *value =
        readout(QStringLiteral("%1 %2").arg(def * dispScale, 0, 'f', decimals).arg(unit));
    head->addWidget(value);
    lay->addLayout(head);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(5, int(vmax * 100));
    slider->setValue(int(def * 100));

    connect(slider, &QSlider::valueChanged, this,
            [value, unit, caution, slider, dispScale, decimals](int v) {
        value->setText(QStringLiteral("%1 %2")
                           .arg(v / 100.0 * dispScale, 0, 'f', decimals)
                           .arg(unit));
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

QWidget *TeleopPanel::buildPostureRow()
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s1);

    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(QStringLiteral("본체 자세")));
    head->addStretch(1);
    postureLabel_ = new QLabel(QStringLiteral("—"));
    postureLabel_->setObjectName(QStringLiteral("Readout"));
    head->addWidget(postureLabel_);
    lay->addLayout(head);

    // damp 을 다른 셋과 같은 줄에 두지 않는다. 앉기·서기는 되돌릴 수 있지만
    // damp 은 관절 힘을 빼는 것이라 서 있는 상태에서 누르면 그대로 주저앉는다.
    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s1);
    struct Item { const char *key; const char *label; };
    for (const auto &it : {Item{"balance_stand", "균형 서기"},
                           Item{"stand_up", "일어서기"},
                           Item{"stand_down", "앉기"}}) {
        auto *b = new QPushButton(QString::fromUtf8(it.label));
        const QString key = QString::fromLatin1(it.key);
        connect(b, &QPushButton::clicked, this,
                [this, key] { emit basePosture(key, false); });
        postureButtons_.insert(key, b);
        row->addWidget(b);
    }
    lay->addLayout(row);

    auto *damp = new QPushButton(QStringLiteral("힘 빼기 (damp)"));
    damp->setObjectName(QStringLiteral("Danger"));
    connect(damp, &QPushButton::clicked, this, [this] {
        // 로봇도 confirm 없이는 거절하지만, 조작자가 결과를 알고 누르게 하는
        // 것은 화면의 몫이다. 여기서 막지 않으면 로봇의 거절만 보고 "왜 안
        // 되지" 하며 다시 누른다.
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("힘 빼기"),
            QStringLiteral("관절 힘을 뺍니다. 서 있는 상태라면 로봇이 주저앉습니다.\n"
                           "주변에 사람과 장비가 없는지 확인하셨습니까?"),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes)
            emit basePosture(QStringLiteral("damp"), true);
    });
    postureButtons_.insert(QStringLiteral("damp"), damp);
    lay->addWidget(damp);

    return host;
}

void TeleopPanel::setBasePosture(const QString &posture)
{
    if (!postureLabel_)
        return;
    static const QHash<QString, QString> kNames{
        {QStringLiteral("balance_stand"), QStringLiteral("균형 서기")},
        {QStringLiteral("stand_up"), QStringLiteral("서 있음")},
        {QStringLiteral("stand_down"), QStringLiteral("앉음")},
        {QStringLiteral("recovery_stand"), QStringLiteral("복구 중")},
        {QStringLiteral("damp"), QStringLiteral("힘 빠짐")},
        // 로봇이 아직 자세를 확인하지 못한 상태. 브릿지가 접속 직후에
        // 보내는 값이라 조작자가 가장 먼저 보게 된다.
        {QStringLiteral("unknown"), QStringLiteral("확인 안 됨")},
    };
    // 모르는 값은 원문 그대로 보여 준다. 임의로 접으면 조작자는 로봇이 무슨
    // 상태인지 모르는 채 아는 것처럼 읽는다.
    postureLabel_->setText(posture.isEmpty() ? QStringLiteral("—")
                                             : kNames.value(posture, posture));
}

void TeleopPanel::setJogEnabled(bool on)
{
    enabled_ = on;
    for (auto it = buttons_.cbegin(); it != buttons_.cend(); ++it)
        it.value()->setEnabled(on || it.key() == QLatin1String("stop"));
    linear_->setEnabled(on);
    angular_->setEnabled(on);
    // 자세 전환도 수동 모드에서만 받는다. 자율주행 중에 앉으면 미션이 깨진다.
    for (auto it = postureButtons_.cbegin(); it != postureButtons_.cend(); ++it)
        it.value()->setEnabled(on);
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

}  // namespace hmi::ui
