"""One subject's measured steps for tests/plugin_compat_matrix.py: listed, load, editor, state round-trip."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import control_socket_harness as H  # noqa: E402
import plugin_compat_lib as L  # noqa: E402

FORMAT_HOSTS = {"vst3": "vst3", "clap": "clap", "lv2": "lv2"}
STATE_TOLERANCE = 0.01  # of the parameter's range
SETTLE_SECONDS = 1.0   # the Dummy audio device runs continuously; a block or two is enough


def package_dirs(args, subject, subdir):
    """The unpacked packages' folders for one format: every package directory that has the folder."""
    root = pathlib.Path(args.root)
    return [str(d / subdir) for d in sorted(root.iterdir()) if (d / subdir).is_dir()]


def library_path(args, subject):
    """The unpacked run-time libraries the subject needs (Dragonfly: freeverb3) in front of the caller's library path."""
    packages = L.load_json(L.SUBJECTS)["packages"]
    dirs = [str(pathlib.Path(args.root) / packages[n]["dir"] / "usr/lib") for n in (subject or {}).get("needs", [])]
    return os.pathsep.join(dirs + [os.environ.get("LD_LIBRARY_PATH", "")])


def instance_env(args, subject=None):
    lv2 = package_dirs(args, None, "usr/lib/lv2") + ["/usr/lib/lv2"]
    return {"LV2_PATH": os.pathsep.join(lv2), "QT_QPA_PLATFORM": "offscreen", "LD_LIBRARY_PATH": library_path(args, subject)}


class Session:
    """One live instance and a numbered way to call it."""

    def __init__(self, args, subject=None):
        self.instance = H.start_instance(args.zene, extra_env=instance_env(args, subject))
        H.wait_for_socket(self.instance, 120)
        self.client = H.connect(self.instance)
        H.wait_ready(self.instance, self.client, H.Transcript(), 120)
        self.serial = 0

    def call(self, command, arguments=None, timeout=120):
        self.serial += 1
        return self.client.call(self.serial, command, arguments or {}, timeout=timeout)

    def close(self):
        self.instance.close()


def scan(session, args):
    for fmt, subdir in (("vst3", "usr/lib/vst3"), ("clap", "usr/lib/clap")):
        session.call("plugin.paths_set", {"format": fmt, "dirs": package_dirs(args, None, subdir)})
    session.call("plugin.rescan", {}, 300)


def find_device(session, subject):
    listed = session.call("plugin.list", {"format": subject["format"]})
    for device in listed.get("result", {}).get("devices", []):
        if device.get("name") == subject["name"]:
            return device
    return None


def scan_evidence(session, subject):
    """What the scan cache says about the subject's file: why a plug-in the scan saw is not in the list."""
    stem = pathlib.Path(subject["file"]).stem
    listed = session.call("plugin.scan_cache_list", {}).get("result", {})
    for record in listed.get("files", listed.get("entries", [])):
        if stem in str(record.get("path", "")):
            keep = {k: record[k] for k in ("status", "error", "plugin_count", "reason") if k in record}
            return "scan cache: %s" % json.dumps(keep, sort_keys=True)
    return "the scan cache holds no record of %s" % stem


def error_text(reply):
    error = reply.get("error", {})
    return "%s: %s" % (error.get("kind", "error"), error.get("message", ""))


CHECKED_PARAMETERS = 4


GENERIC_EFFECT_PARAMETERS = 5  # an effect's first parameters are Zene's own: enabled, wet/dry, auto-quit decay, numerator, denominator


