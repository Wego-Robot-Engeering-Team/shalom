#include "widgets/ValueSlider.h"

#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

/// 슬라이더는 정수만 다룬다. 단위와 무관하게 쓰려면 눈금을 값이 아니라
/// 구간 비율로 두는 편이 낫다. 1000 등분이면 라디안이든 미터든 조작자가
/// 볼 자릿수보다 촘촘하다.
constexpr int kSteps = 1000;

constexpr int kNameW = 60;    ///< "J1" 또는 "앞뒤" 자리
constexpr int kValueW = 78;
constexpr int kRowH = 32;

}  // namespace

ValueSlider::ValueSlider(const QString &name, double lo, double hi, const QString &unit,
                         int decimals, double displayScale, QWidget *parent)
    : QSlider(Qt::Horizontal, parent),
      name_(name), lo_(lo), hi_(hi), unit_(unit), decimals_(decimals), scale_(displayScale)
{
    setRange(0, kSteps);
    // 가동 범위가 0 을 포함하지 않으면 가운데에서 시작한다.
    setCommand(lo < 0 && hi > 0 ? 0.0 : (lo + hi) / 2.0);
    setFixedHeight(kRowH);
    setFocusPolicy(Qt::StrongFocus);   // 키보드로는 조작할 수 있게 둔다
}

void ValueSlider::setActual(double v)
{
    if (hasActual_ && qFuzzyCompare(actual_ + 1.0, v + 1.0))
        return;
    actual_ = v;
    hasActual_ = true;
    setToolTip(QStringLiteral("%1\n지금 %2%4\n보낼 값 %3%4")
                   .arg(name_)
                   .arg(actual_ * scale_, 0, 'f', decimals_)
                   .arg(command() * scale_, 0, 'f', decimals_)
                   .arg(unit_));
    update();
}

void ValueSlider::clearActual()
{
    if (!hasActual_)
        return;
    hasActual_ = false;
    update();
}

double ValueSlider::command() const
{
    return lo_ + (hi_ - lo_) * value() / double(kSteps);
}

void ValueSlider::setCommand(double v)
{
    const double t = (qBound(lo_, v, hi_) - lo_) / (hi_ - lo_);
    setValue(int(qRound(t * kSteps)));
}

bool ValueSlider::diverged() const
{
    // 눈금 두 칸. 단위와 무관하게 "손으로 만졌다" 를 판정하는 크기다.
    return hasActual_ && std::abs(command() - actual_) > (hi_ - lo_) * 2.0 / kSteps;
}

double ValueSlider::trackLeft() const
{
    return kNameW;
}

double ValueSlider::trackWidth() const
{
    return qMax(1, width() - kNameW - kValueW);
}

QRect ValueSlider::valueRect() const
{
    return QRect(width() - kValueW, 2, kValueW - 2, height() - 4);
}

void ValueSlider::setFromX(double x)
{
    const double t = qBound(0.0, (x - trackLeft()) / trackWidth(), 1.0);
    setValue(int(qRound(t * kSteps)));
}

void ValueSlider::beginEdit()
{
    if (!editor_) {
        editor_ = new QLineEdit(this);
        editor_->setAlignment(Qt::AlignRight);
        editor_->setFrame(false);
        connect(editor_, &QLineEdit::editingFinished, this, &ValueSlider::commitEdit);
    }
    editor_->setGeometry(valueRect());
    editor_->setText(QString::number(command() * scale_, 'f', decimals_));
    editor_->selectAll();
    editor_->show();
    editor_->setFocus(Qt::MouseFocusReason);
}

void ValueSlider::commitEdit()
{
    if (!editor_ || !editor_->isVisible())
        return;

    bool ok = false;
    const double typed = editor_->text().trimmed().toDouble(&ok);
    editor_->hide();
    if (!ok)
        return;

    // 범위를 벗어난 값은 자른다. 가동 한계를 넘겨 보내면 로봇이 거부하고,
    // 조작자는 왜 안 갔는지 모른다.
    setCommand(typed / scale_);
    emit valueTyped();
}

