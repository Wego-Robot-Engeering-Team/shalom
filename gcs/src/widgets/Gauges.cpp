#include "widgets/Gauges.h"

#include <cmath>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPropertyAnimation>
#include <QtMath>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

QFont monoFont(int pt, bool demiBold = false)
{
    QFont f = monoFont(10);
    f.setPointSize(pt);
    if (demiBold)
        f.setWeight(QFont::DemiBold);
    return f;
}

/// 게이지 트랙 색. 다크는 한 단계 밝은 면, 라이트는 한 단계 어두운 면을 쓴다.
QColor trackColor(const Colors &C)
{
    return QColor(C.isDark() ? C.surfaceHi : C.surfaceHover);
}

}  // namespace

// ============================ AnimatedValue ============================

AnimatedValue::AnimatedValue(QWidget *parent, int durationMs) : QWidget(parent)
{
    anim_ = new QPropertyAnimation(this, "value", this);
    anim_->setDuration(durationMs);
    anim_->setEasingCurve(QEasingCurve::OutCubic);
}

void AnimatedValue::setValue(double v)
{
    value_ = v;
    update();
}

void AnimatedValue::animateTo(double target)
{
    // 같은 값이 반복해서 들어올 때 애니메이션을 다시 시작하면 값이 영원히
    // 목표에 못 닿은 것처럼 보인다. 10 Hz 스트림에서는 흔한 상황이다.
    if (qAbs(target - value_) < 1e-4)
        return;
    anim_->stop();
    anim_->setStartValue(value_);
    anim_->setEndValue(target);
    anim_->start();
}

// ============================ BatteryRing ============================

BatteryRing::BatteryRing(QWidget *parent, int size, double lowThreshold)
    : AnimatedValue(parent), size_(size), low_(lowThreshold)
{
    setFixedSize(size_, size_);
}

void BatteryRing::setState(double socPercent, bool charging)
{
    charging_ = charging;
    animateTo(qBound(0.0, socPercent, 100.0));
}

void BatteryRing::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double thickness = 6.0;
    const double pad = thickness / 2 + 1;
    const QRectF rect(pad, pad, width() - pad * 2, height() - pad * 2);

    p.setPen(QPen(trackColor(C), thickness, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(rect, 0, 360 * 16);

    QColor arc(C.success);
    if (charging_)
        arc = QColor(C.accent);
    else if (value() <= low_ * 0.6)
        arc = QColor(C.danger);
    else if (value() <= low_)
        arc = QColor(C.warning);

    if (value() > 0.3) {
        p.setPen(QPen(arc, thickness, Qt::SolidLine, Qt::FlatCap));
        // 12시에서 시계 방향으로 채운다.
        p.drawArc(rect, 90 * 16, -int(360 * 16 * value() / 100.0));
    }

    // 임계값 눈금 — 배경색으로 호를 끊어 표시한다.
    const double ang = qDegreesToRadians(90.0 - 360.0 * low_ / 100.0);
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double rOut = rect.width() / 2 + thickness / 2;
    const double rIn = rect.width() / 2 - thickness / 2;
    p.setPen(QPen(QColor(C.surface), 2));
    p.drawLine(QPointF(cx + rIn * std::cos(ang), cy - rIn * std::sin(ang)),
               QPointF(cx + rOut * std::cos(ang), cy - rOut * std::sin(ang)));

    p.setFont(monoFont(int(size_ * 0.21), true));
    p.setPen(QColor(C.text));
    p.drawText(rect.adjusted(0, -5, 0, -5), Qt::AlignCenter,
               QString::number(value(), 'f', 0));

    QFont small;
    small.setPointSize(qMax(7, int(size_ * 0.09)));
    p.setFont(small);
    p.setPen(QColor(C.textMute));
    const int off = int(size_ * 0.27);
    p.drawText(rect.adjusted(0, off, 0, off), Qt::AlignCenter,
               charging_ ? QStringLiteral("충전 중") : QStringLiteral("%"));
}

// ============================ ArcGauge ============================

// ============================ BatteryPill ============================

BatteryPill::BatteryPill(QWidget *parent, double lowThreshold)
    : AnimatedValue(parent), low_(lowThreshold)
{
    setFixedSize(74, 22);
}

void BatteryPill::setState(double socPercent, bool charging)
{
    charging_ = charging;
    setToolTip(charging_ ? QStringLiteral("충전 중 · %1%").arg(socPercent, 0, 'f', 0)
                         : QStringLiteral("배터리 %1%").arg(socPercent, 0, 'f', 0));
    animateTo(qBound(0.0, socPercent, 100.0));
}

void BatteryPill::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double soc = value();
    const QColor tone = charging_       ? QColor(C.success)
                        : soc <= low_ / 2 ? QColor(C.danger)
                        : soc <= low_     ? QColor(C.warning)
                                          : QColor(C.success);

    // 셀 몸통과 오른쪽 단자.
    const QRectF cell(0.75, 4.5, 30.0, 13.0);
    p.setPen(QPen(QColor(C.borderHi), 1.2));
    p.setBrush(QColor(C.surfaceHi));
    p.drawRoundedRect(cell, 3, 3);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.borderHi));
    p.drawRoundedRect(QRectF(cell.right() + 1.0, 8.5, 2.4, 5.0), 1, 1);

    // 잔량. 0 % 에서도 실선이 보이도록 최소 폭을 준다.
    const double inner = cell.width() - 3.0;
    p.setBrush(tone);
    p.drawRoundedRect(QRectF(cell.left() + 1.5, cell.top() + 1.5,
                             qMax(2.0, inner * soc / 100.0), cell.height() - 3.0),
                      1.5, 1.5);

    QFont f;
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor(soc <= low_ ? tone : QColor(C.text)));
    p.drawText(QRectF(38, 0, width() - 38, height()), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1%").arg(soc, 0, 'f', 0));
}

