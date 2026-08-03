#include "mem/dprh_hslot.hh"

#include <gtest/gtest.h>

using namespace gem5;
using dprh::classifyHslotCycle;
using dprh::HslotReason;

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
