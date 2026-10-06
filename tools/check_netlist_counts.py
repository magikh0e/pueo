#!/usr/bin/env python3
"""The netlist figures the PCB doc quotes, against the generator that makes them.

pcb-design.md describes what `tools/gen_netlist.py` emits and gives the size
of it, net by net. That sentence had gone stale by a net and four connections:
it said 17 nets and 73 connections while the generator was producing 16 and 69.

Nothing noticed, because the generator prints its totals to a terminal that
nobody reads twice and the doc is prose. It is the same failure
check_doc_counts.py was written for, one directory over: a number that
describes generated output, written by hand, in a file that is never run.

    python tools/check_netlist_counts.py

Imports the generator and counts; no board and no network. It does not write
anything, so running it cannot change what ends up in dist/.

What it asserts
---------------
  nets            The count in the doc is the number of nets the generator
                  defines.

  connections     The same for the total endpoints across them, which is the
                  figure that moves when a part is added to an existing net
                  and the net count does not.

  parts           The BOM size, for the same reason.
"""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "tools" / "gen_netlist.py"
DOC = ROOT / "docs" / "pueo" / "pcb-design.md"


def load():
    spec = importlib.util.spec_from_file_location("gen_netlist", GEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def main():
    gen = load()
    nets = len(gen.NETS)
    conns = sum(len(c) for _n, c, _d in gen.NETS)
    parts = len(gen.BOM)

    doc = DOC.read_text(encoding="utf-8", errors="replace")
    checks = 0
    fails = []

    def ok(what, cond, why=""):
        nonlocal checks
        checks += 1
        if cond:
            print("  ok    %s" % what)
        else:
            fails.append(what)
            print("  FAIL  %-34s %s" % (what, why))

    m = re.search(r"(\d+)\s+nets,\s*(\d+)\s+connections", doc)
    ok("the doc states the netlist size", m is not None,
       "no 'N nets, M connections' sentence found")
    if m:
        ok("  nets", int(m.group(1)) == nets,
           "doc says %s, the generator defines %d" % (m.group(1), nets))
        ok("  connections", int(m.group(2)) == conns,
           "doc says %s, the generator emits %d" % (m.group(2), conns))

    # The part count is quoted separately, where it is quoted at all.
    p = re.search(r"(\d+)\s+parts", doc)
    if p:
        ok("  parts", int(p.group(1)) == parts,
           "doc says %s, the BOM has %d" % (p.group(1), parts))
    else:
        print("  ...   the doc does not quote a part count, so none is checked")

    print()
    if fails:
        print("FAILED: %d of %d" % (len(fails), checks))
        return 1
    print("%d checks passed  (%d nets, %d connections, %d parts)"
          % (checks, nets, conns, parts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
