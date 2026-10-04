#!/usr/bin/env python3
"""wasm.device_load control-surface transcript (0.5.0, engine audit E22).

Before E22 a `wasm_effect` DEVICE got its module only through its modal file
chooser, so a headless caller could put the device in a chain (plugin.load) and
never give it anything to run; the sandbox's own wasm.load hosts a module that is
in no chain and is never heard. wasm.device_load hands a module to a device in
any chain - a channel's own chain or a parallel chain of its rack - all or
nothing, saved with the project, one undo step.

What this transcript proves, against a live instance:
  * a wasm_effect loaded with plugin.load takes a module through wasm.device_load,
    and the module path is what the device saves with the project;
  * a device on a rack's parallel chain (plugin.load {chain: 1}) takes one too;
  * a module the device cannot host (a .wat text module: the device's worker
    compiles binaries only) is refused, typed, and the device keeps the module it
    had - the project still names it;
  * a device that is not a wasm_effect is refused, typed;
  * one control.undo puts the device's previous module back (none, for a first
    load): the saved project names no module again.

The modules are the demo binaries the build assembles into <build>/wasm-modules
(src/wasm/CMakeLists.txt); the build directory is found from the binary's path.
WASM-ON builds only, like ControlWasmSandbox: the command does not exist without
the wasmtime C API.

Usage: QT_QPA_PLATFORM=offscreen python3 control-wasm-device.py <zene-binary>
Exit code 0 only when every check held.
"""

import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import control_socket_harness as H  # noqa: E402  (path set above)

# The sandbox transcript's Session/Recorder/report helpers, shared rather than copied.
_SPEC = importlib.util.spec_from_file_location("control_wasm_sandbox", os.path.join(HERE, "control-wasm-sandbox.py"))
SANDBOX = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(SANDBOX)

TEXT_MODULE = SANDBOX.EXAMPLE  # softclip.wat: sound, but text - the device's worker takes binaries


def module_dir(binary):
    """<build>/wasm-modules, looked for from the binary's directory up."""
    here = os.path.dirname(os.path.abspath(binary))
    for _ in range(4):
        candidate = os.path.join(here, "wasm-modules")
        if os.path.isdir(candidate):
            return candidate
        here = os.path.dirname(here)
    return None


def wasm_effect_entry(session):
    for entry in session.result("plugin.list").get("devices") or []:
        if entry.get("name") == "wasm_effect" and entry.get("loadable") is True:
            return entry
    return None


def other_effect_entry(session):
    for entry in session.result("plugin.list").get("devices") or []:
        if (entry.get("kind") == "effect" and entry.get("format") == "builtin"
                and entry.get("loadable") is True and entry.get("name") != "wasm_effect"):
            return entry
    return None


def saved_modules(session, path):
    """The module attributes the saved project's wasm_effect devices carry."""
    session.result("project.save", {"path": path})
    with open(path, encoding="utf-8", errors="replace") as handle:
        text = handle.read()
    return re.findall(r'<wasmeffectcontrols[^>]*?module="([^"]*)"', text)


def check_registered(session, recorder):
    ids = {entry.get("id") for entry in session.result("control.commands_list").get("commands") or []}
    recorder.check("wasm.device_load is registered", "wasm.device_load" in ids, "ids=%d" % len(ids))


def check_channel_device(session, recorder, entry, modules, scratch):
    channel = session.result("mixer.add_channel").get("channel")
    device = session.result("plugin.load", {"target": channel, "device": entry.get("id")}).get("id")
    gain = os.path.join(modules, "gain.wasm")
    loaded = session.result("wasm.device_load", {"target": channel, "plugin": device, "path": gain})
    recorder.check("wasm.device_load gives a channel's wasm_effect its module",
                   loaded.get("module") == gain and loaded.get("previous_module") == "", "reply=%r" % loaded)
    saved = saved_modules(session, os.path.join(scratch, "device-loaded.mmp"))
    recorder.check("the device saves the module with the project", gain in saved, "saved=%r" % saved)
    return channel, device, gain


