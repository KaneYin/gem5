"""DPRH single-core SE runner. Fast-forward (Atomic) then measure (O3).

One responsibility: drive one real-workload run.

Structure (SimPoint deferred: skip-init + fixed offset per user decision):
  1. Build the frozen system from dprh_common (caches, L2 prefetcher, MemCtrl).
  2. Fast-forward `--ff-offset` insts on an AtomicSimpleCPU.
  3. m5.switchCpus -> X86O3CPU (the measured core).
  4. Warm `--warmup` insts on O3, then m5.stats.reset().
  5. Measure `--measure` insts on O3; dump stats.

Hard requirements (plan Task 3 Step 3):
  (a) exactly one core measured with X86O3CPU;
  (b) L2 carries the prefetcher via C.make_prefetcher(pf_kind) with
      prefetch_on_access = True (set inside make_prefetcher);
  (c) memory controller from C.make_mem_ctrl(args.config);
  (d) fast-forward on Atomic, switchCpus, warm, reset, measure;
  (e) --config B0 disables the prefetcher.

This runner uses gem5's stdlib m5.objects wiring (mirrors
configs/deprecated/example/se.py), not a helper board, so the cache hierarchy
and prefetcher attachment are explicit and auditable.
"""
import argparse

import m5
from m5.objects import (
    System,
    SrcClockDomain,
    VoltageDomain,
    SEWorkload,
    Process,
    AtomicSimpleCPU,
    X86O3CPU,
    SystemXBar,
    L2XBar,
    Root,
    AddrRange,
)

import dprh_common as C

p = argparse.ArgumentParser(description="DPRH single-core SE runner")
p.add_argument("--config", required=True, choices=["B0", "B1", "B2", "DPRH"])
p.add_argument("--cmd", required=True, help="workload binary")
p.add_argument("--options", default="", help="workload args (space-separated)")
p.add_argument(
    "--prefetcher", default="spp", choices=["spp", "stride", "none"]
)
p.add_argument("--warmup", type=int, default=50_000_000)
p.add_argument("--measure", type=int, default=100_000_000)
p.add_argument(
    "--ff-offset",
    type=int,
    default=1_000_000_000,
    help="fixed skip-init offset (insts fast-forwarded on Atomic)",
)
p.add_argument("--a-guard", type=int, default=0,
               help="aged-demand guard in cycles (sizes the AGED_DEMAND bin)")
args = p.parse_args()

# B0 forces the prefetcher off regardless of --prefetcher (requirement e).
pf_kind = "none" if args.config == "B0" else args.prefetcher

# FIX-6: self-document the frozen config into every simout.
print(C.frozen_summary())

# ---------------------------------------------------------------------------
# System + clock + memory range
# ---------------------------------------------------------------------------
system = System()
system.clk_domain = SrcClockDomain(
    clock=C.FROZEN["clk"], voltage_domain=VoltageDomain()
)
system.mem_mode = "atomic"  # start atomic for fast-forward
system.mem_ranges = [AddrRange("2GB")]

# ---------------------------------------------------------------------------
# CPUs: fast-forward Atomic (switched in) + measure O3 (switched out).
# Exactly one measured core, X86O3CPU (requirement a).
# ---------------------------------------------------------------------------
system.cpu = AtomicSimpleCPU(switched_out=False, cpu_id=0)
system.o3 = X86O3CPU(switched_out=True, cpu_id=0)

# ---------------------------------------------------------------------------
# Interconnect + last-level cache + memory controller.
# The L1/L2 private caches are attached per-CPU below; the LLC and membus are
# shared and persist across the CPU switch.
# ---------------------------------------------------------------------------
system.membus = SystemXBar()
system.system_port = system.membus.cpu_side_ports

# LLC (2 MB) sits between the private-L2 mem side and the memory controller.
# Topology: L1 -> tol2bus(L2XBar) -> L2(+prefetcher) -> membus -> LLC -> MemCtrl
system.llc = C.LLC()
system.tollcbus = L2XBar(clk_domain=system.clk_domain)


