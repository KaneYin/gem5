#include "mem/dprh_filter.hh"
namespace gem5 {
DprhFilter::DprhFilter(uint32_t accuracy_epoch, uint8_t accept_pct)
    : epoch(accuracy_epoch ? accuracy_epoch : 64), acceptPct(accept_pct) {}

uint8_t DprhFilter::accuracyPct() const { return cachedAccuracy; }

void DprhFilter::noteUseful()  { ++used;    }
void DprhFilter::noteEvicted() { ++evicted; }

bool DprhFilter::accept() {
    if (++sinceRecompute >= epoch) {
        uint64_t total = used + evicted;
        cachedAccuracy = total ? (uint8_t)((used * 100) / total) : 100;
        sinceRecompute = 0;
    }
    // Accept when the prefetcher is at least acceptPct accurate this epoch.
    return cachedAccuracy >= acceptPct;
}
} // namespace gem5
