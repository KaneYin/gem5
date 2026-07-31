/*
 * DPRH: pure H_slot cycle classification.
 *
 * H_slot (research_plan.md §5) is the fraction of FR-FCFS scheduling cycles
 * with no timing-legal demand command but at least one timing-ready, row-hit,
 * turnaround-safe accepted prefetch. The per-cycle verdict below is factored
 * out of MemCtrl::chooseNext so it can be unit-tested without instantiating a
 * full controller (mirrors the self-contained dprh_filter split). It contains
 * no gem5 timing state and mutates nothing.
 */
#ifndef __MEM_DPRH_HSLOT_HH__
#define __MEM_DPRH_HSLOT_HH__

namespace gem5
{
namespace dprh
{

/** Why a scheduling cycle did (or did not) count as an H_slot. */
enum class HslotReason
{
    Harvestable,       // true H_slot: a ready, row-hit, turnaround-safe pf exists
    DemandReady,       // a timing-legal demand blocks harvesting
    NoPrefetch,        // no timing-ready accepted prefetch queued
    PfNotRowHit,       // ready prefetch(es) present, but none is a row hit
    TurnaroundUnsafe   // ready row-hit prefetch(es), but none is turnaround-safe
};

/** Per-cycle H_slot accounting verdict. */
struct HslotVerdict
{
    bool hslot;              // increment cyclesHslot (the TRUE predicate)
    bool readyPrefetchProxy; // increment cyclesReadyPrefetchNoDemand (upper bound)
    HslotReason reason;      // decomposition bin
};

/**
 * Classify one FR-FCFS scheduling cycle for H_slot accounting.
 *
 * Inputs are single-pass summaries of the per-channel read queue this cycle:
 *   @param anyLegalDemand   a timing-legal demand read exists
 *   @param anyReadyPrefetch a timing-ready accepted prefetch exists (the proxy
 *                           upper bound on H_slot)
 *   @param anyReadyRowHit   a timing-ready accepted prefetch that is ALSO a row
 *                           hit exists
 *   @param anyHarvestable   a timing-ready, row-hit prefetch that is ALSO
 *                           turnaround-safe exists (the TRUE H_slot condition)
 *
 * The caller must supply monotone inputs:
 *   anyHarvestable ==> anyReadyRowHit ==> anyReadyPrefetch.
 * The proxy (readyPrefetchProxy) is the upper bound H_slot used to admit before
 * FIX-1; the gap proxy - true measures how loose that upper bound was.
 */
inline HslotVerdict
classifyHslotCycle(bool anyLegalDemand, bool anyReadyPrefetch,
                   bool anyReadyRowHit, bool anyHarvestable)
{
    if (anyLegalDemand)
        return {false, false, HslotReason::DemandReady};

    const bool proxy = anyReadyPrefetch;
    if (anyHarvestable)
        return {true, proxy, HslotReason::Harvestable};
    if (!anyReadyPrefetch)
        return {false, proxy, HslotReason::NoPrefetch};
    if (!anyReadyRowHit)
        return {false, proxy, HslotReason::PfNotRowHit};
    return {false, proxy, HslotReason::TurnaroundUnsafe};
}

} // namespace dprh
} // namespace gem5

#endif // __MEM_DPRH_HSLOT_HH__
