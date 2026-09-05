#include "diag/LogStore.h"

// 헤더는 QFile/QTextStream 을 전방 선언만 한다(빌드 시간). 구현부에서 실제 정의가 필요하다.
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

namespace gcs::diag {

QString LogEntry::timeText() const
{
    return QDateTime::fromMSecsSinceEpoch(timestampMs).toString(QStringLiteral("HH:mm:ss"));
}

QJsonObject LogEntry::toJson() const
{
    QJsonObject o;
    // ISO 8601 로 남긴다. 보고서 생성 시 파싱하기 쉽고 사람도 읽을 수 있다.
    o[QStringLiteral("ts")] =
        QDateTime::fromMSecsSinceEpoch(timestampMs).toString(Qt::ISODateWithMs);
    o[QStringLiteral("ts_ms")] = timestampMs;
    o[QStringLiteral("severity")] = severityToString(severity);
    if (!code.isEmpty())
        o[QStringLiteral("code")] = code;
    o[QStringLiteral("msg")] = message;
    if (!detail.isEmpty())
        o[QStringLiteral("detail")] = detail;
    return o;
}

LogStore::LogStore(QObject *parent, int capacity)
    : QObject(parent), capacity_(capacity > 0 ? capacity : 1)
{
}

LogStore::~LogStore() = default;

void LogStore::log(const QString &code, const QJsonObject &detail,
                   const QString &messageOverride)
{
    const CodeEntry *e = CodeCatalog::instance().find(code);

    LogEntry le;
    le.timestampMs = QDateTime::currentMSecsSinceEpoch();
    le.code = code;
    le.severity = e ? e->severity : Severity::Info;
    le.message = !messageOverride.isEmpty() ? messageOverride
                 : (e ? e->title : code);
    le.detail = detail;
    append(std::move(le));
}

void LogStore::note(Severity severity, const QString &message, const QJsonObject &detail)
{
    LogEntry le;
    le.timestampMs = QDateTime::currentMSecsSinceEpoch();
    le.severity = severity;
    le.message = message;
    le.detail = detail;
    append(std::move(le));
}

void LogStore::append(LogEntry e)
{
    if (sinkStream_) {
        // 프로세스가 죽어도 남아야 하므로 매 건 flush 한다.
        // 초당 수천 건이 아니므로 비용은 무시할 만하다.
        *sinkStream_ << QString::fromUtf8(
                            QJsonDocument(e.toJson()).toJson(QJsonDocument::Compact))
                     << '\n';
        sinkStream_->flush();
    }

    entries_.append(e);
    if (entries_.size() > capacity_)
        entries_.remove(0, entries_.size() - capacity_);

    emit appended(entries_.last());
}

QList<LogEntry> LogStore::query(Severity minSeverity, const QString &search) const
{
    QList<LogEntry> out;
    for (const auto &e : entries_) {
        if (int(e.severity) < int(minSeverity))
            continue;
        if (!search.isEmpty()
            && !e.message.contains(search, Qt::CaseInsensitive)
            && !e.code.contains(search, Qt::CaseInsensitive))
            continue;
        out << e;
    }
    return out;
}

int LogStore::countAtOrAbove(Severity s) const
{
    int n = 0;
    for (const auto &e : entries_)
        if (int(e.severity) >= int(s))
            ++n;
    return n;
}

void LogStore::clear()
{
    entries_.clear();
    emit cleared();
}

bool LogStore::startFileSink(const QString &path, QString *err)
{
    auto f = std::make_unique<QFile>(path);
    if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("로그 파일을 열 수 없다: %1").arg(f->errorString());
        return false;
    }
    sink_ = std::move(f);
    sinkStream_ = std::make_unique<QTextStream>(sink_.get());
    sinkStream_->setEncoding(QStringConverter::Utf8);
    return true;
}

bool LogStore::exportJsonl(const QString &path, QString *err) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("내보내기 실패: %1").arg(f.errorString());
        return false;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    for (const auto &e : entries_)
        out << QString::fromUtf8(QJsonDocument(e.toJson()).toJson(QJsonDocument::Compact)) << '\n';
    return true;
}

}  // namespace gcs::diag
