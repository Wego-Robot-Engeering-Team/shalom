#pragma once

// Qt-flavoured adapter over the shared framing implementation.
//
// The byte layout lives in protocol/include/shalom/framing.hpp, which is
// compiled into both this application and the robot-side bridge node. Two
// implementations of one wire format would eventually disagree, and the
// disagreement would show up as a corrupted stream in the field rather than as
// a build failure, so there is exactly one and this file only converts types.
//
// See docs/bridge_protocol.md sections 1.1 and 1.3.

#include <QByteArray>

#include "shalom/framing.hpp"

namespace gcs::net {

inline constexpr std::uint32_t kMagic = shalom::kMagic;
inline constexpr std::uint32_t kMaxBodyLen = shalom::kMaxBodyLen;
inline constexpr qsizetype kCompactThreshold = qsizetype(shalom::kCompactThreshold);

struct Frame {
    QByteArray header;   ///< UTF-8 JSON envelope
    QByteArray payload;  ///< empty when the message carries no binary body
};

/// Serialises a single frame.
QByteArray encodeFrame(const QByteArray &header, const QByteArray &payload = {});

/// Recovers frames from a byte stream.
///
/// A single readyRead() may deliver several frames, so callers must drain the
/// decoder in a loop; handling one frame per read event accumulates latency
/// without ever reporting an error.
///
/// @code
///     decoder.append(socket->readAll());
///     Frame f;
///     for (;;) {
///         const auto st = decoder.next(f);
///         if (st == FrameDecoder::Status::NeedMore) break;
///         if (st != FrameDecoder::Status::Ok) { socket->abort(); break; }
///         handle(f);
///     }
/// @endcode
class FrameDecoder {
public:
    enum class Status {
        Ok,        ///< one frame was produced
        NeedMore,  ///< frame incomplete; append more data and retry
        BadMagic,  ///< stream desynchronised - close, do not resynchronise
        TooLarge,  ///< declared length exceeds the cap or contradicts the body
    };

    void append(const QByteArray &chunk);
    Status next(Frame &out);

    /// Must be called when reconnecting. Bytes left over from a previous
    /// connection would otherwise misalign the first frame of the new one.
    void reset() { inner_.reset(); }

    qsizetype buffered() const { return qsizetype(inner_.buffered()); }

private:
    shalom::FrameDecoder inner_;
};

}  // namespace gcs::net
