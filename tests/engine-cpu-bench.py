#!/usr/bin/env python3
"""engine-cpu-bench.py - the engine's render cost as a tracked number (relief plan R7.4).

WHAT. Renders fixed bundled projects headless through the real binary (`zene render`) and
measures the child's CPU time (user + sys) - steadier than wall time on a shared box - taking
the MEDIAN of N runs per fixture. It exists so engine-rate work (per-sample automation and
modulation, R1) has a number to be judged against before it lands.

WHY IT IS NOT A CTEST. A timing is a property of a machine. A gate that compares against a
figure taken on a different CPU is noise, and a flaky gate is worse than none. So a baseline
row is keyed by HOST, `--check` compares only against this host's own row (and says "no
baseline for this host" - exit 3, never a pass - when there is none), and the tolerance is
explicit (default +20%, the plan's figure).

Usage:
    python3 tests/engine-cpu-bench.py <zene-binary>                      # measure and print
    python3 tests/engine-cpu-bench.py <zene-binary> --record [--reason "..."]
    python3 tests/engine-cpu-bench.py <zene-binary> --check [--tolerance 0.20]
Exit: 0 ok; 1 a fixture regressed past the tolerance, or a render failed; 3 no baseline for
this host; 2 usage.
"""
import argparse
import os
import resource
import socket
import statistics
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BASELINE = os.path.join(HERE, "engine-cpu-baseline.tsv")
FIXTURES = [
    "data/projects/shorties/Root84-TrancyLoop.mmpz",
    "data/projects/shorties/Skiessi-222.mmpz",
    "data/projects/demos/DnB.mmpz",
]


def render_cpu(binary, project, home):
    out = os.path.join(home, "bench.wav")
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    env = dict(os.environ, HOME=home, QT_QPA_PLATFORM="offscreen")
    proc = subprocess.run([binary, "render", os.path.join(REPO, project), "-o", out],
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env, timeout=600)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if proc.returncode != 0 or not os.path.isfile(out):
        raise RuntimeError("render of %s failed (exit %d): %s"
                           % (project, proc.returncode, proc.stderr.decode(errors="replace")[-300:]))
    return (after.ru_utime - before.ru_utime) + (after.ru_stime - before.ru_stime)


def measure(binary, runs):
    results = {}
    with tempfile.TemporaryDirectory() as home:
        for project in FIXTURES:
            times = [render_cpu(binary, project, home) for _ in range(runs)]
            results[project] = (statistics.median(times), min(times), max(times))
            print("%-48s median %.3fs cpu (min %.3f, max %.3f, n=%d)"
                  % (project, results[project][0], results[project][1], results[project][2], runs))
    return results


def read_baseline():
    rows = {}
    if os.path.isfile(BASELINE):
        for line in open(BASELINE):
            if line.startswith("#") or not line.strip():
                continue
            host, project, cpu = line.rstrip("\n").split("\t")[:3]
            rows[(host, project)] = float(cpu)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    parser.add_argument("--runs", type=int, default=5)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--record", action="store_true")
    mode.add_argument("--check", action="store_true")
    parser.add_argument("--tolerance", type=float, default=0.20)
    parser.add_argument("--reason", default="")
    args = parser.parse_args()
    host = socket.gethostname()
    try:
        results = measure(os.path.abspath(args.binary), args.runs)
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print("FAIL: %s" % error)
        return 1
    if args.record:
        if not args.reason.strip():
            print("usage: --record needs --reason: a baseline without a reason is not recorded")
            return 2
        rows = {k: v for k, v in read_baseline().items() if k[0] != host}
        for project, (median, _, _) in results.items():
            rows[(host, project)] = median
        with open(BASELINE, "w") as fh:
            fh.write("# engine CPU baseline - median child CPU seconds per render, per HOST "
                     "(tests/engine-cpu-bench.py)\n# last recorded on %s: %s\n" % (host, args.reason))
            for (h, p), cpu in sorted(rows.items()):
                fh.write("%s\t%s\t%.4f\n" % (h, p, cpu))
        print("RECORDED for host %s" % host)
        return 0
    if args.check:
        base = read_baseline()
        mine = {p: c for (h, p), c in base.items() if h == host}
        if not mine:
            print("NO BASELINE for host %s - nothing to compare (not a pass)" % host)
            return 3
        failed = 0
        for project, (median, _, _) in results.items():
            ref = mine.get(project)
            if ref is None:
                print("new   %s: no baseline row" % project); continue
            ratio = median / ref
            verdict = "ok" if ratio <= 1.0 + args.tolerance else "SLOWER"
            failed += verdict != "ok"
            print("%-6s %s: %.3fs vs %.3fs (%+.1f%%)" % (verdict, project, median, ref, (ratio - 1) * 100))
        print("RESULT: %s" % ("PASS" if not failed else "FAIL (%d fixture(s) past +%d%%)"
                              % (failed, round(args.tolerance * 100))))
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
