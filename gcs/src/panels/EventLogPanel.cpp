#include "panels/EventLogPanel.h"

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolTip>
#include <QVBoxLayout>

#include "diag/CodeInfoPopup.h"
#include "diag/LogStore.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::diag::CodeInfoPopup;
using gcs::diag::LogEntry;
using gcs::diag::LogStore;
using gcs::diag::Severity;

namespace {

// 아이템 데이터 롤. LogEntry 를 통째로 QVariant 에 담으려면 메타타입 등록이
// 필요한데, 표시에 쓰는 네 값만 있으면 충분하다.
constexpr int kRoleTime = Qt::UserRole;
constexpr int kRoleSeverity = Qt::UserRole + 1;
constexpr int kRoleCode = Qt::UserRole + 2;
constexpr int kRoleMessage = Qt::UserRole + 3;

constexpr int kRowHeight = 32;
constexpr int kIconSize = 14;
constexpr int kRightPad = 8;

QColor severityColor(Severity s)
{
    const Colors &C = colors();
    switch (s) {
    case Severity::Ok: return QColor(C.success);
    case Severity::Warn: return QColor(C.warning);
    case Severity::Error:
    case Severity::Critical: return QColor(C.danger);
    case Severity::Info: break;
    }
    return QColor(C.textDim);
}

class LogDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    /// 뷰가 호버 중인 행을 알려준다. -1 이면 없음.
    int hoveredIconRow = -1;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {0, kRowHeight};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const Colors &C = colors();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);

        const QRect r = opt.rect;
        const auto sev = Severity(idx.data(kRoleSeverity).toInt());
        const QString code = idx.data(kRoleCode).toString();
        const QColor sc = severityColor(sev);

        if (opt.state & QStyle::State_MouseOver) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(C.surfaceHi));
            p->drawRect(r);
        }

        // 좌측 심각도 막대 하나로 레벨을 표시한다. 배경 전체를 물들이면
        // 오류가 몇 건 쌓였을 때 화면이 읽히지 않는다.
        p->setPen(Qt::NoPen);
        p->setBrush(sc);
        p->drawRect(QRectF(r.left(), r.top() + 5, 2, r.height() - 10));

        int x = r.left() + 10;

        // 시각
        QFont fm(monoFamily());
        fm.setPointSize(9);
        p->setFont(fm);
        p->setPen(QColor(C.textMute));
        const QString time = idx.data(kRoleTime).toString();
        const int timeW = QFontMetrics(fm).horizontalAdvance(time) + 10;
        p->drawText(QRect(x, r.top(), timeW, r.height()),
                    Qt::AlignLeft | Qt::AlignVCenter, time);
        x += timeW;

        // 코드 (있을 때). 행의 주인공이므로 등폭 + 심각도 색.
        if (!code.isEmpty()) {
            QFont fc(monoFamily());
            fc.setPointSize(10);
            fc.setWeight(QFont::DemiBold);
            p->setFont(fc);
            p->setPen(sc);
            const int codeW = QFontMetrics(fc).horizontalAdvance(code) + 8;
            p->drawText(QRect(x, r.top(), codeW, r.height()),
                        Qt::AlignLeft | Qt::AlignVCenter, code);
            x += codeW;

            // (i) 아이콘 — 코드 바로 옆
            const QRect ic(x, r.center().y() - kIconSize / 2, kIconSize, kIconSize);
            const bool hot = hoveredIconRow == idx.row();
            p->setPen(QPen(hot ? sc : QColor(C.textMute), 1.2));
            // 삼항으로 QColor 와 Qt::NoBrush 를 섞으면 안 된다.
            // Qt::NoBrush 는 값이 0 이라 QColor(Qt::color0) = 검정으로 변환되어
            // 테두리만 그리려던 원이 까맣게 채워진다.
            p->setBrush(hot ? QBrush(QColor(C.surfaceHover)) : QBrush(Qt::NoBrush));
            p->drawEllipse(ic);

            QFont fi;
            fi.setPointSize(8);
            fi.setWeight(QFont::Bold);
            p->setFont(fi);
            p->setPen(hot ? sc : QColor(C.textMute));
            p->drawText(ic, Qt::AlignCenter, QStringLiteral("i"));
            x = ic.right() + 10;
        }

        // 제목 — 남은 폭 전부
        QFont ft;
        ft.setPointSize(10);
        p->setFont(ft);
        p->setPen(QColor(C.text));
        const QRect textRect(x, r.top(), r.right() - kRightPad - x, r.height());
        if (textRect.width() > 20) {
            const QString msg = QFontMetrics(ft).elidedText(
                idx.data(kRoleMessage).toString(), Qt::ElideRight, textRect.width());
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, msg);
        }
        p->restore();
    }
};

