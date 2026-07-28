"""Frozen DPRH system definition (research_plan.md §3.2 + A0). Do not edit mid-project.

Single source of the frozen system knobs (cache sizes, DRAM device, address map,
page policy, queue sizes, prefetcher selection). Imported by every runnable
config. One responsibility: the frozen system definition.

Decisions (locked, see plan/refs/A0_findings.md):
  D-A0  = SPP primary prefetcher (SignaturePathPrefetcher), Stride sensitivity.
  D-A0b = DDR4_2400_16x4 DRAM device.
"""
import m5
from m5.objects import (
    Cache,
    L2XBar,
    SystemXBar,
    StridePrefetcher,
    SignaturePathPrefetcher,
    MemCtrl,
    DDR4_2400_16x4,
    AddrRange,
)


# --- Caches (single-core; research_plan §3.2) ---
class L1I(Cache):
    size = "32kB"
    assoc = 8
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 20


class L1D(Cache):
    size = "32kB"
    assoc = 8
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 16
    tgts_per_mshr = 20


class L2(Cache):
    size = "256kB"
    assoc = 8
    tag_latency = 10
    data_latency = 10
    response_latency = 10
    mshrs = 32  # >=32 MSHR: prefetch must not bottleneck
    tgts_per_mshr = 20


class LLC(Cache):
    size = "2MB"
    assoc = 16
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 64
    tgts_per_mshr = 20


# Prefetcher profiles (A0/D-A0): SPP primary, Stride sensitivity.
def make_prefetcher(kind):
    """Return a prefetcher object with prefetch_on_access enabled, or None.

    Note (grounded in real gem5 v25.1.0.1): `prefetch_on_access` is a param on
    the *prefetcher* object (BasePrefetcher.py), not on the cache. The plan's
    Task 3 hard-requirement (b) is satisfied by setting it on the prefetcher and
    attaching the prefetcher to the L2 cache (see run_se.py).
    """
    if kind == "spp":
        pf = SignaturePathPrefetcher()
        pf.prefetch_on_access = True
        return pf
    if kind == "stride":
        pf = StridePrefetcher()
        pf.prefetch_on_access = True
        return pf
    if kind == "none":
        return None
    raise ValueError(kind)


def make_mem_ctrl(config):
    """config in {B0,B1,B2,DPRH}: only the scheduler-layer flags differ.

    Phase 0 note: the three scheduler-layer flags (enable_filter, demand_first,
    enable_dprh) are MemCtrl params that DO NOT EXIST until Tasks 6-8. Per the
    plan (Task 3 Step 1 note), they are intentionally OMITTED here and appended
    in Task 8 Step 2. Until then this returns a controller identical to stock
    FR-FCFS gem5, so B0 (which needs none of the flags) runs cleanly.
    """
    ctrl = MemCtrl()
    ctrl.dram = DDR4_2400_16x4(range=AddrRange("2GB"))  # D-A0b default
    # Freeze address map + page policy explicitly (these already match stock
    # gem5 defaults for DDR4_2400_16x4, set here so the freeze is auditable and
    # robust against upstream default drift). DRAMInterface timing is NOT
    # touched (hard invariant).
    ctrl.dram.addr_mapping = FROZEN["addr_mapping"]
    ctrl.dram.page_policy = FROZEN["page_policy"]
    ctrl.mem_sched_policy = "frfcfs"
    ctrl.read_buffer_size = 64
    ctrl.write_buffer_size = 64
    return ctrl


FROZEN = dict(
    addr_mapping="RoRaBaCoCh",
    page_policy="open_adaptive",
    dram="DDR4_2400_16x4",
    clk="4GHz",
)
