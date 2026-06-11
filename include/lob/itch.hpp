#pragma once

#include <cstddef>
#include <cstdint>

// Minimal NASDAQ TotalView-ITCH 5.0 parser.
//
// Parses the length-prefixed BinaryFILE framing used by NASDAQ's downloadable
// sample feeds: each message is preceded by a 2-byte big-endian length, followed
// by the message body whose first byte is the message type. All multi-byte
// fields are big-endian; prices are 4 implied decimal places (value / 10000 USD),
// timestamps are 6-byte nanoseconds-since-midnight.
//
// Only the message types needed to reconstruct the displayable order book are
// decoded (Add, Execute, Execute-with-price, Cancel, Delete, Replace); other
// messages are skipped by length. The parser is allocation-free and operates on
// an in-memory byte span so it is trivially testable.
namespace lob::itch {

[[nodiscard]] inline std::uint16_t rd16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((std::uint32_t(p[0]) << 8) | p[1]);
}
[[nodiscard]] inline std::uint32_t rd32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
[[nodiscard]] inline std::uint64_t rd64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

// ITCH message body lengths (including the 1-byte type), used to validate frames.
enum class Msg : std::uint8_t {
  SystemEvent = 'S',
  AddOrder = 'A',
  AddOrderMpid = 'F',
  OrderExecuted = 'E',
  OrderExecutedPrice = 'C',
  OrderCancel = 'X',
  OrderDelete = 'D',
  OrderReplace = 'U',
};

// Parse a length-prefixed ITCH 5.0 stream, dispatching to `h`. The handler must
// provide: on_add(ref, is_buy, shares, price), on_execute(ref, shares),
// on_execute_price(ref, shares, price), on_cancel(ref, shares),
// on_delete(ref), on_replace(orig_ref, new_ref, shares, price).
// Returns the number of messages processed.
template <class Handler>
std::size_t parse_stream(const std::uint8_t* data, std::size_t len, Handler& h) {
  std::size_t off = 0;
  std::size_t count = 0;
  while (off + 2 <= len) {
    const std::uint16_t msg_len = rd16(data + off);
    if (msg_len == 0 || off + 2 + msg_len > len) break;
    const std::uint8_t* m = data + off + 2; // message body
    const char type = static_cast<char>(m[0]);
    switch (type) {
      case 'A': // Add Order (no MPID)
        h.on_add(rd64(m + 11), m[19] == 'B', rd32(m + 20), rd32(m + 32));
        break;
      case 'F': // Add Order with MPID (attribution at +36, ignored)
        h.on_add(rd64(m + 11), m[19] == 'B', rd32(m + 20), rd32(m + 32));
        break;
      case 'E': // Order Executed
        h.on_execute(rd64(m + 11), rd32(m + 19));
        break;
      case 'C': // Order Executed With Price
        h.on_execute_price(rd64(m + 11), rd32(m + 19), rd32(m + 32));
        break;
      case 'X': // Order Cancel (partial)
        h.on_cancel(rd64(m + 11), rd32(m + 19));
        break;
      case 'D': // Order Delete
        h.on_delete(rd64(m + 11));
        break;
      case 'U': // Order Replace
        h.on_replace(rd64(m + 11), rd64(m + 19), rd32(m + 27), rd32(m + 31));
        break;
      default:
        break; // system event, stock directory, trades, etc. -> skip
    }
    ++count;
    off += 2 + msg_len;
  }
  return count;
}

} // namespace lob::itch
