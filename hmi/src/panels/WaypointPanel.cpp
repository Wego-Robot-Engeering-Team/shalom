// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/WaypointPanel.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollBar>
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

QVariantMap editorFocus(QWidget *row)
{
    auto *focused = QApplication::focusWidget();
    if (!focused || !row->isAncestorOf(focused))
        return {};
    for (auto *field = focused; field && field != row; field = field->parentWidget()) {
        if (!qobject_cast<QDoubleSpinBox *>(field) &&
            (!qobject_cast<QLineEdit *>(field) || field->objectName().startsWith("qt_")))
            continue;
        auto *line = qobject_cast<QLineEdit *>(field);
        if (!line)
            line = field->findChild<QLineEdit *>();
        if (!line)
            return {};
        return {{"field", field->objectName()}, {"text", line->text()},
                {"cursor", line->cursorPosition()}, {"selection", line->selectionStart()},
                {"selectionLength", line->selectedText().size()}};
    }
    return {};
}

void restoreEditorFocus(QWidget *row, const QVariantMap &focus)
{
    if (focus.isEmpty())
        return;
    auto *field = row->findChild<QWidget *>(focus.value("field").toString());
    if (!field || !field->isEnabled())
        return;
    auto *line = qobject_cast<QLineEdit *>(field);
    if (!line)
        line = field->findChild<QLineEdit *>();
    if (!line)
        return;
    field->setFocus(Qt::OtherFocusReason);
    const QSignalBlocker blocker(field);
    line->setText(focus.value("text").toString());
    line->setCursorPosition(focus.value("cursor").toInt());
    const int start = focus.value("selection").toInt();
    const int length = focus.value("selectionLength").toInt();
    if (start >= 0) {
        if (focus.value("cursor").toInt() == start)
            line->setSelection(start + length, -length);
        else
            line->setSelection(start, length);
    }
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
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->viewport()->installEventFilter(this);
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
    if (editingEnabled_ && !enabled)
        ++catalogGeneration_;
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
    ++catalogGeneration_;
    const int scrollPosition = list_->verticalScrollBar()->value();
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
                 {"initialValues", row->property("editValues")},
                 {"submittedPoint", row->property("submittedPoint")},
                 {"submitted", row->property("submitted")},
                 {"focus", editorFocus(row)}};
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
        row->applyButton()->setText(QStringLiteral("목표 선택"));
        row->applyButton()->setToolTip(QStringLiteral("운용에서 확인 후 주행 시작"));
        row->setName(wp.value(QStringLiteral("name"), id).toString());
        row->setDetails(QStringLiteral("X %1  ·  Y %2  ·  yaw %3°")
            .arg(wp.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
            .arg(wp.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
            .arg(qRadiansToDegrees(wp.value(QStringLiteral("theta")).toDouble()), 0, 'f', 1));
        row->setStatus(waypointStatusLabel(wp.value(QStringLiteral("status")).toString()));
        row->onSelected([this, it] { list_->setCurrentItem(it); });
        list_->setItemWidget(it, row);

        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
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
        form->addRow(QStringLiteral("방향 (yaw)"), yaw);
        yaw->setToolTip(QStringLiteral("도착 시 로봇이 바라볼 방향"));
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
        const auto resizeItem = [this] { updateListItemSizes(); };
        connect(row->editButton(), &QPushButton::clicked, this, [this, it, row, wp, name, x, y, yaw, resizeItem] {
            if (!row->property("draftActive").toBool()) {
                row->setProperty("editBase", wp);
                row->setProperty("editValues", QVariantMap{{"x", x->value()}, {"y", y->value()},
                                                          {"yaw", yaw->value()}});
            }
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
            list_->setCurrentItem(it);
            resizeItem();
            list_->scrollToItem(it, QAbstractItemView::EnsureVisible);
            name->setFocus(Qt::OtherFocusReason);
            name->selectAll();
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
            const auto base = row->property("editBase").toMap();
            const auto initial = row->property("editValues").toMap();
            updated[QStringLiteral("name")] = newName;
            updated[QStringLiteral("x")] = initial.contains("x") && x->value() == initial.value("x").toDouble()
                ? base.value("x") : QVariant(x->value());
            updated[QStringLiteral("y")] = initial.contains("y") && y->value() == initial.value("y").toDouble()
                ? base.value("y") : QVariant(y->value());
            updated[QStringLiteral("theta")] = initial.contains("yaw") && yaw->value() == initial.value("yaw").toDouble()
                ? base.value("theta", 0.0) : QVariant(qDegreesToRadians(yaw->value()));
            updated[QStringLiteral("_expected_point")] = row->property("editBase");
            row->setProperty("submitted", true);
            row->setProperty("submittedPoint", updated);
            emit updateRequested(id, updated);
        });
        connect(remove, &QPushButton::clicked, this, [this, id, wp] {
            const QPointer<WaypointPanel> panel(this);
            const quint64 generation = catalogGeneration_;
            const QString targetId = id;
            if (!editingEnabled_ || QMessageBox::question(
                this, QStringLiteral("웨이포인트 삭제"),
                QStringLiteral("'%1' 웨이포인트를 삭제하시겠습니까?")
                    .arg(wp.value(QStringLiteral("name"), id).toString()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            if (!panel || generation != catalogGeneration_ || !editingEnabled_)
                return;
            emit deleteRequested(targetId);
        });
        connect(row->applyButton(), &QPushButton::clicked, this, [this, id] {
            if (editingEnabled_)
                emit gotoRequested(id);
        });
        it->setSizeHint(QSize(0, row->sizeHint().height()));
        if (drafts.contains(id)) {
            const auto draft = drafts.value(id);
            const auto submitted = draft.value("submittedPoint").toMap();
            const bool committed = draft.value("submitted").toBool() &&
                !submitted.isEmpty() && submitted.value("name").toString() == wp.value("name").toString() &&
                std::abs(submitted.value("x").toDouble() - wp.value("x").toDouble()) < 1e-6 &&
                std::abs(submitted.value("y").toDouble() - wp.value("y").toDouble()) < 1e-6 &&
                std::abs(std::remainder(submitted.value("theta").toDouble() -
                                      wp.value("theta").toDouble(), 2 * M_PI)) < 1e-6;
            if (!committed) {
                name->setText(draft.value(QStringLiteral("name")).toString());
                x->setValue(draft.value(QStringLiteral("x")).toDouble());
                y->setValue(draft.value(QStringLiteral("y")).toDouble());
                yaw->setValue(draft.value(QStringLiteral("yaw")).toDouble());
                row->setProperty("draftActive", true);
                row->setProperty("editBase", draft.value("base"));
                row->setProperty("editValues", draft.value("initialValues"));
                row->setProperty("submitted", draft.value("submitted"));
                row->setProperty("submittedPoint", draft.value("submittedPoint"));
                row->setEditing(draft.value("editing").toBool());
                it->setSizeHint(QSize(0, row->sizeHint().height()));
                if (row->isEditing())
                    restoreEditorFocus(row, draft.value("focus").toMap());
            }
        }
        if (id == selectedId)
            list_->setCurrentItem(it);
    }
    count_->setText(QString::number(waypoints.size()));
    updateActionButtons();
    updateListItemSizes();
    list_->verticalScrollBar()->setValue(scrollPosition);
    emit waypointsChanged(waypoints);
}

bool WaypointPanel::eventFilter(QObject *object, QEvent *event)
{
    if (object == list_->viewport() && event->type() == QEvent::Resize)
        updateListItemSizes();
    return QWidget::eventFilter(object, event);
}

void WaypointPanel::updateListItemSizes()
{
    const int width = list_->viewport()->width();
    bool changed = false;
    for (int i = 0; i < list_->count(); ++i) {
        auto *item = list_->item(i);
        auto *row = static_cast<CatalogRow *>(list_->itemWidget(item));
        if (!row)
            continue;
        row->layout()->activate();
        const int height = row->heightForWidth(width);
        const QSize hint(0, height >= 0 ? height : row->sizeHint().height());
        if (item->sizeHint() != hint) {
            item->setSizeHint(hint);
            changed = true;
        }
    }
    if (changed)
        list_->doItemsLayout();
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
