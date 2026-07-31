/*
 * DPRH: per-bank demand-first (PADC) eligibility for the B2 baseline.
 *
 * B2 prioritizes demands over prefetches. PADC (Lee et al., "Prefetch-Aware
 * DRAM Controllers") applies that priority *within a bank*: a prefetch to bank
 * X is deprioritized only by demands to bank X; across banks the ordinary
 * row-hit FR-FCFS arbiter decides. This differs from *global* demand-first,
 * where any ready demand suppresses every prefetch (which makes B2
 * artificially strong). The predicate below is factored out of
 * MemCtrl::chooseNext so the semantics are unit-testable without a full
 * controller; it holds no timing state and mutates nothing.
 */
#ifndef __MEM_DPRH_DEMAND_FIRST_HH__
#define __MEM_DPRH_DEMAND_FIRST_HH__

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace gem5
{
namespace dprh
{

/** Encode a DRAM bank identity (channel, rank, bank) into a single key. */
inline uint32_t
bankKey(uint8_t pseudoChannel, uint8_t rank, uint8_t bank)
{
    return (uint32_t(pseudoChannel) << 16) | (uint32_t(rank) << 8) | bank;
}

/**
 * Per-bank demand-first per-request predicate: a prefetch is eligible for
 * issue only if its bank has no queued demand; demands are always eligible.
 * Eligible commands then arbitrate by ordinary FR-FCFS (row-hit-first).
 */
inline bool
demandFirstEligible(bool isPrefetch, bool bankHasDemand)
{
    return !isPrefetch || !bankHasDemand;
}

/** One queued read as seen by per-bank demand-first arbitration. */
struct BankReq
{
    uint32_t bank; // bankKey(...)
    bool isPrefetch;
};

/**
 * Unit-testable wrapper: return an eligibility mask parallel to @p q under
 * per-bank demand-first. A prefetch is eligible iff no demand shares its bank.
 */
inline std::vector<bool>
demandFirstEligibility(const std::vector<BankReq> &q)
{
    std::set<uint32_t> demandBanks;
    for (const auto &r : q) {
        if (!r.isPrefetch) {
            demandBanks.insert(r.bank);
        }
    }
    std::vector<bool> elig(q.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        elig[i] = demandFirstEligible(q[i].isPrefetch,
                                      demandBanks.count(q[i].bank) != 0);
    }
    return elig;
}

} // namespace dprh
} // namespace gem5

#endif // __MEM_DPRH_DEMAND_FIRST_HH__
