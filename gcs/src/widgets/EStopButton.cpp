#include "widgets/EStopButton.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

QPropertyAnimation *makePulse(QObject *target, int durationMs)
{
    auto *a = new QPropertyAnimation(target, "pulse", target);
    a->setDuration(durationMs);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::InOutSine);
    a->setLoopCount(-1);
    return a;
}

}  // namespace

// ============================ EStopButton ============================

EStopButton::EStopButton(QWidget *parent, int size) : QWidget(parent), size_(size)
{
    setFixedSize(size_, size_);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("비상정지 — 1회 클릭으로 즉시 발동"));
    anim_ = makePulse(this, 760);
}

void EStopButton::setPulse(double v)
{
    pulse_ = v;
    update();
}

void EStopButton::setEngaged(bool engaged)
{
    if (engaged == engaged_)
        return;
    engaged_ = engaged;
    if (engaged_) {
        anim_->start();
    } else {
        anim_->stop();
        pulse_ = 0.0;
    }
    update();
}

void EStopButton::enterEvent(QEnterEvent *ev)
{
    hover_ = true;
    update();
    QWidget::enterEvent(ev);
}

void EStopButton::leaveEvent(QEvent *ev)
{
    hover_ = false;
    update();
    QWidget::leaveEvent(ev);
}

void EStopButton::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton)
        return;
    // 발동은 확인 절차 없이 즉시. 해제만 상위에서 확인을 태운다.
    if (engaged_)
        emit releaseRequested();
    else
        emit engageRequested();
}

void EStopButton::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double outerR = size_ * 0.46;
    const double btnR = size_ * 0.34;

    // 베이스 링 — 물리 스위치의 받침을 중립 색으로 절제해 표현한다.
    p.setPen(QPen(QColor(C.borderHi), 1));
    p.setBrush(QColor(C.surfaceHi));
    p.drawEllipse(QRectF(cx - outerR, cy - outerR, outerR * 2, outerR * 2));

    // 발동 중에만 얇은 링을 맥동시킨다. 글로우 대신 선 하나로 —
    // 관제 화면에서 빛 번짐은 다른 상태 표시를 덮는다.
    if (engaged_) {
        QColor ring(C.danger);
        ring.setAlpha(int(90 + 130 * pulse_));
        p.setPen(QPen(ring, 2.0));
        p.setBrush(Qt::NoBrush);
        const double rr = outerR + 1 + 2 * pulse_;
        p.drawEllipse(QRectF(cx - rr, cy - rr, rr * 2, rr * 2));
    }

    p.setPen(QPen(QColor(C.dangerLo), 1.5));
    p.setBrush(QColor((hover_ || engaged_) ? C.dangerHi : C.danger));
    p.drawEllipse(QRectF(cx - btnR, cy - btnR, btnR * 2, btnR * 2));

    QFont f;
    f.setPointSize(qMax(7, int(size_ * 0.115)));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(QColor("#FFFFFF"));
    p.drawText(QRectF(cx - btnR, cy - btnR, btnR * 2, btnR * 2), Qt::AlignCenter,
               QStringLiteral("STOP"));
}

// ============================ AlertFrame ============================

AlertFrame::AlertFrame(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
    anim_ = makePulse(this, 1000);
}

void AlertFrame::setPulse(double v)
{
    pulse_ = v;
    update();
}

void AlertFrame::setActive(bool active)
{
    if (active == active_)
        return;
    active_ = active;
    if (active_) {
        show();
        raise();
        anim_->start();
    } else {
        anim_->stop();
        hide();
    }
}

void AlertFrame::paintEvent(QPaintEvent *)
{
    if (!active_)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = 3.0;
    QColor c(colors().danger);
    c.setAlpha(int(140 + 90 * pulse_));
    p.setPen(QPen(c, w));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(w / 2, w / 2, width() - w, height() - w));
}

}  // namespace gcs::ui
