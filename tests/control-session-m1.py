#!/usr/bin/env python3
"""Milestone M1 for Zene Studio 0.3.0-alpha, driven END TO END through the
control socket - not through a grid, because there is no grid.

THE CLAIM UNDER TEST (NEXT-0.3.0-AGENT-PROMPT.md 3.2, restated for 0.3.0):

    a saved project launches 4 clips across 2 scenes in sync at the next bar,
    driven end to end through --control-socket

WHAT THIS SCRIPT ACTUALLY PROVES, and where it stops short, because a milestone
proof that overstates itself is worse than none:

  1. Two song tracks are added and a 2x2 Session View grid is built through
     `session.set_grid` / `session.set_scene` / `session.set_slot` - four clip
     slots, two in scene 0 and two in scene 1. The slots reference patterns by
     id (the model's own semantics: a slot stores a reference, SPEC A1); the
     test does NOT claim the referenced pattern exists.
  2. The project is SAVED (`project.save`) and the saved file is read back on
     the client side and asserted to contain a <session> block with four
     <clip> children - external evidence, not the engine's word for it.
  3. It is REOPENED (`project.open`) and the grid reads back as 2x2 with four
     clips, so the launch below happens against a SAVED project.
  4. The transport runs, the play head is moved to a position strictly inside a
     bar, and BOTH scenes are launched. Each launch reports the tick it was
     scheduled for; the assertions are that it is the next bar line, that it is
     the SAME line for all four clips (`in_sync` / `sync_tick`), and that the
     four clips are the four cells that were built.
  5. The launch engine is then polled until it reports four completed launches,
     and the assertions are that all four STARTED ON THAT ONE LINE
     (`launch.start_line == sync_tick`, `launch.start_line_starts == 4`) - which
     is what "in sync at the next bar" means - and that the audio thread noticed
     them within a small distance of the line, so the tick is a measurement of
     the audio thread's clock and not a restatement of the request
     (`launch.start_observed` is reported and checked).

WHAT IT DOES NOT PROVE, stated plainly: a launched session slot does not render
audio in this tree (there is no session-clip playback path - SessionClip.cpp is
serialisation only), so no assertion here is about sound. And the whole flow is
driven through the socket; no part of it is reachable from the interface, which
is both the 0.3.0 promise and the release's documented limitation.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-session-m1.py <zene-binary> \
        [--transcript PATH] [--clip-timeout SECONDS]

Exit code 0 only when every assertion held. --transcript writes the raw
request/response log plus the derived evidence, and is written even when a step
raises, so the committed evidence shows what happened rather than what was hoped
for.
"""

import argparse
import hashlib
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The bar-relative position the play head is moved to before launching: a
#: quarter of the way into a bar, so the next bar line is three quarters of a
#: bar away and no launch is racing a boundary. The tick itself is derived from
#: the ticks-per-bar the engine reports, never hardcoded.
SEEK_FRACTION = 0.25
#: How long to wait for the four clips to start once they are launched. At the
#: 120 BPM / 4-4 the instance starts in, three quarters of a bar is 1.5 s; the
#: bound is generous because a loaded build box is not a metronome.
CLIP_TIMEOUT = 30.0


class Evidence:
    """The transcript: every exchange, then the derived findings."""

    def __init__(self):
        self.lines = []

    def line(self, text=""):
        self.lines.append(text)
        print(text, flush=True)

    def section(self, title):
        self.line("")
        self.line("=" * 78)
        self.line(title)
        self.line("=" * 78)

    def finding(self, label, text):
        self.line("  %-34s %s" % (label, text))

    def write(self, path, verdict):
        if not path:
            return
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(self.lines) + "\n")
            handle.write("\nVERDICT: %s\n" % verdict)


def sha256_of(path):
    try:
        with open(path, "rb") as handle:
            return hashlib.sha256(handle.read()).hexdigest()
    except OSError:
        return ""


def session_block(text):
    """The <session>...</session> substring, or "" when the file has none.

    The clips are counted INSIDE this block: the arrangement writes <clip>
    elements of its own, and conflating the two would let a project with no
    session block pass.
    """
    start = text.find("<session")
    if start < 0:
        return ""
    end = text.find("</session>", start)
    return text[start:end] if end > start else text[start:]


