#pragma once

#include <cstdint>
#include <vector>

#include "lob/itch.hpp"

// Encodes ITCH 5.0 messages into the length-prefixed BinaryFILE framing, which
// is how the tests and the demo generator get a spec-faithful stream without
// downloading a multi-gigabyte NASDAQ sample feed.
namespace lob::itch {

class ItchWriter {
public:
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return buf_; }

  void system_event(char code) {
    begin('S');
    put48(0);
    msg_.push_back(static_cast<std::uint8_t>(code));
    end();
  }

  void add_order(std::uint64_t ref, bool is_buy, std::uint32_t shares,
                 std::uint32_t price) {
    begin('A');
    put48(0);
    put64(ref);
    msg_.push_back(is_buy ? 'B' : 'S');
    put32(shares);
    for (int i = 0; i < 8; ++i) msg_.push_back(' '); // stock symbol, space padded
    put32(price);
    end();
  }

  void execute(std::uint64_t ref, std::uint32_t shares,
               std::uint64_t match_num = 0) {
    begin('E');
    put48(0);
    put64(ref);
    put32(shares);
    put64(match_num);
    end();
  }

  void cancel(std::uint64_t ref, std::uint32_t shares) {
    begin('X');
    put48(0);
    put64(ref);
    put32(shares);
    end();
  }

  void delete_order(std::uint64_t ref) {
    begin('D');
    put48(0);
    put64(ref);
    end();
  }

  void replace(std::uint64_t orig_ref, std::uint64_t new_ref,
               std::uint32_t shares, std::uint32_t price) {
    begin('U');
    put48(0);
    put64(orig_ref);
    put64(new_ref);
    put32(shares);
    put32(price);
    end();
  }

private:
  // Writes the header shared by every message: type, then a zeroed stock_locate
  // and tracking number. Callers follow it with a 6-byte timestamp (put48) and
  // the type-specific payload, then call end() to emit the length prefix.
  void begin(char type) {
    msg_.clear();
    msg_.push_back(static_cast<std::uint8_t>(type));
    msg_.push_back(0);
    msg_.push_back(0);
    msg_.push_back(0);
    msg_.push_back(0);
  }
  void end() {
    const std::uint16_t len = static_cast<std::uint16_t>(msg_.size());
    buf_.push_back(static_cast<std::uint8_t>(len >> 8));
    buf_.push_back(static_cast<std::uint8_t>(len & 0xFF));
    buf_.insert(buf_.end(), msg_.begin(), msg_.end());
  }
  void put32(std::uint32_t v) {
    msg_.push_back(static_cast<std::uint8_t>(v >> 24));
    msg_.push_back(static_cast<std::uint8_t>(v >> 16));
    msg_.push_back(static_cast<std::uint8_t>(v >> 8));
    msg_.push_back(static_cast<std::uint8_t>(v));
  }
  void put64(std::uint64_t v) {
    for (int i = 7; i >= 0; --i)
      msg_.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
  }
  void put48(std::uint64_t v) {
    for (int i = 5; i >= 0; --i)
      msg_.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
  }

  std::vector<std::uint8_t> buf_;
  std::vector<std::uint8_t> msg_;
};

} // namespace lob::itch
