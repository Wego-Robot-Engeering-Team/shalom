// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/WaypointPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "theme/Style.h"
#include "widgets/Primitives.h"
#include "widgets/WaypointDelegate.h"

namespace hmi::ui {

using namespace hmi::theme;

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

    card_ = new Card(QStringLiteral("Waypoints"));
    count_ = new Badge(QStringLiteral("0"), QStringLiteral("neutral"));
    card_->addHeaderWidget(count_);
    outer->addWidget(card_);

    list_ = new QListWidget;
    list_->setItemDelegate(new WaypointDelegate(list_));
    list_->setDragDropMode(QAbstractItemView::NoDragDrop);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMouseTracking(true);
    card_->body()->addWidget(list_, 1);

    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur) {
                updateActionButtons();
                if (cur)
                    emit waypointSelected(
                        cur->data(kWaypointRole).toMap().value(QStringLiteral("id")).toString());
            });

    // 두 등록 방법은 같은 웨이포인트 목록에 저장된다. 다른 카드에 같은
    // "지도에서 추가" 버튼을 두면 어느 목록에 들어가는지 헷갈린다.
    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(metrics::s2);
    fromRobot_ = makeButton(QStringLiteral("로봇 위치로 추가"));
    fromRobot_->setObjectName(QStringLiteral("WaypointFromRobotButton"));
    fromRobot_->setProperty("variant", "primary");
    add_ = makeButton(QStringLiteral("지도에서 추가"));
    add_->setObjectName(QStringLiteral("WaypointFromMapButton"));
    add_->setToolTip(QStringLiteral("지도를 클릭해 위치를, 드래그해 도착 방향을 지정합니다"));
    addRow->addWidget(fromRobot_, 1);
    addRow->addWidget(add_, 1);
    card_->body()->addLayout(addRow);

    // ---- 목록 편집 및 이동 ----
    auto *edit = new QHBoxLayout;
    edit->setSpacing(metrics::s2);
    edit_ = makeButton(QStringLiteral("편집"));
    edit_->setObjectName(QStringLiteral("WaypointEditButton"));
    delete_ = makeButton(QStringLiteral("삭제"));
    go_ = makeButton(QStringLiteral("선택 위치로 이동"));
    for (auto *b : {fromRobot_, add_, edit_, delete_, go_})
        b->setProperty("size", "sm");
    edit->addWidget(edit_, 1);
    edit->addWidget(delete_, 1);
    edit->addWidget(go_, 2);
    card_->body()->addLayout(edit);

    saveStatus_ = new QLabel;
    saveStatus_->setObjectName(QStringLiteral("WaypointSaveStatus"));
    saveStatus_->setWordWrap(true);
    saveStatus_->hide();
    card_->body()->addWidget(saveStatus_);

    connect(add_, &QPushButton::clicked, this, &WaypointPanel::addRequested);
    connect(fromRobot_, &QPushButton::clicked, this,
            &WaypointPanel::captureFromRobotRequested);
    connect(edit_, &QPushButton::clicked, this, [this] {
        if (const auto *it = list_->currentItem())
            emit editRequested(it->data(kWaypointRole).toMap()
                                   .value(QStringLiteral("id")).toString());
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        if (edit_->isEnabled())
            emit editRequested(it->data(kWaypointRole).toMap()
                                   .value(QStringLiteral("id")).toString());
    });
    connect(delete_, &QPushButton::clicked, this, [this] {
        if (auto *it = list_->currentItem())
            emit deleteRequested(it->data(kWaypointRole).toMap()
                                     .value(QStringLiteral("id")).toString());
    });
    connect(go_, &QPushButton::clicked, this, [this] {
        if (const auto *it = list_->currentItem())
            emit gotoRequested(it->data(kWaypointRole).toMap()
                                   .value(QStringLiteral("id")).toString());
    });
    updateActionButtons();
}

void WaypointPanel::setEditingEnabled(bool enabled)
{
    editingEnabled_ = enabled;
    updateActionButtons();
}

void WaypointPanel::setRobotPoseAvailable(bool available, const QString &reason)
{
    robotPoseAvailable_ = available;
    fromRobot_->setToolTip(available
        ? QStringLiteral("현재 로봇 위치와 방향을 웨이포인트로 저장합니다") : reason);
    updateActionButtons();
}

void WaypointPanel::updateActionButtons()
{
    add_->setEnabled(editingEnabled_);
    fromRobot_->setEnabled(editingEnabled_ && robotPoseAvailable_);
    const bool selected = editingEnabled_ && list_->currentItem();
    edit_->setEnabled(selected);
    delete_->setEnabled(selected);
    go_->setEnabled(selected);
}

void WaypointPanel::setSaveStatus(const QString &message, bool error)
{
    saveStatus_->setText(message);
    saveStatus_->setProperty("tone", error ? "danger" : "info");
    theme::repolish(saveStatus_);
    saveStatus_->setVisible(!message.isEmpty());
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
    updateActionButtons();
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


}  // namespace hmi::ui
