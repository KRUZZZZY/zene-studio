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
  * the hosted formats really load: a LADSPA device, and - through the LV2 host

Usage: QT_QPA_PLATFORM=offscreen python3 control-socket-integration.py <lmms> <project.mmp>
Exit code 0 only when every assertion passed.
"""

import array
import json
import math
import os
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time
from typing import NoReturn
import wave

CONNECT_TIMEOUT = 30.0
ENGINE_TIMEOUT = 120.0
RESPONSE_TIMEOUT = 60.0
RENDER_TIMEOUT = 240.0

# The audio-truth fixture: tests/data/automation-audio-fixture.mmp is one
# instrument track with AUDIO_TICKS ticks of notes at AUDIO_BPM in 4/4, so the
# level comparison window is derived rather than guessed.
AUDIO_BPM = 140
AUDIO_TICKS = 384
AUDIO_TICKS_PER_BAR = 192
AUDIO_BEATS_PER_BAR = 4.0

TRANSCRIPT = []


def record(direction, payload):
    TRANSCRIPT.append("%s %s" % (direction, payload))


def fail(message, process=None, log_path=None) -> NoReturn:
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


class Flow:
    """Sequential request ids for one leg of the flow (the reply must carry the
    id that was sent, so the ids cannot be reused across legs)."""

    def __init__(self, client, last_id):
        self.client = client
        self.id = last_id

    def call(self, cmd, args=None):
        self.id += 1
        return self.id, self.client.call(self.id, cmd, args)

    def ok(self, cmd, args=None):
        request_id, reply = self.call(cmd, args)
        return ok_result(reply, request_id)

    def err(self, cmd, kind, args=None):
        request_id, reply = self.call(cmd, args)
        typed_error(reply, request_id, kind)
        return reply.get("error") or {}


def find_device(devices, name=None, format=None, kind=None, loadable=None):
    for device in devices:
        if name is not None and device.get("name") != name:
            continue
        if format is not None and device.get("format") != format:
            continue
        if kind is not None and device.get("kind") != kind:
            continue
        if loadable is not None and bool(device.get("loadable")) != loadable:
            continue
        return device
    return None


def pick_parameter(parameters):
    """A numeric parameter with a real range, preferring the well-known Volume."""
    usable = [p for p in parameters
              if p.get("type") == "number" and float(p.get("max", 0)) > float(p.get("min", 0))]
    if not usable:
        return None
    for parameter in usable:
        if parameter.get("name") == "Volume":
            return parameter
    return max(usable, key=lambda p: float(p["max"]) - float(p["min"]))


def tolerance(parameter):
    """Float round-trip tolerance: the model's own step, at least 1e-4."""
    return max(1e-4, abs(float(parameter.get("step") or 0.0)))


# The LV2 device this test drives end to end. MDA Delay is the worked example
# because *all* of its parameter ports are plain control-rate inputs: six
# lv2:ControlPort inputs (L/R Delay, Feedback, Fb Tone, FX Mix, Output) plus two
# audio inputs and two audio outputs, and no atom/CV/decimal port in the bundle
# at all (/usr/lib/lv2/mda.lv2/Delay.ttl). So every parameter is a float the
# engine's own model can carry, with nothing for a UI to supply.
LV2_WORKED_EXAMPLE_URI = "http://drobilla.net/plugins/mda/Delay"

# The six control-rate *input* ports /usr/lib/lv2/mda.lv2/Delay.ttl declares for
# MDA Delay (`a lv2:InputPort , lv2:ControlPort`). They are the device's whole
# parameter set on top of the Effect-level models every effect carries, and the
# test asserts each one is addressable - a port that the engine builds a model
# for must be readable and writable, or the surface is not really there.
LV2_WORKED_EXAMPLE_PORTS = ("L Delay", "R Delay", "Feedback", "Fb Tone", "FX Mix", "Output")

# Where LV2 bundles live. Only used to read the bundles' own declarations for
# the cross-check below - never to enumerate the device list under test.
LV2_BUNDLE_DIRS = ("/usr/lib/lv2", "/usr/local/lib/lv2", "/usr/lib64/lv2")

TTL_PREFIX_RE = re.compile(r"@prefix\s+([A-Za-z][\w.\-]*|):\s*<([^>]*)>")
TTL_SUBJECT_RE = re.compile(r"^\s*(<[^>]*>|[A-Za-z][\w.\-]*:[\w.\-]*)")


def _ttl_statements(text):
    """Split a Turtle document into top-level statements.

    Hand-rolled on purpose: the bundles declare their plugins with *prefixed*
    names (`mda:Delay`, prefix in the same file), so a grep for the URI would
    miss every MDA plugin. Comments and quoted strings are skipped; a '.' inside
    an IRI or a literal does not end a statement.
    """
    statements = []
    current = []
    in_iri = False
    in_string = False
    index = 0
    while index < len(text):
        char = text[index]
        if in_string:
            current.append(char)
            if char == "\\" and index + 1 < len(text):
                current.append(text[index + 1])
                index += 2
                continue
            if char == '"':
                in_string = False
            index += 1
            continue
        if in_iri:
            current.append(char)
            if char == ">":
                in_iri = False
            index += 1
            continue
        if char == "<":
            in_iri = True
            current.append(char)
        elif char == '"':
            in_string = True
            current.append(char)
        elif char == "#":
            while index < len(text) and text[index] != "\n":
                index += 1
            continue
        elif char == "." and (index + 1 == len(text) or text[index + 1] in " \t\r\n"):
            statements.append("".join(current))
            current = []
        else:
            current.append(char)
        index += 1
    if "".join(current).strip():
        statements.append("".join(current))
    return statements


def lv2_bundle_declared_uris():
    """Every plugin URI the installed LV2 bundles declare, per bundle.

    LV2 requires a bundle to list every plugin it contains in its manifest.ttl,
    so this is the bundles' own statement of what is installed. It is
    deliberately a *different* reader from the one under test (the engine's LV2
    host module): it is the ground truth the engine's list is checked against.
    """
    declared = {}
    for root in LV2_BUNDLE_DIRS:
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            manifest = os.path.join(root, name, "manifest.ttl")
            if not os.path.isfile(manifest):
                continue
            with open(manifest, "r", errors="replace") as handle:
                text = handle.read()
            prefixes = {}
            for match in TTL_PREFIX_RE.finditer(text):
                prefixes[match.group(1)] = match.group(2)
            statements = _ttl_statements(text)
            # A subject is a plugin when 'lv2:Plugin' appears in its own
            # statement, or when a `<subject> a lv2:Plugin` statement names it.
            plain_plugins = set()
            for statement in statements:
                subject = TTL_SUBJECT_RE.match(statement)
                if subject and "lv2:Plugin" in statement:
                    plain_plugins.add(subject.group(1))
            if not plain_plugins:
                continue

            def resolve(token):
                if token.startswith("<") and token.endswith(">"):
                    return token[1:-1]
                prefix, _, local = token.partition(":")
                return prefixes.get(prefix, "") + local

            uris = {resolve(token) for token in plain_plugins}
            uris = {uri for uri in uris if uri.startswith("http") or uri.startswith("urn:")}
            if uris:
                declared[name] = uris
    return declared



def fixture_track_ids(flow, process, log_path):
    """The fixture's track ids AS THE SURFACE REPORTS THEM, by type.

    The number in trk-<n> is CREATION-ASSIGNED and persisted in the project file
    (SPEC-stable-ids.md), not a position in Song::tracks(), so a test must READ it
    from the surface instead of spelling a literal. The fixture's song container
    is one pattern track and one instrument track, and the instrument track is
    NOT trk-1: the pattern track carries a nested <trackcontainer> whose own track
    also takes a number from the one project-scoped counter, so the song
    container's ids are trk-0 and trk-2. Hardcoding the position is exactly the
    fragility the stable-id change removes.
    """
    tracks = flow.ok("track.list").get("tracks", [])
    instrument = next((t["id"] for t in tracks if t.get("type") == "instrument"), None)
    pattern = next((t["id"] for t in tracks if t.get("type") == "pattern"), None)
    if instrument is None or pattern is None:
        fail("the fixture does not have one pattern and one instrument track: %r" % tracks,
             process, log_path)
    return instrument, pattern


