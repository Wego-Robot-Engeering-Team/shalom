#include "widgets/NavRail.h"

#include <QButtonGroup>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Gauges.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

constexpr int kRailWidth = 112;
constexpr int kItemHeight = 58;
constexpr int kIconBox = 22;

/// 네비게이션 아이콘. 전부 선 기반이라 테마 색을 그대로 따르고,
/// 아이콘 폰트나 SVG 자산 의존성이 생기지 않는다.
void drawIcon(QPainter &p, NavItem item, const QRectF &r, const QColor &c)
{
    QPen pen(c, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const double x = r.x();
    const double y = r.y();
    const double s = r.width();

    switch (item) {
    case NavItem::Drive: {
        // 나침반 — 원 + 방향 바늘
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
        QPainterPath needle;
        needle.moveTo(x + s * 0.50, y + s * 0.24);
        needle.lineTo(x + s * 0.66, y + s * 0.72);
        needle.lineTo(x + s * 0.50, y + s * 0.60);
        needle.lineTo(x + s * 0.34, y + s * 0.72);
        needle.closeSubpath();
        p.setBrush(c);
        p.drawPath(needle);
        break;
    }
    case NavItem::Locations: {
        // 지도 핀
        QPainterPath pin;
        pin.moveTo(x + s * 0.50, y + s * 0.92);
        pin.cubicTo(x + s * 0.50, y + s * 0.58, x + s * 0.84, y + s * 0.52,
                    x + s * 0.84, y + s * 0.37);
        pin.cubicTo(x + s * 0.84, y + s * 0.15, x + s * 0.16, y + s * 0.15,
                    x + s * 0.16, y + s * 0.37);
        pin.cubicTo(x + s * 0.16, y + s * 0.52, x + s * 0.50, y + s * 0.58,
                    x + s * 0.50, y + s * 0.92);
        p.drawPath(pin);
        p.setBrush(c);
        p.drawEllipse(QPointF(x + s * 0.50, y + s * 0.37), s * 0.10, s * 0.10);
        break;
    }
    case NavItem::Arm: {
        // 두 마디 링크와 관절점
        p.drawLine(QPointF(x + s * 0.18, y + s * 0.86), QPointF(x + s * 0.42, y + s * 0.38));
        p.drawLine(QPointF(x + s * 0.42, y + s * 0.38), QPointF(x + s * 0.84, y + s * 0.26));
        p.setBrush(c);
        p.drawEllipse(QPointF(x + s * 0.18, y + s * 0.86), s * 0.09, s * 0.09);
        p.drawEllipse(QPointF(x + s * 0.42, y + s * 0.38), s * 0.09, s * 0.09);
        p.drawEllipse(QPointF(x + s * 0.84, y + s * 0.26), s * 0.09, s * 0.09);
        break;
    }
    case NavItem::Capture: {
        // 카메라 몸체 + 렌즈
        p.drawRoundedRect(QRectF(x + s * 0.06, y + s * 0.30, s * 0.88, s * 0.54), 3, 3);
        QPainterPath hump;
        hump.moveTo(x + s * 0.32, y + s * 0.30);
        hump.lineTo(x + s * 0.40, y + s * 0.16);
        hump.lineTo(x + s * 0.60, y + s * 0.16);
        hump.lineTo(x + s * 0.68, y + s * 0.30);
        p.drawPath(hump);
        p.drawEllipse(QPointF(x + s * 0.50, y + s * 0.58), s * 0.16, s * 0.16);
        break;
    }
    case NavItem::Diagnostics: {
        // 이벤트 파형
        QPainterPath wave;
        wave.moveTo(x + s * 0.06, y + s * 0.56);
        wave.lineTo(x + s * 0.28, y + s * 0.56);
        wave.lineTo(x + s * 0.40, y + s * 0.22);
        wave.lineTo(x + s * 0.55, y + s * 0.84);
        wave.lineTo(x + s * 0.67, y + s * 0.56);
        wave.lineTo(x + s * 0.94, y + s * 0.56);
        p.drawPath(wave);
        break;
    }
    }
}

struct ItemSpec {
    NavItem item;
    const char *label;
};

const ItemSpec kItems[] = {
    {NavItem::Drive, "주행"},
    {NavItem::Locations, "위치"},
    {NavItem::Arm, "로봇팔"},
    {NavItem::Capture, "촬영"},
    {NavItem::Diagnostics, "진단"},
};

}  // namespace

