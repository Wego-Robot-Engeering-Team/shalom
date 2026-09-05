#include "widgets/HealthRow.h"

#include <QFont>
#include <QPainter>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

QColor stateColor(const QString &state)
{
    const Colors &C = colors();
    if (state == QLatin1String("ok"))
        return QColor(C.success);
    if (state == QLatin1String("degraded"))
        return QColor(C.warning);
    return QColor(C.danger);   // lost, fault
}

}  // namespace

HealthRow::HealthRow(const QString &name, double expectedHz, QWidget *parent)
    : QWidget(parent), name_(name), expectedHz_(expectedHz)
{
    setFixedHeight(26);
}

void HealthRow::setState(const QString &state, double actualHz, qint64 lastSeenMs,
                         const QString &detail)
{
    state_ = state;
    actualHz_ = actualHz;
    lastSeenMs_ = lastSeenMs;
    detail_ = detail;
    update();
}

void HealthRow::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = width();
    const double h = height();
    const QColor sc = stateColor(state_);
    const bool lost = state_ == QLatin1String("lost") || state_ == QLatin1String("fault");

    // 상태 점
    p.setPen(Qt::NoPen);
    p.setBrush(sc);
    p.drawEllipse(QRectF(0, h / 2 - 3, 6, 6));

    QFont f;
    f.setPointSize(10);
    p.setFont(f);
    p.setPen(lost ? sc : QColor(C.text));
    p.drawText(QRectF(14, 0, w * 0.40, h), Qt::AlignLeft | Qt::AlignVCenter, name_);

    // 속도 막대 — 기대 주기 대비 비율로 그린다. 절대 Hz 로 그리면
    // 200 Hz IMU 옆에서 1 Hz 배터리가 항상 빈 막대로 보인다.
    const double barX = w * 0.42;
    const double barW = w * 0.20;
    const double ratio = expectedHz_ > 0 ? qBound(0.0, actualHz_ / expectedHz_, 1.0) : 0.0;
    p.setBrush(QColor(C.isDark() ? C.surfaceHi : C.surfaceHover));
    p.drawRect(QRectF(barX, h / 2 - 1.5, barW, 3));
    if (ratio > 0) {
        p.setBrush(sc);
        p.drawRect(QRectF(barX, h / 2 - 1.5, qMax(2.0, barW * ratio), 3));
    }

    QFont fm(monoFamily());
    fm.setPointSize(9);
    p.setFont(fm);
    p.setPen(QColor(C.textDim));
    const QString rate =
        lost ? QStringLiteral("— / %1 Hz").arg(expectedHz_, 0, 'g', 3)
             : QStringLiteral("%1 / %2 Hz").arg(actualHz_, 0, 'f', actualHz_ < 10 ? 1 : 0)
                   .arg(expectedHz_, 0, 'g', 3);
    p.drawText(QRectF(barX + barW + 8, 0, w * 0.22, h),
               Qt::AlignLeft | Qt::AlignVCenter, rate);

    // 우측 부가 정보: 정상이면 detail, 끊겼으면 경과 시간
    QString right = detail_;
    if (lost && lastSeenMs_ > 0)
        right = lastSeenMs_ >= 1000 ? QStringLiteral("%1초 전").arg(lastSeenMs_ / 1000)
                                    : QStringLiteral("%1ms 전").arg(lastSeenMs_);
    if (!right.isEmpty()) {
        p.setPen(lost ? sc : QColor(C.textMute));
        p.drawText(QRectF(w * 0.72, 0, w * 0.28, h),
                   Qt::AlignRight | Qt::AlignVCenter, right);
    }
}

}  // namespace gcs::ui
