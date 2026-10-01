// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/WaypointPanel.h"

#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

#include "theme/Tokens.h"
#include "theme/Style.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
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
    list_->setDragDropMode(QAbstractItemView::NoDragDrop);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMouseTracking(true);
    card_->body()->addWidget(list_, 1);

    connect(list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur) {
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

    for (auto *b : {fromRobot_, add_})
        b->setProperty("size", "sm");

    saveStatus_ = new QLabel;
    saveStatus_->setObjectName(QStringLiteral("WaypointSaveStatus"));
    saveStatus_->setWordWrap(true);
    saveStatus_->hide();
    card_->body()->addWidget(saveStatus_);

    connect(add_, &QPushButton::clicked, this, &WaypointPanel::addRequested);
    connect(fromRobot_, &QPushButton::clicked, this,
            &WaypointPanel::captureFromRobotRequested);
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
    for (int i = 0; i < list_->count(); ++i) {
        auto *row = static_cast<CatalogRow *>(list_->itemWidget(list_->item(i)));
        if (!row)
            continue;
        row->setPending(!editingEnabled_);
        row->setEditEnabled(editingEnabled_);
        row->setApplyEnabled(editingEnabled_);
    }
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
    QHash<QString, QVariantMap> drafts;
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        auto *row = static_cast<CatalogRow *>(list_->itemWidget(item));
        if (!row || !row->property("draftActive").toBool())
            continue;
        const QString editingId = item->data(kWaypointRole).toMap()
                        .value(QStringLiteral("id")).toString();
        drafts[editingId] = {{QStringLiteral("name"), row->findChild<QLineEdit *>(
                      QStringLiteral("WaypointRowName"))->text()},
                 {QStringLiteral("x"), row->findChild<QDoubleSpinBox *>(
                      QStringLiteral("WaypointRowX"))->value()},
                 {QStringLiteral("y"), row->findChild<QDoubleSpinBox *>(
                      QStringLiteral("WaypointRowY"))->value()},
                 {QStringLiteral("yaw"), row->findChild<QDoubleSpinBox *>(
                      QStringLiteral("WaypointRowYaw"))->value()},
                 {"base", row->property("editBase")}, {"editing", row->isEditing()},
                 {"submitted", row->property("submitted")}};
    }
    const QString selectedId = list_->currentItem()
        ? list_->currentItem()->data(kWaypointRole).toMap()
              .value(QStringLiteral("id")).toString() : QString{};
    const QSignalBlocker blocker(list_);
    list_->clear();
    for (const auto &wp : waypoints) {
        const QString id = wp.value(QStringLiteral("id")).toString();
        auto *it = new QListWidgetItem(list_);
        it->setData(kWaypointRole, wp);
        auto *row = new CatalogRow(list_);
        row->setName(wp.value(QStringLiteral("name"), id).toString());
        row->setDetails(QStringLiteral("X %1  ·  Y %2  ·  yaw %3°")
            .arg(wp.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
            .arg(wp.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
            .arg(qRadiansToDegrees(wp.value(QStringLiteral("theta")).toDouble()), 0, 'f', 1));
        row->setStatus(waypointStatusLabel(wp.value(QStringLiteral("status")).toString()));
        row->onSelected([this, it] { list_->setCurrentItem(it); });
        list_->setItemWidget(it, row);

        auto *form = new QFormLayout;
        auto *name = new QLineEdit(wp.value(QStringLiteral("name"), id).toString());
        name->setObjectName(QStringLiteral("WaypointRowName"));
        name->setMaxLength(120);
        form->addRow(QStringLiteral("이름"), name);
        const auto coordinate = [](const QString &objectName, double value) {
            auto *spin = new QDoubleSpinBox;
            spin->setObjectName(objectName);
            spin->setRange(-1000000.0, 1000000.0);
            spin->setDecimals(3);
            spin->setSingleStep(0.1);
            spin->setSuffix(QStringLiteral(" m"));
            spin->setValue(value);
            return spin;
        };
        auto *x = coordinate(QStringLiteral("WaypointRowX"),
                             wp.value(QStringLiteral("x")).toDouble());
        auto *y = coordinate(QStringLiteral("WaypointRowY"),
                             wp.value(QStringLiteral("y")).toDouble());
        form->addRow(QStringLiteral("X"), x);
        form->addRow(QStringLiteral("Y"), y);
        auto *yaw = new QDoubleSpinBox;
        yaw->setObjectName(QStringLiteral("WaypointRowYaw"));
        yaw->setRange(-180.0, 180.0);
        yaw->setDecimals(1);
        yaw->setSingleStep(5.0);
        yaw->setSuffix(QStringLiteral("°"));
        yaw->setValue(std::remainder(qRadiansToDegrees(
            wp.value(QStringLiteral("theta")).toDouble()), 360.0));
        form->addRow(QStringLiteral("도착 방향 (yaw)"), yaw);
        row->editorLayout()->addLayout(form);

        auto *actions = new QHBoxLayout;
        auto *save = new QPushButton(QStringLiteral("저장"));
        save->setObjectName(QStringLiteral("WaypointRowSave"));
        save->setProperty("variant", "primary");
        auto *remove = new QPushButton(QStringLiteral("삭제"));
        remove->setObjectName(QStringLiteral("WaypointRowDelete"));
        auto *cancel = new QPushButton(QStringLiteral("취소"));
        cancel->setObjectName(QStringLiteral("WaypointRowCancel"));
        for (auto *button : {save, remove, cancel})
            button->setProperty("size", "sm");
        actions->addWidget(save, 1);
        actions->addWidget(remove);
        actions->addWidget(cancel);
        row->editorLayout()->addLayout(actions);
        const auto resizeItem = [this, it, row] {
            row->layout()->activate();
            it->setSizeHint(QSize(0, row->sizeHint().height()));
            list_->doItemsLayout();
        };
        connect(row->editButton(), &QPushButton::clicked, this, [this, row, wp, resizeItem] {
            if (!row->property("draftActive").toBool())
                row->setProperty("editBase", wp);
            row->setProperty("draftActive", true);
            for (int i = 0; i < list_->count(); ++i) {
                auto *otherItem = list_->item(i);
                auto *other = static_cast<CatalogRow *>(list_->itemWidget(otherItem));
                if (other && other != row && other->isEditing()) {
                    other->setEditing(false);
                    otherItem->setSizeHint(QSize(0, other->sizeHint().height()));
                }
            }
            row->setEditing(true);
            resizeItem();
        });
        connect(cancel, &QPushButton::clicked, this, [row, wp, name, x, y, yaw, resizeItem] {
            name->setText(wp.value("name", wp.value("id")).toString());
            x->setValue(wp.value("x").toDouble());
            y->setValue(wp.value("y").toDouble());
            yaw->setValue(std::remainder(qRadiansToDegrees(wp.value("theta").toDouble()), 360.0));
            row->setProperty("draftActive", false);
            row->setProperty("submitted", false);
            row->setEditing(false);
            resizeItem();
        });
        connect(save, &QPushButton::clicked, this, [this, id, wp, name, x, y, yaw, row] {
            if (!editingEnabled_)
                return;
            const QString newName = name->text().trimmed();
            if (newName.isEmpty() || newName.toUtf8().size() > 120) {
                setSaveStatus(QStringLiteral("웨이포인트 이름을 확인하십시오."), true);
                return;
            }
            QVariantMap updated = wp;
            updated[QStringLiteral("name")] = newName;
            updated[QStringLiteral("x")] = x->value();
            updated[QStringLiteral("y")] = y->value();
            updated[QStringLiteral("theta")] = qDegreesToRadians(yaw->value());
            updated[QStringLiteral("_expected_point")] = row->property("editBase");
            row->setProperty("submitted", true);
            emit updateRequested(id, updated);
        });
        connect(remove, &QPushButton::clicked, this, [this, id, wp] {
            if (!editingEnabled_ || QMessageBox::question(
                this, QStringLiteral("웨이포인트 삭제"),
                QStringLiteral("'%1' 웨이포인트를 삭제하시겠습니까?")
                    .arg(wp.value(QStringLiteral("name"), id).toString()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            emit deleteRequested(id);
        });
        connect(row->applyButton(), &QPushButton::clicked, this, [this, id] {
            if (editingEnabled_)
                emit gotoRequested(id);
        });
        it->setSizeHint(QSize(0, row->sizeHint().height()));
        if (drafts.contains(id)) {
            const auto draft = drafts.value(id);
            const bool committed = draft.value("submitted").toBool() &&
                draft.value("name").toString() == wp.value("name").toString() &&
                std::abs(draft.value("x").toDouble() - wp.value("x").toDouble()) < 1e-6 &&
                std::abs(draft.value("y").toDouble() - wp.value("y").toDouble()) < 1e-6 &&
                std::abs(std::remainder(qDegreesToRadians(draft.value("yaw").toDouble()) -
                                      wp.value("theta").toDouble(), 2 * M_PI)) < 1e-6;
            if (!committed) {
                name->setText(draft.value(QStringLiteral("name")).toString());
                x->setValue(draft.value(QStringLiteral("x")).toDouble());
                y->setValue(draft.value(QStringLiteral("y")).toDouble());
                yaw->setValue(draft.value(QStringLiteral("yaw")).toDouble());
                row->setProperty("draftActive", true);
                row->setProperty("editBase", draft.value("base"));
                row->setProperty("submitted", draft.value("submitted"));
                row->setEditing(draft.value("editing").toBool());
                it->setSizeHint(QSize(0, row->sizeHint().height()));
            }
        }
        if (id == selectedId)
            list_->setCurrentItem(it);
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
        if (auto *row = static_cast<CatalogRow *>(list_->itemWidget(it)))
            row->setStatus(waypointStatusLabel(status));
        emit waypointsChanged(waypoints());
        return;
    }
}


}  // namespace hmi::ui
