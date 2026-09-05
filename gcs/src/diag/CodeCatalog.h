#pragma once

// Diagnostic code catalog, loaded from resources/error_codes.json.
//
// That JSON file is the single source of truth for diagnostic codes. It feeds
// three consumers:
//   1. the control station UI, behind the (i) affordance on a log row,
//   2. the interpretation of error codes returned by the bridge, and
//   3. the delivered "emergency response manual" document, which is generated
//      from it rather than written by hand.
//
// Writing that document separately guarantees it will drift out of step with
// the software, so new codes are added to the JSON file only.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace gcs::diag {

/// Ordered by escalation; comparisons on the underlying value are meaningful,
/// which is what the log filter relies on.
enum class Severity { Info, Ok, Warn, Error, Critical };

Severity severityFromString(const QString &s);
QString severityToString(Severity s);
/// Localised label for display in the log list and status badges.
QString severityLabel(Severity s);

struct CodeEntry {
    QString code;
    Severity severity = Severity::Info;
    QString category;
    QString title;        ///< short description shown as the popup heading
    QString cause;        ///< what the condition means
    QStringList actions;  ///< operator steps, in the order they should be tried
    QString channel;      ///< related protocol channel, or "-"

    bool isValid() const { return !code.isEmpty(); }
};

/// Process-wide catalog, loaded once from Qt resources on first access.
class CodeCatalog {
public:
    static const CodeCatalog &instance();

    /// Returns nullptr for a code that is not in the catalog. Callers must
    /// still display the raw code string in that case: an unknown code is
    /// usually a version mismatch, and dropping it would hide the very event
    /// that explains the failure.
    const CodeEntry *find(const QString &code) const;

    const QList<CodeEntry> &all() const { return entries_; }
    QStringList categories() const;
    QList<CodeEntry> byCategory(const QString &category) const;

    bool isLoaded() const { return loaded_; }
    QString loadError() const { return loadError_; }

private:
    CodeCatalog();
    void load();

    QList<CodeEntry> entries_;
    QHash<QString, int> index_;
    bool loaded_ = false;
    QString loadError_;
};

}  // namespace gcs::diag
