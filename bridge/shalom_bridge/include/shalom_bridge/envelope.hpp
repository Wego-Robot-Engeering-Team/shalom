#pragma once

// JSON envelope, robot side.
//
// Mirrors the control station's implementation (gcs/src/net/Envelope.h) and
// must stay consistent with docs/bridge_protocol.md sections 1.2 and 1.4. The
// framing beneath it is genuinely shared code (shalom/framing.hpp); the
// envelope is not, because the two sides use different JSON libraries, and the
// format is specified well enough that separate parsers are safe.
//
// The one thing that must not diverge is the version check: running the
// control station against a bridge of a different protocol version is the most
// dangerous failure mode available, so both sides refuse the connection rather
// than degrade.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace shalom_bridge {

using json = nlohmann::json;

inline constexpr int kProtocolVersion = 1;

namespace mtype {
inline constexpr auto kHb = "hb";
inline constexpr auto kSub = "sub";
inline constexpr auto kUnsub = "unsub";
inline constexpr auto kPub = "pub";
inline constexpr auto kReq = "req";
inline constexpr auto kRes = "res";
inline constexpr auto kEvt = "evt";
}  // namespace mtype

namespace err {
inline constexpr auto kVersion = "E_VERSION";
inline constexpr auto kUnknownChannel = "E_UNKNOWN_CHANNEL";
inline constexpr auto kBadPayload = "E_BAD_PAYLOAD";
inline constexpr auto kEstopEngaged = "E_ESTOP_ENGAGED";
inline constexpr auto kMode = "E_MODE";
inline constexpr auto kBusy = "E_BUSY";
inline constexpr auto kUnreachable = "E_UNREACHABLE";
inline constexpr auto kLimit = "E_LIMIT";
inline constexpr auto kHardware = "E_HARDWARE";
}  // namespace err

struct Envelope {
    int v = kProtocolVersion;
    std::string t;
    std::string ch;
    std::string id;
    double ts = 0.0;
    std::optional<std::int64_t> seq;
    json p = json::object();

    /// Serialises the envelope for use as a frame header.
    std::string toHeader() const;

    /// Parses a frame header. Returns nullopt on malformed input or on a
    /// protocol version mismatch, with the reason in `err` when non-null.
    static std::optional<Envelope> fromHeader(const std::string &text,
                                              std::string *err = nullptr);
};

double nowSeconds();

Envelope makeResponse(const Envelope &request, bool ok, const std::string &errCode = {},
                      const std::string &errMsg = {});
Envelope makePublish(const std::string &channel, json payload,
                     std::optional<std::int64_t> seq = std::nullopt);
Envelope makeEvent(const std::string &channel, json payload);
Envelope makeHeartbeat(std::int64_t seq);

}  // namespace shalom_bridge
