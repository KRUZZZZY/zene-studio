#!/usr/bin/env python3
"""Q050-010: the long jobs, driven over the REAL socket through app.tasks.

For the plug-in scan, a render and a browser peak read, each under the id its start reply named:
start it, read it from app.tasks while it runs, see the surface answer a ping meanwhile, follow it
to its end, then start another and stop it with app.task_cancel and check nothing it would have
published is there. Stem jobs take the same steps in ControlStemCommands, which owns the stub model
and the python backend they need (tests/stem_commands_lib.py, check_stem_task).
"""

from __future__ import annotations

import os
import shutil
import struct
import sys
import tempfile
import time
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

END_BOUND_S = 120.0
PING_BOUND_S = 2.0
TERMINAL = ("done", "failed", "cancelled")


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, problems, command, args=None):
        reply = self.call(command, args)
        problems.require(reply.get("ok") is True, "%s answered %r" % (command, reply))
        return reply.get("result") or {}

    def row(self, task):
        reply = self.call("app.tasks")
        for row in (reply.get("result") or {}).get("tasks", []):
            if row.get("id") == task:
                return row
        return {}

    def ping_seconds(self):
        before = time.monotonic()
        self.call("control.ping")
        return time.monotonic() - before

    def wait_end(self, problems, task):
        """The task's terminal row, pinging all the way: the worst ping is the proof the surface was free."""
        deadline = time.monotonic() + END_BOUND_S
        worst = 0.0
        while time.monotonic() < deadline:
            row = self.row(task)
            if row.get("state") in TERMINAL:
                problems.require(worst < PING_BOUND_S, "%s: a ping took %.2f s during the job" % (task, worst))
                return row
            worst = max(worst, self.ping_seconds())
            time.sleep(0.01)
        problems.add("%s never ended: %r" % (task, self.row(task)))
        return {}


def write_wave(path, seconds):
    with wave.open(path, "wb") as handle:
        handle.setnchannels(2)
        handle.setsampwidth(2)
        handle.setframerate(48000)
        block = b"".join(struct.pack("<hh", (i * 37) % 2000 - 1000, (i * 53) % 2000 - 1000) for i in range(48000))
        for _ in range(seconds):
            handle.writeframes(block)


def check_plugin_scan(session, problems):
    before = session.result(problems, "plugin.list").get("total")
    started = session.result(problems, "plugin.scan_start", {"pace_ms": 5})
    task = started.get("task")
    problems.require(task == "plugin-scan-%s" % started.get("task_id"), "the scan's id is %r" % (started,))
    row = session.row(task)
    problems.require(row.get("kind") == "plugin_scan" and row.get("state") == "running", "the running scan is %r" % (row,))
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "done" and row.get("progress") == 1.0, "the scan ended %r" % (row,))

    started = session.result(problems, "plugin.scan_start", {"pace_ms": 1000})
    task = started.get("task")
    session.result(problems, "app.task_cancel", {"id": task})
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "cancelled", "the cancelled scan ended %r" % (row,))
    problems.require(session.result(problems, "plugin.list").get("total") == before, "a cancelled scan changed the catalogue")
    refused = session.call("app.task_cancel", {"id": task})
    problems.require(refused.get("ok") is False, "cancelling an ended scan answered %r" % (refused,))


def check_peaks(session, problems, workdir):
    path = os.path.join(workdir, "long.wav")
    write_wave(path, 180)
    started = session.result(problems, "browser.peaks_start", {"path": path})
    task = started.get("task")
    problems.require(task == "peak-%s" % started.get("task_id"), "the read's id is %r" % (started,))
    row = session.row(task)
    problems.require(row.get("kind") == "browser_peak", "the read is listed as %r" % (row,))
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "done", "the read ended %r" % (row,))
    peaks = session.result(problems, "browser.peaks", {"path": path, "buckets": 16})
    problems.require(peaks.get("cached") is True and len(peaks.get("peaks", [])) == 16, "the read did not reach the cache")

    other = os.path.join(workdir, "cancelled.wav")
    write_wave(other, 180)
    task = session.result(problems, "browser.peaks_start", {"path": other}).get("task")
    session.result(problems, "app.task_cancel", {"id": task})
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "cancelled", "the cancelled read ended %r" % (row,))
    peaks = session.result(problems, "browser.peaks", {"path": other, "buckets": 16})
    problems.require(peaks.get("cached") is False, "a cancelled read filled the cache")


def check_render(session, problems, workdir):
    track = session.result(problems, "track.add", {"type": "instrument", "name": "Listed"}).get("track")
    clip = session.result(problems, "clip.add", {"track": track, "position": 0}).get("clip")
    session.result(problems, "note.add", {"clip": clip, "key": 60, "position": 0, "length": 192, "velocity": 100})
    out = os.path.join(workdir, "render.wav")
    started = session.result(problems, "render.queue_add", {"out": out})
    task = started.get("task")
    problems.require(task == "render-%s" % started.get("job"), "the render's id is %r" % (started,))
    row = session.row(task)
    problems.require(row.get("kind") == "render" and row.get("state") in ("queued", "running"), "the render is listed as %r" % (row,))
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "done", "the render ended %r" % (row,))
    problems.require(os.path.getsize(out) > 44 if os.path.exists(out) else False, "the render wrote no audio")

    other = os.path.join(workdir, "cancelled-render.wav")
    task = session.result(problems, "render.queue_add", {"out": other}).get("task")
    session.result(problems, "app.task_cancel", {"id": task})
    row = session.wait_end(problems, task)
    problems.require(row.get("state") == "cancelled", "the cancelled render ended %r" % (row,))
    problems.require(not os.path.exists(other), "a cancelled render left its output")
    leftovers = [name for name in os.listdir(workdir) if name.startswith("cancelled-render") or ".partial" in name]
    problems.require(not leftovers, "a cancelled render left %r" % (leftovers,))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    transcript = H.Transcript()
    workdir = tempfile.mkdtemp(prefix="zene-background-tasks-")
    try:
        with H.start_instance(argv[1]) as instance:
            H.wait_for_socket(instance)
            client = H.connect(instance)
            H.wait_ready(instance, client, transcript)
            session = Session(client, transcript)
            check_plugin_scan(session, problems)
            check_peaks(session, problems, workdir)
            check_render(session, problems, workdir)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    if problems:
        transcript.dump()
        problems.report("background tasks")
        return 1
    H.ok("the plug-in scan, a render and a peak read ran, were followed and were cancelled through app.tasks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
