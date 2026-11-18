#!/usr/bin/env python3
"""Q050-180.1: several positioned video clips, driven through a REAL MCP stdio session.

THE CLAIM UNDER TEST: an agent can do the whole multi-clip workflow through the MCP
bridge (`tools/mcp-zene-control/server.py`) against a running instance - the tools are
generated from the live registry, so this is the same registered `video.*` commands the
video lane and the menus invoke:

  1. `zene_video_clip_add` twice - two different files at distinct song ticks - and the
     replies name two distinct clip ids;
  2. `zene_video_set_offset` moves ONE clip by id; the other keeps its offset;
  3. `zene_control_undo` puts that move back in one step, `zene_control_redo` repeats it;
  4. `zene_project_save` / `zene_project_new` (the track is empty) / `zene_project_open`,
     then `zene_video_get_state` reads both clips back - id, path, offset, fps,
     duration, size and sound - exactly as before the save;
  5. `zene_video_render_frames` over a range spanning both clips returns one frame per
     1/fps second of song time, each naming the clip active at its tick (none in the gap),
     and the PNG files are on disk.

The clips are made with the ffmpeg program (no binary video is committed). SKIPPED (exit 77,
ctest "Skipped") when there is no ffmpeg program or no interpreter with the `mcp`
distribution; a build without FFmpeg answers `requires`, which is also a SKIP.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import control_socket_harness as H  # noqa: E402
from mcp_stdio_session import (BRIDGE_DIR, SKIP_CODE, BridgeSession,  # noqa: E402
                               protocol_version, server_python)

sys.path.insert(0, str(BRIDGE_DIR))
try:
    from zene_control import registry as R  # noqa: E402
except ImportError:  # pragma: no cover - the SKIP in main() fires
    R = None  # type: ignore[assignment]

TOOLS = ("video.clip_add", "video.clip_remove", "video.set_offset", "video.get_state",
         "video.render_frames", "video.attach", "video.detach")
CLIP_FIELDS = ("id", "path", "offset", "end", "fps", "duration", "width", "height", "has_audio")


def make_clip(ffmpeg: str, path: str, first: str, second: str) -> None:
    """A 160x90 25 fps clip: one second of `first`, one of `second`, with a tone."""
    piece = ":s=160x90:r=25:d=1"
    subprocess.run([ffmpeg, "-v", "error", "-y",
                    "-f", "lavfi", "-i", "color=c=%s%s" % (first, piece),
                    "-f", "lavfi", "-i", "color=c=%s%s" % (second, piece),
                    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=2",
                    "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v]", "-map", "[v]", "-map", "2:a",
                    "-c:v", "mpeg4", "-g", "10", "-c:a", "pcm_s16le", "-ac", "2", path], check=True)


class Run:
    def __init__(self, session: BridgeSession):
        self.session = session
        self.problems: list[str] = []

    def call(self, command_id: str, arguments: dict | None = None) -> dict:
        reply = self.session.call_tool(R.tool_name(command_id), arguments or {})
        if reply["is_error"]:
            raise RuntimeError("%s refused: %s" % (command_id, reply["summary"]))
        return (reply["payload"] or {}).get("result") or {}

    def require(self, condition: bool, text: str) -> None:
        print(("ok    " if condition else "FAIL  ") + text)
        if not condition:
            self.problems.append(text)


def clips_of(state: dict) -> dict:
    return {clip["id"]: {key: clip.get(key) for key in CLIP_FIELDS} for clip in state.get("clips", [])}


def add_and_move(run: Run, first: str, second: str) -> tuple[str, str]:
    """Steps 1-3: two clips, one moved by id, the move undone and redone."""
    tools = {entry["name"] for entry in run.session.tools_list()}
    for command_id in TOOLS:
        run.require(R.tool_name(command_id) in tools, "MCP lists %s for %s" % (R.tool_name(command_id), command_id))
    run.call("project.new")
    run.call("transport.set_tempo", {"bpm": 120})  # one second is 96 ticks
    a = run.call("video.clip_add", {"path": first, "offset": 96})["clip"]["id"]
    b = run.call("video.clip_add", {"path": second, "offset": 480})["clip"]["id"]
    run.require(bool(a) and bool(b) and a != b, "two clips with distinct ids (%s, %s)" % (a, b))
    run.call("video.set_offset", {"clip": b, "offset": 576})
    state = clips_of(run.call("video.get_state"))
    run.require(state[a]["offset"] == 96 and state[b]["offset"] == 576,
                "set_offset moved only clip %s (offsets %s, %s)" % (b, state[a]["offset"], state[b]["offset"]))
    run.call("control.undo")
    run.require(clips_of(run.call("video.get_state"))[b]["offset"] == 480, "one undo puts the move back")
    run.call("control.redo")
    run.require(clips_of(run.call("video.get_state"))[b]["offset"] == 576, "one redo repeats it")
    return a, b


def save_and_reopen(run: Run, first: str, second: str, work: str, a: str, b: str) -> None:
    """Step 4: project.save / new / open reads both clips back field for field."""
    before = clips_of(run.call("video.get_state"))
    project = os.path.join(work, "clips.mmp")
    run.call("project.save", {"path": project})
    run.call("project.new")
    run.require(run.call("video.get_state").get("clip_count") == 0, "project.new empties the video track")
    run.call("project.open", {"path": project})
    after = clips_of(run.call("video.get_state"))
    run.require(after == before, "save/reopen reads both clips back field for field: %s" % json.dumps(after, sort_keys=True))
    run.require(after[a]["path"] == first and after[b]["path"] == second, "each clip keeps its own file")


def render_range(run: Run, work: str, a: str, b: str) -> None:
    """Step 5: video.render_frames over before / A / gap / B / after."""
    out = os.path.join(work, "frames")
    rendered = run.call("video.render_frames", {"from": 0, "to": 864, "out": out, "fps": 10,
                                                "width": 32, "height": 18})
    frames = rendered.get("frames", [])
    run.require(len(frames) == 90, "9 s of song at 10 fps is 90 frames (got %d)" % len(frames))
    by_second = {round(frame["index"] / 10.0, 1): frame["clip"] for frame in frames}
    expected = {0.5: "", 1.5: a, 4.0: "", 6.5: b, 8.5: ""}
    got = {second: by_second.get(second) for second in expected}
    run.require(got == expected, "each frame names the clip at its tick: before, A, gap, B, after (%s)" % got)
    run.require(all(os.path.isfile(frame["file"]) for frame in frames), "every frame's PNG is on disk")


def drive(run: Run, first: str, second: str, work: str) -> None:
    a, b = add_and_move(run, first, second)
    save_and_reopen(run, first, second, work, a, b)
    render_range(run, work, a, b)


def prerequisites() -> tuple[str | None, str | None]:
    """The ffmpeg program and the bridge's interpreter; a None is a SKIP (it says why)."""
    if R is None:
        print("SKIP: the bridge package at %s is not importable" % BRIDGE_DIR)
        return None, None
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        print("SKIP: no ffmpeg program to make the test clips")
        return None, None
    return ffmpeg, bridge_python()