/// 델리게이트가 그린 (i) 아이콘의 실제 사각형을 뷰에서 다시 계산한다.
/// 아이콘이 코드 문자열 폭에 따라 움직이므로 그때 쓴 폰트를 그대로 쓴다.
QRect iconRectFor(const QRect &itemRect, const QString &time, const QString &code)
{
    if (code.isEmpty())
        return {};
    QFont fm(monoFamily());
    fm.setPointSize(9);
    QFont fc(monoFamily());
    fc.setPointSize(10);
    fc.setWeight(QFont::DemiBold);

    int x = itemRect.left() + 10;
    x += QFontMetrics(fm).horizontalAdvance(time) + 10;
    x += QFontMetrics(fc).horizontalAdvance(code) + 8;
    return QRect(x, itemRect.center().y() - kIconSize / 2, kIconSize, kIconSize);
}

/// (i) 아이콘의 호버/클릭을 처리하는 리스트.
class LogList : public QListWidget {
public:
    explicit LogList(QWidget *parent = nullptr) : QListWidget(parent)
    {
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
        delegate_ = new LogDelegate(this);
        setItemDelegate(delegate_);
        setSelectionMode(QAbstractItemView::NoSelection);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setUniformItemSizes(true);
    }

protected:
    void mouseMoveEvent(QMouseEvent *ev) override
    {
        const int row = iconRowAt(ev->pos());
        if (row != delegate_->hoveredIconRow) {
            delegate_->hoveredIconRow = row;
            viewport()->setCursor(row >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            viewport()->update();
        }
        QListWidget::mouseMoveEvent(ev);
    }

    void leaveEvent(QEvent *ev) override
    {
        if (delegate_->hoveredIconRow != -1) {
            delegate_->hoveredIconRow = -1;
            viewport()->update();
        }
        QListWidget::leaveEvent(ev);
    }

    void mousePressEvent(QMouseEvent *ev) override
    {
        const int row = iconRowAt(ev->pos());
        if (row >= 0) {
            CodeInfoPopup::showFor(item(row)->data(kRoleCode).toString(),
                                   ev->globalPosition().toPoint(), this);
            return;   // 행 선택으로 넘기지 않는다
        }
        QListWidget::mousePressEvent(ev);
    }

    bool viewportEvent(QEvent *ev) override
    {
        if (ev->type() == QEvent::ToolTip) {
            auto *he = static_cast<QHelpEvent *>(ev);
            const int row = iconRowAt(he->pos());
            if (row >= 0) {
                QToolTip::showText(he->globalPos(),
                                   CodeInfoPopup::tooltipHtml(
                                       item(row)->data(kRoleCode).toString()),
                                   this);
            } else {
                QToolTip::hideText();
            }
            return true;
        }
        return QListWidget::viewportEvent(ev);
    }

private:
    int iconRowAt(const QPoint &pos) const
    {
        const QModelIndex idx = indexAt(pos);
        if (!idx.isValid())
            return -1;
        const QString code = idx.data(kRoleCode).toString();
        if (code.isEmpty())
            return -1;
        const QRect ic = iconRectFor(visualRect(idx), idx.data(kRoleTime).toString(), code);
        return ic.adjusted(-3, -3, 3, 3).contains(pos) ? idx.row() : -1;
    }

    LogDelegate *delegate_ = nullptr;
};

}  // namespace