void ValueSlider::wheelEvent(QWheelEvent *ev)
{
    // 값을 바꾸지 않고 부모로 넘긴다. QSlider 기본 동작은 휠에 값을 바꾸는
    // 것이라, 패널을 스크롤하다 포인터가 이 위를 지나가는 것만으로 팔이
    // 다른 자세로 명령된다. 화면 어디에도 왜 그랬는지 남지 않는다.
    ev->ignore();
}

void ValueSlider::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) {
        ev->ignore();
        return;
    }
    if (valueRect().contains(ev->position().toPoint())) {
        beginEdit();
        ev->accept();
        return;
    }
    setSliderDown(true);
    setFromX(ev->position().x());
    ev->accept();
}

void ValueSlider::mouseMoveEvent(QMouseEvent *ev)
{
    if (!isSliderDown())
        return;
    setFromX(ev->position().x());
    ev->accept();
}

void ValueSlider::mouseReleaseEvent(QMouseEvent *ev)
{
    setSliderDown(false);
    update();
    ev->accept();
}

void ValueSlider::resizeEvent(QResizeEvent *ev)
{
    QSlider::resizeEvent(ev);
    if (editor_ && editor_->isVisible())
        editor_->setGeometry(valueRect());
}

void ValueSlider::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double trackX = trackLeft();
    const double trackW = trackWidth();
    if (trackW <= 8)
        return;
    const double cy = height() / 2.0;

    const auto xAt = [&](double v) {
        const double t = (v - lo_) / (hi_ - lo_);
        return trackX + qBound(0.0, t, 1.0) * trackW;
    };

    QFont fn;
    fn.setPointSize(10);
    p.setFont(fn);
    p.setPen(QColor(C.textDim));
    p.drawText(QRect(0, 0, kNameW - 6, height()), Qt::AlignLeft | Qt::AlignVCenter, name_);

    // 가동 범위
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.isDark() ? C.surfaceHi : C.surfaceHover));
    p.drawRoundedRect(QRectF(trackX, cy - 2, trackW, 4), 2, 2);

    const double cmdX = xAt(command());
    const double actualX = hasActual_ ? xAt(actual_) : cmdX;
    const bool gap = diverged();

    // 아직 보내지 않은 구간. 경고색으로 칠했더니 값을 조금만 만져도 화면이
    // 붉어져 진짜 경고와 구분되지 않았다. 강조색을 옅게 깔아 "편집 중"
    // 정도로만 보이게 한다.
    if (gap) {
        QColor pending(C.accent);
        pending.setAlpha(90);
        p.setBrush(pending);
        p.drawRoundedRect(QRectF(qMin(actualX, cmdX), cy - 2,
                                 std::abs(cmdX - actualX), 4), 2, 2);
    }

    // 지금 값은 속이 빈 고리, 보낼 값은 속을 채운 손잡이. 색만 다르게 하면
    // 두 점이 같은 종류로 보인다. 채움 여부가 다르면 형태로 구분된다.
    if (hasActual_) {
        p.setPen(QPen(QColor(C.textDim), 2.0));
        p.setBrush(QColor(C.surface));
        p.drawEllipse(QPointF(actualX, cy), 4.6, 4.6);
    }

    p.setPen(QPen(QColor(C.accentLo), 1.4));
    p.setBrush(QColor(isSliderDown() || underMouse() ? C.accentHi : C.accent));
    p.drawEllipse(QPointF(cmdX, cy), 7.0, 7.0);

    if (editor_ && editor_->isVisible())
        return;

    // 값 칸. 눌러서 직접 칠 수 있다는 것을 옅은 바탕으로 알린다.
    const QRect vr = valueRect();
    if (underMouse()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(C.surfaceHi));
        p.drawRoundedRect(vr, metrics::rSm, metrics::rSm);
    }

    p.setFont(monoFont(10));
    p.setPen(QColor(gap ? C.accent : C.text));
    p.drawText(vr.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1%2").arg(command() * scale_, 0, 'f', decimals_).arg(unit_));
}

}  // namespace gcs::ui
