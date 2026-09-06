#include "widgets/JointSlider.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QWheelEvent>
#include <QtMath>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

/// 슬라이더는 정수만 다룬다. 0.1도 단위로 저장하면 FR3 가동범위
/// (±3 rad 안팎)가 int 안에 넉넉히 들어가고, 조작자가 볼 소수 첫째 자리도
/// 그대로 표현된다.
constexpr double kTicksPerDegree = 10.0;

int toTicks(double rad) { return int(qRadiansToDegrees(rad) * kTicksPerDegree); }
double fromTicks(int ticks) { return qDegreesToRadians(ticks / kTicksPerDegree); }

constexpr int kNameW = 28;    ///< "J1" 자리
constexpr int kValueW = 66;   ///< "-135.0°" 자리
constexpr int kRowH = 30;

/// 명령과 실측이 이만큼 벌어지면 눈에 띄게 표시한다. 약 2도.
constexpr double kDivergedRad = 0.035;

}  // namespace

JointSlider::JointSlider(const QString &name, double lo, double hi, QWidget *parent)
    : QSlider(Qt::Horizontal, parent), name_(name), lo_(lo), hi_(hi)
{
    setRange(toTicks(lo), toTicks(hi));
    // 가동 범위가 0 을 포함하지 않는 축은 중앙에서 시작한다.
    setValue(lo < 0 && hi > 0 ? 0 : toTicks((lo + hi) / 2));
    setFixedHeight(kRowH);
    setFocusPolicy(Qt::StrongFocus);   // 키보드로는 조작할 수 있게 둔다
}

void JointSlider::setActual(double rad)
{
    if (hasActual_ && qFuzzyCompare(actual_ + 1.0, rad + 1.0))
        return;
    actual_ = rad;
    hasActual_ = true;
    setToolTip(QStringLiteral("%1 축\n지금 %2°\n보낼 값 %3°")
                   .arg(name_)
                   .arg(qRadiansToDegrees(actual_), 0, 'f', 1)
                   .arg(qRadiansToDegrees(command()), 0, 'f', 1));
    update();
}

double JointSlider::command() const
{
    return fromTicks(value());
}

void JointSlider::setCommand(double rad)
{
    setValue(toTicks(qBound(lo_, rad, hi_)));
}

bool JointSlider::diverged() const
{
    return hasActual_ && std::abs(command() - actual_) > kDivergedRad;
}

double JointSlider::trackLeft() const
{
    return kNameW;
}

double JointSlider::trackWidth() const
{
    return qMax(1, width() - kNameW - kValueW);
}

void JointSlider::setFromX(double x)
{
    const double t = qBound(0.0, (x - trackLeft()) / trackWidth(), 1.0);
    setValue(toTicks(lo_ + t * (hi_ - lo_)));
}

void JointSlider::mousePressEvent(QMouseEvent *ev)
{
    if (ev->button() != Qt::LeftButton) {
        ev->ignore();
        return;
    }
    setSliderDown(true);
    setFromX(ev->position().x());
    ev->accept();
}

void JointSlider::mouseMoveEvent(QMouseEvent *ev)
{
    if (!isSliderDown())
        return;
    setFromX(ev->position().x());
    ev->accept();
}

void JointSlider::mouseReleaseEvent(QMouseEvent *ev)
{
    setSliderDown(false);
    update();
    ev->accept();
}

void JointSlider::wheelEvent(QWheelEvent *ev)
{
    // 값을 바꾸지 않고 부모로 넘긴다. QSlider 기본 동작은 휠에 값을 바꾸는
    // 것이라, 패널을 스크롤하다 포인터가 관절 위를 지나가는 것만으로 팔이
    // 다른 각도로 명령된다. 화면 어디에도 왜 그랬는지 남지 않는다.
    ev->ignore();
}

void JointSlider::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int trackX = kNameW;
    const int trackW = width() - kNameW - kValueW;
    if (trackW <= 8)
        return;
    const double cy = height() / 2.0;

    const auto xAt = [&](double rad) {
        const double t = (rad - lo_) / (hi_ - lo_);
        return trackX + qBound(0.0, t, 1.0) * trackW;
    };

    // 축 이름
    QFont fn;
    fn.setPointSize(10);
    p.setFont(fn);
    p.setPen(QColor(C.textDim));
    p.drawText(QRect(0, 0, kNameW - 4, height()), Qt::AlignLeft | Qt::AlignVCenter, name_);

    // 가동 범위
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.isDark() ? C.surfaceHi : C.surfaceHover));
    p.drawRoundedRect(QRectF(trackX, cy - 2, trackW, 4), 2, 2);

    const double cmdX = xAt(command());

    // 한 줄에 점 두 개. 작은 점이 지금 각도, 큰 손잡이가 보낼 각도다.
    // 둘이 벌어져 있으면 그 사이를 이어 "여기서 저기로 간다"를 길이로
    // 보여준다. 두 값을 위아래 두 줄에 따로 적던 때보다 차이가 먼저 읽힌다.
    const double actualX = hasActual_ ? xAt(actual_) : cmdX;
    const bool gap = diverged();

    if (gap) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(C.warning));
        p.drawRoundedRect(QRectF(qMin(actualX, cmdX), cy - 2,
                                 std::abs(cmdX - actualX), 4), 2, 2);
    }

    // 지금 각도는 속이 빈 고리, 보낼 각도는 속을 채운 손잡이. 색만 다르게
    // 하면 두 점이 같은 종류로 보인다. 채움 여부가 다르면 형태로 구분된다.
    if (hasActual_) {
        p.setPen(QPen(QColor(gap ? C.warning : C.textDim), 2.0));
        p.setBrush(QColor(C.surface));
        p.drawEllipse(QPointF(actualX, cy), 4.6, 4.6);
    }

    // 보낼 각도 — 손잡이. 지금 각도보다 크게 그려 어느 쪽이 조작 대상인지
    // 모양만으로 구분되게 한다.
    p.setPen(QPen(QColor(C.accentLo), 1.4));
    p.setBrush(QColor(isSliderDown() || underMouse() ? C.accentHi : C.accent));
    p.drawEllipse(QPointF(cmdX, cy), 7.0, 7.0);

    // 값은 보낼 각도를 적는다. 지금 각도는 점 위치로 읽고, 정확한 숫자는
    // 도구 설명에 있다.
    p.setFont(monoFont(10));
    p.setPen(QColor(gap ? C.warning : C.text));
    p.drawText(QRect(width() - kValueW, 0, kValueW, height()),
               Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1°").arg(qRadiansToDegrees(command()), 0, 'f', 1));
}

}  // namespace gcs::ui