def attach_private_caches(cpu, pf_kind):
    """Attach L1I/L1D + a private L2 (carrying the prefetcher) to `cpu`.

    Wiring: cpu icache/dcache -> tol2bus -> L2(+pf) -> system.membus.
    The prefetcher is attached to the L2 (requirement b); Task 4 greps
    `system.cpu.l2cache.*` for the prefetch-issued stat, so the L2 attribute
    name on the switched-in CPU is `l2cache`.
    """
    cpu.icache = C.L1I()
    cpu.dcache = C.L1D()
    cpu.icache.cpu_side = cpu.icache_port
    cpu.dcache.cpu_side = cpu.dcache_port

    cpu.tol2bus = L2XBar(clk_domain=system.clk_domain)
    cpu.icache.mem_side = cpu.tol2bus.cpu_side_ports
    cpu.dcache.mem_side = cpu.tol2bus.cpu_side_ports

    cpu.l2cache = C.L2()
    pf = C.make_prefetcher(pf_kind)
    if pf is not None:
        cpu.l2cache.prefetcher = pf
    cpu.l2cache.cpu_side = cpu.tol2bus.mem_side_ports
    cpu.l2cache.mem_side = system.membus.cpu_side_ports


# Attach caches to BOTH CPUs so takeOverFrom sees an equivalent hierarchy.
# (gem5 requires matching port topology across the switch.)
attach_private_caches(system.cpu, pf_kind)
attach_private_caches(system.o3, pf_kind)

# LLC between membus and MemCtrl.
system.llc.cpu_side = system.membus.mem_side_ports
system.llc.mem_side = system.tollcbus.cpu_side_ports

# Memory controller from the frozen definition (requirement c).
system.mem_ctrl = C.make_mem_ctrl(args.config, a_guard=args.a_guard)
system.mem_ctrl.port = system.tollcbus.mem_side_ports

# ---------------------------------------------------------------------------
# Workload / process
# ---------------------------------------------------------------------------
process = Process()
process.cmd = [args.cmd] + (args.options.split() if args.options else [])
process.executable = args.cmd
system.workload = SEWorkload.init_compatible(args.cmd)

for cpu in (system.cpu, system.o3):
    cpu.workload = process
    cpu.createThreads()
    cpu.createInterruptController()
    # X86: the local APIC is memory-mapped, so its PIO and interrupt-message
    # ports must be connected to the membus. Without this, X86ISA::Interrupts::
    # init() aborts (SIGABRT / exit 134). Both CPUs are wired (only one is
    # switched-in at a time, so there is no runtime PIO-range conflict).
    cpu.interrupts[0].pio = system.membus.mem_side_ports
    cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
    cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# ---------------------------------------------------------------------------
# Fast-forward schedule: stop Atomic after ff-offset insts, then switch.
# ---------------------------------------------------------------------------
if args.ff_offset > 0:
    system.cpu.max_insts_any_thread = args.ff_offset

root = Root(full_system=False, system=system)
m5.instantiate()

# --- Phase 1: fast-forward on Atomic ---
if args.ff_offset > 0:
    print(f"[dprh] fast-forwarding {args.ff_offset} insts on AtomicSimpleCPU")
    exit_event = m5.simulate()
    print(f"[dprh] ff exit: {exit_event.getCause()}")

# --- Switch to O3 (the measured core) ---
system.mem_mode = "timing"
m5.switchCpus(system, [(system.cpu, system.o3)])

# --- Phase 2: warm-up on O3 (no stats), then reset ---
# NOTE: assigning system.o3.max_insts_any_thread here would be a NO-OP.
# BaseCPU schedules its instruction-stop event exactly once, in init(), from
# params().max_insts_any_thread captured at m5.instantiate() -- when o3 was
# switched_out with the default 0. A post-instantiate Python assignment is never
# read back, so the warmup/measure windows would not exist and the whole run
# would execute in the warmup phase. Use scheduleInstStop(tid, insts, cause),
# which schedules an exit at (current committed inst count + insts) on the
# now-switched-in core -- the supported way to bound a phase by instructions.
if args.warmup > 0:
    system.o3.scheduleInstStop(0, args.warmup, "dprh warmup complete")
    print(f"[dprh] warming {args.warmup} insts on X86O3CPU")
    exit_event = m5.simulate()
    print(f"[dprh] warmup exit: {exit_event.getCause()}")

m5.stats.reset()

# --- Phase 3: measure on O3 ---
system.o3.scheduleInstStop(0, args.measure, "dprh measure complete")
print(f"[dprh] measuring {args.measure} insts on X86O3CPU")
exit_event = m5.simulate()
print(f"[dprh] measure exit: {exit_event.getCause()}")

m5.stats.dump()
print(f"[dprh] done config={args.config} prefetcher={pf_kind} "
      f"@ tick {m5.curTick()}")
