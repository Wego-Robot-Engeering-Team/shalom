#include "diag/CodeCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>

namespace gcs::diag {

Severity severityFromString(const QString &s)
{
    if (s == QLatin1String("ok"))
        return Severity::Ok;
    if (s == QLatin1String("warn"))
        return Severity::Warn;
    if (s == QLatin1String("error"))
        return Severity::Error;
    if (s == QLatin1String("critical"))
        return Severity::Critical;
    return Severity::Info;
}

QString severityToString(Severity s)
{
    switch (s) {
    case Severity::Ok: return QStringLiteral("ok");
    case Severity::Warn: return QStringLiteral("warn");
    case Severity::Error: return QStringLiteral("error");
    case Severity::Critical: return QStringLiteral("critical");
    case Severity::Info: break;
    }
    return QStringLiteral("info");
}

QString severityLabel(Severity s)
{
    switch (s) {
    case Severity::Ok: return QStringLiteral("정상");
    case Severity::Warn: return QStringLiteral("주의");
    case Severity::Error: return QStringLiteral("오류");
    case Severity::Critical: return QStringLiteral("위험");
    case Severity::Info: break;
    }
    return QStringLiteral("정보");
}

const CodeCatalog &CodeCatalog::instance()
{
    static CodeCatalog c;
    return c;
}

CodeCatalog::CodeCatalog()
{
    load();
}

void CodeCatalog::load()
{
    QFile f(QStringLiteral(":/error_codes.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        loadError_ = QStringLiteral("리소스 :/error_codes.json 를 열 수 없다");
        return;
    }

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        loadError_ = QStringLiteral("카탈로그 JSON 파싱 실패: %1").arg(pe.errorString());
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("codes")).toArray();
    entries_.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        CodeEntry e;
        e.code = o.value(QStringLiteral("code")).toString();
        if (e.code.isEmpty())
            continue;
        e.severity = severityFromString(o.value(QStringLiteral("severity")).toString());
        e.category = o.value(QStringLiteral("category")).toString();
        e.title = o.value(QStringLiteral("title")).toString();
        e.cause = o.value(QStringLiteral("cause")).toString();
        e.channel = o.value(QStringLiteral("channel")).toString();
        for (const QJsonValue &a : o.value(QStringLiteral("actions")).toArray())
            e.actions << a.toString();

        index_.insert(e.code, int(entries_.size()));
        entries_.append(e);
    }
    loaded_ = !entries_.isEmpty();
    if (!loaded_)
        loadError_ = QStringLiteral("카탈로그가 비어 있다");
}

const CodeEntry *CodeCatalog::find(const QString &code) const
{
    const auto it = index_.constFind(code);
    return it == index_.constEnd() ? nullptr : &entries_.at(*it);
}

QStringList CodeCatalog::categories() const
{
    QStringList out;
    for (const auto &e : entries_)
        if (!out.contains(e.category))
            out << e.category;
    return out;
}

QList<CodeEntry> CodeCatalog::byCategory(const QString &category) const
{
    QList<CodeEntry> out;
    for (const auto &e : entries_)
        if (e.category == category)
            out << e;
    return out;
}

}  // namespace gcs::diag