ArcGauge::ArcGauge(QWidget *parent, const QString &caption, double warnBelow,
                   double dangerBelow)
    : AnimatedValue(parent), caption_(caption), warn_(warnBelow), danger_(dangerBelow)
{
    setMinimumHeight(70);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ArcGauge::setState(double normalized, const QString &rawText)
{
    rawText_ = rawText.isEmpty() ? QString::number(normalized, 'f', 3) : rawText;
    animateTo(qBound(0.0, normalized, 1.0));
}

void ArcGauge::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double thickness = 6.0;
    const double d = qMin(width() - 14.0, (height() - 22.0) * 2);
    const QRectF rect((width() - d) / 2, 9, d, d);

    p.setPen(QPen(trackColor(C), thickness, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(rect, 180 * 16, -180 * 16);

    // 낮을수록 위험한 지표라 색이 역방향이다.
    QColor tone(C.success);
    if (value() <= danger_)
        tone = QColor(C.danger);
    else if (value() <= warn_)
        tone = QColor(C.warning);

    p.setPen(QPen(tone, thickness, Qt::SolidLine, Qt::FlatCap));
    p.drawArc(rect, 180 * 16, -int(180 * 16 * value()));

    p.setFont(monoFont(12, true));
    p.setPen(QColor(C.text));
    p.drawText(QRectF(0, height() - 32, width(), 18), Qt::AlignCenter, rawText_);

    if (!caption_.isEmpty()) {
        QFont f;
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(QColor(C.textMute));
        p.drawText(QRectF(0, height() - 15, width(), 13), Qt::AlignCenter, caption_);
    }
}

// ============================ StatBar ============================

StatBar::StatBar(const QString &label, const QString &unit, QWidget *parent,
                 double warnAbove, double dangerAbove, double vmax)
    : QWidget(parent), label_(label), unit_(unit),
      warn_(warnAbove), danger_(dangerAbove), vmax_(vmax)
{
    setFixedHeight(26);
}

void StatBar::setReading(double v)
{
    value_ = v;
    valid_ = true;
    update();
}

void StatBar::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = width();

    QFont f;
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(QColor(C.textMute));
    p.drawText(QRectF(0, 0, w * 0.55, 13), Qt::AlignLeft | Qt::AlignVCenter, label_);

    p.setFont(monoFont(9));
    p.setPen(valid_ ? QColor(C.text) : QColor(C.textMute));
    const QString txt = valid_ ? QString::number(value_, 'f', 0) + unit_
                               : QStringLiteral("—");
    p.drawText(QRectF(w * 0.45, 0, w * 0.55, 13), Qt::AlignRight | Qt::AlignVCenter, txt);

    p.setPen(Qt::NoPen);
    p.setBrush(trackColor(C));
    p.drawRect(QRectF(0, 18, w, 3));

    if (valid_ && value_ > 0) {
        QColor fill(C.textDim);
        if (danger_ >= 0 && value_ >= danger_)
            fill = QColor(C.danger);
        else if (warn_ >= 0 && value_ >= warn_)
            fill = QColor(C.warning);
        p.setBrush(fill);
        p.drawRect(QRectF(0, 18, qMax(2.0, w * qBound(0.0, value_ / vmax_, 1.0)), 3));
    }
}

// ============================ JointBar ============================

JointBar::JointBar(const QString &name, double lo, double hi, QWidget *parent)
    : QWidget(parent), name_(name), lo_(lo), hi_(hi)
{
    setFixedHeight(22);
}

void JointBar::setActual(double rad)
{
    actual_ = rad;
    update();
}

void JointBar::setCommand(double rad)
{
    command_ = rad;
    hasCommand_ = true;
    update();
}

void JointBar::clearCommand()
{
    hasCommand_ = false;
    update();
}

double JointBar::fraction(double v) const
{
    return qBound(0.0, (v - lo_) / (hi_ - lo_), 1.0);
}

bool JointBar::nearLimit() const
{
    const double f = fraction(actual_);
    return f < 0.04 || f > 0.96;
}

void JointBar::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = width();
    const double h = height();
    const double barX = 26;
    const double barW = w - barX - 56;

    QFont f;
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(QColor(C.textMute));
    p.drawText(QRectF(0, 0, 22, h), Qt::AlignLeft | Qt::AlignVCenter, name_);

    const double y = h / 2 - 1.5;
    p.setPen(Qt::NoPen);
    p.setBrush(trackColor(C));
    p.drawRect(QRectF(barX, y, barW, 3));

    // 명령값 고스트. 실제값과 갈라지는 순간이 계획 실패·리플렉스의 신호다.
    if (hasCommand_) {
        const double cx = barX + barW * fraction(command_);
        p.setBrush(QColor(C.textMute));
        p.drawRect(QRectF(cx - 1, y - 3, 2, 9));
    }

    // 실제값. 한계 근접은 마커 색으로만 알린다 — 막대에 음영을 깔면
    // 7 축이 세로로 쌓였을 때 얼룩처럼 보인다.
    const double ax = barX + barW * fraction(actual_);
    p.setBrush(nearLimit() ? QColor(C.warning) : QColor(C.accent));
    p.drawEllipse(QRectF(ax - 3.5, h / 2 - 3.5, 7, 7));

    p.setFont(monoFont(9));
    p.setPen(QColor(C.text));
    p.drawText(QRectF(w - 52, 0, 52, h), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1°").arg(qRadiansToDegrees(actual_), 0, 'f', 1));
}

}  // namespace gcs::ui