def check_refusals(session, recorder, channel, device, gain, other, scratch):
    kind = session.error_kind("wasm.device_load", {"target": channel, "plugin": device, "path": TEXT_MODULE})
    saved = saved_modules(session, os.path.join(scratch, "device-refused.mmp"))
    recorder.check("a module the device cannot host is refused, typed, the old module kept",
                   kind == "refused" and gain in saved, "kind=%r saved=%r" % (kind, saved))
    missing = session.error_kind("wasm.device_load", {"target": channel, "plugin": device, "path": "/no/such/m.wasm"})
    recorder.check("a module file that is not there: not_found", missing == "not_found", "kind=%r" % missing)
    if other is None:
        recorder.check("a non-wasm device to refuse exists in this build", False, "no other built-in effect")
        return
    plain = session.result("plugin.load", {"target": channel, "device": other.get("id")}).get("id")
    wrong = session.error_kind("wasm.device_load", {"target": channel, "plugin": plain, "path": gain})
    recorder.check("a device that is not a wasm_effect is refused, typed", wrong == "refused", "kind=%r" % wrong)
    session.result("control.undo")  # the plain device's plugin.load


def check_undo(session, recorder, scratch):
    undone = session.call("control.undo")
    saved = saved_modules(session, os.path.join(scratch, "device-undone.mmp"))
    recorder.check("one control.undo takes the first load back: no module is saved",
                   undone.get("ok") is True and saved == [], "undo=%r saved=%r" % (undone.get("ok"), saved))
    redone = session.call("control.redo")
    saved = saved_modules(session, os.path.join(scratch, "device-redone.mmp"))
    recorder.check("control.redo hosts it again", redone.get("ok") is True and len(saved) == 1,
                   "redo=%r saved=%r" % (redone.get("ok"), saved))


def check_rack_chain(session, recorder, entry, modules):
    channel = session.result("mixer.add_channel").get("channel")
    session.result("rack.add_chain", {"channel": channel})
    device = session.result("plugin.load", {"target": channel, "device": entry.get("id"), "chain": 1}).get("id")
    latency = os.path.join(modules, "latency.wasm")
    loaded = session.result("wasm.device_load", {"target": channel, "plugin": device, "path": latency})
    recorder.check("a wasm_effect on a rack's parallel chain takes a module", loaded.get("module") == latency,
                   "reply=%r" % loaded)


def check_transaction(session, recorder):
    records = session.result("control.transactions").get("transactions") or []
    mine = [r for r in records if r.get("command") == "wasm.device_load"]
    recorder.check("wasm.device_load records true_inverse, reversible",
                   mine and all(r.get("class") == "true_inverse" and r.get("reversible") is True for r in mine),
                   "records=%r" % [[r.get("class"), r.get("reversible")] for r in mine])


def run_checks(session, recorder, binary, scratch):
    check_registered(session, recorder)
    modules = module_dir(binary)
    entry = wasm_effect_entry(session)
    if modules is None or entry is None:
        recorder.check("the build has the demo modules and a loadable wasm_effect", False,
                       "modules=%r wasm_effect=%r" % (modules, entry))
        return
    channel, device, gain = check_channel_device(session, recorder, entry, modules, scratch)
    check_refusals(session, recorder, channel, device, gain, other_effect_entry(session), scratch)
    check_undo(session, recorder, scratch)
    check_rack_chain(session, recorder, entry, modules)
    check_transaction(session, recorder)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = SANDBOX.Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = SANDBOX.Session(client, transcript)
        run_checks(session, recorder, argv[1], instance.tmp)
        SANDBOX.check_quit(session, instance, recorder)
    SANDBOX.report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("\nFAIL: wasm.device_load transcript")
        for item in recorder.problems.items:
            print("  - %s" % item)
        return 1
    H.ok("wasm.device_load transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
