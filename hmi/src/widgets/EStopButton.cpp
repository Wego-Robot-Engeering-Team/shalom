#include "widgets/EStopButton.h"

#include <QFont>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>

#include "theme/Tokens.h"

namespace hmi::ui {

using namespace hmi::theme;

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

EStopButton::EStopButton(QWidget *parent, int height) : QWidget(parent), size_(height)
{
    // 버튼 하나가 상단 바에서 가장 큰 요소여야 한다. 원형 머리만으로는
    // 1720 px 폭 화면의 구석에서 존재감이 나오지 않아, 글자를 새긴
    // 판 위에 얹은 형태로 만든다.
    setFixedSize(int(size_ * 2.9) + kVisualInset * 2, size_ + kVisualInset * 2);
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

    const QRectF plate(kVisualInset + 0.5, kVisualInset + 0.5,
                       width() - kVisualInset * 2 - 1.0,
                       height() - kVisualInset * 2 - 1.0);
    const double radius = plate.height() * 0.28;

    // ---- 판 ----
    // 평상시에도 붉은 테두리를 둘러 이 영역이 다른 버튼과 다르다는 것을
    // 먼저 알린다. 발동 중에는 판 전체를 채워 화면에서 가장 눈에 띄게 한다.
    QColor plateFill(C.danger);
    if (!engaged_)
        plateFill.setAlpha(hover_ ? 46 : 26);
    p.setPen(QPen(QColor(engaged_ ? C.dangerLo : C.danger), engaged_ ? 1.5 : 1.4));
    p.setBrush(plateFill);
    p.drawRoundedRect(plate, radius, radius);

    // 발동 중에는 판 둘레를 맥동시킨다. 글로우 대신 선 하나로 —
    // 관제 화면에서 빛 번짐은 다른 상태 표시를 덮는다.
    if (engaged_) {
        QColor ring(C.danger);
        ring.setAlpha(int(70 + 120 * (1.0 - pulse_)));
        p.setPen(QPen(ring, 3.0));
        p.setBrush(Qt::NoBrush);
        const double g = 2.0 + 3.0 * pulse_;
        p.drawRoundedRect(plate.adjusted(-g, -g, g, g), radius + g, radius + g);
    }

    // ---- 버섯 머리와 글자 ----
    // 원과 글자를 한 덩어리로 묶어 판 가운데에 놓는다. 원을 왼쪽에 고정하고
    // 글자를 남은 폭에 왼쪽 정렬하면, 글자가 짧을 때 오른쪽만 크게 비어
    // 한쪽으로 쏠려 보인다.
    const QString label = engaged_ ? QStringLiteral("눌러 해제")
                                   : QStringLiteral("비상정지");
    QFont f;
    f.setPointSize(qMax(9, int(size_ * 0.235)));
    f.setWeight(QFont::Bold);
    p.setFont(f);

    // 글자는 폭(advance) 이 아니라 잉크로 잰다. 한글 글리프는 advance 안에
    // 오른쪽 여백을 달고 있어서, advance 로 가운데를 잡으면 판 안에서
    // 내용이 왼쪽으로 몇 픽셀 쏠린다 — 좌우 여백이 안 맞아 보이는 이유다.
    const QRectF ink = QFontMetricsF(f).tightBoundingRect(label);
    const double outerR = size_ * 0.36;
    const double btnR = size_ * 0.27;
    const double gap = size_ * 0.22;
    const double groupW = outerR * 2 + gap + ink.width();

    const double left = plate.left() + (plate.width() - groupW) / 2.0;
    const double cy = plate.center().y();
    const double cx = left + outerR;

    p.setPen(QPen(QColor(engaged_ ? C.dangerLo : C.borderHi), 1));
    p.setBrush(QColor(engaged_ ? C.dangerHi : C.surfaceHi));
    p.drawEllipse(QRectF(cx - outerR, cy - outerR, outerR * 2, outerR * 2));

    p.setPen(QPen(QColor(C.dangerLo), 1.5));
    p.setBrush(QColor((hover_ || engaged_) ? C.dangerHi : C.danger));
    p.drawEllipse(QRectF(cx - btnR, cy - btnR, btnR * 2, btnR * 2));

    // 기준선으로 직접 놓는다. 잉크 기준으로 재 놓고 정렬은 상자에 맡기면
    // 상자 여백이 다시 끼어든다. 세로도 같은 이유로 잉크 가운데에 맞춘다 —
    // 한글에는 내려긋는 획이 없어 글꼴 높이로 맞추면 위로 떠 보인다.
    p.setPen(QColor(engaged_ ? QLatin1String("#FFFFFF") : C.danger));
    p.drawText(QPointF(left + outerR * 2 + gap - ink.left(), cy - ink.center().y()), label);
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

}  // namespace hmi::ui