class Milestone:
    """One real instance, driven through the safety-checked path, step by step.

    Each step is a method so that no one function carries the whole milestone:
    the gate that measures this file's complexity is the same one that measures
    the C++ side, and it is right about both.
    """

    def __init__(self, options):
        self.options = options
        self.binary = os.path.abspath(options.binary)
        self.evidence = Evidence()
        self.problems = H.Problems()
        self.calls = []
        self.ticks_per_bar = 0
        self.next_bar = 0
        self.launched = []
        self.project = ""

    # ------------------------------------------------------------------
    # plumbing
    # ------------------------------------------------------------------

    def call(self, request_id, cmd, args=None):
        """One exchange, echoed into the transcript verbatim."""
        reply = self.client.call(request_id, cmd, args)
        self.calls.append("-> %s %s" % (cmd, json.dumps(args or {}, separators=(",", ":"))))
        self.calls.append("<- %s" % json.dumps(reply, separators=(",", ":")))
        return reply

    def ok(self, request_id, cmd, args=None):
        return H.ok_result(self.call(request_id, cmd, args), request_id)

    def state(self, request_id):
        return self.ok(request_id, "session.get_state")

    def launch_state(self, request_id):
        return self.state(request_id).get("launch", {})

    def require(self, condition, text):
        self.problems.require(condition, text)

    def finding(self, label, text):
        self.evidence.finding(label, text)

    # ------------------------------------------------------------------
    # the five steps
    # ------------------------------------------------------------------

    def connect(self):
        H.wait_for_socket(self.instance)
        self.client = H.connect(self.instance)
        H.wait_ready(self.instance, self.client, None)
        self.finding("socket", self.instance.socket_path)
        self.finding("version", json.dumps(self.ok(1, "app.version").get("version")))

    def step_build(self):
        self.evidence.section("1. build the session through the socket: 2 tracks x 2 scenes")
        for request_id in (10, 11):
            self.finding("track.add", json.dumps(self.ok(request_id, "track.add",
                                                         {"type": "instrument"})))
        song_tracks = int(self.ok(12, "track.list").get("count", 0))
        self.finding("track.list", "count=%d" % song_tracks)
        self.require(song_tracks >= 2, "fewer than 2 song tracks exist, so the grid columns "
                                       "could not map to tracks")

        before = self.state(20)
        self.finding("grid before", json.dumps(before.get("grid")))
        self.ticks_per_bar = int(before.get("launch", {}).get("ticks_per_bar", 0) or 0)
        self.finding("ticks_per_bar", "%d" % self.ticks_per_bar)
        empty = (int(before.get("grid", {}).get("tracks", -1)) == 0
                 and int(before.get("grid", {}).get("scenes", -1)) == 0)
        self.require(empty, "the session grid was not empty at the start of the run")
        self.require(self.ticks_per_bar > 0, "the engine reports no ticks per bar")

        grid = self.ok(21, "session.set_grid", {"tracks": 2, "scenes": 2})
        self.finding("session.set_grid", json.dumps(grid))
        self.require(grid.get("grid", {}).get("tracks") == 2,
                     "session.set_grid did not establish a 2x2 grid")
        self.require(grid.get("grid", {}).get("scenes") == 2,
                     "session.set_grid did not establish a 2x2 grid")

        quantisation = self.ok(22, "session.set_quantisation", {"quantisation": "bar"})
        self.finding("global quantisation", json.dumps(quantisation))
        self.require(quantisation.get("quantisation") == "bar",
                     "the session default quantisation is not one bar")

        for scene, name in ((0, "Scene A"), (1, "Scene B")):
            reply = self.ok(30 + scene, "session.set_scene", {"scene": scene, "name": name})
            self.finding("session.set_scene %d" % scene, json.dumps(reply.get("scene", {})))

        # Four clip slots: two per scene. `quantisation: global` is deliberate -
        # it is the per-clip value that DEFERS, so the launch path has to resolve
        # it against the session default for the sync claim to mean anything.
        built = []
        for scene in (0, 1):
            for track in (0, 1):
                reply = self.ok(100 + track + 10 * scene, "session.set_slot", {
                    "track": track, "scene": scene, "type": "midi", "pattern": track + 1,
                    "name": "clip-%d-%d" % (track, scene),
                    "mode": "trigger", "quantisation": "global"})
                built.append((track, scene, reply.get("slot", {}).get("name")))
        self.finding("slots built", json.dumps(built))
        self.require(len(built) == 4, "fewer than 4 clip slots were built")

        after = self.state(110)
        self.finding("grid after build", json.dumps(after.get("grid")))
        self.require(int(after.get("grid", {}).get("clips", 0)) == 4,
                     "the model does not hold 4 clip slots after building them")

    def step_save(self):
        self.evidence.section("2. save the project and read the file back on the client side")
        self.project = os.path.join(self.instance.tmp, "session-m1.mmp")
        saved = self.ok(200, "project.save", {"path": self.project})
        trimmed = {key: value for key, value in saved.items() if key != "revisions"}
        self.finding("project.save", json.dumps(trimmed))
        try:
            with open(self.project, "r", errors="replace") as handle:
                text = handle.read()
        except OSError as error:
            self.problems.add("could not read the saved project back: %s" % error)
            return
        block = session_block(text)
        clips = block.count("<clip ")
        self.finding("saved file", "%d bytes, session block %d bytes, %d <clip> children"
                     % (len(text), len(block), clips))
        self.finding("saved sha256", sha256_of(self.project))
        self.require("<session" in text, "the saved project carries no <session> block")
        self.require(clips == 4, "the saved project's <session> block has %d <clip> children, "
                                 "expected 4" % clips)

    def step_reopen(self):
        self.evidence.section("3. reopen the saved project - the launch is against a SAVED session")
        self.ok(210, "project.open", {"path": self.project})
        # project.open replaces the whole session, and the scheduler's reset
        # request is applied by the audio thread on its next period, so the
        # launch counters below start from a settled, empty engine.
        time.sleep(0.5)
        grid = self.state(211).get("grid", {})
        self.finding("grid after reopen", json.dumps(grid))
        self.require(int(grid.get("tracks", 0)) == 2 and int(grid.get("scenes", 0)) == 2,
                     "the reopened project does not carry the 2x2 grid")
        self.require(int(grid.get("clips", 0)) == 4,
                     "the reopened project's session block does not hold 4 clips")

    def step_transport(self):
        self.evidence.section("4a. run the transport and park inside a bar")
        self.ok(300, "transport.play")
        self.ok(301, "transport.seek", {"ticks": int(self.ticks_per_bar * SEEK_FRACTION)})
        # The play head must ADVANCE ON ITS OWN after the seek, not merely hold
        # the tick the seek put it on: that is what proves the audio thread is
        # calling Song::processNextBuffer (and so the session scheduler) while
        # this test drives the socket from another process. Two samples, half a
        # second apart.
        first = int(self.launch_state(302).get("position", 0))
        time.sleep(0.5)
        prelaunch = self.launch_state(303)
        self.finding("launch state before", json.dumps(prelaunch))
        position = int(prelaunch.get("position", 0))
        self.next_bar = int(prelaunch.get("next_bar", 0))
        self.require(first > 0 and position > first,
                     "the play head did not advance between two samples %r -> %r: the audio "
                     "thread is not running, so a scheduled launch could never fire"
                     % (first, position))
        self.require(prelaunch.get("transport_running") is True,
                     "the song transport is not running, so a launch would be scheduled against "
                     "the free-running session clock instead of the arrangement's own grid")
        self.require(self.next_bar > position,
                     "next_bar (%d) is not after the play head (%d)" % (self.next_bar, position))
        self.require(self.next_bar % self.ticks_per_bar == 0,
                     "next_bar (%d) is not a multiple of the bar (%d)"
                     % (self.next_bar, self.ticks_per_bar))
        self.require(self.next_bar - position < self.ticks_per_bar,
                     "next_bar (%d) is more than one bar ahead of the play head (%d)"
                     % (self.next_bar, position))

    def step_launch(self):
        self.evidence.section("4b. launch BOTH scenes")
        for index, scene in enumerate((0, 1)):
            reply = self.ok(310 + index, "session.launch_scene", {"scene": scene})
            self.finding("launch_scene %d" % scene, json.dumps(reply))
            self.launched.extend(reply.get("launched", []))
            self.require(int(reply.get("clips", 0)) == 2,
                         "scene %d launched %r clips, expected 2" % (scene, reply.get("clips")))
            self.require(reply.get("in_sync") is True,
                         "scene %d reports its clips are not on one grid line" % scene)
            self.require(int(reply.get("sync_tick", -1)) == self.next_bar,
                         "scene %d was scheduled for tick %r, not the next bar %d"
                         % (scene, reply.get("sync_tick"), self.next_bar))
        self.finding("clips launched", json.dumps(self.launched))
        self.require(len(self.launched) == 4,
                     "4 clips were not launched (got %d)" % len(self.launched))
        scenes = sorted({int(entry.get("scene", -1)) for entry in self.launched})
        tracks = sorted({int(entry.get("track", -1)) for entry in self.launched})
        ticks = sorted({int(entry.get("scheduled_tick", -1)) for entry in self.launched})
        self.finding("across scenes", json.dumps(scenes))
        self.finding("across tracks", json.dumps(tracks))
        self.finding("distinct scheduled ticks", json.dumps(ticks))
        self.require(scenes == [0, 1], "the 4 clips are not spread across 2 scenes")
        self.require(tracks == [0, 1], "the 4 clips are not spread across 2 tracks")
        self.require(ticks == [self.next_bar],
                     "the 4 clips resolved to %r, not ONE next-bar line %d"
                     % (ticks, self.next_bar))

    def step_readback(self):
        self.evidence.section("5. the engine's read-back: all four started on that one line")
        deadline = time.time() + self.options.clip_timeout
        final = {}
        while time.time() < deadline:
            final = self.launch_state(400)
            if int(final.get("completed_launches", 0)) >= 4:
                break
            time.sleep(0.1)
        self.finding("launch state after", json.dumps(final))
        self.require(int(final.get("completed_launches", 0)) == 4,
                     "the engine completed %r launches, expected 4"
                     % final.get("completed_launches"))
        self.require(int(final.get("start_line", -1)) == self.next_bar,
                     "the engine's start line is %r, not the launched bar line %d"
                     % (final.get("start_line"), self.next_bar))
        self.require(int(final.get("start_line_starts", 0)) == 4,
                     "%r clip(s) started on that grid line, expected 4 - the launch was not in "
                     "sync" % final.get("start_line_starts"))
        observed = int(final.get("start_observed", -1))
        self.require(0 <= observed - self.next_bar < self.ticks_per_bar,
                     "the audio thread noticed the starts at tick %d, which is not within one bar "
                     "of the line %d" % (observed, self.next_bar))
        self.require(int(final.get("dropped_commands", -1)) == 0,
                     "the scheduler dropped commands: %r" % final.get("dropped_commands"))
        self.check_model_cells()

    def check_model_cells(self):
        cells = sorted((int(slot.get("scene", -1)), int(slot.get("track", -1)))
                       for slot in self.state(401).get("slots", []))
        self.finding("model cells", json.dumps(cells))
        self.require(cells == [(0, 0), (0, 1), (1, 0), (1, 1)],
                     "the model no longer holds exactly the four built cells: %r" % cells)

    def step_quit(self):
        self.ok(402, "transport.stop")
        self.finding("control.quit", json.dumps(self.call(403, "control.quit")))
        self.client.close()
        self.instance.wait_for_exit(H.QUIT_TIMEOUT)

    # ------------------------------------------------------------------
    # the run
    # ------------------------------------------------------------------

    def run(self):
        self.evidence.section("Zene Studio 0.3.0-alpha - Session View milestone M1")
        self.finding("binary", self.binary)
        self.finding("binary sha256", sha256_of(self.binary))
        self.finding("claim", "a saved project launches 4 clips across 2 scenes in sync at the "
                              "next bar, driven end to end through --control-socket")
        self.instance = H.start_instance(self.binary)
        try:
            self.connect()
            for step in (self.step_build, self.step_save, self.step_reopen,
                         self.step_transport, self.step_launch, self.step_readback,
                         self.step_quit):
                step()
        except Exception as error:  # noqa: BLE001  (the transcript must survive anything)
            # A raised error (a hang, a dead instance, a malformed reply) is a
            # failure like any other, and the committed transcript has to show it.
            self.problems.add("the run raised %s: %s" % (type(error).__name__, error))
            self.finding("raised", "%s: %s" % (type(error).__name__, error))
        finally:
            self.instance.close()
        return self.report()

    def report(self):
        self.evidence.section("RAW TRANSCRIPT (every request, every reply)")
        for line in self.calls:
            self.evidence.line(line)
        self.evidence.section("RESULT")
        passed = self.problems.report("session milestone M1")
        for item in self.problems.items:
            self.evidence.line("  FAILED CHECK: %s" % item)
        self.evidence.line("  %s" % ("PASS: M1 - a saved project launched 4 clips across 2 "
                                     "scenes in sync at the next bar, through --control-socket"
                                     if passed else
                                     "FAIL: M1 - see the failed checks above"))
        self.evidence.write(self.options.transcript, "PASS" if passed else "FAIL")
        if self.options.transcript:
            print("transcript: %s" % self.options.transcript)
        return 0 if passed else 1


def main(argv):
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("binary")
    parser.add_argument("--transcript", default=None)
    parser.add_argument("--clip-timeout", type=float, default=CLIP_TIMEOUT)
    options = parser.parse_args(argv[1:])
    if not os.path.exists(options.binary):
        print("no binary at %s" % options.binary)
        return 2
    return Milestone(options).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