/// 아이콘 + 라벨을 세로로 쌓은 네비게이션 항목.
///
/// QPushButton 을 QSS 로 꾸미는 대신 직접 그린다. 아이콘·라벨·선택 표시·배지를
/// 한 번에 배치해야 하는데, 스타일시트로는 그 정렬을 통제할 수 없다.
class NavButton : public QWidget {
public:
    NavButton(NavItem item, const QString &label, QWidget *parent = nullptr)
        : QWidget(parent), item_(item), label_(label)
    {
        setFixedHeight(kItemHeight);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setChecked(bool on)
    {
        if (on == checked_)
            return;
        checked_ = on;
        update();
    }

    void setAlerts(int count)
    {
        if (count == alerts_)
            return;
        alerts_ = count;
        update();
    }

    NavItem item() const { return item_; }

Q_SIGNALS:

protected:
    void enterEvent(QEnterEvent *ev) override
    {
        hover_ = true;
        update();
        QWidget::enterEvent(ev);
    }

    void leaveEvent(QEvent *ev) override
    {
        hover_ = false;
        update();
        QWidget::leaveEvent(ev);
    }

    void mousePressEvent(QMouseEvent *) override { pressedOnce_ = true; }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (pressedOnce_ && onClick_)
            onClick_();
        pressedOnce_ = false;
    }

    void paintEvent(QPaintEvent *) override
    {
        const Colors &C = colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (checked_) {
            QColor bg(C.accent);
            bg.setAlpha(28);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRect(rect());
            // 선택 표시는 좌측 막대 하나. 전체를 물들이면 아이콘이 묻힌다.
            p.setBrush(QColor(C.accent));
            p.drawRect(QRectF(0, 0, 2.5, height()));
        } else if (hover_) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.surfaceHi));
            p.drawRect(rect());
        }

        const QColor fg(checked_ ? C.accent : (hover_ ? C.text : C.textDim));

        const QRectF iconRect((width() - kIconBox) / 2.0, 8, kIconBox, kIconBox);
        drawIcon(p, item_, iconRect, fg);

        QFont f;
        f.setPointSize(10);
        f.setWeight(checked_ ? QFont::DemiBold : QFont::Normal);
        p.setFont(f);
        p.setPen(fg);
        p.drawText(QRectF(0, 34, width(), 18), Qt::AlignCenter, label_);

        if (alerts_ > 0) {
            const double d = 15;
            const QRectF badge(width() / 2.0 + kIconBox / 2.0 - 3, 5, d, d);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.danger));
            p.drawEllipse(badge);
            QFont bf;
            bf.setPointSize(8);
            bf.setWeight(QFont::Bold);
            p.setFont(bf);
            p.setPen(QColor(C.textOnAccent));
            p.drawText(badge, Qt::AlignCenter,
                       alerts_ > 99 ? QStringLiteral("99+") : QString::number(alerts_));
        }
    }

public:
    std::function<void()> onClick_;

private:
    NavItem item_;
    QString label_;
    bool checked_ = false;
    bool hover_ = false;
    bool pressedOnce_ = false;
    int alerts_ = 0;
};

NavRail::NavRail(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("NavRail"));
    setFixedWidth(kRailWidth);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, metrics::s2, 0, metrics::s3);
    lay->setSpacing(0);

    for (const auto &spec : kItems) {
        auto *btn = new NavButton(spec.item, QString::fromUtf8(spec.label), this);
        btn->onClick_ = [this, item = spec.item] { setCurrent(item); emit navigated(item); };
        lay->addWidget(btn);
        buttons_ << btn;
        if (spec.item == NavItem::Diagnostics)
            diagnosticsButton_ = btn;
    }

    lay->addStretch(1);

    // ---- 상시 요약 ----
    // 어느 뷰에 있든 배터리와 위치는 보여야 한다. 주행 뷰로 돌아가서
    // 확인해야 한다면 그건 이미 늦은 경우가 많다.
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s3);

    battery_ = new BatteryRing(this, 58, 25.0);
    lay->addWidget(battery_, 0, Qt::AlignHCenter);

    lay->addSpacing(metrics::s2);
    pose_ = new QLabel(QStringLiteral("—"), this);
    pose_->setObjectName(QStringLiteral("Mono"));
    pose_->setAlignment(Qt::AlignCenter);
    pose_->setWordWrap(true);
    lay->addWidget(pose_);

    setCurrent(NavItem::Drive);
}

void NavRail::setCurrent(NavItem item)
{
    current_ = item;
    for (auto *b : std::as_const(buttons_))
        b->setChecked(b->item() == item);
}

void NavRail::setBattery(double socPercent)
{
    battery_->setState(socPercent);
}

void NavRail::setPoseText(const QString &text)
{
    pose_->setText(text);
}

void NavRail::setDiagnosticsAlerts(int count)
{
    if (diagnosticsButton_)
        diagnosticsButton_->setAlerts(count);
}

}  // namespace gcs::ui
