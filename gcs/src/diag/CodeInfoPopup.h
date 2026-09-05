#pragma once

// Detail popup for a diagnostic code.
//
// Log rows show only the code, to keep the list scannable during operation.
// The full explanation - what the condition means and what to do about it -
// is revealed on demand from the (i) affordance next to the code.
//
// The audience is a depot operator, not a robotics engineer, so the content
// leads with the remedial steps rather than the internal cause.

#include <QWidget>

namespace gcs::diag {

struct CodeEntry;

class CodeInfoPopup : public QWidget {
    Q_OBJECT
public:
    /// Builds and shows a popup for `code` anchored near `globalPos`.
    /// Does nothing when the code is not in the catalog.
    static void showFor(const QString &code, const QPoint &globalPos, QWidget *parent);

    /// Compact rich-text summary for a hover tooltip: title and cause only.
    /// Returns the bare code when it is not in the catalog, so the operator
    /// still sees something actionable to report.
    static QString tooltipHtml(const QString &code);

private:
    explicit CodeInfoPopup(const CodeEntry &entry, QWidget *parent);
};

}  // namespace gcs::diag
