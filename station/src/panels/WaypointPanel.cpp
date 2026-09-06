#include "panels/WaypointPanel.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"
#include "widgets/WaypointDelegate.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

QPushButton *makeButton(const QString &text, int width = 0)
{
    auto *b = new QPushButton(text);
    if (width > 0)
        b->setFixedWidth(width);
    return b;
}

}  // namespace

WaypointPanel::WaypointPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("점검포인트 순서"));
    count_ = new Badge(QStringLiteral("0"), QStringLiteral("neutral"));
    card_->addHeaderWidget(count_);
    outer->addWidget(card_);

    list_ = new QListWidget;
    list_->setItemDelegate(new WaypointDelegate(list_));
    list_->setDragDropMode(QAbstractItemView::InternalMove);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMouseTracking(true);
    card_->body()->addWidget(list_, 1);

    connect(list_->model(), &QAbstractItemModel::rowsMoved, this,
            [this] { emitOrder(); });
    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur) {
                if (cur)
                    emit waypointSelected(
                        cur->data(kWaypointRole).toMap().value(QStringLiteral("id")).toString());
            });

    // ---- 편집 ----
    auto *edit = new QHBoxLayout;
    edit->setSpacing(metrics::s2);
    auto *add = makeButton(QStringLiteral("지도에서 추가"));
    auto *del = makeButton(QStringLiteral("삭제"));
    auto *up = makeButton(QStringLiteral("↑"), 36);
    auto *down = makeButton(QStringLiteral("↓"), 36);
    for (auto *b : {add, del, up, down})
        b->setProperty("size", "sm");
    edit->addWidget(add, 1);
    edit->addWidget(del);
    edit->addWidget(up);
    edit->addWidget(down);
    card_->body()->addLayout(edit);

    connect(add, &QPushButton::clicked, this, &WaypointPanel::addRequested);
    connect(del, &QPushButton::clicked, this, [this] {
        if (auto *it = list_->currentItem())
            emit deleteRequested(it->data(kWaypointRole).toMap()
                                     .value(QStringLiteral("id")).toString());
    });
    connect(up, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down, &QPushButton::clicked, this, [this] { move(1); });

    // ---- 미션 제어 ----
    auto *run = new QHBoxLayout;
    run->setSpacing(metrics::s2);
    start_ = makeButton(QStringLiteral("자율주행 시작"));
    start_->setProperty("variant", "primary");
    pause_ = makeButton(QStringLiteral("일시정지"));
    resume_ = makeButton(QStringLiteral("재개"));
    stop_ = makeButton(QStringLiteral("정지"));
    run->addWidget(start_, 2);
    run->addWidget(pause_, 1);
    run->addWidget(resume_, 1);
    run->addWidget(stop_, 1);
    card_->body()->addLayout(run);

    connect(start_, &QPushButton::clicked, this, &WaypointPanel::missionStart);
    connect(pause_, &QPushButton::clicked, this, &WaypointPanel::missionPause);
    connect(resume_, &QPushButton::clicked, this, &WaypointPanel::missionResume);
    connect(stop_, &QPushButton::clicked, this, &WaypointPanel::missionStop);

    setMissionState(false, false);
}

void WaypointPanel::setWaypoints(const QList<QVariantMap> &waypoints)
{
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const auto &wp : waypoints) {
        auto *it = new QListWidgetItem;
        it->setData(kWaypointRole, wp);
        list_->addItem(it);
    }
    count_->setText(QString::number(waypoints.size()));
    emit waypointsChanged(waypoints);
}

QList<QVariantMap> WaypointPanel::waypoints() const
{
    QList<QVariantMap> out;
    out.reserve(list_->count());
    for (int i = 0; i < list_->count(); ++i)
        out << list_->item(i)->data(kWaypointRole).toMap();
    return out;
}

void WaypointPanel::setStatus(const QString &id, const QString &status)
{
    for (int i = 0; i < list_->count(); ++i) {
        auto *it = list_->item(i);
        QVariantMap d = it->data(kWaypointRole).toMap();
        if (d.value(QStringLiteral("id")).toString() != id)
            continue;
        if (d.value(QStringLiteral("status")).toString() == status)
            return;                       // 불필요한 갱신은 건너뛴다
        d[QStringLiteral("status")] = status;
        it->setData(kWaypointRole, d);
        list_->update(list_->indexFromItem(it));
        emit waypointsChanged(waypoints());
        return;
    }
}

void WaypointPanel::setMissionState(bool running, bool paused)
{
    start_->setEnabled(!running);
    pause_->setEnabled(running && !paused);
    resume_->setEnabled(running && paused);
    stop_->setEnabled(running);
}

void WaypointPanel::move(int delta)
{
    const int row = list_->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= list_->count())
        return;
    auto *it = list_->takeItem(row);
    list_->insertItem(target, it);
    list_->setCurrentRow(target);
    emitOrder();
}

void WaypointPanel::emitOrder()
{
    QStringList ids;
    for (const auto &w : waypoints())
        ids << w.value(QStringLiteral("id")).toString();
    emit orderChanged(ids);
}

}  // namespace gcs::ui
