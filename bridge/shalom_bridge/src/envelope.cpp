#include "shalom_bridge/envelope.hpp"

#include <chrono>

namespace shalom_bridge {

double nowSeconds()
{
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::string Envelope::toHeader() const
{
    json o;
    o["v"] = v;
    o["t"] = t;
    o["ts"] = ts > 0.0 ? ts : nowSeconds();
    if (!ch.empty())
        o["ch"] = ch;
    if (!id.empty())
        o["id"] = id;
    if (seq)
        o["seq"] = *seq;
    if (!p.is_null() && !p.empty())
        o["p"] = p;
    return o.dump();
}

std::optional<Envelope> Envelope::fromHeader(const std::string &text, std::string *err)
{
    const auto fail = [err](const std::string &m) -> std::optional<Envelope> {
        if (err)
            *err = m;
        return std::nullopt;
    };

    json o = json::parse(text, nullptr, false);
    if (o.is_discarded() || !o.is_object())
        return fail("envelope is not a JSON object");

    if (!o.contains("v") || !o["v"].is_number_integer())
        return fail("missing field 'v'");

    // 버전이 다르면 거부한다. 어긋난 채로 도는 것이 가장 위험하다.
    const int version = o["v"].get<int>();
    if (version != kProtocolVersion)
        return fail("protocol version mismatch: got " + std::to_string(version)
                    + ", expected " + std::to_string(kProtocolVersion));

    if (!o.contains("t") || !o["t"].is_string())
        return fail("missing field 't'");

    Envelope e;
    e.v = version;
    e.t = o["t"].get<std::string>();
    if (o.contains("ch") && o["ch"].is_string())
        e.ch = o["ch"].get<std::string>();
    if (o.contains("id") && o["id"].is_string())
        e.id = o["id"].get<std::string>();
    if (o.contains("ts") && o["ts"].is_number())
        e.ts = o["ts"].get<double>();
    if (o.contains("seq") && o["seq"].is_number_integer())
        e.seq = o["seq"].get<std::int64_t>();
    if (o.contains("p")) {
        if (!o["p"].is_object())
            return fail("field 'p' must be an object");
        e.p = o["p"];
    }
    return e;
}

Envelope makeResponse(const Envelope &request, bool ok, const std::string &errCode,
                      const std::string &errMsg)
{
    Envelope e;
    e.t = mtype::kRes;
    e.ch = request.ch;
    e.id = request.id;
    e.ts = nowSeconds();
    e.p = json::object();
    e.p["ok"] = ok;
    if (!ok)
        e.p["err"] = json{{"code", errCode}, {"msg", errMsg}};
    return e;
}

Envelope makePublish(const std::string &channel, json payload,
                     std::optional<std::int64_t> seq)
{
    Envelope e;
    e.t = mtype::kPub;
    e.ch = channel;
    e.p = std::move(payload);
    e.seq = seq;
    e.ts = nowSeconds();
    return e;
}

Envelope makeEvent(const std::string &channel, json payload)
{
    Envelope e;
    e.t = mtype::kEvt;
    e.ch = channel;
    e.p = std::move(payload);
    e.ts = nowSeconds();
    return e;
}

Envelope makeHeartbeat(std::int64_t seq)
{
    Envelope e;
    e.t = mtype::kHb;
    e.ts = nowSeconds();
    e.p = json{{"seq", seq}};
    return e;
}

}  // namespace shalom_bridge
