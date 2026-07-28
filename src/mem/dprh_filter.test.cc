#include <gtest/gtest.h>
#include "mem/dprh_filter.hh"
using namespace gem5;
TEST(DprhFilter, AcceptsDuringWarmup) {
    DprhFilter f(/*epoch=*/64, /*accept_pct=*/50);
    EXPECT_TRUE(f.accept());              // no feedback yet -> optimistic
}
TEST(DprhFilter, DropsWhenAccuracyLow) {
    DprhFilter f(/*epoch=*/4, /*accept_pct=*/50);
    for (int i = 0; i < 4; ++i) f.noteEvicted();  // 0% accurate
    for (int i = 0; i < 4; ++i) f.accept();        // force one recompute
    EXPECT_FALSE(f.accept());
}
