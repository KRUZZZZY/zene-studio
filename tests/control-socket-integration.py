#!/usr/bin/env python3
"""Headless integration test for Zene Studio's agent control socket (SPEC A12/A16).

Starts the real `lmms` binary with `--control-socket <path>` under
QT_QPA_PLATFORM=offscreen, then drives it from an EXTERNAL client over the
AF_UNIX socket with line-delimited JSON-RPC. It asserts:

  * the socket file is created with mode 0600 and is unlinked on exit;
  * control.ping / control.version / control.commands_list answer;
  * the full flow open -> read mixer -> set a channel volume -> render -> save;
  * the editing flow of the notes/clips/tracks group: add a track, add a clip,
    add notes, move/resize/set the velocity of one, read it back through
    roll.get_state, delete it, undo, read back - and the same for clip.add and
    clip.delete against arrangement.get_state;
  * typed error paths (not_found, invalid_args, refused), including a bogus
    track id and out-of-range note values;
  * control.undo / control.redo reverse a recorded mutating command;
  * the transaction list, printed with its reversible flag per command.

Usage: QT_QPA_PLATFORM=offscreen python3 control-socket-integration.py <lmms> <project.mmp>
Exit code 0 only when every assertion passed.
"""

import json
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

CONNECT_TIMEOUT = 30.0
ENGINE_TIMEOUT = 120.0
RESPONSE_TIMEOUT = 60.0
RENDER_TIMEOUT = 240.0

TRANSCRIPT = []


def record(direction, payload):
    TRANSCRIPT.append("%s %s" % (direction, payload))


def fail(message, process=None, log_path=None):
    print("\nFAIL: %s" % message)
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    if process is not None:
        try:
            process.kill()
        except OSError:
            pass
    if log_path and os.path.exists(log_path):
        print("\n---- app log ----")
        with open(log_path, "r", errors="replace") as handle:
            print(handle.read()[-8000:])
    sys.exit(1)


