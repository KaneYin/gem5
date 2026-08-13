#include "mem/dprh_hslot.hh"

#include <gtest/gtest.h>

using namespace gem5;
using dprh::classifyHslotCycle;
using dprh::frfcfsCommandReady;
using dprh::HslotCycleInputs;
using dprh::HslotReason;
using dprh::observeHslotCandidate;
using dprh::shouldRecordHslotDecision;

// A singleton demand is still an FR-FCFS READ scheduling decision. When its
// column command is legal, it must enter the denominator and block H_slot.
TEST(DprhHslot, SingletonDemandIsAccountedAndBlocksHarvest)
{
    EXPECT_TRUE(shouldRecordHslotDecision(/*queueSize=*/1,
                                          /*isFrfcfs=*/true,
                                          /*readBus=*/true));
    HslotCycleInputs inputs;
    observeHslotCandidate(inputs, /*isPrefetch=*/false,
                          /*commandReady=*/true, /*rowHit=*/false,
                          /*turnaroundSafe=*/true);
    auto v = classifyHslotCycle(inputs);
    EXPECT_FALSE(v.hslot);
    EXPECT_EQ(v.reason, HslotReason::DemandReady);
}

// A singleton, command-ready row-hit prefetch is a genuine harvestable slot;
// the old queue-size fast path skipped this decision entirely.
TEST(DprhHslot, SingletonRowHitPrefetchCountsHslot)
{
    HslotCycleInputs inputs;
    observeHslotCandidate(inputs, /*isPrefetch=*/true,
                          /*commandReady=*/true, /*rowHit=*/true,
                          /*turnaroundSafe=*/true);
    auto v = classifyHslotCycle(inputs);
    EXPECT_TRUE(v.hslot);
    EXPECT_TRUE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::Harvestable);
}

// Refresh-busy ranks cannot contribute a legal demand or a ready prefetch,
// even if the bank and column timing inputs would otherwise be ready.
TEST(DprhHslot, RankRefreshBlockedIsNotCommandReady)
{
    const bool ready = frfcfsCommandReady(
        /*rankRefreshIdle=*/false, /*bankPrepReady=*/true,
        /*colAllowedAt=*/100, /*minColAt=*/100);
    EXPECT_FALSE(ready);

    HslotCycleInputs inputs;
    observeHslotCandidate(inputs, /*isPrefetch=*/true, ready,
                          /*rowHit=*/true, /*turnaroundSafe=*/true);
    EXPECT_EQ(classifyHslotCycle(inputs).reason, HslotReason::NoPrefetch);
}

// Rank-idle alone is insufficient. The existing per-bank column-ready tick is
// load-bearing, including its exact <= boundary.
TEST(DprhHslot, RankIdleButReadColumnNotYetAllowedIsNotReady)
{
    EXPECT_FALSE(frfcfsCommandReady(
        /*rankRefreshIdle=*/true, /*bankPrepReady=*/true,
        /*colAllowedAt=*/101, /*minColAt=*/100));
    EXPECT_TRUE(frfcfsCommandReady(
        /*rankRefreshIdle=*/true, /*bankPrepReady=*/true,
        /*colAllowedAt=*/100, /*minColAt=*/100));
    EXPECT_FALSE(frfcfsCommandReady(
        /*rankRefreshIdle=*/true, /*bankPrepReady=*/false,
        /*colAllowedAt=*/100, /*minColAt=*/100));
}

// H_slot's denominator includes FR-FCFS read arbitration only. Write-drain,
// FCFS, and empty-queue visits are deliberately excluded.
TEST(DprhHslot, ReadWriteBusStateExclusion)
{
    EXPECT_TRUE(shouldRecordHslotDecision(1, true, true));
    EXPECT_FALSE(shouldRecordHslotDecision(1, true, false));
    EXPECT_FALSE(shouldRecordHslotDecision(1, false, true));
    EXPECT_FALSE(shouldRecordHslotDecision(0, true, true));
}

// In a mixed queue, a blocked demand plus a ready row-hit prefetch is H_slot;
// making any demand command-ready must dominate and close the slot.
TEST(DprhHslot, MixedDemandPrefetchQueue)
{
    HslotCycleInputs inputs;
    observeHslotCandidate(inputs, /*isPrefetch=*/false,
                          /*commandReady=*/false, /*rowHit=*/false,
                          /*turnaroundSafe=*/true);
    observeHslotCandidate(inputs, /*isPrefetch=*/true,
                          /*commandReady=*/true, /*rowHit=*/true,
                          /*turnaroundSafe=*/true);
    EXPECT_EQ(classifyHslotCycle(inputs).reason, HslotReason::Harvestable);

    observeHslotCandidate(inputs, /*isPrefetch=*/false,
                          /*commandReady=*/true, /*rowHit=*/false,
                          /*turnaroundSafe=*/true);
    auto v = classifyHslotCycle(inputs);
    EXPECT_FALSE(v.hslot);
    EXPECT_EQ(v.reason, HslotReason::DemandReady);
}

