#include "panels/EventLogPanel.h"

#include <QJsonObject>

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
constexpr int kRoleDetail = Qt::UserRole + 4;

constexpr int kRowHeight = 54;

/// 상세 JSON 을 한 줄 요약으로 접는다. 중첩 객체는 값이 길어지기만 하고
/// 한 줄에서는 읽히지 않으므로 건너뛴다.
QString detailText(const QJsonObject &detail)
{
    QStringList parts;
    for (auto it = detail.constBegin(); it != detail.constEnd(); ++it) {
        const QJsonValue v = it.value();
        if (v.isObject() || v.isArray())
            continue;
        QString text = v.isDouble() ? QString::number(v.toDouble(), 'g', 6) : v.toVariant().toString();
        if (text.isEmpty())
            continue;
        parts << QStringLiteral("%1 %2").arg(it.key(), text);
        if (parts.size() >= 4)
            break;
    }
    return parts.join(QStringLiteral("   "));
}

constexpr int kIconSize = 16;
constexpr int kRightPad = 8;

/// 로그 목록에 찍는 등급 약어.
QString severityCode(Severity s)
{
    switch (s) {
    case Severity::Ok: return QStringLiteral("OK");
    case Severity::Warn: return QStringLiteral("WARN");
    case Severity::Error: return QStringLiteral("ERROR");
    case Severity::Critical: return QStringLiteral("CRIT");
    case Severity::Info: break;
    }
    return QStringLiteral("INFO");
}

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
        const bool loud = sev == Severity::Error || sev == Severity::Critical;

        if (opt.state & QStyle::State_MouseOver) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(C.surfaceHi));
            p->drawRoundedRect(r.adjusted(2, 1, -2, -1), metrics::rSm, metrics::rSm);
        }

        // 예전에는 행 왼쪽에 2 px 색 막대만 세웠다. 정보인지 오류인지
        // 구분되지 않으면서 목록만 어수선했다. 등급을 글자로 적고, 좁은
        // 열에 다 넣기 위해 두 줄로 쓴다.
        //
        //   ● 오류   MODE_AUTO  (i)              20:47:23
        //     자율 모드 전환                     map_id ...
        const int top = r.top() + 7;
        const int bottom = r.top() + 28;

        p->setPen(Qt::NoPen);
        p->setBrush(sev == Severity::Info ? QColor(C.textMute) : sc);
        p->drawEllipse(QPointF(r.left() + 12, top + 9), loud ? 4.0 : 3.0, loud ? 4.0 : 3.0);

        int x = r.left() + 22;

        QFont fl;
        fl.setPointSize(11);
        fl.setWeight(QFont::DemiBold);
        p->setFont(fl);
        p->setPen(sev == Severity::Info ? QColor(C.textMute) : sc);
        // 등급은 영문으로 적는다. 바로 옆 코드가 영문 대문자라 한글과
        // 섞이면 줄이 들쭉날쭉하고, INFO/WARN/ERROR 는 로그를 보는 사람에게
        // 이미 익은 낱말이다. 조작자용 한글 표기는 알림과 코드 설명에 있다.
        const QString level = severityCode(sev);
        const int levelW = QFontMetrics(fl).horizontalAdvance(level) + 8;
        p->drawText(QRect(x, top, levelW, 19), Qt::AlignLeft | Qt::AlignVCenter, level);
        x += levelW;

        // 시각은 오른쪽 끝에. 훑을 때 세로로 줄이 맞는 편이 읽기 쉽다.
        QFont fm = monoFont(11);
        p->setFont(fm);
        p->setPen(QColor(C.textMute));
        const QString time = idx.data(kRoleTime).toString();
        const int timeW = QFontMetrics(fm).horizontalAdvance(time) + 2;
        p->drawText(QRect(r.right() - kRightPad - timeW, top, timeW, 19),
                    Qt::AlignRight | Qt::AlignVCenter, time);

        const int firstLineRight = r.right() - kRightPad - timeW - 8;

        if (!code.isEmpty()) {
            QFont fc = monoFont(11);
            fc.setWeight(QFont::DemiBold);
            p->setFont(fc);
            p->setPen(QColor(C.textDim));
            const int codeW = qMin(QFontMetrics(fc).horizontalAdvance(code) + 8,
                                   qMax(0, firstLineRight - x));
            p->drawText(QRect(x, top, codeW, 19), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics(fc).elidedText(code, Qt::ElideRight, codeW));
            x += codeW;

            // (i) 아이콘 — 코드 바로 옆
            const QRect ic(x, top + 2, kIconSize, kIconSize);
            const bool hot = hoveredIconRow == idx.row();
            p->setPen(QPen(hot ? sc : QColor(C.textMute), 1.2));
            // 삼항으로 QColor 와 Qt::NoBrush 를 섞으면 안 된다.
            // Qt::NoBrush 는 값이 0 이라 QColor(Qt::color0) = 검정으로 변환되어
            // 테두리만 그리려던 원이 까맣게 채워진다.
            p->setBrush(hot ? QBrush(QColor(C.surfaceHover)) : QBrush(Qt::NoBrush));
            p->drawEllipse(ic);

            QFont fi;
            fi.setPointSize(10);
            fi.setWeight(QFont::Bold);
            p->setFont(fi);
            p->setPen(hot ? sc : QColor(C.textMute));
            p->drawText(ic, Qt::AlignCenter, QStringLiteral("i"));
        }

        // 둘째 줄 — 내용, 그리고 자리가 남으면 상세 값
        int detailW = 0;
        const QString detail = idx.data(kRoleDetail).toString();
        const int avail = r.right() - kRightPad - (r.left() + 22);
        if (!detail.isEmpty() && avail > 260) {
            QFont fd = monoFont(11);
            const QFontMetrics dm(fd);
            detailW = qMin(dm.horizontalAdvance(detail) + 12, avail / 2);
            p->setFont(fd);
            p->setPen(QColor(C.textMute));
            p->drawText(QRect(r.right() - kRightPad - detailW, bottom, detailW, 19),
                        Qt::AlignRight | Qt::AlignVCenter,
                        dm.elidedText(detail, Qt::ElideRight, detailW));
        }

        QFont ft;
        ft.setPointSize(12);
        ft.setWeight(loud ? QFont::DemiBold : QFont::Normal);
        p->setFont(ft);
        p->setPen(loud ? sc : QColor(C.text));
        const QRect textRect(r.left() + 22, bottom, avail - detailW, 19);
        if (textRect.width() > 20) {
            p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetrics(ft).elidedText(idx.data(kRoleMessage).toString(),
                                                    Qt::ElideRight, textRect.width()));
        }
        p->restore();
    }
};

