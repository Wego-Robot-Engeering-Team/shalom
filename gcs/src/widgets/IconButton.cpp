#include "widgets/IconButton.h"

#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

constexpr int kSize = 30;
constexpr double kGlyph = 15.0;   ///< 글리프가 차지하는 지름

void drawSliders(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    // 가로줄 세 개와 그 위의 손잡이. 손잡이 위치를 어긋나게 두어야
    // 눈금이 아니라 조절기로 읽힌다.
    p.setPen(QPen(tone, 1.4, Qt::SolidLine, Qt::RoundCap));
    const double x0 = c.x() - r;
    const double x1 = c.x() + r;
    const double knobAt[3] = {0.34, 0.66, 0.46};

    for (int i = 0; i < 3; ++i) {
        const double y = c.y() + (i - 1) * r * 0.66;
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(x0, y), QPointF(x1, y));

        p.setBrush(tone);
        p.drawEllipse(QPointF(x0 + (x1 - x0) * knobAt[i], y), r * 0.20, r * 0.20);
    }
}

void drawSun(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    // 속을 채운 작은 원에 긴 살. 톱니처럼 보이지 않으려면 가운데가 작고
    // 살이 길어야 한다.
    p.setPen(Qt::NoPen);
    p.setBrush(tone);
    p.drawEllipse(c, r * 0.38, r * 0.38);

    p.setPen(QPen(tone, 1.5, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 8; ++i) {
        const double a = 2 * M_PI * i / 8;
        const QPointF d(std::cos(a), std::sin(a));
        p.drawLine(c + d * (r * 0.62), c + d * (r * 1.02));
    }
}

void drawMoon(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    // 초승달은 원에서 살짝 옮긴 원을 빼서 만든다. 호 두 개로 그리면
    // 양 끝이 뾰족하게 만나지 않는다.
    QPainterPath disc;
    disc.addEllipse(c, r * 0.86, r * 0.86);

    QPainterPath bite;
    bite.addEllipse(c + QPointF(r * 0.42, -r * 0.30), r * 0.78, r * 0.78);

    p.setPen(Qt::NoPen);
    p.setBrush(tone);
    p.drawPath(disc.subtracted(bite));
}

}  // namespace

IconButton::IconButton(Glyph glyph, QWidget *parent) : QPushButton(parent), glyph_(glyph)
{
    setProperty("variant", "ghost");
    setFixedSize(kSize, kSize);
    setCursor(Qt::PointingHandCursor);
}

void IconButton::setGlyph(Glyph glyph)
{
    if (glyph_ == glyph)
        return;
    glyph_ = glyph;
    update();
}

void IconButton::paintEvent(QPaintEvent *ev)
{
    // 배경과 호버는 스타일시트가 그린다. 글리프만 그 위에 얹는다.
    QPushButton::paintEvent(ev);

    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPointF c(width() / 2.0, height() / 2.0);
    const double r = kGlyph / 2.0;
    const QColor tone(isDown() || underMouse() ? C.text : C.textDim);

    switch (glyph_) {
    case Glyph::Sliders: drawSliders(p, c, r * 0.86, tone); break;
    case Glyph::Sun:     drawSun(p, c, r, tone); break;
    case Glyph::Moon:    drawMoon(p, c, r * 0.92, tone); break;
    }
}

}  // namespace gcs::ui
