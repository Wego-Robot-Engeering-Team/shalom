#pragma once

// Wire framing for the undercarriage inspection control protocol.
//
// Header-only and dependency-free on purpose: this is compiled into both the
// control station (Qt) and the robot-side bridge node (ROS 2), and those two
// must never disagree about the byte layout. Two implementations of the same
// format is a bug waiting for the worst possible moment, so there is one.
//
// See docs/bridge_protocol.md sections 1.1 and 1.3 for the specification and
// for the rationale behind each of the checks below.
//
//  0        4                 8                    12
//  +--------+-----------------+--------------------+---------------+---------+
//  | magic  | body_len uint32 | header_len uint32  | header (JSON) | payload |
//  +--------+-----------------+--------------------+---------------+---------+
//           |<-------------------- body_len bytes ---------------------------|
//
// All integers are little-endian, written explicitly rather than by memcpy of
// a struct, so the format does not depend on the host's byte order or on how
// the compiler lays anything out.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace inspection {

/// "SHLM" read as a little-endian uint32. Detects a wrong peer or a stream
/// that has lost synchronisation.
inline constexpr std::uint32_t kMagic = 0x4D4C4853u;

/// Upper bound on a single frame body; exceeding it must close the connection.
/// Without this guard one bad length field causes an unbounded allocation.
inline constexpr std::uint32_t kMaxBodyLen = 32u * 1024u * 1024u;

/// Bytes before the decoder trims what it has already consumed. Trimming on
/// every frame would make decoding quadratic in the buffer size.
inline constexpr std::size_t kCompactThreshold = 64u * 1024u;

/// Fixed prefix: magic and body length.
inline constexpr std::size_t kPrefixLen = 8;

/// Smallest legal body: the header-length field alone.
inline constexpr std::size_t kBodyMinLen = 4;

struct Frame {
    std::string header;   ///< UTF-8 JSON envelope
    std::string payload;  ///< empty when the message carries no binary body
};

enum class DecodeStatus {
    Ok,        ///< one frame was produced
    NeedMore,  ///< frame incomplete; append more data and retry
    BadMagic,  ///< stream desynchronised - close, do not try to resynchronise
    TooLarge,  ///< declared length exceeds the cap or contradicts the body
};

namespace detail {

inline void appendU32(std::string &out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
    out.push_back(static_cast<char>((v >> 16) & 0xFFu));
    out.push_back(static_cast<char>((v >> 24) & 0xFFu));
}

inline std::uint32_t readU32(const char *p)
{
    const auto *u = reinterpret_cast<const unsigned char *>(p);
    return static_cast<std::uint32_t>(u[0]) | (static_cast<std::uint32_t>(u[1]) << 8)
           | (static_cast<std::uint32_t>(u[2]) << 16)
           | (static_cast<std::uint32_t>(u[3]) << 24);
}

}  // namespace detail

/// Serialises one frame.
inline std::string encodeFrame(const std::string &header, const std::string &payload = {})
{
    const auto bodyLen =
        static_cast<std::uint32_t>(kBodyMinLen + header.size() + payload.size());

    std::string out;
    out.reserve(kPrefixLen + bodyLen);
    detail::appendU32(out, kMagic);
    detail::appendU32(out, bodyLen);
    detail::appendU32(out, static_cast<std::uint32_t>(header.size()));
    out += header;
    out += payload;
    return out;
}

/// Recovers frames from a byte stream.
///
/// A single read may deliver several frames, so callers must drain this in a
/// loop; handling one frame per read event accumulates latency without ever
/// reporting an error.
///
///     decoder.append(bytesJustRead);
///     Frame f;
///     for (;;) {
///         const auto st = decoder.next(f);
///         if (st == DecodeStatus::NeedMore) break;
///         if (st != DecodeStatus::Ok) { closeConnection(); break; }
///         handle(f);
///     }
class FrameDecoder {
public:
    void append(const char *data, std::size_t size) { buf_.append(data, size); }
    void append(const std::string &chunk) { buf_ += chunk; }

    DecodeStatus next(Frame &out)
    {
        // The eight-byte prefix itself can arrive split across reads.
        if (buffered() < kPrefixLen)
            return DecodeStatus::NeedMore;

        const char *base = buf_.data() + offset_;

        if (detail::readU32(base) != kMagic)
            return DecodeStatus::BadMagic;

        const std::uint32_t bodyLen = detail::readU32(base + 4);
        if (bodyLen < kBodyMinLen || bodyLen > kMaxBodyLen)
            return DecodeStatus::TooLarge;

        if (buffered() < kPrefixLen + static_cast<std::size_t>(bodyLen))
            return DecodeStatus::NeedMore;

        const char *body = base + kPrefixLen;
        const std::uint32_t headerLen = detail::readU32(body);

        // A header length pointing outside the body means the frame contradicts
        // itself. Do not try to salvage it.
        if (kBodyMinLen + static_cast<std::size_t>(headerLen) > bodyLen)
            return DecodeStatus::TooLarge;

        const std::size_t payloadLen = bodyLen - kBodyMinLen - headerLen;
        out.header.assign(body + kBodyMinLen, headerLen);
        out.payload.assign(body + kBodyMinLen + headerLen, payloadLen);

        offset_ += kPrefixLen + static_cast<std::size_t>(bodyLen);

        if (offset_ >= kCompactThreshold || offset_ == buf_.size())
            compact();

        return DecodeStatus::Ok;
    }

    /// Must be called when reconnecting: bytes left over from the previous
    /// connection would misalign the first frame of the new one.
    void reset()
    {
        buf_.clear();
        offset_ = 0;
    }

    std::size_t buffered() const { return buf_.size() - offset_; }

private:
    void compact()
    {
        if (offset_ == 0)
            return;
        buf_.erase(0, offset_);
        offset_ = 0;
    }

    std::string buf_;
    std::size_t offset_ = 0;
};

}  // namespace inspection
