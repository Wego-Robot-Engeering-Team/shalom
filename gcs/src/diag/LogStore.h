#pragma once

// Event log storage.
//
// Beyond the history requirement in the statement of work (2.2.7 [5] item 3),
// this store is the data source for two contractual reports:
//   - per-car capture time, measured between the CAR_START and CAR_COMPLETE
//     entries, which must stay under seven minutes, and
//   - positioning accuracy, from the robot pose recorded in each entry's
//     detail object.
//
// The deployment is air-gapped and remote access requires written approval, so
// exporting this log is in practice the only remote diagnostic channel
// available during the warranty period. Entries are therefore written straight
// through to a JSONL file as well as kept in memory: whatever caused a crash
// has to survive it.

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

#include "diag/CodeCatalog.h"

class QFile;
class QTextStream;

namespace gcs::diag {

struct LogEntry {
    qint64 timestampMs = 0;
    Severity severity = Severity::Info;
    QString code;        ///< catalog code; may be empty for free-form notes
    QString message;     ///< one-line summary
    QJsonObject detail;  ///< robot pose, rejected request payload, error reason

    QString timeText() const;   ///< "HH:mm:ss" for the log list
    QJsonObject toJson() const; ///< one JSONL record
};

class LogStore : public QObject {
    Q_OBJECT
public:
    explicit LogStore(QObject *parent = nullptr, int capacity = 5000);
    ~LogStore() override;

    /// Records an event by catalog code, taking severity and the default
    /// message from the catalog. Codes missing from the catalog are still
    /// recorded verbatim.
    void log(const QString &code, const QJsonObject &detail = {},
             const QString &messageOverride = {});

    /// Records a free-form entry that has no catalog code.
    void note(Severity severity, const QString &message, const QJsonObject &detail = {});

    const QList<LogEntry> &entries() const { return entries_; }

    /// Entries at or above minSeverity whose message or code contains search
    /// (case-insensitive). An empty search matches everything.
    QList<LogEntry> query(Severity minSeverity, const QString &search) const;

    int countAtOrAbove(Severity s) const;
    void clear();

    /// Begins writing every subsequent entry to path as JSONL. Returns false
    /// on failure; in-memory recording continues regardless, because losing
    /// the file sink must not also lose the live log.
    bool startFileSink(const QString &path, QString *err = nullptr);

    /// Writes the current in-memory contents to path, for handover to support.
    bool exportJsonl(const QString &path, QString *err = nullptr) const;

signals:
    void appended(const gcs::diag::LogEntry &entry);
    void cleared();

private:
    void append(LogEntry e);

    QList<LogEntry> entries_;
    int capacity_;
    std::unique_ptr<QFile> sink_;
    std::unique_ptr<QTextStream> sinkStream_;
};

}  // namespace gcs::diag
