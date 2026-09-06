#include "widgets/NotificationCenter.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

constexpr int kBellW = 34;
constexpr int kBellH = 30;
constexpr int kPopupW = 340;

QColor severityColor(const QString &severity)
{
    const Colors &C = colors();
    if (severity == QLatin1String("ok"))
        return QColor(C.success);
    if (severity == QLatin1String("warn"))
        return QColor(C.warning);
    if (severity == QLatin1String("error") || severity == QLatin1String("critical"))
        return QColor(C.danger);
    return QColor(C.accent);
}

}  // namespace

NotificationBell::NotificationBell(QWidget *parent) : QWidget(parent)
{
    setFixedSize(kBellW, kBellH);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("알림 — 지나간 알림을 다시 확인합니다"));
}

void NotificationBell::add(const Notification &n)
{
    items_.prepend(n);
    while (items_.size() > kMaxKept)
        items_.removeLast();
    ++unread_;
    update();
}

void NotificationBell::enterEvent(QEnterEvent *ev)
{
    hover_ = true;
    update();
    QWidget::enterEvent(ev);
}

void NotificationBell::leaveEvent(QEvent *ev)
{
    hover_ = false;
    update();
    QWidget::leaveEvent(ev);
}

void NotificationBell::mousePressEvent(QMouseEvent *)
{
    if (popup_ && popup_->isVisible()) {
        popup_->close();
        return;
    }
    openPopup();
}

void NotificationBell::openPopup()
{
    // 목록을 열었다는 것이 곧 확인했다는 뜻이다. 시간이 지났다고 저절로
    // 지워지면 자리를 비운 사이의 알림을 놓친다.
    unread_ = 0;
    update();

    delete popup_;
    popup_ = new QWidget(nullptr, Qt::Popup);
    popup_->setObjectName(QStringLiteral("Card"));
    popup_->setAttribute(Qt::WA_DeleteOnClose, false);

    auto *lay = new QVBoxLayout(popup_);
    lay->setContentsMargins(metrics::s3, metrics::s3, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s2);

    auto *head = new QLabel(items_.isEmpty()
                                ? QStringLiteral("알림 없음")
                                : QStringLiteral("최근 알림 %1건").arg(items_.size()));
    head->setObjectName(QStringLiteral("Hint"));
    lay->addWidget(head);

    if (items_.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("아직 표시된 알림이 없습니다."));
        empty->setWordWrap(true);
        lay->addWidget(empty);
    } else {
        auto *inner = new QWidget;
        auto *list = new QVBoxLayout(inner);
        list->setContentsMargins(0, 0, 0, 0);
        list->setSpacing(metrics::s2);

        for (const auto &n : std::as_const(items_)) {
            auto *row = new QLabel(
                QStringLiteral("<span style='color:%1'>●</span>  "
                               "<b>%2</b>  <span style='color:%3'>%4</span>%5")
                    .arg(severityColor(n.severity).name(),
                         n.title.toHtmlEscaped(),
                         QString(colors().textMute),
                         n.at.toString(QStringLiteral("HH:mm:ss")),
                         n.detail.isEmpty()
                             ? QString()
                             : QStringLiteral("<br><span style='color:%1'>%2</span>")
                                   .arg(QString(colors().textDim), n.detail.toHtmlEscaped())));
            row->setTextFormat(Qt::RichText);
            row->setWordWrap(true);
            list->addWidget(row);
        }

        auto *scroll = new QScrollArea;
        scroll->setWidget(inner);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMaximumHeight(360);
        lay->addWidget(scroll);
    }

    popup_->setFixedWidth(kPopupW);
    popup_->adjustSize();

    // 종 바로 아래, 오른쪽 끝을 맞춰 연다. 창 밖으로 나가지 않게 민다.
    QPoint at = mapToGlobal(QPoint(width() - kPopupW, height() + metrics::s1));
    at.setX(qMax(8, at.x()));
    popup_->move(at);
    popup_->show();
}

void NotificationBell::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (hover_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(C.surfaceHover));
        p.drawRoundedRect(rect(), metrics::rMd, metrics::rMd);
    }

    const double cx = width() / 2.0;
    const double cy = height() / 2.0 - 1;

    // 종 — 몸통 하나와 아래 선, 손잡이. 글꼴 이모지를 쓰면 플랫폼마다
    // 모양과 색이 달라져 납품물이 같아 보이지 않는다.
    QPainterPath bell;
    bell.moveTo(cx - 5.5, cy + 4);
    bell.cubicTo(cx - 5.5, cy - 2, cx - 4.5, cy - 5.5, cx, cy - 5.5);
    bell.cubicTo(cx + 4.5, cy - 5.5, cx + 5.5, cy - 2, cx + 5.5, cy + 4);

    p.setPen(QPen(QColor(unread_ > 0 ? C.text : C.textDim), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bell);
    p.drawLine(QPointF(cx - 7, cy + 4), QPointF(cx + 7, cy + 4));
    p.drawLine(QPointF(cx - 1.6, cy + 6.6), QPointF(cx + 1.6, cy + 6.6));

    if (unread_ <= 0)
        return;

    // 안 읽은 건수. 두 자리를 넘으면 폭이 흔들리므로 9+ 로 자른다.
    const QString text = unread_ > 9 ? QStringLiteral("9+") : QString::number(unread_);
    QFont f;
    f.setPointSize(7);
    f.setWeight(QFont::Bold);
    p.setFont(f);

    const double w = text.size() > 1 ? 15.0 : 12.0;
    const QRectF dot(width() - w - 1, 1, w, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.danger));
    p.drawRoundedRect(dot, 6, 6);
    p.setPen(QColor(QLatin1String("#FFFFFF")));
    p.drawText(dot, Qt::AlignCenter, text);
}

}  // namespace gcs::ui
