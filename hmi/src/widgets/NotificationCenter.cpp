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

namespace hmi::ui {

using namespace hmi::theme;

namespace {

constexpr int kBellW = 38;
constexpr int kBellH = 34;
constexpr int kPopupW = 348;

constexpr int kRadius = 10;

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

/// 한 건을 그리는 줄. 점 · 제목 · 시각 · 상세를 직접 그린다.
///
/// 리치 텍스트 라벨로 만들면 줄 간격과 색이 플랫폼 기본 스타일에 끌려다녀
/// 목록이 딱딱해 보인다. 여기서는 여백과 색을 전부 토큰으로 잡는다.
class NotificationRow : public QWidget {
public:
    explicit NotificationRow(const Notification &n) : n_(n)
    {
        QFont fb;
        fb.setPointSize(10);
        const QFontMetrics bm(fb);
        detailH_ = n_.detail.isEmpty()
                       ? 0
                       : bm.boundingRect(0, 0, kPopupW - 62, 400, Qt::TextWordWrap,
                                         n_.detail).height() + 3;
        setFixedHeight(24 + detailH_ + 10);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Colors &C = colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.surfaceHi));
            p.drawRoundedRect(rect(), metrics::rMd, metrics::rMd);
        }

        const QColor tone = severityColor(n_.severity);
        p.setPen(Qt::NoPen);
        p.setBrush(tone);
        p.drawEllipse(QPointF(14, 15), 3.5, 3.5);

        QFont ft;
        ft.setPointSize(10);
        ft.setWeight(QFont::DemiBold);
        p.setFont(ft);

        QFont fs = monoFont(9);
        const int timeW = QFontMetrics(fs).horizontalAdvance(QStringLiteral("00:00:00")) + 4;

        const int textX = 28;
        const int textW = width() - textX - timeW - 12;
        p.setPen(QColor(C.text));
        p.drawText(QRect(textX, 6, textW, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(ft).elidedText(n_.title, Qt::ElideRight, textW));

        p.setFont(fs);
        p.setPen(QColor(C.textMute));
        p.drawText(QRect(width() - timeW - 12, 6, timeW, 18), Qt::AlignRight | Qt::AlignVCenter,
                   n_.at.toString(QStringLiteral("HH:mm:ss")));

        if (n_.detail.isEmpty())
            return;

        QFont fd;
        fd.setPointSize(10);
        p.setFont(fd);
        p.setPen(QColor(C.textDim));
        p.drawText(QRect(textX, 25, width() - textX - 12, detailH_),
                   Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, n_.detail);
    }

    void enterEvent(QEnterEvent *ev) override { update(); QWidget::enterEvent(ev); }
    void leaveEvent(QEvent *ev) override { update(); QWidget::leaveEvent(ev); }

private:
    Notification n_;
    int detailH_ = 0;
};

}  // namespace

// ========================== NotificationPopup ==========================

NotificationPopup::NotificationPopup(const QList<Notification> &items)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    // 반투명 창(WA_TranslucentBackground)은 쓰지 않는다. 컴포지터가 없는
    // 우분투 세션에서는 투명 영역이 검게 칠해져, 부드러운 그림자를 그리려던
    // 여백이 오히려 검은 테두리로 보인다. 불투명하게 칠하고 모서리는
    // 마스크로 깎는다 — 세 플랫폼에서 같은 그림이 나온다.
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s3, metrics::s3, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s2);

    auto *head = new QLabel(items.isEmpty()
                                ? QStringLiteral("알림")
                                : QStringLiteral("알림  %1건").arg(items.size()));
    head->setObjectName(QStringLiteral("SectionLabel"));
    lay->addWidget(head);

    if (items.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("아직 지나간 알림이 없습니다."));
        empty->setObjectName(QStringLiteral("Hint"));
        empty->setWordWrap(true);
        lay->addWidget(empty);
    } else {
        auto *inner = new QWidget;
        auto *list = new QVBoxLayout(inner);
        list->setContentsMargins(0, 0, 0, 0);
        list->setSpacing(1);

        int wanted = 0;
        for (const auto &n : items) {
            auto *row = new NotificationRow(n);
            list->addWidget(row);
            wanted += row->height() + 1;
        }

        auto *scroll = new QScrollArea;
        scroll->setWidget(inner);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(QStringLiteral("background: transparent;"));
        scroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
        scroll->setFixedHeight(qMin(wanted, kMaxListHeight));
        lay->addWidget(scroll);
    }

    setFixedWidth(kPopupW);
}

void NotificationPopup::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);

    // 둥근 모서리는 마스크로 만든다. 마스크는 컴포지터가 없어도 동작한다.
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), kRadius, kRadius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void NotificationPopup::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 테두리는 창틀이 아니라 테마의 얇은 선이다. 예전에는 플랫폼 창틀이
    // 그대로 나와 검은 자를 대 놓은 것처럼 보였다.
    const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(QColor(C.borderHi), 1));
    p.setBrush(QColor(C.surface));
    p.drawRoundedRect(card, kRadius, kRadius);
}

// =========================== NotificationBell ===========================

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
    emit opened();

    delete popup_;
    popup_ = new NotificationPopup(items_);
    popup_->adjustSize();

    // 종 바로 아래, 오른쪽 끝을 맞춰 연다.
    QPoint at = mapToGlobal(QPoint(width() - popup_->width(), height() + metrics::s1));
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
    bell.moveTo(cx - 6.5, cy + 4.6);
    bell.cubicTo(cx - 6.5, cy - 2.4, cx - 5.3, cy - 6.4, cx, cy - 6.4);
    bell.cubicTo(cx + 5.3, cy - 6.4, cx + 6.5, cy - 2.4, cx + 6.5, cy + 4.6);

    p.setPen(QPen(QColor(unread_ > 0 ? C.text : C.textDim), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bell);
    p.drawLine(QPointF(cx - 8.2, cy + 4.6), QPointF(cx + 8.2, cy + 4.6));
    p.drawLine(QPointF(cx - 1.9, cy + 7.5), QPointF(cx + 1.9, cy + 7.5));

    if (unread_ <= 0)
        return;

    // 안 읽은 건수. 두 자리를 넘으면 폭이 흔들리므로 9+ 로 자른다.
    const QString text = unread_ > 9 ? QStringLiteral("9+") : QString::number(unread_);
    QFont f;
    f.setPointSize(8);
    f.setWeight(QFont::Bold);
    p.setFont(f);

    const double w = text.size() > 1 ? 17.0 : 14.0;
    const QRectF dot(width() - w - 1, 1, w, 14);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.danger));
    p.drawRoundedRect(dot, 6, 6);
    p.setPen(QColor(QLatin1String("#FFFFFF")));
    p.drawText(dot, Qt::AlignCenter, text);
}

}  // namespace hmi::ui