class Client:
    """One request and one response per line, the wire contract of ControlServer."""

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(RESPONSE_TIMEOUT)
        self.sock.connect(path)
        self.buffer = b""

    def call(self, request_id, cmd, args=None, proto=1):
        request = {"id": request_id, "cmd": cmd, "args": args or {}, "proto": proto}
        raw = json.dumps(request, separators=(",", ":"))
        record("->", raw)
        self.sock.sendall(raw.encode("utf-8") + b"\n")
        reply = self._read_line()
        record("<-", reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def raw_call(self, raw):
        record("->", raw.decode("utf-8", "replace").rstrip("\n"))
        self.sock.sendall(raw)
        reply = self._read_line()
        record("<-", reply.decode("utf-8", "replace"))
        return json.loads(reply)

    def _read_line(self):
        deadline = time.time() + RESPONSE_TIMEOUT
        while b"\n" not in self.buffer:
            if time.time() > deadline:
                raise TimeoutError("no response line before the timeout")
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the server closed the connection")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return line

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def typed_error(reply, expected_id, expected_kind):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not False:
        raise AssertionError("expected ok=false, got %r" % reply)
    error = reply.get("error") or {}
    if error.get("kind") != expected_kind:
        raise AssertionError("expected error.kind=%r, got %r" % (expected_kind, error))
    if not error.get("message"):
        raise AssertionError("typed error carries no message: %r" % reply)
    return error


def ok_result(reply, expected_id):
    if reply.get("id") != expected_id:
        raise AssertionError("reply id %r != %r" % (reply.get("id"), expected_id))
    if reply.get("ok") is not True:
        raise AssertionError("expected ok=true, got %r" % reply)
    return reply.get("result") or {}


def wait_for_socket(path, process, log_path):
    deadline = time.time() + CONNECT_TIMEOUT
    while time.time() < deadline:
        if process.poll() is not None:
            fail("the app exited before the socket was usable (exit %s)" % process.returncode,
                 process, log_path)
        if os.path.exists(path):
            try:
                probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                probe.settimeout(1.0)
                probe.connect(path)
                probe.close()
                return
            except (ConnectionRefusedError, FileNotFoundError, OSError):
                pass
        time.sleep(0.1)
    fail("the control socket %s never became connectable" % path, process, log_path)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    project = os.path.abspath(sys.argv[2])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1
    if not os.path.exists(project):
        print("FAIL: no fixture project at %s" % project)
        return 1

    tmp = tempfile.mkdtemp(prefix="zctl-", dir="/tmp")
    socket_path = os.path.join(tmp, "zene.sock")
    workspace = os.path.join(tmp, "workspace")
    os.makedirs(workspace)
    config_path = os.path.join(tmp, "lmmsrc.xml")
    # `audiodev` must be exactly AudioDummy::name(): with no sound card in CI the
    # app otherwise falls back to the dummy device and MainWindow puts up a modal
    # "Audio device setup failed" dialog before the event loop starts.
    with open(config_path, "w") as handle:
        handle.write(
            '<?xml version="1.0"?>\n'
            '<!DOCTYPE lmms-config-file>\n'
            '<lmmsconfig version="0.2.0-alpha" configversion="3">\n'
            '  <app configured="1"/>\n'
            '  <audioengine audiodev="Dummy (no sound output)"/>\n'
            '  <paths workingdir="%s"/>\n'
            '</lmmsconfig>\n' % workspace
        )

    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = tmp
    env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
    env["XDG_DATA_HOME"] = os.path.join(tmp, "data")
    os.makedirs(env["XDG_CONFIG_HOME"], exist_ok=True)
    os.makedirs(env["XDG_DATA_HOME"], exist_ok=True)

    log_path = os.path.join(tmp, "app.log")
    log_file = open(log_path, "wb")
    process = subprocess.Popen(
        [binary, "--config", config_path, "--control-socket", socket_path],
        stdout=log_file, stderr=subprocess.STDOUT, env=env, cwd=tmp,
    )

    try:
        wait_for_socket(socket_path, process, log_path)

        mode = stat.S_IMODE(os.stat(socket_path).st_mode)
        if mode != 0o600:
            fail("socket mode is 0o%o, expected 0o600" % mode, process, log_path)

        client = Client(socket_path)

        # --- handshake -----------------------------------------------------
        ping = {}
        deadline = time.time() + ENGINE_TIMEOUT
        while time.time() < deadline:
            ping = client.call(1, "control.ping")
            if ping.get("ok") and (ping.get("result") or {}).get("engine_ready"):
                break
            if process.poll() is not None:
                fail("the app exited while waiting for the engine", process, log_path)
            time.sleep(0.2)
        if not ping.get("ok") or not (ping.get("result") or {}).get("engine_ready"):
            fail("the engine never became ready (last ping: %r)" % ping, process, log_path)

        version = ok_result(client.call(2, "control.version"), 2)
        if not version.get("version"):
            fail("control.version returned no version string", process, log_path)

        schema = ok_result(client.call(3, "control.commands_list"), 3)
        if schema.get("count", 0) < 19:
            fail("control.commands_list reports only %s commands" % schema.get("count"), process, log_path)

        # --- typed error paths --------------------------------------------
        typed_error(client.call(4, "control.no_such_command"), 4, "not_found")
        typed_error(client.call(5, "transport.seek", {"ticks": "not-a-number"}), 5, "invalid_args")
        typed_error(client.call(6, "mixer.set_volume", {"channel": "ch-9999", "volume": 0.5}), 6, "not_found")
        typed_error(client.call(7, "control.version", proto=99), 7, "refused")
        # mixer.set_pan is an honest refusal: this tree has no pan on a mixer channel.
        typed_error(client.call(8, "mixer.set_pan", {"channel": "ch-0", "pan": 0.5}), 8, "refused")
        # the master channel cannot be removed.
        typed_error(client.call(9, "mixer.remove_channel", {"channel": "ch-0"}), 9, "refused")

        # --- the full flow: open -> mixer -> set volume -> render -> save ---
        opened = ok_result(client.call(10, "project.open", {"path": project}), 10)
        if not opened.get("file"):
            fail("project.open did not report the loaded file", process, log_path)

        mixer_before = ok_result(client.call(11, "mixer.get_state"), 11)
        channels = mixer_before.get("channels", [])
        if not channels:
            fail("the fixture project loaded no mixer channels", process, log_path)

        # The fixture ships one (master) channel; make a second one to address.
        added = ok_result(client.call(12, "mixer.add_channel"), 12)
        target = added.get("channel")
        if not target or not target.startswith("ch-"):
            fail("mixer.add_channel returned %r" % target, process, log_path)
        original_volume = 1.0

        set_volume = ok_result(client.call(13, "mixer.set_volume", {"channel": target, "volume": 0.5}), 13)
        if abs(float(set_volume.get("volume", -1)) - 0.5) > 1e-6:
            fail("mixer.set_volume did not report the new fader value: %r" % set_volume, process, log_path)

        mixer_after = ok_result(client.call(14, "mixer.get_state"), 14)
        changed = [c for c in mixer_after.get("channels", []) if c.get("id") == target]
        if not changed or abs(float(changed[0].get("volume", -1)) - 0.5) > 1e-6:
            fail("mixer.get_state did not read back the new fader value", process, log_path)

        # --- SPEC A16: the recorded change is reversible through the journal
        undone = ok_result(client.call(15, "control.undo"), 15)
        if not undone.get("undone"):
            fail("control.undo reported nothing undone after mixer.set_volume", process, log_path)
        mixer_undone = ok_result(client.call(16, "mixer.get_state"), 16)
        restored = [c for c in mixer_undone.get("channels", []) if c.get("id") == target]
        if not restored or abs(float(restored[0].get("volume")) - float(original_volume)) > 1e-6:
            fail("control.undo did not restore the previous fader value", process, log_path)
        redone = ok_result(client.call(17, "control.redo"), 17)
        if not redone.get("redone"):
            fail("control.redo reported nothing redone", process, log_path)
        mixer_redone = ok_result(client.call(18, "mixer.get_state"), 18)
        reapplied = [c for c in mixer_redone.get("channels", []) if c.get("id") == target]
        if not reapplied or abs(float(reapplied[0].get("volume")) - 0.5) > 1e-6:
            fail("control.redo did not reapply the fader value", process, log_path)

        out_wav = os.path.join(tmp, "render.wav")
        rendered = ok_result(client.call(19, "render.render", {"out": out_wav, "format": "wav"}), 19)
        if rendered.get("path") != out_wav:
            fail("render.render reported %r" % rendered, process, log_path)
        if not rendered.get("sha256"):
            fail("render.render returned no sha256", process, log_path)
        if int(rendered.get("frames", 0)) <= 0:
            fail("render.render produced %r frames" % rendered.get("frames"), process, log_path)
        if not os.path.exists(out_wav) or os.path.getsize(out_wav) == 0:
            fail("render.render left no audio at %s" % out_wav, process, log_path)

        saved_path = os.path.join(tmp, "saved.mmp")
        saved = ok_result(client.call(20, "project.save", {"path": saved_path}), 20)
        if saved.get("file") != saved_path or not os.path.exists(saved_path):
            fail("project.save did not write %s" % saved_path, process, log_path)

        state = ok_result(client.call(21, "project.get_state"), 21)
        if not state.get("file"):
            fail("project.get_state returned no file", process, log_path)

        # --- transactions were recorded for the mutating commands ---------
        transactions = ok_result(client.call(22, "control.transactions"), 22).get("transactions", [])
        commands_recorded = {t.get("command"): t for t in transactions}
        for expected in ("mixer.set_volume", "mixer.add_channel", "project.open", "project.save"):
            if expected not in commands_recorded:
                fail("no transaction recorded for %s" % expected, process, log_path)
        if not commands_recorded["mixer.set_volume"].get("reversible"):
            fail("mixer.set_volume was not recorded as reversible", process, log_path)
        if commands_recorded["mixer.add_channel"].get("reversible"):
            fail("mixer.add_channel must honestly report itself as not reversible", process, log_path)

        # --- the editing flow: notes / clips / tracks (SPEC A16) ----------
        # The fixture ships one pattern track with one clip. Its clip is a
        # PatternClip: a real clip with a real id, but no note list, so the piano
        # roll refuses it by type instead of pretending it is empty.
        arrangement = ok_result(client.call(24, "arrangement.get_state"), 24)
        if arrangement.get("track_count") != 1 or arrangement.get("clip_count") != 1:
            fail("the fixture's arrangement is %r" % arrangement, process, log_path)
        fixture_track = arrangement["tracks"][0]
        fixture_clip = arrangement["clips"][0]
        if fixture_track.get("id") != "trk-0" or fixture_clip.get("id") != "clip-0":
            fail("stable ids are not trk-<n>/clip-<n>: %r %r" % (fixture_track, fixture_clip), process, log_path)
        if fixture_clip.get("note_count") is not None:
            fail("a PatternClip must report note_count null, got %r" % fixture_clip, process, log_path)
        typed_error(client.call(25, "roll.get_state", {"clip": "clip-0"}), 25, "refused")

        # A new instrument track and a clip on it.
        added_track = ok_result(client.call(26, "track.add", {"type": "instrument"}), 26)
        track = added_track.get("track")
        if not track or not track.startswith("trk-"):
            fail("track.add returned %r" % added_track, process, log_path)
        if added_track.get("type") != "instrument" or not added_track.get("name"):
            fail("track.add did not report the new track's type/name: %r" % added_track, process, log_path)

        # track.rename / track.set_mute / track.set_solo, read back per track.
        renamed = ok_result(client.call(261, "track.rename", {"track": track, "name": "Agent Track"}), 261)
        muted = ok_result(client.call(262, "track.set_mute", {"track": track, "muted": True}), 262)
        if renamed.get("name") != "Agent Track" or not muted.get("muted"):
            fail("track.rename/track.set_mute did not report the new state: %r %r" % (renamed, muted),
                 process, log_path)
        track_read = ok_result(client.call(263, "track.get_state", {"track": track}), 263)
        if track_read.get("name") != "Agent Track" or not track_read.get("muted"):
            fail("track.get_state did not read the renamed/muted track back: %r" % track_read,
                 process, log_path)

        # Soloing is the product's whole solo action, not just a flag: TrackView
        # connects the solo model's dataChanged to Track::toggleSolo(), so the
        # soloed track is unmuted and every other track is muted. The assertions
        # below pin that, rather than pretending set_solo writes one bit.
        soloed = ok_result(client.call(264, "track.set_solo", {"track": track, "solo": True}), 264)
        if not soloed.get("soloed"):
            fail("track.set_solo did not report the new state: %r" % soloed, process, log_path)
        after_solo = ok_result(client.call(265, "track.get_state", {"track": track}), 265)
        if not after_solo.get("soloed") or after_solo.get("muted"):
            fail("the solo action did not un-mute the soloed track: %r" % after_solo, process, log_path)
        other = ok_result(client.call(266, "track.get_state", {"track": "trk-0"}), 266)
        if not other.get("muted"):
            fail("the solo action did not mute the other track: %r" % other, process, log_path)

        # track.set_mute claims a real journal inverse: prove it with an undo.
        remuted = ok_result(client.call(267, "track.set_mute", {"track": track, "muted": True}), 267)
        if not remuted.get("muted"):
            fail("track.set_mute did not mute again: %r" % remuted, process, log_path)
        if not ok_result(client.call(268, "control.undo"), 268).get("undone"):
            fail("control.undo reported nothing undone after track.set_mute", process, log_path)
        after_undo_mute = ok_result(client.call(269, "track.get_state", {"track": track}), 269)
        if after_undo_mute.get("muted"):
            fail("undo of track.set_mute did not restore the unmuted state: %r" % after_undo_mute,
                 process, log_path)

        first_clip = ok_result(client.call(27, "clip.add", {"track": track, "position": 0, "length": 192}), 27)
        clip = first_clip.get("clip")
        if not clip or not clip.startswith("clip-"):
            fail("clip.add returned %r" % first_clip, process, log_path)
        # clip.add is claimed reversible (Track checkpoint): prove it with a real undo.
        if not ok_result(client.call(28, "control.undo"), 28).get("undone"):
            fail("control.undo reported nothing undone after clip.add", process, log_path)
        after_undo = ok_result(client.call(29, "arrangement.get_state"), 29)
        if after_undo.get("clip_count") != 1:
            fail("undo of clip.add left %r clips" % after_undo.get("clip_count"), process, log_path)

        # Create it again, then arrange two notes into it.
        clip = ok_result(client.call(30, "clip.add", {"track": track, "position": 0, "length": 192}), 30).get("clip")
        roll = ok_result(client.call(31, "roll.get_state", {"clip": clip}), 31)
        if roll.get("notes") != [] or roll.get("clip") != clip:
            fail("a fresh clip must roll as an empty note list: %r" % roll, process, log_path)

        # clip.select remembers the clip for a roll.get_state that names none.
        selected = ok_result(client.call(311, "clip.select", {"clip": clip}), 311)
        if selected.get("selected_clip") != clip:
            fail("clip.select returned %r" % selected, process, log_path)
        selected_roll = ok_result(client.call(312, "roll.get_state"), 312)
        if selected_roll.get("clip") != clip:
            fail("roll.get_state did not fall back to the selected clip: %r" % selected_roll,
                 process, log_path)

        note_a = ok_result(client.call(32, "note.add",
            {"clip": clip, "key": 60, "position": 0, "length": 24, "velocity": 100}), 32)
        note_b = ok_result(client.call(33, "note.add",
            {"clip": clip, "key": 64, "position": 48, "length": 24, "velocity": 64}), 33)
        if not note_a.get("note", "").startswith("note-") or not note_b.get("note", "").startswith("note-"):
            fail("note.add returned %r / %r" % (note_a, note_b), process, log_path)

        roll = ok_result(client.call(34, "roll.get_state", {"clip": clip}), 34)
        if roll.get("note_count") != 2:
            fail("roll.get_state reports %r notes after two note.add calls" % roll.get("note_count"), process, log_path)
        for note, key, velocity in zip(roll["notes"], (60, 64), (100, 64)):
            if note.get("key") != key or note.get("clip") != clip:
                fail("roll.get_state note %r does not carry its key/clip" % note, process, log_path)
            if abs(float(note.get("velocity", -1)) - velocity) > 1e-6:
                fail("roll.get_state note %r does not carry its velocity" % note, process, log_path)

        moved = ok_result(client.call(35, "note.move",
            {"clip": clip, "note": note_b["note"], "position": 96}), 35)
        resized = ok_result(client.call(36, "note.resize",
            {"clip": clip, "note": moved.get("note"), "length": 48}), 36)
        velocity = ok_result(client.call(37, "note.velocity_set",
            {"clip": clip, "note": moved.get("note"), "velocity": 30}), 37)
        if moved.get("position") != 96 or resized.get("length") != 48:
            fail("note.move/note.resize did not report the new geometry: %r %r" % (moved, resized), process, log_path)
        if abs(float(velocity.get("velocity", -1)) - 30) > 1e-6:
            fail("note.velocity_set did not report the new velocity: %r" % velocity, process, log_path)
        ok_result(client.call(38, "note.select", {"clip": clip, "notes": [moved["note"]]}), 38)

        roll = ok_result(client.call(39, "roll.get_state", {"clip": clip}), 39)
        edited = [n for n in roll.get("notes", []) if n.get("id") == moved.get("note")]
        if not edited:
            fail("roll.get_state lost the edited note: %r" % roll, process, log_path)
        edited = edited[0]
        if edited.get("position") != 96 or edited.get("length") != 48:
            fail("roll.get_state did not read back the move/resize: %r" % edited, process, log_path)
        if abs(float(edited.get("velocity", -1)) - 30) > 1e-6:
            fail("roll.get_state did not read back the velocity: %r" % edited, process, log_path)
        if not edited.get("selected"):
            fail("roll.get_state does not report the selected note: %r" % edited, process, log_path)

        # Delete the edited note, undo, read it back.
        removed = ok_result(client.call(40, "note.remove", {"clip": clip, "note": moved["note"]}), 40)
        if removed.get("note_count") != 1:
            fail("note.remove left %r notes" % removed.get("note_count"), process, log_path)
        if not ok_result(client.call(41, "control.undo"), 41).get("undone"):
            fail("control.undo reported nothing undone after note.remove", process, log_path)
        roll = ok_result(client.call(42, "roll.get_state", {"clip": clip}), 42)
        if roll.get("note_count") != 2:
            fail("undo of note.remove did not restore the note: %r" % roll, process, log_path)

        # Delete the clip, undo, read the arrangement back.
        ok_result(client.call(43, "clip.delete", {"clip": clip}), 43)
        after_delete = ok_result(client.call(44, "arrangement.get_state"), 44)
        if after_delete.get("clip_count") != 1:
            fail("clip.delete left %r clips" % after_delete.get("clip_count"), process, log_path)
        if not ok_result(client.call(45, "control.undo"), 45).get("undone"):
            fail("control.undo reported nothing undone after clip.delete", process, log_path)
        after_restore = ok_result(client.call(46, "arrangement.get_state"), 46)
        if after_restore.get("clip_count") != 2:
            fail("undo of clip.delete did not restore the clip: %r" % after_restore, process, log_path)
        clip = [c.get("id") for c in after_restore.get("clips", []) if c.get("id") != "clip-0"][0]

        # --- clip.move / clip.resize / clip.split / clip.duplicate --------
        moved_clip = ok_result(client.call(47, "clip.move", {"clip": clip, "position": 192}), 47)
        resized_clip = ok_result(client.call(48, "clip.resize", {"clip": clip, "length": 240}), 48)
        if moved_clip.get("position") != 192 or resized_clip.get("length") != 240:
            fail("clip.move/clip.resize did not report the new geometry: %r %r"
                 % (moved_clip, resized_clip), process, log_path)
        # a cut at either end is refused, exactly as the GUI's split refuses it
        typed_error(client.call(49, "clip.split", {"clip": clip, "position": 192}), 49, "invalid_args")
        split = ok_result(client.call(50, "clip.split", {"clip": clip, "position": 288}), 50)
        if not split.get("left") or not split.get("right") or split.get("left") == split.get("right"):
            fail("clip.split returned %r" % split, process, log_path)
        duplicated = ok_result(client.call(51, "clip.duplicate",
            {"clip": split["right"], "position": 480}), 51)
        if not duplicated.get("clip") or duplicated.get("source") != split.get("right"):
            fail("clip.duplicate returned %r" % duplicated, process, log_path)

        arrangement = ok_result(client.call(52, "arrangement.get_state"), 52)
        if arrangement.get("clip_count") != 4:
            fail("after split+duplicate the arrangement has %r clips: %r"
                 % (arrangement.get("clip_count"), arrangement), process, log_path)
        copy_roll = ok_result(client.call(53, "roll.get_state", {"clip": duplicated["clip"]}), 53)
        if copy_roll.get("note_count") != 2:
            fail("clip.duplicate did not copy the notes: %r" % copy_roll, process, log_path)

        # --- typed errors of the new group --------------------------------
        typed_error(client.call(54, "track.rename", {"track": "trk-999", "name": "x"}), 54, "not_found")
        typed_error(client.call(55, "clip.add", {"track": "trk-999", "position": 0}), 55, "not_found")
        # an out-of-range note key and an out-of-range velocity
        typed_error(client.call(56, "note.add",
            {"clip": copy_roll["clip"], "key": 300, "position": 0, "length": 12}), 56, "invalid_args")
        typed_error(client.call(57, "note.velocity_set",
            {"clip": copy_roll["clip"], "note": "note-0", "velocity": 9999}), 57, "invalid_args")
        # track.set_arm is an honest refusal: no arm flag exists on a Track here.
        typed_error(client.call(58, "track.set_arm", {"track": track, "armed": True}), 58, "refused")

        # --- the transaction split, printed as evidence --------------------
        transactions = ok_result(client.call(59, "control.transactions"), 59).get("transactions", [])
        print("\n---- transactions (SPEC A16) ----")
        for entry in transactions:
            print("%-22s reversible=%-5s %s" % (entry.get("command"),
                str(bool(entry.get("reversible"))).lower(), entry.get("mechanism")))
        by_command = {}
        for entry in transactions:
            by_command.setdefault(entry.get("command"), entry)
        for expected in ("note.add", "note.remove", "note.move", "note.resize",
                         "note.velocity_set", "clip.add", "clip.delete", "clip.move",
                         "clip.resize", "clip.split", "clip.duplicate", "track.rename",
                         "track.set_mute"):
            entry = by_command.get(expected)
            if entry is None:
                fail("no transaction recorded for %s" % expected, process, log_path)
            if not entry.get("reversible"):
                fail("%s was not recorded as reversible" % expected, process, log_path)
        for expected in ("track.add", "clip.select", "note.select", "track.set_solo"):
            entry = by_command.get(expected)
            if entry is None:
                fail("no transaction recorded for %s" % expected, process, log_path)
            if entry.get("reversible"):
                fail("%s must honestly report itself as not reversible" % expected, process, log_path)
            if not entry.get("mechanism"):
                fail("%s recorded no reason for being irreversible" % expected, process, log_path)

        # --- track.remove, the one destructive command of the group --------
        # (dry_run previews it; the real call is exercised here and its
        # transaction is checked in a second read of the list.)
        preview = ok_result(client.call(60, "track.remove", {"track": track, "dry_run": True}), 60)
        if not preview.get("dry_run"):
            fail("track.remove did not honour dry_run: %r" % preview, process, log_path)
        still_there = ok_result(client.call(61, "arrangement.get_state"), 61)
        if still_there.get("track_count") != 2:
            fail("a dry_run removed the track anyway: %r" % still_there, process, log_path)

        removed_track = ok_result(client.call(62, "track.remove", {"track": track}), 62)
        if removed_track.get("removed") != track:
            fail("track.remove returned %r" % removed_track, process, log_path)
        final = ok_result(client.call(63, "arrangement.get_state"), 63)
        if final.get("track_count") != 1 or final.get("clip_count") != 1:
            fail("track.remove left %r / %r" % (final.get("track_count"), final.get("clip_count")),
                 process, log_path)
        transactions = ok_result(client.call(64, "control.transactions"), 64).get("transactions", [])
        removals = [t for t in transactions if t.get("command") == "track.remove"]
        if len(removals) != 2:
            fail("expected a dry_run and a real transaction for track.remove, got %r" % removals,
                 process, log_path)
        if removals[0].get("reversible") or removals[-1].get("reversible"):
            fail("track.remove must honestly report itself as not reversible: %r" % removals,
                 process, log_path)

        # --- shutdown unlinks the socket ----------------------------------
        client.call(65, "control.quit")
        client.close()
        deadline = time.time() + 30.0
        while time.time() < deadline and process.poll() is None:
            time.sleep(0.1)
        if process.poll() is None:
            fail("the app did not exit after control.quit", process, log_path)
        if os.path.exists(socket_path):
            fail("the control socket file was not unlinked on exit", process, log_path)
        if process.returncode != 0:
            fail("the app exited with %s" % process.returncode, process, log_path)
    except (ConnectionError, TimeoutError, OSError, AssertionError) as error:
        # A crash of the instance (or a dropped connection) is a failure of this
        # test like any other: report it with the app log, not as a traceback.
        fail("the session died or an assertion raised: %s" % error, process, log_path)
    finally:
        log_file.close()
        if process.poll() is None:
            process.kill()
            process.wait()
        shutil.rmtree(tmp, ignore_errors=True)

    print("PASS: control socket integration (offscreen, external client)")
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
