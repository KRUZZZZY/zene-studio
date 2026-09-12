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
        if schema.get("count", 0) < 41:
            fail("control.commands_list reports only %s commands" % schema.get("count"), process, log_path)
        described = set()
        for entry in schema.get("commands", []):
            described.add(entry.get("id"))
        for required in ("plugin.list", "plugin.load", "plugin.unload", "plugin.bypass",
                         "plugin.param_get", "plugin.param_set", "plugin.state_save",
                         "plugin.state_load", "plugin.preset_list", "plugin.preset_load",
                         "plugin.preset_save", "dsp.get_state", "settings.get", "settings.set",
                         "audio.device_list", "audio.device_set", "midi.device_list",
                         "app.version"):
            if required not in described:
                fail("control.commands_list has no %s" % required, process, log_path)

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


        # --- plugin.* / dsp.* / settings.* / audio.* / midi.* / app.* ------
        # The plugin leg starts above every id the notes leg used above (312).
        last_id = plugin_and_settings_flow(client, process, log_path, tmp, 312)

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
        shutil.rmtree(tmp, ignore_errors=True)

    print("PASS: control socket integration (offscreen, external client)")
    print("\n---- request/response transcript ----")
    for line in TRANSCRIPT:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