def lv2_device_flow(client, process, log_path, tmp, last_id, listing):
    """The LV2 leg: catalogue visibility, then load -> param_get -> param_set ->
    state_save -> state_load -> unload on a real installed LV2 plugin, plus the
    typed refusals the LV2 port/state model produces (SPEC A11-A14)."""
    flow = Flow(client, last_id)
    instrument_track, pattern_track = fixture_track_ids(flow, process, log_path)

    # --- the LV2 half of the catalogue ------------------------------------
    devices = listing.get("devices", [])
    by_format = listing.get("counts_by_format", {})
    lv2_devices = [d for d in devices if d.get("format") == "lv2"]
    if "lv2" not in by_format:
        fail("plugin.list's format breakdown has no lv2 bucket: %r" % by_format, process, log_path)
    if int(by_format.get("lv2", 0)) != len(lv2_devices):
        fail("counts_by_format.lv2=%r but %d lv2 devices were returned"
             % (by_format.get("lv2"), len(lv2_devices)), process, log_path)
    if not lv2_devices:
        fail("this build has an LV2 host and the box has LV2 bundles, but plugin.list "
             "lists no LV2 device", process, log_path)
    for device in lv2_devices:
        if not device.get("uri"):
            fail("lv2 device %r carries no URI" % device, process, log_path)
        if device.get("name") != device.get("uri"):
            fail("lv2 device %r does not use its URI as its own id" % device, process, log_path)
        if device.get("kind") not in ("effect", "instrument"):
            fail("lv2 device %r has kind %r" % (device, device.get("kind")), process, log_path)
        if not device.get("loadable"):
            fail("lv2 device %r is not loadable" % device, process, log_path)
    print("plugin.list format=lv2: count=%d of %d total; by_kind=%r"
          % (len(lv2_devices), int(listing.get("count", 0)),
             {k: sum(1 for d in lv2_devices if d.get("kind") == k)
              for k in ("effect", "instrument")}))

    # The engine may only name devices the installed bundles declare, and every
    # bundle that declares a plugin must contribute at least one the engine can
    # see - a whole bundle silently missing is the bug this measures.
    declared = lv2_bundle_declared_uris()
    if declared:
        declared_all = set().union(*declared.values())
        invented = [d["uri"] for d in lv2_devices if d["uri"] not in declared_all]
        if invented:
            fail("plugin.list names LV2 devices no installed bundle declares: %r"
                 % invented, process, log_path)
        invisible = [name for name, uris in sorted(declared.items())
                     if not (uris & {d["uri"] for d in lv2_devices})]
        if invisible:
            fail("LV2 bundles declare plugins but contribute nothing the engine can see: %r"
                 % invisible, process, log_path)
        print("lv2 bundles: %d declaring plugins [%s], engine sees %d %s; every one is "
              "declared by its bundle"
              % (len(declared), ", ".join("%s=%d" % (n, len(u))
                                          for n, u in sorted(declared.items())),
                 len(lv2_devices), "device" if len(lv2_devices) == 1 else "devices"))
    else:
        print("lv2 bundles: no bundle manifest under %r declared a plugin; the "
              "invented-URI cross-check is skipped" % (LV2_BUNDLE_DIRS,))

    # --- pick the worked example ------------------------------------------
    worked = next((d for d in lv2_devices if d.get("uri") == LV2_WORKED_EXAMPLE_URI), None)
    if worked is None:
        fail("the LV2 worked example %s is not installed here; lv2 devices are %r"
             % (LV2_WORKED_EXAMPLE_URI, [d.get("uri") for d in lv2_devices]),
             process, log_path)
    print("lv2 worked example: %s (%s) dev id %s"
          % (worked["uri"], worked["kind"], worked["id"]))

    # --- typed errors on the way in ---------------------------------------
    # a dev id beyond the catalogue is not_found, typed.
    flow.err("plugin.load", "not_found", {"target": instrument_track, "device": "dev-999999"})
    # trk-0 is the fixture's Beat/Bassline track: no device chain at all.
    flow.err("plugin.load", "refused", {"target": pattern_track, "device": worked["id"]})

    # --- load an LV2 effect -----------------------------------------------
    loaded = flow.ok("plugin.load", {"target": instrument_track, "device": worked["id"]})
    if loaded.get("kind") != "effect" or not str(loaded.get("id", "")).startswith("fx-"):
        fail("plugin.load of the LV2 device returned %r" % loaded, process, log_path)
    if loaded.get("plugin") != "lv2effect" or loaded.get("device") != worked["id"]:
        fail("plugin.load did not report the LV2 host module and the device id: %r" % loaded,
             process, log_path)
    fx = loaded["id"]
    print("plugin.load %s -> %s via %s" % (worked["id"], fx, loaded.get("plugin")))

    # --- the parameters really exist (the port models) --------------------
    state = flow.ok("dsp.get_state", {"target": instrument_track})
    entry = None
    for device in (state.get("chains") or [{}])[0].get("devices", []):
        if device.get("id") == fx:
            entry = device
    if entry is None:
        fail("dsp.get_state does not list %s: %r" % (fx, state), process, log_path)
    parameters = entry.get("parameters", [])
    if not parameters:
        fail("the LV2 device exposes no parameters at all: %r" % entry, process, log_path)
    # The hosted plugin's own ports, not the Effect-level models every effect
    # has (enabled / wet-dry / auto-quit), must all be present.
    port_parameters = [p for p in parameters if p.get("name") in LV2_WORKED_EXAMPLE_PORTS]
    exposed = {p.get("name") for p in parameters}
    missing = [name for name in LV2_WORKED_EXAMPLE_PORTS if name not in exposed]
    if missing:
        fail("the LV2 device does not expose its declared control ports %r; it exposes %r"
             % (missing, [p.get("name") for p in parameters]), process, log_path)
    if len(port_parameters) != len(LV2_WORKED_EXAMPLE_PORTS):
        fail("the LV2 device exposes %d of its %d declared ports as parameters (names must "
             "be unique): %r" % (len(port_parameters), len(LV2_WORKED_EXAMPLE_PORTS),
                                 [p.get("name") for p in port_parameters]),
             process, log_path)
    # Address a port by name, the way an agent would.
    target_param = next(p for p in port_parameters if p["name"] == "Feedback")
    print("dsp.get_state %s: %d parameter(s), of which the %d declared control-rate ports %r"
          % (fx, len(parameters), len(port_parameters),
             [p.get("name") for p in port_parameters]))

    # --- plugin.param_get / plugin.param_set ------------------------------
    name = target_param["name"]
    low, high = float(target_param["min"]), float(target_param["max"])
    tol = tolerance(target_param)
    middle = low + (high - low) / 2.0
    if abs(float(target_param["value"]) - middle) <= tol:
        middle = low + (high - low) * 0.75
    away = low + (high - low) * 0.25

    got = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if got.get("parameter", {}).get("name") != name:
        fail("plugin.param_get returned %r for %r" % (got, name), process, log_path)
    set_reply = flow.ok("plugin.param_set",
                        {"target": instrument_track, "plugin": fx, "name": name, "value": middle})
    if abs(float(set_reply.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_set did not report the new value: %r" % set_reply, process, log_path)
    read_back = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if abs(float(read_back.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_get did not read back %r" % read_back, process, log_path)
    print("plugin.param_get/param_set %s.%s: %r -> %r (range %r..%r)"
          % (fx, name, got.get("parameter", {}).get("value"),
             read_back.get("parameter", {}).get("value"), low, high))

    # --- typed errors on the parameters -----------------------------------
    flow.err("plugin.param_get", "not_found",
             {"target": instrument_track, "plugin": fx, "name": "No Such LV2 Port"})
    flow.err("plugin.param_set", "invalid_args",
             {"target": instrument_track, "plugin": fx, "name": name, "value": high + 1000.0})

    # --- plugin.state_save / plugin.state_load ----------------------------
    state_path = os.path.join(tmp, "lv2-state.xml")
    saved = flow.ok("plugin.state_save", {"target": instrument_track, "plugin": fx, "path": state_path})
    if not saved.get("sha256") or int(saved.get("bytes", 0)) <= 0:
        fail("plugin.state_save reported %r" % saved, process, log_path)
    if not os.path.exists(state_path) or os.path.getsize(state_path) == 0:
        fail("plugin.state_save left no state at %s" % state_path, process, log_path)
    with open(state_path, "r", errors="replace") as handle:
        document = handle.read()
    if 'hosted_uri="%s"' % worked["uri"] not in document:
        fail("the LV2 state file does not name the plugin's URI, so it cannot be bound "
             "back to it: %s" % document[:400], process, log_path)
    print("plugin.state_save: %s (%d bytes, sha256 %s, names hosted_uri=%s)"
          % (saved.get("path"), int(saved.get("bytes", 0)), str(saved.get("sha256"))[:16],
             worked["uri"]))

    flow.ok("plugin.param_set", {"target": instrument_track, "plugin": fx, "name": name, "value": away})
    flow.ok("plugin.state_load", {"target": instrument_track, "plugin": fx, "path": state_path})
    restored = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if abs(float(restored.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.state_load did not restore the saved parameter: %r" % restored,
             process, log_path)
    print("plugin.state_load round trip: %s.%s restored to %r"
          % (fx, name, restored.get("parameter", {}).get("value")))

    # --- the typed refusal LV2's own identity model produces --------------
    # Every LV2 effect shares the descriptor name "lv2effect", so without the
    # URI in the state file one LV2 device's state would be accepted by any
    # other. Load a *different* LV2 effect and hand it this state file.
    other = next((d for d in lv2_devices if d.get("uri") != worked["uri"]
                  and d.get("kind") == "effect"), None)
    if other is None:
        print("lv2 typed refusal: only one LV2 effect is installed, so the "
              "cross-device state refusal cannot be exercised here")
    else:
        loaded_other = flow.ok("plugin.load", {"target": "ch-1", "device": other["id"]})
        other_fx = loaded_other["id"]
        refusal = flow.err("plugin.state_load",
                           "refused",
                           {"target": "ch-1", "plugin": other_fx, "path": state_path})
        if worked["uri"] not in refusal.get("message", ""):
            fail("the cross-device state refusal does not name the file's URI: %r" % refusal,
                 process, log_path)
        print("plugin.state_load refused across LV2 devices: %s" % refusal["message"])
        flow.ok("plugin.unload", {"target": "ch-1", "plugin": other_fx})

    # --- plugin.unload ----------------------------------------------------
    unloaded = flow.ok("plugin.unload", {"target": instrument_track, "plugin": fx})
    if unloaded.get("removed") != fx or int(unloaded.get("count", -1)) != 0:
        fail("plugin.unload returned %r" % unloaded, process, log_path)
    flow.err("plugin.unload", "not_found", {"target": instrument_track, "plugin": fx})
    print("plugin.unload %s: %d device(s) left on trk-1"
          % (fx, int(unloaded.get("count", -1))))

    # --- a device whose bundle ships an LV2 UI is still headless-safe -----
    # (SPEC A13: the LV2 UI is never required. calflv2gui.so is on disc next to
    # the calf plugins, so loading a calf device here is the "UI needs a display
    # but the device does not" case - measured, under QT_QPA_PLATFORM=offscreen
    # with no DISPLAY.)
    calf = next((d for d in lv2_devices if d.get("uri", "").startswith(
        "http://calf.sourceforge.net/plugins/")), None)
    if calf is None:
        print("lv2 UI-free load: no calf device in this build's world, so the "
              "\"bundle ships a UI\" case is not exercisable here")
    else:
        ui_binary = next((os.path.join(root, "calf.lv2", "calflv2gui.so")
                          for root in LV2_BUNDLE_DIRS
                          if os.path.exists(os.path.join(root, "calf.lv2", "calflv2gui.so"))),
                         None)
        loaded_calf = flow.ok("plugin.load", {"target": instrument_track, "device": calf["id"]})
        calf_fx = loaded_calf["id"]
        calf_state = flow.ok("dsp.get_state", {"target": instrument_track})
        calf_params = []
        for device in (calf_state.get("chains") or [{}])[0].get("devices", []):
            if device.get("id") == calf_fx:
                calf_params = device.get("parameters", [])
        if not calf_params:
            fail("the calf LV2 device exposes no parameter headlessly: %r" % calf_state,
                 process, log_path)
        flow.ok("plugin.param_get", {"target": instrument_track, "plugin": calf_fx, "index": 0})
        flow.ok("plugin.unload", {"target": instrument_track, "plugin": calf_fx})
        print("lv2 UI-free load: %s (%d parameters, UI binary %s) loaded, read and "
              "unloaded with QT_QPA_PLATFORM=offscreen and no DISPLAY"
              % (calf["uri"], len(calf_params), ui_binary or "not found on disc"))

    # --- an LV2 instrument: the catalogue's instrument half ----------------
    # A hosted instrument goes through the same key path but the instrument
    # slot, and an instrument has no unload (a later load replaces it), so this
    # is the last thing the leg does.
    lv2_instrument = next((d for d in lv2_devices if d.get("kind") == "instrument"), None)
    if lv2_instrument is None:
        print("lv2 instrument: none in this build's LV2 world, so the instrument half is "
              "catalogue-only here")
    else:
        loaded_inst = flow.ok("plugin.load",
                              {"target": instrument_track, "device": lv2_instrument["id"]})
        if loaded_inst.get("id") != "inst" or loaded_inst.get("kind") != "instrument":
            fail("plugin.load of an LV2 instrument returned %r" % loaded_inst, process, log_path)
        if loaded_inst.get("plugin") != "lv2instrument":
            fail("plugin.load did not report the LV2 instrument host: %r" % loaded_inst,
                 process, log_path)
        inst_state = flow.ok("dsp.get_state", {"target": instrument_track})
        inst_entry = (inst_state.get("chains") or [{}])[0].get("instrument") or {}
        inst_params = inst_entry.get("parameters", [])
        if not inst_params:
            fail("the LV2 instrument exposes no parameters: %r" % inst_entry, process, log_path)
        read_inst = flow.ok("plugin.param_get",
                            {"target": instrument_track, "plugin": "inst", "index": 0})
        print("lv2 instrument: %s loaded as 'inst' via %s with %d parameter(s), index 0 = %r"
              % (lv2_instrument["uri"], loaded_inst.get("plugin"), len(inst_params),
                 read_inst.get("parameter", {}).get("name")))

    return flow.id


def plugin_and_settings_flow(client, process, log_path, tmp, last_id):
    """The plugin.* / dsp.* / settings.* leg: catalogue, load, parameters,
    state, presets, unload and the settings/device read-back (SPEC A11-A16).

    Every `is None` guard below calls fail(), which prints the transcript and
    exits the process, so the subscripts that follow a guard are safe."""
    flow = Flow(client, last_id)
    instrument_track, pattern_track = fixture_track_ids(flow, process, log_path)

    # --- plugin.list: the build's device catalogue ------------------------
    listing = flow.ok("plugin.list")
    devices = listing.get("devices", [])
    by_format = listing.get("counts_by_format", {})
    by_kind = listing.get("counts_by_kind", {})
    if not devices:
        fail("plugin.list returned no devices", process, log_path)
    if int(listing.get("count", -1)) != len(devices):
        fail("plugin.list count %r != %d returned devices" % (listing.get("count"), len(devices)),
             process, log_path)
    if not by_format.get("builtin") or not by_format.get("ladspa"):
        fail("plugin.list is not broken down by format: %r" % by_format, process, log_path)
    if int(listing.get("loadable_count", 0)) <= 0:
        fail("plugin.list reports no loadable device", process, log_path)
    print("plugin.list: count=%d by_format=%r by_kind=%r loadable_count=%d"
          % (len(devices), by_format, by_kind, int(listing.get("loadable_count", 0))))

    ladspa = flow.ok("plugin.list", {"format": "ladspa", "loadable_only": True})
    if int(ladspa.get("count", 0)) <= 0:
        fail("no loadable LADSPA device: this build does not host the format", process, log_path)
    print("plugin.list format=ladspa loadable_only=true: count=%d" % int(ladspa.get("count", 0)))

    effect_device = (find_device(devices, name="amplifier", format="builtin")
                     or find_device(devices, format="builtin", kind="effect"))
    instrument_device = find_device(devices, format="builtin", kind="instrument")
    ladspa_device = find_device(ladspa.get("devices", []), loadable=True)
    if effect_device is None or instrument_device is None:
        fail("the catalogue has no builtin effect or instrument: %r" % by_kind, process, log_path)
    print("plugin.list: effect=%s(%s) instrument=%s(%s) ladspa=%s(%s)"
          % (effect_device["id"], effect_device["name"], instrument_device["id"],
             instrument_device["name"], ladspa_device["id"], ladspa_device["name"]))

    # --- typed errors on the way in --------------------------------------
    flow.err("plugin.load", "not_found", {"target": instrument_track, "device": "dev-999999"})
    flow.err("plugin.load", "invalid_args", {"target": instrument_track, "device": "amplifier"})
    # trk-0 is the fixture's Beat/Bassline track: it has no device chain at all.
    flow.err("plugin.load", "refused", {"target": pattern_track, "device": effect_device["id"]})
    # an instrument loads onto a track, never onto a mixer channel.
    flow.err("plugin.load", "refused", {"target": "ch-1", "device": instrument_device["id"]})

    # --- load an effect onto the instrument track ------------------------
    loaded = flow.ok("plugin.load", {"target": instrument_track, "device": effect_device["id"]})
    if loaded.get("kind") != "effect" or not str(loaded.get("id", "")).startswith("fx-"):
        fail("plugin.load returned %r" % loaded, process, log_path)
    fx = loaded["id"]
    print("plugin.load %s -> %s (%s)" % (effect_device["id"], fx, loaded.get("plugin")))

    # --- dsp.get_state: the read-back the ids come from ------------------
    state = flow.ok("dsp.get_state", {"target": instrument_track})
    chains = state.get("chains", [])
    if not chains or chains[0].get("id") != instrument_track:
        fail("dsp.get_state did not read trk-1: %r" % chains, process, log_path)
    entry = None
    for device in chains[0].get("devices", []):
        if device.get("id") == fx:
            entry = device
    if entry is None:
        fail("dsp.get_state does not list %s: %r" % (fx, chains[0]), process, log_path)
    if entry.get("enabled") is not True:
        fail("a freshly loaded device is not enabled: %r" % entry, process, log_path)
    parameters = entry.get("parameters", [])
    target_param = pick_parameter(parameters)
    if target_param is None:
        fail("%s exposes no ranged numeric parameter: %r" % (fx, parameters), process, log_path)
    instrument = chains[0].get("instrument") or {}
    if instrument.get("id") != "inst" or not instrument.get("parameters"):
        fail("dsp.get_state did not report the track instrument: %r" % instrument, process,
             log_path)
    print("dsp.get_state trk-1: %d device(s); %s has %d parameters, instrument has %d"
          % (chains[0].get("count"), fx, len(parameters), len(instrument.get("parameters"))))

    # --- plugin.param_get / plugin.param_set -----------------------------
    name = target_param["name"]
    low, high = float(target_param["min"]), float(target_param["max"])
    tol = tolerance(target_param)
    middle = low + (high - low) / 2.0
    current = float(target_param["value"])
    if abs(current - middle) <= tol:
        # Keep the first write a real change, so the read-back proves something.
        middle = low + (high - low) * 0.75

    got = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if got.get("parameter", {}).get("name") != name:
        fail("plugin.param_get returned %r for %r" % (got, name), process, log_path)

    set_reply = flow.ok("plugin.param_set",
                        {"target": instrument_track, "plugin": fx, "name": name, "value": middle})
    if abs(float(set_reply.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_set did not report the new value: %r" % set_reply, process, log_path)
    read_back = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if abs(float(read_back.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_get did not read back %r" % read_back, process, log_path)
    print("plugin.param_get/param_set %s: %r -> %r (range %r..%r, tolerance %g)"
          % (name, got.get("parameter", {}).get("value"),
             read_back.get("parameter", {}).get("value"), low, high, tol))

    # --- typed errors: bad parameter name, bad instance id, out-of-range --
    flow.err("plugin.param_get", "not_found",
             {"target": instrument_track, "plugin": fx, "name": "No Such Parameter"})
    flow.err("plugin.param_get", "not_found",
             {"target": instrument_track, "plugin": "fx-99", "name": name})
    flow.err("plugin.param_set", "invalid_args",
             {"target": instrument_track, "plugin": fx, "name": name, "value": high + 1000.0})
    flow.err("plugin.param_set", "invalid_args",
             {"target": instrument_track, "plugin": fx, "name": name, "value": low - 1000.0})

    # --- SPEC A16: a parameter change is a journal checkpoint ------------
    away = low + (high - low) * 0.25
    flow.ok("plugin.param_set", {"target": instrument_track, "plugin": fx, "name": name, "value": away})
    undone = flow.ok("control.undo")
    after_undo = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if not undone.get("undone"):
        fail("control.undo reported nothing undone after plugin.param_set", process, log_path)
    if abs(float(after_undo.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("control.undo did not restore the parameter: %r" % after_undo, process, log_path)
    print("control.undo reversed plugin.param_set: %s back to %r"
          % (name, after_undo.get("parameter", {}).get("value")))

    # --- plugin.state_save / plugin.state_load ---------------------------
    state_path = os.path.join(tmp, "plugin-state.xml")
    saved = flow.ok("plugin.state_save", {"target": instrument_track, "plugin": fx, "path": state_path})
    if not saved.get("sha256") or int(saved.get("bytes", 0)) <= 0:
        fail("plugin.state_save reported %r" % saved, process, log_path)
    if not os.path.exists(state_path) or os.path.getsize(state_path) == 0:
        fail("plugin.state_save left no state at %s" % state_path, process, log_path)
    print("plugin.state_save: %s (%d bytes, sha256 %s)"
          % (saved.get("path"), int(saved.get("bytes", 0)), str(saved.get("sha256"))[:16]))
    # a second save to the same path must refuse rather than silently clobber.
    flow.err("plugin.state_save", "refused",
             {"target": instrument_track, "plugin": fx, "path": state_path})

    flow.ok("plugin.param_set", {"target": instrument_track, "plugin": fx, "name": name, "value": away})
    flow.ok("plugin.state_load", {"target": instrument_track, "plugin": fx, "path": state_path})
    restored = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": fx, "name": name})
    if abs(float(restored.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.state_load did not restore the saved parameter: %r" % restored, process,
             log_path)
    print("plugin.state_load round trip: %s restored to %r"
          % (name, restored.get("parameter", {}).get("value")))

    # --- plugin.preset_list / preset_save / preset_load ------------------
    preset = flow.ok("plugin.preset_save",
                     {"target": instrument_track, "plugin": fx, "name": "agent-flow"})
    if not str(preset.get("path", "")).endswith(".xpf") or not os.path.exists(preset["path"]):
        fail("plugin.preset_save wrote nothing: %r" % preset, process, log_path)
    listed = flow.ok("plugin.preset_list", {"target": instrument_track, "plugin": fx})
    if not any(p.get("name") == "agent-flow" for p in listed.get("presets", [])):
        fail("plugin.preset_list does not show the saved preset: %r" % listed, process, log_path)
    flow.ok("plugin.preset_load", {"target": instrument_track, "plugin": fx, "name": "agent-flow"})
    print("plugin.preset_save/list/load: %s in %s"
          % (preset.get("name"), listed.get("dir")))
    flow.err("plugin.preset_load", "not_found",
             {"target": instrument_track, "plugin": fx, "name": "no-such-preset"})
    flow.err("plugin.preset_save", "invalid_args",
             {"target": instrument_track, "plugin": fx, "name": "../escape"})

    # --- instrument: load, parameters, state, preset ---------------------
    loaded_inst = flow.ok("plugin.load", {"target": instrument_track, "device": instrument_device["id"]})
    if loaded_inst.get("id") != "inst" or loaded_inst.get("kind") != "instrument":
        fail("plugin.load of an instrument returned %r" % loaded_inst, process, log_path)
    inst_state = flow.ok("dsp.get_state", {"target": instrument_track})
    inst_entry = (inst_state.get("chains") or [{}])[0].get("instrument") or {}
    inst_param = pick_parameter(inst_entry.get("parameters", []))
    if inst_param is None:
        fail("the instrument exposes no ranged numeric parameter: %r" % inst_entry, process,
             log_path)
    inst_low, inst_high = float(inst_param["min"]), float(inst_param["max"])
    inst_mid = inst_low + (inst_high - inst_low) / 2.0
    inst_away = inst_low + (inst_high - inst_low) * 0.25
    inst_tol = tolerance(inst_param)

    flow.ok("plugin.param_set", {"target": instrument_track, "plugin": "inst",
                                 "index": inst_param["index"], "value": inst_mid})
    inst_read = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": "inst",
                                             "index": inst_param["index"]})
    if abs(float(inst_read.get("parameter", {}).get("value", -1e30)) - inst_mid) > inst_tol:
        fail("plugin.param_get on 'inst' did not read back %r" % inst_read, process, log_path)
    inst_state_path = os.path.join(tmp, "instrument-state.xpf")
    flow.ok("plugin.state_save", {"target": instrument_track, "plugin": "inst",
                                  "path": inst_state_path})
    flow.ok("plugin.param_set", {"target": instrument_track, "plugin": "inst",
                                 "index": inst_param["index"], "value": inst_away})
    flow.ok("plugin.state_load", {"target": instrument_track, "plugin": "inst",
                                  "path": inst_state_path})
    inst_restored = flow.ok("plugin.param_get", {"target": instrument_track, "plugin": "inst",
                                                 "index": inst_param["index"]})
    if abs(float(inst_restored.get("parameter", {}).get("value", -1e30)) - inst_mid) > inst_tol:
        fail("the instrument state round trip did not restore %r" % inst_restored, process,
             log_path)
    flow.ok("plugin.preset_save", {"target": instrument_track, "plugin": "inst", "name": "agent-inst"})
    # an instrument has no unload: the refusal must name the supported surface.
    flow.err("plugin.unload", "invalid_args", {"target": instrument_track, "plugin": "inst"})
    print("instrument %s: %s round-tripped through plugin.state_save/state_load"
          % (instrument_device["name"], inst_param["name"]))

    # --- plugin.bypass ---------------------------------------------------
    bypassed = flow.ok("plugin.bypass", {"target": instrument_track, "plugin": fx, "bypass": True})
    if bypassed.get("enabled") is not False or bypassed.get("processing") is not False:
        fail("plugin.bypass did not switch the device off: %r" % bypassed, process, log_path)
    bypass_state = flow.ok("dsp.get_state", {"target": instrument_track})
    bypass_entry = None
    for device in (bypass_state.get("chains") or [{}])[0].get("devices", []):
        if device.get("id") == fx:
            bypass_entry = device
    if bypass_entry is None or bypass_entry.get("enabled") is not False:
        fail("dsp.get_state does not show the bypassed device: %r" % bypass_state, process,
             log_path)
    flow.ok("plugin.bypass", {"target": instrument_track, "plugin": fx, "bypass": False})
    print("plugin.bypass: %s enabled=%r processing=%r -> off, read back off, back on"
          % (fx, bypassed.get("enabled"), bypassed.get("processing")))

    # --- plugin.unload ---------------------------------------------------
    unloaded = flow.ok("plugin.unload", {"target": instrument_track, "plugin": fx})
    if unloaded.get("removed") != fx or int(unloaded.get("count", -1)) != 0:
        fail("plugin.unload returned %r" % unloaded, process, log_path)
    flow.err("plugin.unload", "not_found", {"target": instrument_track, "plugin": fx})
    print("plugin.unload %s: %d device(s) left on trk-1" % (fx, int(unloaded.get("count", -1))))

    # --- the hosted format really loads: a LADSPA device on a channel ----
    ladspa_loaded = flow.ok("plugin.load", {"target": "ch-1", "device": ladspa_device["id"]})
    ladspa_fx = ladspa_loaded["id"]
    ladspa_state = flow.ok("dsp.get_state", {"target": "ch-1"})
    ladspa_params = []
    for device in (ladspa_state.get("chains") or [{}])[0].get("devices", []):
        if device.get("id") == ladspa_fx:
            ladspa_params = device.get("parameters", [])
    if not ladspa_params:
        fail("the LADSPA device exposes no parameters: %r" % ladspa_state, process, log_path)
    # LADSPA ports carry more than one model per name, so address by index.
    flow.ok("plugin.param_get", {"target": "ch-1", "plugin": ladspa_fx, "index": 0})
    flow.ok("plugin.unload", {"target": "ch-1", "plugin": ladspa_fx})
    print("plugin.load LADSPA %s (%s) -> %s on ch-1, %d parameters, unloaded"
          % (ladspa_device["id"], ladspa_device["name"], ladspa_fx, len(ladspa_params)))

    # --- settings.get / settings.set -------------------------------------
    device_setting = flow.ok("settings.get", {"key": "audioengine/audiodev"})
    if not device_setting.get("present") or not device_setting.get("value"):
        fail("settings.get audioengine/audiodev returned %r" % device_setting, process, log_path)
    key = "ui/saveinterval"
    before = flow.ok("settings.get", {"key": key})
    written = flow.ok("settings.set", {"key": key, "value": "7"})
    if written.get("value") != "7" or written.get("persisted") is not True:
        fail("settings.set returned %r" % written, process, log_path)
    if written.get("previous") != before.get("value"):
        fail("settings.set reported previous %r, read %r"
             % (written.get("previous"), before.get("value")), process, log_path)
    after = flow.ok("settings.get", {"key": key})
    if after.get("value") != "7":
        fail("settings.get did not read back the new value: %r" % after, process, log_path)
    print("settings.get/set %s: %r -> %r (previous %r), persisted to the config file"
          % (key, before.get("value"), after.get("value"), written.get("previous")))
    flow.ok("settings.set", {"key": key, "value": before.get("value", "5")})
    flow.err("settings.get", "invalid_args", {"key": "not-a-key"})
    flow.err("settings.set", "invalid_args", {"key": "not-a-key", "value": "1"})

    # --- audio.device_list / audio.device_set ----------------------------
    audio = flow.ok("audio.device_list")
    audio_devices = audio.get("devices", [])
    if not audio_devices or not audio.get("current"):
        fail("audio.device_list returned %r" % audio, process, log_path)
    if not any(device.get("current") for device in audio_devices):
        fail("audio.device_list names no running device: %r" % audio_devices, process, log_path)
    print("audio.device_list: current=%r count=%d devices=%r"
          % (audio.get("current"), int(audio.get("count", 0)),
             [d.get("name") for d in audio_devices]))
    flow.err("audio.device_set", "not_found", {"device": "Not A Backend"})
    other = next((d["name"] for d in audio_devices if not d.get("current")), audio["current"])
    chosen = flow.ok("audio.device_set", {"device": other})
    if chosen.get("applied") != "next_start" or chosen.get("restart_required") is not True:
        fail("audio.device_set did not report an honest next-start result: %r" % chosen, process,
             log_path)
    stored = flow.ok("settings.get", {"key": "audioengine/audiodev"})
    if stored.get("value") != other:
        fail("audio.device_set did not write the config key: %r" % stored, process, log_path)
    flow.ok("audio.device_set", {"device": audio["current"]})
    print("audio.device_set %r: applied=%r restart_required=%r (config now %r)"
          % (other, chosen.get("applied"), chosen.get("restart_required"), stored.get("value")))

    # --- midi.device_list / app.version ----------------------------------
    midi = flow.ok("midi.device_list")
    if not midi.get("client"):
        fail("midi.device_list named no client: %r" % midi, process, log_path)
    print("midi.device_list: client=%r readable=%d writable=%d configured=%r"
          % (midi.get("client"), len(midi.get("readable") or []),
             len(midi.get("writable") or []), midi.get("configured")))
    version = flow.ok("app.version")
    if not version.get("version") or int(version.get("proto", 0)) != 1:
        fail("app.version returned %r" % version, process, log_path)
    print("app.version: %s (%s) proto=%r" % (version.get("version"), version.get("product"),
                                             version.get("proto")))

    # --- every mutating command left a transaction (SPEC A16) ------------
    transactions = flow.ok("control.transactions").get("transactions", [])
    recorded = {}
    for transaction in transactions:
        recorded.setdefault(transaction.get("command"), []).append(transaction)
    expected = ["plugin.load", "plugin.unload", "plugin.bypass", "plugin.param_set",
                "plugin.state_save", "plugin.state_load", "plugin.preset_save",
                "plugin.preset_load", "settings.set", "audio.device_set"]
    for command in expected:
        if command not in recorded:
            fail("no transaction recorded for %s" % command, process, log_path)
    for command in ("plugin.param_set", "plugin.bypass", "plugin.load",
                    "plugin.state_load", "plugin.preset_load", "settings.set",
                    "audio.device_set"):
        if not recorded[command][-1].get("reversible"):
            fail("%s must record reversible=true" % command, process, log_path)
    for command in ("plugin.unload", "plugin.state_save", "plugin.preset_save"):
        if recorded[command][-1].get("reversible"):
            fail("%s must honestly record reversible=false" % command, process, log_path)
    if recorded["plugin.load"][0].get("inverse", {}).get("op") != "plugin.unload":
        fail("plugin.load did not record plugin.unload as its inverse", process, log_path)
    unload_tx = recorded["plugin.unload"][-1]
    if not unload_tx.get("before", {}).get("state_xml"):
        fail("plugin.unload recorded no state snapshot: %r" % unload_tx, process, log_path)
    if not recorded["plugin.state_load"][-1].get("before", {}).get("state_xml"):
        fail("plugin.state_load recorded no pre-load snapshot", process, log_path)
    print("transactions recorded (command -> reversible):")
    for command in expected:
        print("  %-22s %s" % (command, recorded[command][-1].get("reversible")))

    # --- the second hosted format: LV2 (SPEC A11-A14, task #627) ----------
    # The catalogue it rides on is `listing`, the unfiltered plugin.list taken
    # at the top of this leg.
    flow = Flow(client, lv2_device_flow(client, process, log_path, tmp, flow.id, listing))

    return flow.id


def pick_audio_parameter(parameters):
    """A ranged numeric parameter that is on the audio path of an effect: the
    level control first, then the widest range."""
    usable = [p for p in parameters
              if p.get("type") == "number" and float(p.get("max", 0)) > float(p.get("min", 0))]
    if not usable:
        return None
    for parameter in usable:
        if (parameter.get("name") or "").strip().lower() in ("volume", "gain", "amplitude"):
            return parameter
    return max(usable, key=lambda p: float(p["max"]) - float(p["min"]))


def wav_dbfs(path, window_frames=None, offset_frames=0):
    """The rendered file's own RMS in dBFS, measured, not asserted. Returns
    (dbfs, frames, channels, sample_width); a silent file reports -inf. With
    window_frames/offset_frames only that slice of the file is measured - the
    render carries a silent tail past the song, so a whole-file figure dilutes
    the automation's effect and the named window is the honest measurement."""
    with wave.open(path, "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        frames = handle.getnframes()
        if offset_frames:
            handle.setpos(min(offset_frames, frames))
        available = max(0, frames - min(offset_frames, frames))
        raw = handle.readframes(available if window_frames is None
                                else min(available, window_frames))
    if width == 2:
        samples = array.array("h")
        scale = 32768.0
    elif width == 4:
        samples = array.array("f")
        scale = 1.0
    elif width == 1:
        samples = array.array("b")
        scale = 128.0
    else:
        raise AssertionError("unsupported WAV sample width %d" % width)
    samples.frombytes(raw)
    if not len(samples):
        return float("-inf"), frames, channels, width
    total = 0.0
    for value in samples:
        total += float(value) * float(value)
    rms = math.sqrt(total / len(samples)) / scale
    dbfs = 20.0 * math.log10(rms) if rms > 0.0 else float("-inf")
    return dbfs, frames, channels, width


def frames_for_ticks(ticks):
    """\a ticks of the audio fixture's timeline in frames at the render's 44100 Hz."""
    seconds_per_bar = AUDIO_BEATS_PER_BAR * 60.0 / AUDIO_BPM
    return int(ticks / float(AUDIO_TICKS_PER_BAR) * seconds_per_bar * 44100)


def note_region_frames():
    """The audio fixture's note region - AUDIO_TICKS ticks at AUDIO_BPM in 4/4 -
    in frames. The render itself is longer than the song, and that tail is
    silence for every shape, so the level comparison is made over the notes."""
    return frames_for_ticks(AUDIO_TICKS)


def automation_parameter(state, track_id, parameter_id):
    for track in state.get("tracks", []):
        if track.get("id") != track_id:
            continue
        for parameter in track.get("parameters", []):
            if parameter.get("id") == parameter_id:
                return parameter
    return None


def automation_points(state, track_id, parameter_id):
    parameter = automation_parameter(state, track_id, parameter_id) or {}
    return ((parameter.get("automation") or {}).get("points")) or []


def scripting_flow(client, process, log_path, tmp, last_id):
    """The script.* leg: list the shipped scripts, run one IN THIS INSTANCE and
    read its Lua log lines back, then the typed refusals (missing file, both
    selectors, and a script that burns the instruction budget).

    The in-instance part is the point: `--run-script <file>` is run-and-exit, so
    the test asserts the instance is still the same live process afterwards and
    that the script saw the session the socket client just built."""
    flow = Flow(client, last_id)

    # --- script.list: the scripts this build ships -----------------------
    listing = flow.ok("script.list")
    scripts = listing.get("scripts", [])
    shipped = sorted(s.get("name") for s in scripts)
    directory = listing.get("dir") or ""
    if not directory or not os.path.isdir(directory):
        fail("script.list resolved no on-disk scripts dir: %r" % listing, process, log_path)
    for required in ("hello.lua", "create-pattern.lua", "generative-bass.lua", "midi-router.lua"):
        if required not in shipped:
            fail("script.list does not ship %s: %r" % (required, shipped), process, log_path)
    if int(listing.get("count", -1)) != len(scripts):
        fail("script.list count %r != %d scripts" % (listing.get("count"), len(scripts)),
             process, log_path)
    for script in scripts:
        if not script.get("sha256") or int(script.get("bytes", 0)) <= 0:
            fail("script.list entry carries no hash/size: %r" % script, process, log_path)
    print("script.list: %d shipped script(s) in %s: %s"
          % (len(scripts), directory, ", ".join(shipped)))

    # --- run a shipped script in the live instance ------------------------
    hello_path = os.path.join(directory, "hello.lua")
    ran = flow.ok("script.run", {"path": hello_path})
    logs = ran.get("log") or []
    if ran.get("ran") is not True or int(ran.get("log_lines", -1)) != len(logs) or len(logs) < 3:
        fail("script.run(hello.lua) returned %r" % ran, process, log_path)
    joined = "\n".join(logs)
    for expected in ("Hello from Lua", "LMMS Lua API", "hello.lua finished"):
        if expected not in joined:
            fail("script.run did not return hello.lua's %r line: %r" % (expected, logs),
                 process, log_path)
    print("script.run hello.lua: %d log lines" % len(logs))
    for line in logs:
        print("    %s" % line)

    # The run-and-exit CLI would have taken the instance with it.
    if process.poll() is not None:
        fail("the instance died during script.run (run-and-exit CLI behaviour, not in-process)",
             process, log_path)
    flow.ok("control.version")

    # --- the script sees THIS instance's session --------------------------
    flow.ok("transport.set_tempo", {"bpm": 143})
    in_instance = flow.ok("script.run", {
        "source": "lmms.log():info(\"live tempo=\" .. lmms.song():tempo())\n"})
    live_logs = in_instance.get("log") or []
    if not any("live tempo=143" in line for line in live_logs):
        fail("script.run did not see the running instance's tempo (expected 143): %r"
             % in_instance, process, log_path)
    print("script.run saw the live session: %s" % live_logs[0])
    flow.ok("transport.set_tempo", {"bpm": 140})

    # --- typed errors -----------------------------------------------------
    flow.err("script.run", "not_found", {"path": os.path.join(tmp, "no-such-script.lua")})
    flow.err("script.run", "invalid_args", {"path": hello_path, "source": "return 1"})
    flow.err("script.run", "invalid_args", {})
    error = flow.err("script.run", "refused", {
        "source": "lmms.log():info('runaway')\nwhile true do end\n", "budget": 20000})
    if "instruction budget exceeded" not in (error.get("message") or ""):
        fail("the budget refusal does not name the budget: %r" % error, process, log_path)
    print("script.run budget refusal: %s" % error.get("message"))
    # a runaway script must not take the instance or its engine down.
    flow.ok("control.version")
    if int(flow.ok("transport.get_state").get("tempo", 0)) != 140:
        fail("the engine stopped answering after a runaway script", process, log_path)

    transactions = flow.ok("control.transactions").get("transactions", [])
    recorded = [t for t in transactions if t.get("command") == "script.run"]
    if not recorded:
        fail("script.run recorded no transaction", process, log_path)
    if recorded[-1].get("reversible"):
        fail("script.run must honestly record reversible=false: %r" % recorded[-1],
             process, log_path)
    if "script" not in str(recorded[-1].get("mechanism", "")):
        fail("script.run's transaction does not name the mechanism: %r" % recorded[-1],
             process, log_path)
    print("script.run transaction: reversible=%r mechanism=%r"
          % (recorded[-1].get("reversible"), recorded[-1].get("mechanism")))

    return flow.id


def automation_flow(client, process, log_path, tmp, project, last_id):
    """The automation.* leg, ending in rendered audio: load an instrument and an
    effect on the audio fixture's track, automate the effect's level with two
    different point shapes, and measure that the renders differ."""
    flow = Flow(client, last_id)

    fixture_dir = os.path.dirname(os.path.abspath(project))
    audio_fixture = os.path.join(fixture_dir, "automation-audio-fixture.mmp")
    if not os.path.exists(audio_fixture):
        fail("no audio-truth fixture at %s" % audio_fixture, process, log_path)

    opened = flow.ok("project.open", {"path": audio_fixture})
    if not opened.get("file"):
        fail("project.open did not report the audio fixture: %r" % opened, process, log_path)

    tracks = flow.ok("track.list").get("tracks", [])
    if len(tracks) != 1 or tracks[0].get("type") != "instrument":
        fail("the audio fixture is not one instrument track: %r" % tracks, process, log_path)
    target = tracks[0]["id"]

    # --- load an instrument and an effect, the audio path of the render ----
    listing = flow.ok("plugin.list")
    devices = listing.get("devices", [])
    instrument_device = (find_device(devices, name="tripleoscillator")
                         or find_device(devices, format="builtin", kind="instrument"))
    effect_device = (find_device(devices, name="amplifier")
                     or find_device(devices, format="builtin", kind="effect"))
    if instrument_device is None or effect_device is None:
        fail("the catalogue has no instrument/effect to load: %r"
             % listing.get("counts_by_kind"), process, log_path)
    loaded_instrument = flow.ok("plugin.load", {"target": target,
                                                "device": instrument_device["id"]})
    if loaded_instrument.get("kind") != "instrument":
        fail("plugin.load of the instrument returned %r" % loaded_instrument, process, log_path)
    loaded_effect = flow.ok("plugin.load", {"target": target, "device": effect_device["id"]})
    fx = loaded_effect.get("id")
    if loaded_effect.get("kind") != "effect" or not str(fx).startswith("fx-"):
        fail("plugin.load of the effect returned %r" % loaded_effect, process, log_path)

    state = flow.ok("dsp.get_state", {"target": target})
    chain = (state.get("chains") or [{}])[0]
    entry = next((d for d in chain.get("devices", []) if d.get("id") == fx), None)
    if entry is None:
        fail("dsp.get_state does not list %s: %r" % (fx, chain), process, log_path)
    level = pick_audio_parameter(entry.get("parameters", []))
    if level is None:
        fail("%s exposes no ranged numeric parameter: %r" % (fx, entry), process, log_path)
    parameter = "%s/%d" % (fx, level["index"])
    low, high = float(level["min"]), float(level["max"])
    print("automation target: %s on %s (%s range %g..%g, default %g)"
          % (parameter, target, level["name"], low, high, float(level["value"])))

    # --- read-back before anything is automated ---------------------------
    before_state = flow.ok("automation.get_state", {"track": target})
    entry_json = automation_parameter(before_state, target, parameter)
    if entry_json is None:
        fail("automation.get_state does not list %s: %r" % (parameter, before_state),
             process, log_path)
    if entry_json.get("automated") or int(before_state.get("automated_parameter_count", -1)) != 0:
        fail("the fixture's parameter is already automated: %r" % entry_json, process, log_path)
    if (entry_json.get("id") or "") != parameter:
        fail("automation.get_state listed %r where %r was asked for: %r"
             % (entry_json.get("id"), parameter, entry_json), process, log_path)
    print("automation.get_state %s: %d parameter(s), %d automated"
          % (target, len(before_state.get("tracks", [{}])[0].get("parameters", [])),
             int(before_state.get("automated_parameter_count", -1))))

    # --- typed errors on the way in ---------------------------------------
    flow.err("automation.get_state", "not_found", {"track": "trk-9"})
    flow.err("automation.add_point", "not_found",
             {"track": target, "parameter": "inst/99", "ticks": 0, "value": 1})
    flow.err("automation.add_point", "invalid_args",
             {"track": target, "parameter": "no-slash", "ticks": 0, "value": 1})
    flow.err("automation.add_point", "invalid_args",
             {"track": target, "parameter": parameter, "ticks": 0, "value": high + 1000.0})
    flow.err("automation.add_point", "invalid_args",
             {"track": target, "parameter": parameter, "ticks": 0})
    # automation.mode_set: this tree has no modes, and says which fact is missing.
    refused = flow.err("automation.mode_set", "refused",
                       {"track": target, "parameter": parameter, "mode": "write"})
    if "KNOWN-LIMITATIONS" not in (refused.get("message") or ""):
        fail("automation.mode_set's refusal does not cite its source: %r" % refused,
             process, log_path)
    if "no automation modes" not in (refused.get("message") or ""):
        fail("automation.mode_set's refusal does not name what is missing: %r" % refused,
             process, log_path)
    flow.err("automation.mode_set", "invalid_args",
             {"track": target, "parameter": parameter, "mode": "bogus"})
    flow.err("automation.remove_point", "not_found",
             {"track": target, "parameter": parameter, "ticks": 192})
    flow.err("automation.clear", "not_found", {"track": target, "parameter": parameter})
    print("automation.mode_set refused: %s" % refused.get("message"))

    # --- render A: no automation (the parameter's default) ----------------
    window = note_region_frames()
    # The two 96-tick windows the shapes differ over: the head (which the swell
    # drives to silence) and the tail (which the fade drives to silence). A clip's
    # progression type decides whether a point pair is a step or a line; this
    # engine's default is `discrete`, and the windows hold either way.
    head = frames_for_ticks(AUDIO_TICKS // 4)
    tail_offset = frames_for_ticks(AUDIO_TICKS // 2)
    tail = window - tail_offset
    flat_name = "flat (no automation)"
    swell_name = "swell (0 then %g at tick 96)" % high
    fade_name = "fade (%g then 0 at tick 96)" % high
    renders = {}

    def measure(name, path, rendered):
        """Hash plus the measured levels: whole file, the fixture's note region,
        and the head/tail 96-tick windows the two shapes differ over."""
        renders[name] = {
            "sha256": rendered.get("sha256"),
            "frames": rendered.get("frames"),
            "whole_dbfs": wav_dbfs(path)[0],
            "note_dbfs": wav_dbfs(path, window)[0],
            "head_dbfs": wav_dbfs(path, head)[0],
            "tail_dbfs": wav_dbfs(path, tail, tail_offset)[0],
        }
        return renders[name]

    out_a = os.path.join(tmp, "automation-flat.wav")
    rendered = render_to_file(flow, out_a, process, log_path)
    measure(flat_name, out_a, rendered)
    if int(rendered.get("frames", 0)) <= 0:
        fail("the audio fixture rendered no frames: %r" % rendered, process, log_path)

    # --- shape 1: silent, then full from tick 96 --------------------------
    created = flow.ok("automation.add_point",
                      {"track": target, "parameter": parameter, "ticks": 0, "value": 0.0})
    if created.get("created_automation_track") is not True:
        fail("the first add_point did not report creating the clip: %r" % created, process, log_path)
    flow.ok("automation.add_point",
            {"track": target, "parameter": parameter, "ticks": 96, "value": high})
    swell = flow.ok("automation.add_point",
                    {"track": target, "parameter": parameter, "ticks": 384, "value": high})
    if int((swell.get("automation") or {}).get("point_count", 0)) != 3:
        fail("add_point did not leave 3 points: %r" % swell, process, log_path)
    out_b = os.path.join(tmp, "automation-swell.wav")
    rendered = render_to_file(flow, out_b, process, log_path)
    measure(swell_name, out_b, rendered)

    # --- shape 2: full, then silent from tick 96 --------------------------
    flow.ok("automation.clear", {"track": target, "parameter": parameter})
    flow.ok("automation.add_point",
            {"track": target, "parameter": parameter, "ticks": 0, "value": high})
    flow.ok("automation.add_point",
            {"track": target, "parameter": parameter, "ticks": 96, "value": 0.0})
    fade = flow.ok("automation.add_point",
                   {"track": target, "parameter": parameter, "ticks": 384, "value": 0.0})
    if int((fade.get("automation") or {}).get("point_count", 0)) != 3:
        fail("the second shape did not leave 3 points: %r" % fade, process, log_path)
    out_c = os.path.join(tmp, "automation-fade.wav")
    rendered = render_to_file(flow, out_c, process, log_path)
    measure(fade_name, out_c, rendered)

    if window >= renders[flat_name]["frames"]:
        fail("the level window (%d frames) is not narrower than the render (%d frames): the "
             "comparison would be vacuous" % (window, renders[flat_name]["frames"]),
             process, log_path)
    hashes = {name: value["sha256"] for name, value in renders.items()}
    if len(set(hashes.values())) != len(hashes):
        fail("the automation did not change the rendered audio: %r" % hashes, process, log_path)
    flat = renders[flat_name]
    swell = renders[swell_name]
    fade = renders[fade_name]
    # The swell reaches full gain by tick 96 and stays there: over the note region
    # as a whole it is louder than the un-automated render, and its first quarter
    # is silence.
    if not swell["note_dbfs"] > flat["note_dbfs"] + 1.0:
        fail("the 'swell' automation did not raise the note region: %r" % renders,
             process, log_path)
    if not swell["head_dbfs"] < flat["head_dbfs"] - 12.0:
        fail("the 'swell' automation did not start from silence: %r" % renders, process, log_path)
    # The fade drives the parameter to 0 by tick 96: everything after it is
    # silent. (Its note-region average is close to the flat render on purpose -
    # the shape *begins* at full gain, so the surviving head is ~6 dB hotter; the
    # tail window, not the average, is where it shows.)
    if not fade["tail_dbfs"] < flat["tail_dbfs"] - 12.0:
        fail("the 'fade' automation did not silence the second half: %r" % renders,
             process, log_path)
    print("rendered audio (automation changes it, measured; windows in frames at 44100 Hz):")
    print("  note region = [0,%d), head = [0,%d), tail = [%d,%d)"
          % (window, head, tail_offset, window))
    for name in (flat_name, swell_name, fade_name):
        entry = renders[name]
        print("  %-34s sha256=%s note=%8.3f head=%9.3f tail=%9.3f whole=%8.3f"
              % (name, entry["sha256"][:16], entry["note_dbfs"], entry["head_dbfs"],
                 entry["tail_dbfs"], entry["whole_dbfs"]))
    print("  (dBFS; -inf is silence. The fade's note-region average is close to the flat "
          "render because its shape starts at full gain - the tail window is the proof.)")

    # --- read-back, then invert each mutation with a real control.undo ----
    readback = flow.ok("automation.get_state", {"track": target, "automated_only": True})
    points = automation_points(readback, target, parameter)
    ticks = sorted(int(p["ticks"]) for p in points)
    if ticks != [0, 96, 384]:
        fail("automation.get_state did not read back the points: %r" % readback, process, log_path)
    if abs(float(points[0]["value"]) - high) > 1e-3:
        fail("automation.get_state did not read the point value back: %r" % points,
             process, log_path)
    print("automation.get_state read-back: %d point(s) at %r, values %r"
          % (len(points), ticks, [p["value"] for p in points]))

    added = flow.ok("automation.add_point",
                    {"track": target, "parameter": parameter, "ticks": 48, "value": high})
    if int((added.get("automation") or {}).get("point_count", 0)) != 4:
        fail("add_point did not add a 4th point: %r" % added, process, log_path)
    undone = flow.ok("control.undo")
    after_undo = flow.ok("automation.get_state", {"track": target})
    if not undone.get("undone"):
        fail("control.undo reported nothing undone after automation.add_point", process, log_path)
    if len(automation_points(after_undo, target, parameter)) != 3:
        fail("control.undo did not reverse automation.add_point: %r" % after_undo,
             process, log_path)
    print("control.undo reversed automation.add_point (4 points -> %d)"
          % len(automation_points(after_undo, target, parameter)))

    removed = flow.ok("automation.remove_point",
                      {"track": target, "parameter": parameter, "ticks": 96})
    if int((removed.get("automation") or {}).get("point_count", 0)) != 2:
        fail("remove_point did not remove the node: %r" % removed, process, log_path)
    flow.ok("control.undo")
    after_undo = flow.ok("automation.get_state", {"track": target})
    if len(automation_points(after_undo, target, parameter)) != 3:
        fail("control.undo did not reverse automation.remove_point: %r" % after_undo,
             process, log_path)
    print("control.undo reversed automation.remove_point (2 points -> 3)")

    cleared = flow.ok("automation.clear", {"track": target, "parameter": parameter})
    if int(cleared.get("cleared_points", -1)) != 3:
        fail("automation.clear reported %r" % cleared, process, log_path)
    emptied = flow.ok("automation.get_state", {"track": target})
    if int(emptied.get("automated_parameter_count", -1)) != 0:
        fail("the cleared parameter still reports as automated: %r" % emptied, process, log_path)
    flow.ok("control.undo")
    after_undo = flow.ok("automation.get_state", {"track": target})
    if len(automation_points(after_undo, target, parameter)) != 3:
        fail("control.undo did not reverse automation.clear: %r" % after_undo, process, log_path)
    print("control.undo reversed automation.clear (0 points -> 3)")

    # --- every mutating command left a transaction (SPEC A16) -------------
    transactions = flow.ok("control.transactions").get("transactions", [])
    recorded = {}
    for transaction in transactions:
        recorded.setdefault(transaction.get("command"), []).append(transaction)
    for command in ("automation.add_point", "automation.remove_point", "automation.clear"):
        if command not in recorded:
            fail("no transaction recorded for %s" % command, process, log_path)
    if recorded["automation.add_point"][0].get("reversible") is not True:
        fail("the add_point that created the automation track must record reversible=true "
             "(SPEC A16 #623: the created track is one undoable step): %r"
             % recorded["automation.add_point"][0], process, log_path)
    if "action checkpoint" not in str(recorded["automation.add_point"][0].get("mechanism", "")):
        fail("the created-track transaction does not name the mechanism: %r"
             % recorded["automation.add_point"][0], process, log_path)
    for transaction in recorded["automation.add_point"][1:]:
        if transaction.get("reversible") is not True:
            fail("an add_point on an existing clip must record reversible=true: %r" % transaction,
                 process, log_path)
        if "ProjectJournal" not in str(transaction.get("mechanism", "")):
            fail("a reversible add_point does not name its mechanism: %r" % transaction,
                 process, log_path)
    for command in ("automation.remove_point", "automation.clear"):
        if recorded[command][-1].get("reversible") is not True:
            fail("%s must record reversible=true (proved by control.undo above): %r"
                 % (command, recorded[command][-1]), process, log_path)
    print("transactions (command -> reversible, proved by the undos above):")
    for command in ("automation.add_point", "automation.remove_point", "automation.clear"):
        print("  %-24s first=%r last=%r"
              % (command, recorded[command][0].get("reversible"),
                 recorded[command][-1].get("reversible")))

    return flow.id


def render_to_file(flow, out, process, log_path):
    """Render the session to \a out, retrying once.

    render.render reports a failed child as refused, and on a loaded box this
    build's render child has been measured writing a complete, correct file and
    then aborting at shutdown - LMMS's signal handler does `exit(signum)`, so a
    SIGABRT arrives as exit 6 (the same class as the PdcMixerTest subprocess
    abort AGENT-TOOLING.md records under heavy parallel load). The retry is
    printed, never silent, and a genuine render failure fails both attempts."""
    for attempt in (1, 2):
        request_id, reply = flow.call("render.render", {"out": out, "format": "wav"})
        if reply.get("ok") is True:
            if attempt == 2:
                print("render.render %s succeeded on the retry" % os.path.basename(out))
            return ok_result(reply, request_id)
        if attempt == 1:
            print("render.render %s failed (%r); retrying once"
                  % (os.path.basename(out), (reply.get("error") or {}).get("message")))
            continue
        fail("render.render %s failed twice: %r" % (out, reply), process, log_path)
    return None


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
        if schema.get("count", 0) < 48:
            fail("control.commands_list reports only %s commands" % schema.get("count"), process, log_path)
        described = set()
        # SPEC A13: a command that genuinely needs a display declares it. The
        # whole plugin surface - including loading and driving an LV2 device
        # whose bundle ships a GUI - must declare nothing at all, because no
        # editor is created and no GUI is touched on the control path.
        requires_by_id = {}
        for entry in schema.get("commands", []):
            described.add(entry.get("id"))
            requires_by_id[entry.get("id")] = entry.get("requires")
        for required in ("plugin.list", "plugin.load", "plugin.unload", "plugin.bypass",
                         "plugin.param_get", "plugin.param_set", "plugin.state_save",
                         "plugin.state_load", "plugin.preset_list", "plugin.preset_load",
                         "plugin.preset_save", "dsp.get_state", "settings.get", "settings.set",
                         "audio.device_list", "audio.device_set", "midi.device_list",
                         "app.version", "automation.get_state", "automation.add_point",
                         "automation.remove_point", "automation.clear", "automation.mode_set",
                         "script.run", "script.list"):
            if required not in described:
                fail("control.commands_list has no %s" % required, process, log_path)
        for headless_safe in ("plugin.list", "plugin.load", "plugin.unload", "plugin.bypass",
                              "plugin.param_get", "plugin.param_set", "plugin.state_save",
                              "plugin.state_load"):
            if requires_by_id.get(headless_safe):
                fail("%s declares requires=%r but the LV2/built-in path needs neither a "
                     "display nor a device nor a human"
                     % (headless_safe, requires_by_id.get(headless_safe)), process, log_path)
        print("control.commands_list: headless-safe plugin commands declare requires=[] "
              "(no display is required to load or drive an LV2 device)")
        for entry in schema.get("commands", []):
            if entry.get("group") not in ("automation", "script"):
                continue
            if not entry.get("args_schema") or not entry.get("result_schema"):
                fail("%s declares no schemas: %r" % (entry.get("id"), entry), process, log_path)

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
        if not commands_recorded["mixer.add_channel"].get("reversible"):
            fail("mixer.add_channel must record reversible=true (SPEC A16 #623: the created "
                 "channel is one undoable step, proved by the undo above)", process, log_path)

        # --- the editing flow: notes / clips / tracks (SPEC A16) ----------
        # The fixture ships one pattern track with one clip. Its clip is a
        # PatternClip: a real clip with a real id, but no note list, so the piano
        # roll refuses it by type instead of pretending it is empty.
        arrangement = ok_result(client.call(24, "arrangement.get_state"), 24)
        # The shared fixture has grown since this leg was written (the plugin lane added an
        # instrument track to it), so assert the SHAPE this leg depends on - trk-0 is the
        # pattern track and carries clip-0, the PatternClip - rather than exact totals that
        # belong to no single lane. Repaired by the integrating parent, 2026-09-12.
        if arrangement.get("track_count", 0) < 1 or arrangement.get("clip_count", 0) < 1:
            fail("the fixture's arrangement is %r" % arrangement, process, log_path)
        pattern_tracks = [t for t in arrangement["tracks"] if t.get("type") == "pattern"]
        if not pattern_tracks:
            fail("the fixture has no pattern track: %r" % arrangement, process, log_path)
        fixture_track = pattern_tracks[0]
        fixture_clip = next((c for c in arrangement["clips"]
                             if c.get("track") == fixture_track.get("id")), None)
        if fixture_clip is None:
            fail("the pattern track carries no clip: %r" % arrangement, process, log_path)
        if not str(fixture_track.get("id", "")).startswith("trk-") or fixture_clip.get("id") != "clip-0":
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
        other = ok_result(client.call(266, "track.get_state",
                                     {"track": fixture_track.get("id")}), 266)
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

        # Counts are read RELATIVE to whatever the shared fixture holds: the plugin lane added a
        # track and a clip of its own to this fixture, so an absolute expectation belongs to no
        # single lane. Request ids 66/67 sit in the free band (the legs use 1-65, 261-269, 311+).
        # Repaired by the integrating parent, 2026-09-12.
        before_add = ok_result(client.call(66, "arrangement.get_state"), 66).get("clip_count")
        first_clip = ok_result(client.call(27, "clip.add", {"track": track, "position": 0, "length": 192}), 27)
        clip = first_clip.get("clip")
        if not clip or not clip.startswith("clip-"):
            fail("clip.add returned %r" % first_clip, process, log_path)
        added = ok_result(client.call(67, "arrangement.get_state"), 67).get("clip_count")
        if added != (before_add or 0) + 1:
            fail("clip.add did not add exactly one clip: %r -> %r" % (before_add, added), process, log_path)
        # clip.add is claimed reversible (Track checkpoint): prove it with a real undo.
        if not ok_result(client.call(28, "control.undo"), 28).get("undone"):
            fail("control.undo reported nothing undone after clip.add", process, log_path)
        after_undo = ok_result(client.call(29, "arrangement.get_state"), 29)
        if after_undo.get("clip_count") != before_add:
            fail("undo of clip.add left %r clips, expected the %r it started from"
                 % (after_undo.get("clip_count"), before_add), process, log_path)

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
        before_delete = ok_result(client.call(68, "arrangement.get_state"), 68).get("clip_count")
        ok_result(client.call(43, "clip.delete", {"clip": clip}), 43)
        after_delete = ok_result(client.call(44, "arrangement.get_state"), 44)
        if after_delete.get("clip_count") != (before_delete or 0) - 1:
            fail("clip.delete left %r clips, expected one fewer than %r"
                 % (after_delete.get("clip_count"), before_delete), process, log_path)
        if not ok_result(client.call(45, "control.undo"), 45).get("undone"):
            fail("control.undo reported nothing undone after clip.delete", process, log_path)
        after_restore = ok_result(client.call(46, "arrangement.get_state"), 46)
        if after_restore.get("clip_count") != before_delete:
            fail("undo of clip.delete did not restore the clip: %r" % after_restore, process, log_path)
        # re-find this leg's OWN clip by its track: the shared fixture now carries clips that
        # other lanes added, so "everything except clip-0" is no longer this leg's clip.
        clip = [c.get("id") for c in after_restore.get("clips", []) if c.get("track") == track][0]

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
        # this leg has added a net 3 clips since before_add: add, undo, add, split, duplicate
        if arrangement.get("clip_count") != (before_add or 0) + 3:
            fail("after split+duplicate the arrangement has %r clips, expected %r: %r"
                 % (arrangement.get("clip_count"), (before_add or 0) + 3, arrangement), process, log_path)
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
        for expected in ("track.add", "track.set_solo"):
            entry = by_command.get(expected)
            if entry is None:
                fail("no transaction recorded for %s" % expected, process, log_path)
            if not entry.get("reversible"):
                fail("%s must be recorded as reversible (SPEC A16 #623): %r"
                     % (expected, entry), process, log_path)
        # Selection is VIEW state, not project state. Since SPEC A16 these
        # commands are not mutating at all, so they record NO transaction and
        # cannot shadow or block the undo of a real edit.
        for view_only in ("clip.select", "note.select"):
            if by_command.get(view_only) is not None:
                fail("%s must not record a transaction (view state, not project state)"
                     % view_only, process, log_path)

        # --- track.remove, the one destructive command of the group --------
        # (dry_run previews it; the real call is exercised here and its
        # transaction is checked in a second read of the list.)
        tracks_before_remove = ok_result(client.call(69, "arrangement.get_state"), 69).get("track_count")
        preview = ok_result(client.call(60, "track.remove", {"track": track, "dry_run": True}), 60)
        if not preview.get("dry_run"):
            fail("track.remove did not honour dry_run: %r" % preview, process, log_path)
        still_there = ok_result(client.call(61, "arrangement.get_state"), 61)
        if still_there.get("track_count") != tracks_before_remove:
            fail("a dry_run removed the track anyway: %r" % still_there, process, log_path)

        removed_track = ok_result(client.call(62, "track.remove", {"track": track}), 62)
        if removed_track.get("removed") != track:
            fail("track.remove returned %r" % removed_track, process, log_path)
        final = ok_result(client.call(63, "arrangement.get_state"), 63)
        if final.get("track_count") != (tracks_before_remove or 0) - 1:
            fail("track.remove left %r of %r tracks"
                 % (final.get("track_count"), tracks_before_remove), process, log_path)
        if any(c.get("track") == track for c in final.get("clips", [])):
            fail("track.remove left clips of the removed track behind: %r" % final, process, log_path)
        transactions = ok_result(client.call(64, "control.transactions"), 64).get("transactions", [])
        removals = [t for t in transactions if t.get("command") == "track.remove"]
        if len(removals) != 2:
            fail("expected a dry_run and a real transaction for track.remove, got %r" % removals,
                 process, log_path)
        if not removals[-1].get("reversible"):
            fail("track.remove must record reversible=true (SPEC A16 #623: the track's own "
                 "XML is captured and the undo step recreates it): %r" % removals[-1],
                 process, log_path)
        if "recreate" not in str(removals[-1].get("mechanism", "")):
            fail("track.remove does not name its inverse mechanism: %r" % removals[-1],
                 process, log_path)
        if "nothing was changed" not in str(removals[0].get("mechanism", "")):
            fail("the dry_run preview does not say it changed nothing: %r" % removals[0],
                 process, log_path)


        # --- plugin.* / dsp.* / settings.* / audio.* / midi.* / app.* ------
        # The plugin leg starts above every id the notes leg used above (312).
        last_id = plugin_and_settings_flow(client, process, log_path, tmp, 312)

        # --- script.*: Lua execution in THIS running instance --------------
        last_id = scripting_flow(client, process, log_path, tmp, last_id)

        # --- automation.*: model state, and the rendered audio truth -------
        last_id = automation_flow(client, process, log_path, tmp, project, last_id)

        # --- shutdown unlinks the socket ----------------------------------
        client.call(last_id + 1, "control.quit")
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
        # ZENE_KEEP_TMP=1 keeps the rendered WAVs (the audio-truth evidence) and
        # the app log in <tmp> instead of deleting them for inspection.
        if os.environ.get("ZENE_KEEP_TMP") == "1":
            print("kept the run directory: %s" % tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)

    print("PASS: control socket integration (offscreen, external client)")
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