// FIX-1 acceptance case (a): a timing-ready prefetch that is a row CONFLICT
// (not a row hit), with no legal demand. cyclesHslot must NOT increment; the
// proxy (cyclesReadyPrefetchNoDemand) must. Reason: PfNotRowHit.
TEST(DprhHslot, RowConflictReadyPrefetchIsProxyOnly)
{
    auto v = classifyHslotCycle(/*anyLegalDemand=*/false,
                                /*anyReadyPrefetch=*/true,
                                /*anyReadyRowHit=*/false,
                                /*anyHarvestable=*/false);
    EXPECT_FALSE(v.hslot);
    EXPECT_TRUE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::PfNotRowHit);
}

// FIX-1 acceptance case (b): a timing-ready row-hit prefetch, same-direction
// bus (turnaround-safe), no legal demand. Both counters increment.
TEST(DprhHslot, RowHitTurnaroundSafeCountsHslot)
{
    auto v = classifyHslotCycle(/*anyLegalDemand=*/false,
                                /*anyReadyPrefetch=*/true,
                                /*anyReadyRowHit=*/true,
                                /*anyHarvestable=*/true);
    EXPECT_TRUE(v.hslot);
    EXPECT_TRUE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::Harvestable);
}

// FIX-1 acceptance case (c): a timing-ready row-hit prefetch that would
// require a bus turnaround. cyclesHslot must NOT increment; proxy does.
// Reason: TurnaroundUnsafe.
TEST(DprhHslot, RowHitButTurnaroundUnsafeIsProxyOnly)
{
    auto v = classifyHslotCycle(/*anyLegalDemand=*/false,
                                /*anyReadyPrefetch=*/true,
                                /*anyReadyRowHit=*/true,
                                /*anyHarvestable=*/false);
    EXPECT_FALSE(v.hslot);
    EXPECT_TRUE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::TurnaroundUnsafe);
}

// A timing-legal demand blocks harvesting: neither counter increments.
TEST(DprhHslot, LegalDemandBlocksHarvest)
{
    auto v = classifyHslotCycle(/*anyLegalDemand=*/true,
                                /*anyReadyPrefetch=*/true,
                                /*anyReadyRowHit=*/true,
                                /*anyHarvestable=*/true);
    EXPECT_FALSE(v.hslot);
    EXPECT_FALSE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::DemandReady);
}

// No accepted prefetch queued at all: proxy stays flat, reason NoPrefetch.
TEST(DprhHslot, NoPrefetchNoProxy)
{
    auto v = classifyHslotCycle(/*anyLegalDemand=*/false,
                                /*anyReadyPrefetch=*/false,
                                /*anyReadyRowHit=*/false,
                                /*anyHarvestable=*/false);
    EXPECT_FALSE(v.hslot);
    EXPECT_FALSE(v.readyPrefetchProxy);
    EXPECT_EQ(v.reason, HslotReason::NoPrefetch);
}

// The gap term is well-formed: whenever cyclesHslot counts, the proxy also
// counts (hslot ==> proxy), so cyclesHslotUpperGap = proxy - hslot >= 0.
TEST(DprhHslot, HarvestableImpliesProxy)
{
    auto v = classifyHslotCycle(false, true, true, true);
    EXPECT_TRUE(v.hslot);
    EXPECT_TRUE(
        v.readyPrefetchProxy); // gap increment (proxy && !hslot) == false
}

// Phase 1: a true H_slot cycle is "aged-demand blocked" iff a queued demand
// has aged past A_guard -- DPRH's Phase-2 aged-demand guard would suppress the
// harvest even though no demand is timing-ready this cycle.
TEST(DprhHslot, AgedBlockedOnlyWhenHslotAndAgedDemand)
{
    using dprh::hslotAgedBlocked;
    EXPECT_TRUE(hslotAgedBlocked(/*hslot=*/true, /*anyAgedDemand=*/true));
    EXPECT_FALSE(hslotAgedBlocked(/*hslot=*/true, /*anyAgedDemand=*/false));
    EXPECT_FALSE(hslotAgedBlocked(/*hslot=*/false, /*anyAgedDemand=*/true));
    EXPECT_FALSE(hslotAgedBlocked(/*hslot=*/false, /*anyAgedDemand=*/false));
}
