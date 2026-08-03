#include "mem/dprh_late.hh"

#include <gtest/gtest.h>

#include <vector>

using namespace gem5;
using dprh::blocksMatch;
using dprh::demandHasQueuedPrefetch;
using dprh::QueuedRead;

// Burst-block equality is computed on burst-aligned addresses.
TEST(DprhLate, BlocksMatchAlignsToBurst)
{
    // burstSize = 64: 0x1000 and 0x1030 are the same block; 0x1040 is not.
    EXPECT_TRUE(blocksMatch(0x1000, 0x1030, 64));
    EXPECT_FALSE(blocksMatch(0x1000, 0x1040, 64));
}

// A demand is "late" iff a PREFETCH to its block is still queued when it
// arrives.
TEST(DprhLate, DemandLateWhenPrefetchToBlockQueued)
{
    std::vector<QueuedRead> q = {
        {0x2000, /*isPrefetch=*/true},  // queued prefetch to block 0x2000
        {0x3000, /*isPrefetch=*/false}, // a queued demand (ignored)
    };
    EXPECT_TRUE(demandHasQueuedPrefetch(q, /*demandAddr=*/0x2038, 64));
}

// Not late: only a queued DEMAND (not a prefetch) shares the block.
TEST(DprhLate, DemandNotLateWhenOnlyDemandQueued)
{
    std::vector<QueuedRead> q = {{0x3000, /*isPrefetch=*/false}};
    EXPECT_FALSE(demandHasQueuedPrefetch(q, /*demandAddr=*/0x3000, 64));
}

// Not late: prefetch queued for a DIFFERENT block.
TEST(DprhLate, DemandNotLateForDifferentBlock)
{
    std::vector<QueuedRead> q = {{0x2000, /*isPrefetch=*/true}};
    EXPECT_FALSE(demandHasQueuedPrefetch(q, /*demandAddr=*/0x4000, 64));
}

// Empty queue: no queued prefetch, so no demand is late.
TEST(DprhLate, EmptyQueueNeverLate)
{
    std::vector<QueuedRead> q;
    EXPECT_FALSE(demandHasQueuedPrefetch(q, /*demandAddr=*/0x1000, 64));
}
