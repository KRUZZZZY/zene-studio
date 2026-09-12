#!/usr/bin/env python3
"""Headless integration test for Zene Studio's agent control socket (SPEC A12/A16).

Starts the real `lmms` binary with `--control-socket <path>` under
QT_QPA_PLATFORM=offscreen, then drives it from an EXTERNAL client over the
AF_UNIX socket with line-delimited JSON-RPC. It asserts:

  * the socket file is created with mode 0600 and is unlinked on exit;
  * control.ping / control.version / control.commands_list answer;
  * the full flow open -> read mixer -> set a channel volume -> render -> save;
  * typed error paths (not_found, invalid_args);
  * control.undo / control.redo reverse a recorded mutating command.

Usage: QT_QPA_PLATFORM=offscreen python3 control-socket-integration.py <lmms> <project.mmp>
Exit code 0 only when every assertion passed.
"""

import array
import json
import math
import os
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time
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


def plugin_and_settings_flow(client, process, log_path, tmp, last_id):
    """The plugin.* / dsp.* / settings.* leg: catalogue, load, parameters,
    state, presets, unload and the settings/device read-back (SPEC A11-A16).

    Every `is None` guard below calls fail(), which prints the transcript and
    exits the process, so the subscripts that follow a guard are safe."""
    flow = Flow(client, last_id)

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
    flow.err("plugin.load", "not_found", {"target": "trk-1", "device": "dev-999999"})
    flow.err("plugin.load", "invalid_args", {"target": "trk-1", "device": "amplifier"})
    # trk-0 is the fixture's Beat/Bassline track: it has no device chain at all.
    flow.err("plugin.load", "refused", {"target": "trk-0", "device": effect_device["id"]})
    # an instrument loads onto a track, never onto a mixer channel.
    flow.err("plugin.load", "refused", {"target": "ch-1", "device": instrument_device["id"]})

    # --- load an effect onto the instrument track ------------------------
    loaded = flow.ok("plugin.load", {"target": "trk-1", "device": effect_device["id"]})
    if loaded.get("kind") != "effect" or not str(loaded.get("id", "")).startswith("fx-"):
        fail("plugin.load returned %r" % loaded, process, log_path)
    fx = loaded["id"]
    print("plugin.load %s -> %s (%s)" % (effect_device["id"], fx, loaded.get("plugin")))

    # --- dsp.get_state: the read-back the ids come from ------------------
    state = flow.ok("dsp.get_state", {"target": "trk-1"})
    chains = state.get("chains", [])
    if not chains or chains[0].get("id") != "trk-1":
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

    got = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": fx, "name": name})
    if got.get("parameter", {}).get("name") != name:
        fail("plugin.param_get returned %r for %r" % (got, name), process, log_path)

    set_reply = flow.ok("plugin.param_set",
                        {"target": "trk-1", "plugin": fx, "name": name, "value": middle})
    if abs(float(set_reply.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_set did not report the new value: %r" % set_reply, process, log_path)
    read_back = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": fx, "name": name})
    if abs(float(read_back.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.param_get did not read back %r" % read_back, process, log_path)
    print("plugin.param_get/param_set %s: %r -> %r (range %r..%r, tolerance %g)"
          % (name, got.get("parameter", {}).get("value"),
             read_back.get("parameter", {}).get("value"), low, high, tol))

    # --- typed errors: bad parameter name, bad instance id, out-of-range --
    flow.err("plugin.param_get", "not_found",
             {"target": "trk-1", "plugin": fx, "name": "No Such Parameter"})
    flow.err("plugin.param_get", "not_found",
             {"target": "trk-1", "plugin": "fx-99", "name": name})
    flow.err("plugin.param_set", "invalid_args",
             {"target": "trk-1", "plugin": fx, "name": name, "value": high + 1000.0})
    flow.err("plugin.param_set", "invalid_args",
             {"target": "trk-1", "plugin": fx, "name": name, "value": low - 1000.0})

    # --- SPEC A16: a parameter change is a journal checkpoint ------------
    away = low + (high - low) * 0.25
    flow.ok("plugin.param_set", {"target": "trk-1", "plugin": fx, "name": name, "value": away})
    undone = flow.ok("control.undo")
    after_undo = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": fx, "name": name})
    if not undone.get("undone"):
        fail("control.undo reported nothing undone after plugin.param_set", process, log_path)
    if abs(float(after_undo.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("control.undo did not restore the parameter: %r" % after_undo, process, log_path)
    print("control.undo reversed plugin.param_set: %s back to %r"
          % (name, after_undo.get("parameter", {}).get("value")))

    # --- plugin.state_save / plugin.state_load ---------------------------
    state_path = os.path.join(tmp, "plugin-state.xml")
    saved = flow.ok("plugin.state_save", {"target": "trk-1", "plugin": fx, "path": state_path})
    if not saved.get("sha256") or int(saved.get("bytes", 0)) <= 0:
        fail("plugin.state_save reported %r" % saved, process, log_path)
    if not os.path.exists(state_path) or os.path.getsize(state_path) == 0:
        fail("plugin.state_save left no state at %s" % state_path, process, log_path)
    print("plugin.state_save: %s (%d bytes, sha256 %s)"
          % (saved.get("path"), int(saved.get("bytes", 0)), str(saved.get("sha256"))[:16]))
    # a second save to the same path must refuse rather than silently clobber.
    flow.err("plugin.state_save", "refused",
             {"target": "trk-1", "plugin": fx, "path": state_path})

    flow.ok("plugin.param_set", {"target": "trk-1", "plugin": fx, "name": name, "value": away})
    flow.ok("plugin.state_load", {"target": "trk-1", "plugin": fx, "path": state_path})
    restored = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": fx, "name": name})
    if abs(float(restored.get("parameter", {}).get("value", -1e30)) - middle) > tol:
        fail("plugin.state_load did not restore the saved parameter: %r" % restored, process,
             log_path)
    print("plugin.state_load round trip: %s restored to %r"
          % (name, restored.get("parameter", {}).get("value")))

    # --- plugin.preset_list / preset_save / preset_load ------------------
    preset = flow.ok("plugin.preset_save",
                     {"target": "trk-1", "plugin": fx, "name": "agent-flow"})
    if not str(preset.get("path", "")).endswith(".xpf") or not os.path.exists(preset["path"]):
        fail("plugin.preset_save wrote nothing: %r" % preset, process, log_path)
    listed = flow.ok("plugin.preset_list", {"target": "trk-1", "plugin": fx})
    if not any(p.get("name") == "agent-flow" for p in listed.get("presets", [])):
        fail("plugin.preset_list does not show the saved preset: %r" % listed, process, log_path)
    flow.ok("plugin.preset_load", {"target": "trk-1", "plugin": fx, "name": "agent-flow"})
    print("plugin.preset_save/list/load: %s in %s"
          % (preset.get("name"), listed.get("dir")))
    flow.err("plugin.preset_load", "not_found",
             {"target": "trk-1", "plugin": fx, "name": "no-such-preset"})
    flow.err("plugin.preset_save", "invalid_args",
             {"target": "trk-1", "plugin": fx, "name": "../escape"})

    # --- instrument: load, parameters, state, preset ---------------------
    loaded_inst = flow.ok("plugin.load", {"target": "trk-1", "device": instrument_device["id"]})
    if loaded_inst.get("id") != "inst" or loaded_inst.get("kind") != "instrument":
        fail("plugin.load of an instrument returned %r" % loaded_inst, process, log_path)
    inst_state = flow.ok("dsp.get_state", {"target": "trk-1"})
    inst_entry = (inst_state.get("chains") or [{}])[0].get("instrument") or {}
    inst_param = pick_parameter(inst_entry.get("parameters", []))
    if inst_param is None:
        fail("the instrument exposes no ranged numeric parameter: %r" % inst_entry, process,
             log_path)
    inst_low, inst_high = float(inst_param["min"]), float(inst_param["max"])
    inst_mid = inst_low + (inst_high - inst_low) / 2.0
    inst_away = inst_low + (inst_high - inst_low) * 0.25
    inst_tol = tolerance(inst_param)

    flow.ok("plugin.param_set", {"target": "trk-1", "plugin": "inst",
                                 "index": inst_param["index"], "value": inst_mid})
    inst_read = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": "inst",
                                             "index": inst_param["index"]})
    if abs(float(inst_read.get("parameter", {}).get("value", -1e30)) - inst_mid) > inst_tol:
        fail("plugin.param_get on 'inst' did not read back %r" % inst_read, process, log_path)
    inst_state_path = os.path.join(tmp, "instrument-state.xpf")
    flow.ok("plugin.state_save", {"target": "trk-1", "plugin": "inst",
                                  "path": inst_state_path})
    flow.ok("plugin.param_set", {"target": "trk-1", "plugin": "inst",
                                 "index": inst_param["index"], "value": inst_away})
    flow.ok("plugin.state_load", {"target": "trk-1", "plugin": "inst",
                                  "path": inst_state_path})
    inst_restored = flow.ok("plugin.param_get", {"target": "trk-1", "plugin": "inst",
                                                 "index": inst_param["index"]})
    if abs(float(inst_restored.get("parameter", {}).get("value", -1e30)) - inst_mid) > inst_tol:
        fail("the instrument state round trip did not restore %r" % inst_restored, process,
             log_path)
    flow.ok("plugin.preset_save", {"target": "trk-1", "plugin": "inst", "name": "agent-inst"})
    # an instrument has no unload: the refusal must name the supported surface.
    flow.err("plugin.unload", "invalid_args", {"target": "trk-1", "plugin": "inst"})
    print("instrument %s: %s round-tripped through plugin.state_save/state_load"
          % (instrument_device["name"], inst_param["name"]))

    # --- plugin.bypass ---------------------------------------------------
    bypassed = flow.ok("plugin.bypass", {"target": "trk-1", "plugin": fx, "bypass": True})
    if bypassed.get("enabled") is not False or bypassed.get("processing") is not False:
        fail("plugin.bypass did not switch the device off: %r" % bypassed, process, log_path)
    bypass_state = flow.ok("dsp.get_state", {"target": "trk-1"})
    bypass_entry = None
    for device in (bypass_state.get("chains") or [{}])[0].get("devices", []):
        if device.get("id") == fx:
            bypass_entry = device
    if bypass_entry is None or bypass_entry.get("enabled") is not False:
        fail("dsp.get_state does not show the bypassed device: %r" % bypass_state, process,
             log_path)
    flow.ok("plugin.bypass", {"target": "trk-1", "plugin": fx, "bypass": False})
    print("plugin.bypass: %s enabled=%r processing=%r -> off, read back off, back on"
          % (fx, bypassed.get("enabled"), bypassed.get("processing")))

    # --- plugin.unload ---------------------------------------------------
    unloaded = flow.ok("plugin.unload", {"target": "trk-1", "plugin": fx})
    if unloaded.get("removed") != fx or int(unloaded.get("count", -1)) != 0:
        fail("plugin.unload returned %r" % unloaded, process, log_path)
    flow.err("plugin.unload", "not_found", {"target": "trk-1", "plugin": fx})
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
    for command in ("plugin.param_set", "plugin.bypass"):
        if not recorded[command][-1].get("reversible"):
            fail("%s must record reversible=true" % command, process, log_path)
    for command in ("plugin.load", "plugin.unload", "plugin.state_save", "plugin.state_load",
                    "plugin.preset_save", "plugin.preset_load", "settings.set",
                    "audio.device_set"):
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
    if recorded["automation.add_point"][0].get("reversible") is not False:
        fail("the add_point that created the automation track must record reversible=false: %r"
             % recorded["automation.add_point"][0], process, log_path)
    if "snapshot only" not in str(recorded["automation.add_point"][0].get("mechanism", "")):
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
        for entry in schema.get("commands", []):
            described.add(entry.get("id"))
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
        if commands_recorded["mixer.add_channel"].get("reversible"):
            fail("mixer.add_channel must honestly report itself as not reversible", process, log_path)

        # --- plugin.* / dsp.* / settings.* / audio.* / midi.* / app.* ------
        last_id = plugin_and_settings_flow(client, process, log_path, tmp, 22)

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
