// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "widgets/IconButton.h"

#include <cmath>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include "theme/Tokens.h"

namespace hmi::ui {

using namespace hmi::theme;

namespace {

constexpr int kSize = 34;
constexpr double kGlyph = 18.0;   ///< 글리프가 차지하는 지름

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

void drawRefresh(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(tone, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawArc(QRectF(c.x() - r * 0.72, c.y() - r * 0.72,
                     r * 1.44, r * 1.44), 25 * 16, 295 * 16);
    p.drawLine(QPointF(c.x() + r * 0.72, c.y() - r * 0.35),
               QPointF(c.x() + r * 0.72, c.y() + r * 0.18));
    p.drawLine(QPointF(c.x() + r * 0.72, c.y() - r * 0.35),
               QPointF(c.x() + r * 0.22, c.y() - r * 0.30));
}

void drawEdit(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    // 연필의 몸체·캡·깎인 심을 하나의 윤곽으로 그린다. 작은 버튼 안에
    // 문서 테두리까지 겹치면 연필과 합쳐져 알아보기 어려워진다.
    p.save();
    p.translate(c);
    p.scale(r / 9.0, r / 9.0);
    p.setPen(QPen(tone, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    QPainterPath pencil;
    pencil.moveTo(-7.0, 7.0);       // 심
    pencil.lineTo(-6.0, 2.5);
    pencil.lineTo(3.2, -6.7);
    pencil.quadTo(4.0, -7.5, 4.8, -6.7);
    pencil.lineTo(6.8, -4.7);
    pencil.quadTo(7.6, -3.9, 6.8, -3.1);
    pencil.lineTo(-2.5, 6.0);
    pencil.closeSubpath();
    p.drawPath(pencil);
    p.drawLine(QPointF(-6.0, 2.5), QPointF(-2.5, 6.0));
    p.drawLine(QPointF(2.1, -5.6), QPointF(5.7, -2.0));
    QPainterPath tip;
    tip.moveTo(-7.0, 7.0);
    tip.lineTo(-5.7, 3.2);
    tip.lineTo(-3.2, 5.7);
    tip.closeSubpath();
    p.fillPath(tip, tone);
    p.restore();
}

void drawTrash(QPainter &p, const QPointF &c, double r, const QColor &tone)
{
    p.setPen(QPen(tone, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(c.x() - r * 0.72, c.y() - r * 0.45),
               QPointF(c.x() + r * 0.72, c.y() - r * 0.45));
    p.drawLine(QPointF(c.x() - r * 0.28, c.y() - r * 0.72),
               QPointF(c.x() + r * 0.28, c.y() - r * 0.72));
    p.drawRoundedRect(QRectF(c.x() - r * 0.48, c.y() - r * 0.30,
                             r * 0.96, r * 1.05), 1.2, 1.2);
    p.drawLine(QPointF(c.x() - r * 0.15, c.y() - r * 0.08),
               QPointF(c.x() - r * 0.15, c.y() + r * 0.47));
    p.drawLine(QPointF(c.x() + r * 0.15, c.y() - r * 0.08),
               QPointF(c.x() + r * 0.15, c.y() + r * 0.47));
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
    const QColor tone(isDown() || underMouse() || glyph_ == Glyph::Edit
                      ? C.text : C.textDim);

    switch (glyph_) {
    case Glyph::Sliders: drawSliders(p, c, r * 0.86, tone); break;
    case Glyph::Sun:     drawSun(p, c, r, tone); break;
    case Glyph::Moon:    drawMoon(p, c, r * 0.92, tone); break;
    case Glyph::Refresh: drawRefresh(p, c, r, tone); break;
    case Glyph::Edit:    drawEdit(p, c, r, tone); break;
    case Glyph::Trash:   drawTrash(p, c, r, tone); break;
    }
}

}  // namespace hmi::ui
