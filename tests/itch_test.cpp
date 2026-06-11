#include <gtest/gtest.h>

#include "lob/itch.hpp"
#include "lob/itch_replayer.hpp"
#include "lob/itch_writer.hpp"
#include "lob/order_book.hpp"

using namespace lob;
using namespace lob::itch;

// Encode a deterministic ITCH 5.0 stream, parse + replay it, and verify the
// reconstructed book exactly matches the expected state.
TEST(Itch, RoundTripReconstruction) {
  ItchWriter w;
  w.system_event('O');
  w.add_order(/*ref=*/1, /*buy=*/true, /*shares=*/100, /*price=*/1'000'000);  // $100.00
  w.add_order(/*ref=*/2, /*buy=*/false, /*shares=*/50, /*price=*/1'010'000);  // $101.00
  w.add_order(/*ref=*/3, /*buy=*/true, /*shares=*/200, /*price=*/999'000);    // $99.90
  w.execute(/*ref=*/1, /*shares=*/40);                                         // 100 -> 60
  w.cancel(/*ref=*/3, /*shares=*/50);                                          // 200 -> 150
  w.delete_order(/*ref=*/2);                                                   // ask gone
  w.replace(/*orig=*/1, /*new=*/4, /*shares=*/60, /*price=*/1'005'000);        // $100.50

  BookConfig cfg{1, 100'000, 1024};
  FlatBook book(cfg);
  ItchReplayer replayer(book); // divisor 100 -> ticks are cents

  const std::size_t msgs =
      parse_stream(w.bytes().data(), w.bytes().size(), replayer);

  EXPECT_EQ(msgs, 8u); // system event + 3 adds + execute + cancel + delete + replace

  const auto& s = replayer.stats();
  EXPECT_EQ(s.adds, 3u);
  EXPECT_EQ(s.executes, 1u);
  EXPECT_EQ(s.cancels, 1u);
  EXPECT_EQ(s.deletes, 1u);
  EXPECT_EQ(s.replaces, 1u);
  EXPECT_EQ(s.skipped_out_of_band, 0u);

  // Final book: ref4 buy @10050 qty60, ref3 buy @9990 qty150, no asks.
  ASSERT_TRUE(book.best_bid().has_value());
  EXPECT_EQ(*book.best_bid(), 10'050); // 1'005'000 / 100
  EXPECT_FALSE(book.best_ask().has_value());
  EXPECT_EQ(book.size(), 2u);
  EXPECT_EQ(book.total_quantity(), 210u); // 60 + 150
}

TEST(Itch, OutOfBandOrdersSkipped) {
  ItchWriter w;
  w.add_order(1, true, 100, 100'000'000); // $10,000 -> tick 1,000,000 > band
  w.add_order(2, true, 100, 1'000'000);   // $100 -> tick 10,000 ok

  BookConfig cfg{1, 100'000, 64};
  FlatBook book(cfg);
  ItchReplayer replayer(book);
  parse_stream(w.bytes().data(), w.bytes().size(), replayer);

  EXPECT_EQ(replayer.stats().adds, 1u);
  EXPECT_EQ(replayer.stats().skipped_out_of_band, 1u);
  EXPECT_EQ(book.size(), 1u);
}
