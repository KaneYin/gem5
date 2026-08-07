"""DPRH synthetic-traffic harness (R6). One responsibility: drive one synthetic run.

Two PyTrafficGen DRAM generators feed one MemCtrl (from dprh_common):
  - a "demand" generator (untagged reads),
  - a "prefetch" generator whose requests are tagged Request::PREFETCH
    (tag_prefetch=True) via the R6 prefetch-tagging patch to DramGen.

Two independently controlled R6 knobs:
  --demand-period : demand inter-arrival period (ticks) -> injection intensity
  --pf-seq-pkts   : prefetch row-locality density (num_seq_pkts) -> row hits

Runs in timing mode. Row-hit rate is read from DRAM stats by
scripts/analyze_calibration.py across a --pf-seq-pkts sweep.

NOTE on createDram binding: PyTrafficGen exposes createDram via PyBindMethod,
which binds the raw C++ pointer and does NOT surface C++ default args to Python.
The new tag_prefetch parameter must therefore be passed POSITIONALLY as the
final (15th) argument. Stock in-tree callers (traffic_gen.cc) use the C++
default and are unaffected.
"""
import argparse
import math

import m5
from m5.objects import Root, CommMonitor, PyTrafficGen, SystemXBar, System
from m5.objects import SrcClockDomain, VoltageDomain, AddrRange
import m5.objects as mo

import dprh_common as C

p = argparse.ArgumentParser(description="DPRH synthetic-traffic harness (R6)")
p.add_argument("--config", default="B1", choices=["B0", "B1", "B2", "DPRH"])
p.add_argument("--demand-period", type=int, default=1000,
               help="demand inter-arrival period in ticks (R6 intensity knob)")
p.add_argument("--pf-seq-pkts", type=int, default=4,
               help="prefetch row-locality density: num_seq_pkts (R6 knob)")
p.add_argument("--pf-tag", action="store_true", default=False,
               help="tag prefetch-stream requests with Request::PREFETCH")
p.add_argument("--period", type=int, default=100_000_000,
               help="total run period in ticks")
p.add_argument("--rd-perc", type=int, default=100,
               help="percent reads for both generators (default 100)")
p.add_argument("--a-guard", type=int, default=0,
               help="aged-demand guard in cycles (sizes the AGED_DEMAND bin)")
args = p.parse_args()

# FIX-6: self-document the frozen config into every simout.
print(C.frozen_summary())

# ---------------------------------------------------------------------------
# System + memory controller (frozen definition).
# ---------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(
    clock=C.FROZEN["clk"], voltage_domain=VoltageDomain()
)
system.mem_ranges = [AddrRange("2GB")]
system.membus = SystemXBar()

system.mem_ctrl = C.make_mem_ctrl(args.config, a_guard=args.a_guard)
system.mem_ctrl.port = system.membus.mem_side_ports

# ---------------------------------------------------------------------------
# DRAM geometry (read from the frozen DDR4_2400_16x4 device).
# ---------------------------------------------------------------------------
dram = system.mem_ctrl.dram
nbr_banks = dram.banks_per_rank.value
mem_ranks = dram.ranks_per_channel.value
burst_size = int(
    dram.devices_per_rank.value
    * dram.device_bus_width.value
    * dram.burst_length.value
    / 8
)
page_size = int(
    dram.devices_per_rank.value * dram.device_rowbuffer_size.value
)
# Frozen address mapping enum (RoRaBaCoCh) for the generators.
addr_map_enum = mo.AddrMap(C.FROZEN["addr_mapping"])

# createDram's end_addr arg is bound as a plain int (SupportsInt); AddrRange.end
# is an Addr object, which pybind rejects -- convert to int (AddrRange itself
# does int(self.end) internally, so this is exact).
max_addr = int(AddrRange("2GB").end)

# ---------------------------------------------------------------------------
# Two generators -> membus -> MemCtrl.
# ---------------------------------------------------------------------------
system.demand_gen = PyTrafficGen()
system.pf_gen = PyTrafficGen()
system.demand_mon = CommMonitor()
system.pf_mon = CommMonitor()

system.demand_gen.port = system.demand_mon.cpu_side_port
system.demand_mon.mem_side_port = system.membus.cpu_side_ports
system.pf_gen.port = system.pf_mon.cpu_side_port
system.pf_mon.mem_side_port = system.membus.cpu_side_ports

system.system_port = system.membus.cpu_side_ports

root = Root(full_system=False, system=system)
root.system.mem_mode = "timing"
m5.instantiate()


def demand_trace():
    # Untagged demand reads at the requested injection period. num_seq_pkts=1
    # keeps demands from being artificially row-local.
    gen = system.demand_gen.createDram(
        args.period,          # duration
        0,                    # start_addr
        max_addr,             # end_addr
        burst_size,           # blocksize
        args.demand_period,   # min_period
        args.demand_period,   # max_period
        args.rd_perc,         # read_percent
        0,                    # data_limit (0 = unlimited)
        1,                    # num_seq_pkts (demands: no forced row locality)
        page_size,            # page_size
        nbr_banks,            # nbr_of_banks
        nbr_banks,            # nbr_of_banks_util
        addr_map_enum,        # addr_mapping
        mem_ranks,            # nbr_of_ranks
        False,                # tag_prefetch: POSITIONAL ARG -- verify slot on
                              # any gem5 upgrade (see FIX-5)
    )
    # FIX-5: read back the tag the generator actually bound; catches a silent
    # positional-slot shift (e.g. upstream inserting a createDram parameter).
    assert system.demand_gen.getLastDramTagPrefetch() == False, (
        "FIX-5: createDram tag_prefetch bound to the wrong slot (demand gen)")
    yield gen
    yield system.demand_gen.createExit(0)


def pf_trace():
    # Prefetch reads; num_seq_pkts controls row-locality density (R6). Tagged
    # with Request::PREFETCH when --pf-tag is set.
    gen = system.pf_gen.createDram(
        args.period,          # duration
        0,                    # start_addr
        max_addr,             # end_addr
        burst_size,           # blocksize
        args.demand_period,   # min_period (share the injection cadence)
        args.demand_period,   # max_period
        args.rd_perc,         # read_percent
        0,                    # data_limit
        args.pf_seq_pkts,     # num_seq_pkts (R6 row-locality knob)
        page_size,            # page_size
        nbr_banks,            # nbr_of_banks
        nbr_banks,            # nbr_of_banks_util
        addr_map_enum,        # addr_mapping
        mem_ranks,            # nbr_of_ranks
        bool(args.pf_tag),    # tag_prefetch: POSITIONAL ARG -- verify slot on
                              # any gem5 upgrade (see FIX-5)
    )
    # FIX-5: read back the tag the generator actually bound; catches a silent
    # positional-slot shift (e.g. upstream inserting a createDram parameter).
    assert system.pf_gen.getLastDramTagPrefetch() == bool(args.pf_tag), (
        "FIX-5: createDram tag_prefetch bound to the wrong slot (prefetch gen)")
    yield gen
    yield system.pf_gen.createExit(0)


system.demand_gen.start(demand_trace())
system.pf_gen.start(pf_trace())

exit_event = m5.simulate(args.period)
print(f"[dprh-tgen] exit: {exit_event.getCause()} @ tick {m5.curTick()}")
m5.stats.dump()
