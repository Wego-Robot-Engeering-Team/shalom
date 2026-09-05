#include "net/Envelope.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>
#include <QUuid>

namespace gcs::net {
namespace {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128).left(12);
}

}  // namespace

double nowSeconds()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000.0;
}

QByteArray Envelope::toHeader() const
{
    QJsonObject o;
    o["v"] = v;
    o["t"] = t;
    o["ts"] = ts > 0.0 ? ts : nowSeconds();
    if (!ch.isEmpty())
        o["ch"] = ch;
    if (!id.isEmpty())
        o["id"] = id;
    if (seq.has_value())
        o["seq"] = static_cast<double>(*seq);
    if (!p.isEmpty())
        o["p"] = p;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<Envelope> Envelope::fromHeader(const QByteArray &json, QString *err)
{
    const auto fail = [err](const QString &m) -> std::optional<Envelope> {
        if (err)
            *err = m;
        return std::nullopt;
    };

    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError)
        return fail(QStringLiteral("JSON 파싱 실패: %1").arg(pe.errorString()));
    if (!doc.isObject())
        return fail(QStringLiteral("봉투는 객체여야 한다"));

    const QJsonObject o = doc.object();

    if (!o.value("v").isDouble())
        return fail(QStringLiteral("필드 'v' 누락"));
    const int v = o.value("v").toInt();
    if (v != kProtocolVersion)
        return fail(QStringLiteral("프로토콜 버전 불일치: 수신 %1, 기대 %2")
                        .arg(v).arg(kProtocolVersion));

    if (!o.value("t").isString())
        return fail(QStringLiteral("필드 't' 누락"));

    const QJsonValue pv = o.value("p");
    if (!pv.isUndefined() && !pv.isNull() && !pv.isObject())
        return fail(QStringLiteral("필드 'p' 는 객체여야 한다"));

    Envelope e;
    e.v = v;
    e.t = o.value("t").toString();
    e.ch = o.value("ch").toString();
    e.id = o.value("id").toString();
    e.ts = o.value("ts").toDouble();
    e.p = pv.isObject() ? pv.toObject() : QJsonObject{};
    if (o.value("seq").isDouble())
        e.seq = static_cast<qint64>(o.value("seq").toDouble());
    return e;
}

Envelope makeRequest(const QString &channel, const QJsonObject &payload)
{
    Envelope e;
    e.t = mtype::kReq;
    e.ch = channel;
    e.p = payload;
    e.id = newId();
    e.ts = nowSeconds();
    return e;
}

Envelope makeResponse(const Envelope &req, bool ok, const QString &errCode,
                      const QString &errMsg)
{
    Envelope e;
    e.t = mtype::kRes;
    e.ch = req.ch;
    e.id = req.id;
    e.ts = nowSeconds();
    e.p["ok"] = ok;
    if (!ok) {
        QJsonObject eo;
        eo["code"] = errCode;
        eo["msg"] = errMsg;
        e.p["err"] = eo;
    }
    return e;
}

Envelope makePublish(const QString &channel, const QJsonObject &payload,
                     std::optional<qint64> seq, const QByteArray &binary)
{
    Envelope e;
    e.t = mtype::kPub;
    e.ch = channel;
    e.p = payload;
    e.seq = seq;
    e.payload = binary;
    e.ts = nowSeconds();
    return e;
}

Envelope makeEvent(const QString &channel, const QJsonObject &payload)
{
    Envelope e;
    e.t = mtype::kEvt;
    e.ch = channel;
    e.p = payload;
    e.ts = nowSeconds();
    return e;
}

Envelope makeHeartbeat(qint64 seq)
{
    Envelope e;
    e.t = mtype::kHb;
    e.ts = nowSeconds();
    e.p["seq"] = static_cast<double>(seq);
    return e;
}

Envelope makeSubscribe(const QStringList &channels)
{
    QJsonArray arr;
    for (const auto &c : channels)
        arr.append(c);
    Envelope e;
    e.t = mtype::kSub;
    e.id = newId();
    e.ts = nowSeconds();
    e.p["channels"] = arr;
    return e;
}

}  // namespace gcs::net
