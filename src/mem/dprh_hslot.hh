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

#include <cstddef>

#include "base/types.hh"

namespace gem5
{
namespace dprh
{

/**
 * Whether one queued request's next column command is ready at the same
 * seamless-issue boundary used by gem5's FR-FCFS selector.
 *
 * The caller obtains all four inputs from the memory interface's existing
 * refresh, bank-preparation, and RD/WR timing state. Keeping this final
 * boolean combination pure makes the boundary cases unit-testable without
 * introducing a second timing model.
 */
inline bool
frfcfsCommandReady(bool rankRefreshIdle, bool bankPrepReady, Tick colAllowedAt,
                   Tick minColAt)
{
    return rankRefreshIdle && bankPrepReady && colAllowedAt <= minColAt;
}

/**
 * H_slot is defined over non-empty FR-FCFS READ scheduling decisions. This
 * deliberately includes singleton queues and excludes FCFS and write-drain
 * arbitration.
 */
inline bool
shouldRecordHslotDecision(std::size_t queueSize, bool isFrfcfs, bool readBus)
{
    return queueSize != 0 && isFrfcfs && readBus;
}

/** Why a scheduling cycle did (or did not) count as an H_slot. */
enum class HslotReason
{
    Harvestable, // true H_slot: a ready, row-hit, turnaround-safe pf exists
    DemandReady, // a timing-legal demand blocks harvesting
    NoPrefetch,  // no timing-ready accepted prefetch queued
    PfNotRowHit, // ready prefetch(es) present, but none is a row hit
    TurnaroundUnsafe // ready row-hit prefetch(es), but none is turnaround-safe
};

/** Per-cycle H_slot accounting verdict. */
struct HslotVerdict
{
    bool hslot;              // increment cyclesHslot (the TRUE predicate)
    bool readyPrefetchProxy; // increment cyclesReadyPrefetchNoDemand (upper
                             // bound)
    HslotReason reason;      // decomposition bin
};

/** Single-pass summaries accumulated while inspecting one read queue. */
struct HslotCycleInputs
{
    bool anyLegalDemand = false;
    bool anyReadyPrefetch = false;
    bool anyReadyRowHit = false;
    bool anyHarvestable = false;
};

/**
 * Fold one queue entry into the H_slot summaries. A ready demand dominates the
 * final verdict; a prefetch must additionally be a row hit and preserve the
 * current bus direction to become harvestable.
 */
inline void
observeHslotCandidate(HslotCycleInputs &inputs, bool isPrefetch,
                      bool commandReady, bool rowHit, bool turnaroundSafe)
{
    if (!commandReady) {
        return;
    }

    if (!isPrefetch) {
        inputs.anyLegalDemand = true;
        return;
    }

    inputs.anyReadyPrefetch = true;
    if (!rowHit) {
        return;
    }

    inputs.anyReadyRowHit = true;
    if (turnaroundSafe) {
        inputs.anyHarvestable = true;
    }
}

/**
 * Classify one FR-FCFS scheduling cycle for H_slot accounting.
 *
 * Inputs are single-pass summaries of the per-channel read queue this cycle:
 *   @param anyLegalDemand   a timing-legal demand read exists
 *   @param anyReadyPrefetch a timing-ready accepted prefetch exists (the proxy
 *                           upper bound on H_slot)
 *   @param anyReadyRowHit   a timing-ready accepted prefetch that is ALSO a
 * row hit exists
 *   @param anyHarvestable   a timing-ready, row-hit prefetch that is ALSO
 *                           turnaround-safe exists (the TRUE H_slot condition)
 *
 * The caller must supply monotone inputs:
 *   anyHarvestable ==> anyReadyRowHit ==> anyReadyPrefetch.
 * The proxy (readyPrefetchProxy) is the upper bound H_slot used to admit
 * before FIX-1; the gap proxy - true measures how loose that upper bound was.
 */
inline HslotVerdict
classifyHslotCycle(bool anyLegalDemand, bool anyReadyPrefetch,
                   bool anyReadyRowHit, bool anyHarvestable)
{
    if (anyLegalDemand) {
        return {false, false, HslotReason::DemandReady};
    }

    const bool proxy = anyReadyPrefetch;
    if (anyHarvestable) {
        return {true, proxy, HslotReason::Harvestable};
    }
    if (!anyReadyPrefetch) {
        return {false, proxy, HslotReason::NoPrefetch};
    }
    if (!anyReadyRowHit) {
        return {false, proxy, HslotReason::PfNotRowHit};
    }
    return {false, proxy, HslotReason::TurnaroundUnsafe};
}

/** Convenience overload for the queue-summary representation. */
inline HslotVerdict
classifyHslotCycle(const HslotCycleInputs &inputs)
{
    return classifyHslotCycle(inputs.anyLegalDemand, inputs.anyReadyPrefetch,
                              inputs.anyReadyRowHit, inputs.anyHarvestable);
}

/**
 * Phase 1 refinement (research_plan.md §5, AGED_DEMAND bin). A cycle that is a
 * true H_slot (harvestable) is *aged-demand blocked* iff some queued demand
 * has aged past A_guard. This is the load-bearing input to the Phase-3 A_guard
 * sweep: it counts H_slot cycles DPRH's aged-demand guard would decline to
 * harvest. It never changes cyclesHslot (the raw predicate); it is reported
 * alongside it.
 */
inline bool
hslotAgedBlocked(bool hslot, bool anyAgedDemand)
{
    return hslot && anyAgedDemand;
}

} // namespace dprh
} // namespace gem5

#endif // __MEM_DPRH_HSLOT_HH__
