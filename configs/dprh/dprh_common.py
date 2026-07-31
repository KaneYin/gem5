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

    The three scheduler-layer flags (enable_filter, demand_first, enable_dprh)
    are MemCtrl params added in Tasks 6-8 and set here per config profile:
      B0   -> all off (stock FR-FCFS, no prefetcher via run_se.py)
      B1   -> enable_filter (filter accepts stream) + FR-FCFS
      B2   -> enable_filter + demand_first (demand-first FR-FCFS)
      DPRH -> enable_filter + enable_dprh (gate is a Phase 0 no-op skeleton)
    All default False in MemCtrl.py, so an unflagged build == stock gem5.
    """
    ctrl = MemCtrl()
    ctrl.dram = DDR4_2400_16x4(range=AddrRange("2GB"))  # D-A0b default
    # Freeze address map + page policy explicitly (these already match stock
    # gem5 defaults for DDR4_2400_16x4, set here so the freeze is auditable and
    # robust against upstream default drift). DRAMInterface timing is NOT
    # touched (hard invariant).
    ctrl.dram.addr_mapping = FROZEN["addr_mapping"]
    ctrl.dram.page_policy = FROZEN["page_policy"]
    ctrl.mem_sched_policy = FROZEN["mem_sched_policy"]
    ctrl.read_buffer_size = FROZEN["read_buffer_size"]
    ctrl.write_buffer_size = FROZEN["write_buffer_size"]
    # Scheduler-layer flags (params added in Tasks 6-8). Default-off keeps an
    # unflagged build == stock gem5; here we set them per config profile.
    ctrl.enable_filter = config in ("B1", "B2", "DPRH")
    ctrl.demand_first = config == "B2"
    ctrl.enable_dprh = config == "DPRH"
    return ctrl


# FIX-6: single source of truth for the H_slot-relevant frozen system params.
# Row-hit availability -- hence H_slot itself -- depends on page policy, address
# mapping, and read-queue depth, so these are frozen and self-documented by every
# run (see frozen_summary()). Mirrored as a table in results/PHASE_LOG.md.
FROZEN = dict(
    addr_mapping="RoRaBaCoCh",
    page_policy="open_adaptive",
    dram="DDR4_2400_16x4",
    clk="4GHz",
    mem_sched_policy="frfcfs",
    read_buffer_size=64,
    write_buffer_size=64,
    channels=1,                 # one MemCtrl/DRAM interface in SE + tgen configs
    ranks_per_channel=2,        # DDR4_2400_16x4 default
    banks_per_rank=16,          # DDR4_2400_16x4 default
    prefetcher_primary="SignaturePathPrefetcher (SPP)",       # D-A0
    prefetcher_sensitivity="StridePrefetcher",                # D-A0 (was SPP)
)


def frozen_summary():
    """One-line, greppable dump of the frozen params so every simout
    self-documents its config (FIX-6). A results-aggregation step can refuse to
    merge runs whose '[dprh-frozen]' line disagrees."""
    keys = ("dram", "addr_mapping", "page_policy", "mem_sched_policy",
            "read_buffer_size", "write_buffer_size", "channels",
            "ranks_per_channel", "banks_per_rank", "clk",
            "prefetcher_primary", "prefetcher_sensitivity")
    return "[dprh-frozen] " + " ".join(f"{k}={FROZEN[k]}" for k in keys)
