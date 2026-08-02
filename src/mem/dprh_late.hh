/*
 * DPRH: pure late-prefetch predicate (research_plan.md §5, Phase 1).
 *
 * A prefetch is "late" when the demand it was meant to cover arrives while the
 * prefetch is still queued at the memory controller (not yet serviced) -- the
 * timeliness headroom DPRH's Phase-2 harvesting targets. Factored out of
 * MemCtrl::addToReadQueue so the block-match logic is unit-testable without a
 * full controller (mirrors dprh_hslot.hh / dprh_demand_first.hh). Holds no gem5
 * timing state and mutates nothing.
 */
#ifndef __MEM_DPRH_LATE_HH__
#define __MEM_DPRH_LATE_HH__

#include <cstdint>
#include <vector>

namespace gem5
{
namespace dprh
{

/**
 * True iff @p a and @p b fall in the same burst-aligned block.
 * @pre burstSize is a non-zero power of two (DDR4 guarantees this via
 * bytesPerBurst()); the mask idiom is exact only under that precondition. The
 * Task-2 call site asserts it rather than this header (kept assertion-free).
 */
inline bool
blocksMatch(uint64_t a, uint64_t b, uint64_t burstSize)
{
    const uint64_t mask = ~(burstSize - 1);
    return (a & mask) == (b & mask);
}

/** One queued read as seen by the late-prefetch scan. */
struct QueuedRead
{
    uint64_t addr;
    bool isPrefetch;
};

/**
 * True iff an accepted PREFETCH to the incoming demand's block is still queued
 * -- i.e. the demand is covered by a not-yet-serviced prefetch (a late
 * prefetch). Only prefetch entries count; a queued demand to the same block is
 * ordinary read pressure, not lateness.
 */
inline bool
demandHasQueuedPrefetch(const std::vector<QueuedRead> &queued,
                        uint64_t demandAddr, uint64_t burstSize)
{
    for (const auto &r : queued) {
        if (r.isPrefetch && blocksMatch(r.addr, demandAddr, burstSize)) {
            return true;
        }
    }
    return false;
}

} // namespace dprh
} // namespace gem5

#endif // __MEM_DPRH_LATE_HH__
