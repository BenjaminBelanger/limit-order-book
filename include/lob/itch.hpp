#pragma once

#include <cstddef>
#include <cstdint>

// Minimal NASDAQ TotalView-ITCH 5.0 parser for the length-prefixed BinaryFILE
// framing of NASDAQ's downloadable sample feeds: a 2-byte big-endian length,
// then a message body whose first byte is the type. Every multi-byte field is
// big-endian, prices carry 4 implied decimals (value / 10000 USD), and
// timestamps are 6 bytes of nanoseconds since midnight.
//
// Only the messages needed to reconstruct the displayable book are decoded;
// the rest are skipped by their length prefix.
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

// The wire type codes carried in the first byte of a message body.
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

// Dispatches to `h`, which must provide: on_add(ref, is_buy, shares, price),
// on_execute(ref, shares), on_execute_price(ref, shares, price),
// on_cancel(ref, shares), on_delete(ref),
// on_replace(orig_ref, new_ref, shares, price).
// Returns the number of messages processed.
template <class Handler>
std::size_t parse_stream(const std::uint8_t* data, std::size_t len, Handler& h) {
  std::size_t off = 0;
  std::size_t count = 0;
  while (off + 2 <= len) {
    const std::uint16_t msg_len = rd16(data + off);
    if (msg_len == 0 || off + 2 + msg_len > len) break;
    const std::uint8_t* m = data + off + 2;
    const char type = static_cast<char>(m[0]);
    // Every body starts with an 11-byte header (type, stock_locate, tracking
    // number, timestamp), so the type-specific fields begin at offset 11.
    switch (type) {
      case 'A': // Add Order
        h.on_add(rd64(m + 11), m[19] == 'B', rd32(m + 20), rd32(m + 32));
        break;
      case 'F': // Add Order with MPID; the attribution at +36 is ignored
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
      default: // system events, stock directory, trades, and so on
        break;
    }
    ++count;
    off += 2 + msg_len;
  }
  return count;
}

} // namespace lob::itch
