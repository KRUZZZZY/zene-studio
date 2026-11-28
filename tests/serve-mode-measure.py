#!/usr/bin/env python3
"""Start time and memory of serve mode (`zene --engine`) beside the offscreen application (Q050-517).

A measurement, not a test: it prints a table and always exits 0 when both modes came up. Each run
starts the real binary on the Dummy audio device with a throwaway HOME, waits for control.ping to
report engine_ready, reads /proc/<pid>/status and /proc/<pid>/maps, and quits it through control.quit.

Usage: serve-mode-measure.py <zene-binary> [--runs N]
Reported per mode (median of N, with min-max): seconds from spawn to engine_ready, resident memory
(VmRSS), threads, shared objects mapped. Linux only (it reads /proc).
"""

from __future__ import annotations

import argparse
import os
import statistics
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)


class ModeInstance(H.Instance):
    def __init__(self, binary, engine):
        super().__init__(binary)
        self.engine = engine

    def argv(self):
        line = [self.binary, "--config", self.config_path, "--control-socket", self.socket_path]
        return line + (["--engine"] if self.engine else [])


def proc_field(pid, name):
    for line in open("/proc/%d/status" % pid):
        if line.startswith(name + ":"):
            return int(line.split()[1])
    return -1


def shared_objects(pid):
    paths = {line.split()[-1] for line in open("/proc/%d/maps" % pid) if ".so" in line}
    return len(paths), sorted(os.path.basename(p).split(".so")[0] for p in paths if "libQt" in p)


def measure_once(binary, engine):
    started = time.time()
    with ModeInstance(binary, engine) as instance:
        instance.spawn()
        client = H.connect(instance)
        H.wait_ready(instance, client, H.Transcript())
        ready = time.time() - started
        time.sleep(1.0)  # let start-up allocations settle before reading memory
        pid = instance.process.pid
        count, qt = shared_objects(pid)
        sample = {"ready_s": ready, "rss_mb": proc_field(pid, "VmRSS") / 1024.0,
                  "threads": proc_field(pid, "Threads"), "shared_objects": count, "qt": qt}
        client.call(1, "control.quit")
        instance.wait_for_exit(H.QUIT_TIMEOUT)
    return sample


def summarise(samples, key):
    values = [s[key] for s in samples]
    return "%.2f (%.2f-%.2f)" % (statistics.median(values), min(values), max(values))


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("binary")
    parser.add_argument("--runs", type=int, default=5)
    options = parser.parse_args(argv)
    binary = os.path.abspath(options.binary)
    print("| Mode | ready, s | resident, MB | threads | shared objects | Qt libraries mapped |")
    print("|---|---|---|---|---|---|")
    for label, engine in (("offscreen application", False), ("serve mode (--engine)", True)):
        samples = [measure_once(binary, engine) for _ in range(options.runs)]
        print("| %s | %s | %s | %s | %s | %s |" % (
            label, summarise(samples, "ready_s"), summarise(samples, "rss_mb"),
            summarise(samples, "threads"), summarise(samples, "shared_objects"),
            ", ".join(samples[-1]["qt"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