def ranged_parameters(session, track, plugin):
    """Up to CHECKED_PARAMETERS of the plug-in's own parameters with a real range, spread over its list, as
    plugin.param_get reports them (an effect's first few are the host's generic controls and are left out).
    A parameter the plug-in owns - reported read_only (VST3 kIsReadOnly, CLAP_PARAM_IS_READONLY: a meter, an
    output, DPF's Buffer Size) - is not one a state round-trip can move: plugin.param_set refuses it (stress-4)."""
    every = []
    for index in range(GENERIC_EFFECT_PARAMETERS if plugin.startswith("fx-") else 0, 64):
        reply = session.call("plugin.param_get", {"target": track, "plugin": plugin, "index": index})
        if not reply.get("ok"):
            break
        parameter = reply["result"]["parameter"]
        if parameter["max"] > parameter["min"] and not parameter.get("read_only"):
            every.append(parameter)
    if len(every) <= CHECKED_PARAMETERS:
        return every
    spread = sorted({round(i * (len(every) - 1) / (CHECKED_PARAMETERS - 1)) for i in range(CHECKED_PARAMETERS)})
    return [every[i] for i in spread]


def set_parameter(session, track, plugin, index, value):
    session.call("plugin.param_set", {"target": track, "plugin": plugin, "index": index, "value": value})
    reply = session.call("plugin.param_get", {"target": track, "plugin": plugin, "index": index})
    return reply["result"]["parameter"]["value"] if reply.get("ok") else None


def far_end(parameter):
    low, high = parameter["min"], parameter["max"]
    return high if abs(parameter["value"] - high) > abs(parameter["value"] - low) else low


def move_all(session, track, plugin, parameters, ends):
    """Set every checked parameter to its end and return the names of the ones the plug-in did not take."""
    refused = []
    for parameter in parameters:
        target = ends[parameter["index"]]
        got = set_parameter(session, track, plugin, parameter["index"], target)
        if got is None or abs(got - target) > STATE_TOLERANCE * (parameter["max"] - parameter["min"]):
            refused.append(parameter["name"])
    return refused


def state_roundtrip(session, track, plugin, folder):
    parameters = ranged_parameters(session, track, plugin)
    if not parameters:
        return unchanged_state(session, track, plugin, folder)
    ends = {p["index"]: far_end(p) for p in parameters}
    others = {p["index"]: (p["min"] if ends[p["index"]] == p["max"] else p["max"]) for p in parameters}
    refused = move_all(session, track, plugin, parameters, ends)
    if refused:
        return L.cell("not_run", "plugin.param_set did not take the value on %s" % ", ".join(refused))
    time.sleep(SETTLE_SECONDS)  # the plug-in takes the new values on its next audio block, before they are saved
    path = os.path.join(folder, "state.xml")
    saved = session.call("plugin.state_save", {"target": track, "plugin": plugin, "path": path, "overwrite": True})
    if not saved.get("ok"):
        return L.cell("fail", "plugin.state_save: " + error_text(saved))
    move_all(session, track, plugin, parameters, others)
    time.sleep(SETTLE_SECONDS)
    restored = session.call("plugin.state_load", {"target": track, "plugin": plugin, "path": path})
    if not restored.get("ok"):
        return L.cell("fail", "plugin.state_load: " + error_text(restored))
    return compare_restored(session, track, plugin, parameters, ends, saved["result"]["bytes"])


def unchanged_state(session, track, plugin, folder):
    """A plug-in with no parameter to move: its own saved state, loaded back, must save out the same bytes."""
    paths = [os.path.join(folder, "before.xml"), os.path.join(folder, "after.xml")]
    first = session.call("plugin.state_save", {"target": track, "plugin": plugin, "path": paths[0], "overwrite": True})
    if not first.get("ok"):
        return L.cell("fail", "plugin.state_save: " + error_text(first))
    loaded = session.call("plugin.state_load", {"target": track, "plugin": plugin, "path": paths[0]})
    if not loaded.get("ok"):
        return L.cell("fail", "plugin.state_load: " + error_text(loaded))
    second = session.call("plugin.state_save", {"target": track, "plugin": plugin, "path": paths[1], "overwrite": True})
    same = second.get("ok") and second["result"]["sha256"] == first["result"]["sha256"] and first["result"]["bytes"] > 0
    detail = "no parameter with a range to move; the %d-byte state saved, loaded and saved again %s" % (
        first["result"]["bytes"], "identically" if same else "differently")
    return L.cell("pass" if same else "fail", detail)