/// 델리게이트가 그린 (i) 아이콘의 실제 사각형을 뷰에서 다시 계산한다.
/// 아이콘이 코드 문자열 폭에 따라 움직이므로 그때 쓴 폰트를 그대로 쓴다.
QRect iconRectFor(const QRect &itemRect, const QString &time, const QString &code,
                  const QString &level)
{
    if (code.isEmpty())
        return {};
    QFont fm = monoFont(11);
    QFont fc = monoFont(11);
    fc.setWeight(QFont::DemiBold);

    Q_UNUSED(time);
    QFont fl;
    fl.setPointSize(11);
    fl.setWeight(QFont::DemiBold);

    int x = itemRect.left() + 22;
    x += QFontMetrics(fl).horizontalAdvance(level) + 8;
    x += QFontMetrics(fc).horizontalAdvance(code) + 8;
    return QRect(x, itemRect.top() + 8, kIconSize, kIconSize);
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
        const QRect ic = iconRectFor(
            visualRect(idx), idx.data(kRoleTime).toString(), code,
            severityCode(Severity(idx.data(kRoleSeverity).toInt())));
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
    filter_->setFixedWidth(112);
    bar->addWidget(filter_);

    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("검색어를 입력하십시오"));
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
    it->setData(kRoleDetail, detailText(e.detail));
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
        QStringLiteral("%1/inspection_log_%2.jsonl")
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
