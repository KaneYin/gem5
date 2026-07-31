#include "mem/dprh_demand_first.hh"

#include <gtest/gtest.h>

#include <vector>

using namespace gem5;
using dprh::bankKey;
using dprh::BankReq;
using dprh::demandFirstEligibility;

namespace
{
// Distinct bank keys for the tests.
const uint32_t B0 = bankKey(/*ch=*/0, /*rank=*/0, /*bank=*/0);
const uint32_t B1 = bankKey(/*ch=*/0, /*rank=*/0, /*bank=*/1);
} // namespace

// FIX-2 discriminating case: a demand to bank0 and a row-hit prefetch to
// bank1. Under per-bank demand-first BOTH are eligible (bank1 has no demand),
// so the cross-bank FR-FCFS arbiter can pick the prefetch. Global demand-first
// would have suppressed the prefetch -- this is the bug the test would have
// caught.
TEST(DprhDemandFirst, DifferentBankPrefetchIsEligible)
{
    std::vector<BankReq> q = {
        {B0, /*isPrefetch=*/false}, // demand -> bank0
        {B1, /*isPrefetch=*/true},  // prefetch -> bank1
    };
    auto elig = demandFirstEligibility(q);
    EXPECT_TRUE(elig[0]); // demand always eligible
    EXPECT_TRUE(elig[1]); // prefetch to demand-free bank1 -> eligible
}

// Same-bank suppression: a prefetch to a bank that also has a queued demand is
// NOT eligible (demand-first within the bank).
TEST(DprhDemandFirst, SameBankPrefetchSuppressed)
{
    std::vector<BankReq> q = {
        {B0, /*isPrefetch=*/false}, // demand -> bank0
        {B0, /*isPrefetch=*/true},  // prefetch -> bank0 (same bank)
    };
    auto elig = demandFirstEligibility(q);
    EXPECT_TRUE(elig[0]);  // demand eligible
    EXPECT_FALSE(elig[1]); // same-bank prefetch suppressed
}

// Mixed: bank0 has a demand (its prefetch suppressed); bank1 has only a
// prefetch (eligible).
TEST(DprhDemandFirst, PerBankMix)
{
    std::vector<BankReq> q = {
        {B0, false}, // demand -> bank0
        {B0, true},  // prefetch -> bank0  (suppressed)
        {B1, true},  // prefetch -> bank1  (eligible)
    };
    auto elig = demandFirstEligibility(q);
    EXPECT_TRUE(elig[0]);
    EXPECT_FALSE(elig[1]);
    EXPECT_TRUE(elig[2]);
}

// No demands at all: every prefetch is eligible (equivalent to plain FR-FCFS).
TEST(DprhDemandFirst, AllPrefetchesEligibleWhenNoDemand)
{
    std::vector<BankReq> q = {
        {B0, true},
        {B1, true},
    };
    auto elig = demandFirstEligibility(q);
    EXPECT_TRUE(elig[0]);
    EXPECT_TRUE(elig[1]);
}

// bankKey distinguishes channel/rank/bank so cross-bank keys never collide.
TEST(DprhDemandFirst, BankKeyDistinct)
{
    EXPECT_NE(bankKey(0, 0, 0), bankKey(0, 0, 1));
    EXPECT_NE(bankKey(0, 0, 1), bankKey(0, 1, 0));
    EXPECT_NE(bankKey(0, 1, 0), bankKey(1, 0, 0));
}