def compare_restored(session, track, plugin, parameters, ends, size):
    time.sleep(SETTLE_SECONDS)  # a hosted plug-in takes a restored state into its parameters on its next audio block
    kept, lost = [], []
    for parameter in parameters:
        back = session.call("plugin.param_get", {"target": track, "plugin": plugin, "index": parameter["index"]})["result"]["parameter"]["value"]
        within = abs(back - ends[parameter["index"]]) <= STATE_TOLERANCE * (parameter["max"] - parameter["min"])
        (kept if within else lost).append("%s (%s -> %s)" % (parameter["name"], ends[parameter["index"]], back))
    detail = "%d of %d changed parameters came back after save (%d bytes), move, load" % (len(kept), len(parameters), size)
    if lost:
        detail += "; did not: " + ", ".join(lost)
    return L.cell("fail" if lost else "pass", detail)


def socket_steps(args, subject):
    """listed, load and state over the control socket; a host crash ends the steps with the failure it was."""
    steps = {}
    session = Session(args, subject)
    try:
        scan(session, args)
        device = find_device(session, subject)
        if device is None:
            steps["listed"] = L.cell("fail", "plugin.list names no %s plug-in called %r after the scan; %s" % (subject["format"], subject["name"], scan_evidence(session, subject)))
            return steps
        steps["listed"] = L.cell("pass" if device["loadable"] else "fail", "dev id differs per run; file " + device.get("file", device.get("uri", "")))
        track = session.call("track.add", {"type": "instrument"})["result"]["track"]
        loaded = session.call("plugin.load", {"target": track, "device": device["id"]})
        if not loaded.get("ok"):
            steps["load"] = L.cell("fail", "plugin.load: " + error_text(loaded))
            return steps
        steps["load"] = L.cell("pass", "plugin.load put it on a track as %s (%s)" % (loaded["result"]["id"], loaded["result"]["plugin"]))
        with tempfile.TemporaryDirectory(prefix="zene-compat-") as folder:
            steps["state"] = state_roundtrip(session, track, loaded["result"]["id"], folder)
    except (H.Timeout, OSError, KeyError) as exc:
        steps.setdefault("load", L.cell("fail", "the instance stopped answering: %s" % exc))
    finally:
        session.close()
    return steps


def editor_step(args, subject):
    if not args.probe or not args.display:
        return L.cell("not_run", "no display for the editor probe (give --display and --probe)")
    env = dict(os.environ, **instance_env(args, subject))
    env.update({"DISPLAY": args.display, "QT_QPA_PLATFORM": "xcb",
                "ZENE_COMPAT_PROBE": json.dumps(dict(subject, vst3_dirs=package_dirs(args, None, "usr/lib/vst3"),
                                                     clap_dirs=package_dirs(args, None, "usr/lib/clap")))})
    run = subprocess.run([args.probe], env=env, capture_output=True, text=True, timeout=300)
    for line in run.stdout.splitlines():
        if line.startswith("COMPAT_PROBE_RESULT "):
            return json.loads(line.split(" ", 1)[1])["editor"]
    return L.cell("fail", "the probe produced no result (exit %d: %s)" % (run.returncode, (run.stderr or run.stdout)[-160:].strip()))


def measure(args, subject):
    steps = socket_steps(args, subject)
    for step in ("load", "state"):
        steps.setdefault(step, L.cell("not_run", "not reached: the plug-in was not listed" if "listed" in steps and steps["listed"]["outcome"] != "pass" else "not reached"))
    steps["editor"] = editor_step(args, subject)
    return steps


def zene_version(args):
    session = Session(args)
    try:
        return session.call("control.version")["result"]["version"]
    finally:
        session.close()