EventLogPanel::EventLogPanel(LogStore *store, QWidget *parent)
    : QWidget(parent), store_(store)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("이벤트 로그"));
    badge_ = new Badge(QStringLiteral("0"), QStringLiteral("neutral"));
    card_->addHeaderWidget(badge_);
    outer->addWidget(card_);

    // ---- 필터 줄 ----
    auto *bar = new QHBoxLayout;
    bar->setSpacing(metrics::s2);

    filter_ = new QComboBox;
    filter_->addItem(QStringLiteral("전체"), int(Severity::Info));
    filter_->addItem(QStringLiteral("주의 이상"), int(Severity::Warn));
    filter_->addItem(QStringLiteral("오류 이상"), int(Severity::Error));
    filter_->setFixedWidth(96);
    bar->addWidget(filter_);

    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("코드 또는 내용 검색"));
    search_->setClearButtonEnabled(true);
    bar->addWidget(search_, 1);

    auto *btnExport = new QPushButton(QStringLiteral("내보내기"));
    btnExport->setProperty("size", "sm");
    btnExport->setToolTip(QStringLiteral("현장 진단용 JSONL 로그 저장"));
    bar->addWidget(btnExport);
    card_->body()->addLayout(bar);

    list_ = new LogList;
    card_->body()->addWidget(list_, 1);

    connect(filter_, &QComboBox::currentIndexChanged, this, &EventLogPanel::rebuild);
    connect(search_, &QLineEdit::textChanged, this, &EventLogPanel::rebuild);
    connect(btnExport, &QPushButton::clicked, this, &EventLogPanel::exportLog);
    if (store_) {
        connect(store_, &LogStore::appended, this, &EventLogPanel::onAppended);
        connect(store_, &LogStore::cleared, this, &EventLogPanel::rebuild);
    }
    rebuild();
}

void EventLogPanel::addRow(const LogEntry &e)
{
    auto *it = new QListWidgetItem;
    it->setData(kRoleTime, e.timeText());
    it->setData(kRoleSeverity, int(e.severity));
    it->setData(kRoleCode, e.code);
    it->setData(kRoleMessage, e.message);
    list_->insertItem(0, it);   // 최신이 위
}

void EventLogPanel::onAppended(const LogEntry &e)
{
    const auto minSev = Severity(filter_->currentData().toInt());
    if (int(e.severity) < int(minSev))
        return;
    const QString q = search_->text();
    if (!q.isEmpty() && !e.message.contains(q, Qt::CaseInsensitive)
        && !e.code.contains(q, Qt::CaseInsensitive))
        return;
    addRow(e);
    updateBadge();
}

void EventLogPanel::rebuild()
{
    if (!store_)
        return;
    list_->clear();
    const auto rows = store_->query(Severity(filter_->currentData().toInt()), search_->text());
    for (const auto &e : rows)
        addRow(e);
    updateBadge();
}

void EventLogPanel::updateBadge()
{
    if (!store_)
        return;
    const int errors = store_->countAtOrAbove(Severity::Error);
    const int warns = store_->countAtOrAbove(Severity::Warn);
    if (errors > 0)
        badge_->set(QString::number(errors), QStringLiteral("danger"));
    else if (warns > 0)
        badge_->set(QString::number(warns), QStringLiteral("warn"));
    else
        badge_->set(QString::number(store_->entries().size()), QStringLiteral("neutral"));
}

void EventLogPanel::exportLog()
{
    if (!store_)
        return;
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested =
        QStringLiteral("%1/shalom_log_%2.jsonl")
            .arg(dir, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("로그 내보내기"), suggested,
        QStringLiteral("JSON Lines (*.jsonl)"));
    if (path.isEmpty())
        return;

    QString err;
    if (store_->exportJsonl(path, &err))
        store_->note(Severity::Ok, QStringLiteral("로그 내보내기 완료: %1").arg(path));
    else
        QMessageBox::warning(this, QStringLiteral("내보내기 실패"), err);
}

}  // namespace gcs::ui
