#ifndef __MEM_DPRH_FILTER_HH__
#define __MEM_DPRH_FILTER_HH__
#include <cstdint>
namespace gem5 {
/**
 * Option B stand-in for MSF (research_plan.md D2). Interface-compatible with
 * the eventual perceptron (Option A): a single accept/drop decision per
 * prefetch, made at read-queue enqueue, plus feedback hooks for online
 * prefetcher-accuracy tracking. NOTHING here is MSF-internal.
 */
class DprhFilter {
  public:
    DprhFilter(uint32_t accuracy_epoch, uint8_t accept_pct);
    /** Inference: true => accept prefetch into read queue; false => drop. */
    bool accept();
    /** Feedback: a prefetched line was consumed (useful). */
    void noteUseful();
    /** Feedback: a prefetched line was evicted unused (useless). */
    void noteEvicted();
    /** Current binned accuracy in [0,100]. */
    uint8_t accuracyPct() const;
  private:
    uint32_t epoch;        // recompute cadence (MSF: every 64 requests)
    uint8_t  acceptPct;    // accept threshold on running accuracy
    uint64_t used = 0, evicted = 0, sinceRecompute = 0;
    uint8_t  cachedAccuracy = 100;  // optimistic warm-up (accept while learning)
};
} // namespace gem5
#endif