#: True when this interpreter's `mcp` takes the handler keywords the bridge passes to
#: Server() (tools/mcp-zene-control/zene_control/server.py build_server); mcp 1.x lacks them.
BRIDGE_API_PROBE = ("import inspect; from mcp.server.lowlevel import Server; "
                    "raise SystemExit(0 if 'on_list_tools' in inspect.signature(Server.__init__)"
                    ".parameters else 1)")


def bridge_python() -> str | None:
    """server_python()'s candidates, keeping the first whose `mcp` can build the bridge.

    An interpreter that imports an older `mcp` would start a server that dies in
    Server.__init__, a fact about that interpreter rather than about the video commands,
    so it is passed over; none left is a SKIP like having no `mcp` at all.
    """
    candidates = [os.environ.get("ZENE_CONTROL_PYTHON"), sys.executable,
                  "/home/kruzzzzy/.hermes/hermes-agent/venv/bin/python3"]
    for candidate in candidates:
        if not candidate or not os.path.exists(candidate):
            continue
        python = server_python(candidate)
        if python == candidate and subprocess.run([python, "-c", BRIDGE_API_PROBE],
                                                  capture_output=True).returncode == 0:
            return python
    print("SKIP: no interpreter whose `mcp` takes the bridge's Server(on_list_tools=...) API")
    return None


def main(argv) -> int:
    binary = argv[1] if len(argv) > 1 else ""
    if not os.path.exists(binary):
        print("cannot run: no binary at %s" % binary)
        return 2
    ffmpeg, python = prerequisites()
    if ffmpeg is None or python is None:
        return SKIP_CODE
    work = tempfile.mkdtemp(prefix="zene-video-clips-mcp-", dir="/tmp")
    first = os.path.join(work, "first.mkv")
    second = os.path.join(work, "second.mkv")
    make_clip(ffmpeg, first, "red", "blue")
    make_clip(ffmpeg, second, "lime", "yellow")
    with H.start_instance(binary) as instance:
        client = H.connect(instance)
        H.wait_ready(instance, client, None)
        session = BridgeSession(python, instance.socket_path, os.path.join(work, "state"), protocol_version(python))
        run = Run(session)
        try:
            session.initialize()
            available = run.session.call_tool(R.tool_name("video.get_state"), {})
            if available["is_error"] and "requires" in available["summary"]:
                print("SKIP: this build has no FFmpeg (%s)" % available["summary"])
                return SKIP_CODE
            drive(run, first, second, work)
        except (H.Timeout, TimeoutError, RuntimeError, KeyError) as error:
            print("FAIL: %r" % error)
            print("\n".join(session.stderr))
            return 1
        finally:
            session.close()
            client.close()
    if run.problems:
        print("FAIL: %d problem(s)" % len(run.problems))
        return 1
    print("PASS: the multi-clip workflow through MCP")
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
