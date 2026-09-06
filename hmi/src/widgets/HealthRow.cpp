#include "widgets/HealthRow.h"

#include <QFont>
#include <QPainter>

#include "theme/Tokens.h"

namespace hmi::ui {

using namespace hmi::theme;

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
    setFixedHeight(29);
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
    f.setPointSize(11);
    p.setFont(f);
    p.setPen(lost ? sc : QColor(C.text));
    p.drawText(QRectF(14, 0, w * 0.42, h), Qt::AlignLeft | Qt::AlignVCenter, name_);

    // 속도 막대 — 기대 주기 대비 비율로 그린다. 절대 Hz 로 그리면
    // 200 Hz IMU 옆에서 1 Hz 배터리가 항상 빈 막대로 보인다.
    const double barX = w * 0.46;
    const double barW = w * 0.18;
    const double ratio = expectedHz_ > 0 ? qBound(0.0, actualHz_ / expectedHz_, 1.0) : 0.0;
    p.setBrush(QColor(C.isDark() ? C.surfaceHi : C.surfaceHover));
    p.drawRect(QRectF(barX, h / 2 - 1.5, barW, 3));
    if (ratio > 0) {
        p.setBrush(sc);
        p.drawRect(QRectF(barX, h / 2 - 1.5, qMax(2.0, barW * ratio), 3));
    }

    // 오른쪽 한 칸에 한 가지만 적는다. 예전에는 주기와 부가 정보(라이다
    // 점 개수 같은)를 나란히 적었는데, 열이 좁아지면서 두 글자가 겹쳤다.
    // 점 개수로 조작자가 할 일도 없다 — 막대가 이미 정상 여부를 말한다.
    //
    // 끊겼을 때는 주기 대신 경과 시간을 적는다. 그때 급한 것은 "몇 Hz 여야
    // 하는가" 가 아니라 "언제부터 안 오는가" 다.
    const QString right =
        lost && lastSeenMs_ > 0
            ? (lastSeenMs_ >= 1000 ? QStringLiteral("%1초 전").arg(lastSeenMs_ / 1000)
                                   : QStringLiteral("%1ms 전").arg(lastSeenMs_))
        : lost ? QStringLiteral("신호 없음")
               : QStringLiteral("%1 / %2 Hz")
                     .arg(actualHz_, 0, 'f', actualHz_ < 10 ? 1 : 0)
                     .arg(expectedHz_, 0, 'g', 3);

    p.setFont(monoFont(10));
    p.setPen(lost ? sc : QColor(C.textDim));
    p.drawText(QRectF(barX + barW + 8, 0, w - (barX + barW) - 8, h),
               Qt::AlignRight | Qt::AlignVCenter, right);

    // 부가 정보는 도구 설명으로 남긴다. 정비 담당자가 확인할 값이다.
    if (!detail_.isEmpty())
        setToolTip(QStringLiteral("%1 · %2").arg(name_, detail_));
}

}  // namespace hmi::ui
