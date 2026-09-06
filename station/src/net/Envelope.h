#pragma once

// JSON envelope. Implements docs/bridge_protocol.md sections 1.2 and 1.4.
//
// Version 1 of the protocol serialises envelopes as JSON. The envelope carries
// an explicit version field so that the payload encoding can later be replaced
// with protobuf: only this file changes, the framing layer stays as it is.

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace gcs::net {

inline constexpr int kProtocolVersion = 1;

/// Message kinds carried in the envelope's "t" field.
namespace mtype {
inline constexpr auto kHb = "hb";        ///< heartbeat, 5 Hz, both directions
inline constexpr auto kSub = "sub";      ///< subscribe to channels
inline constexpr auto kUnsub = "unsub";
inline constexpr auto kPub = "pub";      ///< stream sample; loss tolerated
inline constexpr auto kReq = "req";      ///< command request; correlated by id
inline constexpr auto kRes = "res";      ///< command response
inline constexpr auto kEvt = "evt";      ///< asynchronous event; not subscription-gated
}  // namespace mtype

struct Envelope {
    int v = kProtocolVersion;
    QString t;                  ///< message kind, see mtype
    QString ch;                 ///< channel name, see Channels.h
    QJsonObject p;              ///< payload
    QString id;                 ///< request/response correlation id
    double ts = 0.0;            ///< sender clock, Unix epoch seconds
    std::optional<qint64> seq;  ///< monotonic sequence, for detecting stream gaps
    QByteArray payload;         ///< binary body (map PNG, capture preview JPEG)

    /// Serialises the envelope. Pass the result as the header argument of
    /// encodeFrame().
    QByteArray toHeader() const;

    /// Parses a frame header. Returns nullopt on failure and, when err is
    /// non-null, stores the reason there.
    ///
    /// A protocol version mismatch is treated as a failure: running the control
    /// station against a bridge of a different version is the most dangerous
    /// failure mode available, so the connection is refused rather than
    /// degraded (protocol section 6).
    static std::optional<Envelope> fromHeader(const QByteArray &json, QString *err = nullptr);
};

/// Current time as Unix epoch seconds, with sub-second precision.
double nowSeconds();

// ---- Construction helpers ----------------------------------------------
Envelope makeRequest(const QString &channel, const QJsonObject &payload = {});
Envelope makeResponse(const Envelope &req, bool ok, const QString &errCode = {},
                      const QString &errMsg = {});
Envelope makePublish(const QString &channel, const QJsonObject &payload,
                     std::optional<qint64> seq = std::nullopt,
                     const QByteArray &binary = {});
Envelope makeEvent(const QString &channel, const QJsonObject &payload);
Envelope makeHeartbeat(qint64 seq);
Envelope makeSubscribe(const QStringList &channels);

/// Error codes returned in a failed response (protocol section 7).
/// Every code here must also exist in resources/error_codes.json; the catalog
/// test enforces this, because a code the control station cannot explain is a
/// dead end for the operator.
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

}  // namespace gcs::net
