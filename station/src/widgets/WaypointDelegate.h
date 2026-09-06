#pragma once

// Shared rendering for an inspection-point list.
//
// The drive view watches the run and the locations view edits it. They must
// show the same rows the same way - a point that reads "done" on one screen
// and "pending" on the other is worse than showing it once.

#include <QStyledItemDelegate>

namespace gcs::ui {

/// Item data role holding the point's QVariantMap (id, name, x, y, status,
/// optional tag_id).
inline constexpr int kWaypointRole = Qt::UserRole;

/// Row height the delegate draws to; callers sizing a list can use it.
inline constexpr int kWaypointRowHeight = 40;

/// Operator-facing word for a status string ("done", "current", "error",
/// anything else meaning pending).
QString waypointStatusLabel(const QString &status);

/// Draws one point: status dot, order and name, coordinates and marker, and
/// the status word. Selection and hover are only painted when the view
/// actually allows them, so a read-only list stays quiet.
class WaypointDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override;
};

}  // namespace gcs::ui
