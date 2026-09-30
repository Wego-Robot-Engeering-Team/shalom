// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QMouseEvent;

namespace hmi::ui {

class IconButton;

// Shared row layout for robot-owned arm poses and map-owned waypoints.
// Editing expands in the row; the list never relies on a separate selection toolbar.
class CatalogRow : public QWidget {
public:
    explicit CatalogRow(QWidget *parent = nullptr);

    void setName(const QString &name);
    void setDetails(const QString &details);
    void setEditing(bool editing);
    bool isEditing() const;
    void setPending(bool pending);
    bool isPending() const { return pending_; }
    void setApplyEnabled(bool enabled);
    void setEditEnabled(bool enabled);
    void setStatus(const QString &text);
    void onSelected(std::function<void()> handler) { selected_ = std::move(handler); }

    IconButton *editButton() const { return edit_; }
    QPushButton *applyButton() const { return apply_; }
    QVBoxLayout *editorLayout() const { return editorLayout_; }

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel *name_ = nullptr;
    QLabel *details_ = nullptr;
    QLabel *status_ = nullptr;
    IconButton *edit_ = nullptr;
    QPushButton *apply_ = nullptr;
    QWidget *editor_ = nullptr;
    QVBoxLayout *editorLayout_ = nullptr;
    bool pending_ = false;
    bool editAllowed_ = true;
    bool applyAllowed_ = true;
    void refreshActions();
    std::function<void()> selected_;
};

}  // namespace hmi::ui
