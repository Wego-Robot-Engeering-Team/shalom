#pragma once

// TCP frame codec. Implements docs/bridge_protocol.md sections 1.1 and 1.3.
//
// TCP is a byte stream with no message boundaries. This file is the single
// place where those boundaries are restored, and the protocol document names it
// as the only new transport-layer risk introduced by this system. Any change
// here must be reviewed together with tests/test_framing.cpp.
//
//  0        4                 8                    12
//  +--------+-----------------+--------------------+---------------+---------+
//  | magic  | body_len uint32 | header_len uint32  | header (JSON) | payload |
//  +--------+-----------------+--------------------+---------------+---------+
//           |<-------------------- body_len bytes ---------------------------|
//
// All integers are little-endian.

#include <QByteArray>
#include <cstdint>

namespace gcs::net {

/// "SHLM" read as a little-endian uint32. Detects a wrong peer or a stream
/// that has lost synchronisation.
inline constexpr std::uint32_t kMagic = 0x4D4C4853u;

/// Upper bound on a single frame body; exceeding it closes the connection
/// (protocol section 1.3, item 4). Without this guard a single bad length
/// field would cause an unbounded allocation.
inline constexpr std::uint32_t kMaxBodyLen = 32u * 1024u * 1024u;

/// The decoder defers trimming consumed bytes until this much has accumulated.
/// Trimming on every frame would make decoding quadratic in the buffer size.
inline constexpr qsizetype kCompactThreshold = 64 * 1024;

struct Frame {
    QByteArray header;   ///< UTF-8 JSON envelope
    QByteArray payload;  ///< empty when the message carries no binary body
};

/// Serialises a single frame.
QByteArray encodeFrame(const QByteArray &header, const QByteArray &payload = {});

/// Recovers frames from a byte stream.
///
/// Usage:
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
///
/// A single readyRead() may deliver several frames, so callers must drain the
/// decoder in a loop (protocol section 1.3, item 2). Handling only one frame
/// per read event accumulates latency without ever reporting an error.
class FrameDecoder {
public:
    enum class Status {
        Ok,        ///< one frame was produced
        NeedMore,  ///< frame incomplete; append more data and retry
        BadMagic,  ///< stream desynchronised - close the connection, do not resynchronise
        TooLarge,  ///< declared length exceeds the cap or contradicts the body
    };

    void append(const QByteArray &chunk);
    Status next(Frame &out);

    /// Must be called when reconnecting. Bytes left over from a previous
    /// connection would otherwise misalign the first frame of the new one
    /// (protocol section 1.3, item 6).
    void reset();

    qsizetype buffered() const { return buf_.size() - offset_; }

private:
    void compact();

    QByteArray buf_;
    qsizetype offset_ = 0;
};

}  // namespace gcs::net
