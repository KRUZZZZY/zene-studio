#!/usr/bin/env python3
"""wasm.* control-surface transcript: the WASM DSP sandbox driven END TO END
through --control-socket, and docs/WASM-EFFECT-ABI.md's claims executed rather
than read.

Before the wasm.* group (item #614) the sandbox was reachable from the interface
only through the `wasm_effect` plugin's modal module chooser - a display - so
nothing about it could be seen or driven headless. This is the proof the 0.3.0
scope contract asks for: a real instance, started with --control-socket, asked to
host a real module and report what happened. Each check's name below states the
ABI-doc property it settles, and s12's list of what the document could NOT
determine is settled in part by the four probes under
tests/data/wasm-effect-abi/probes/ (a memory.grow inside process(); a `channels`
global mutated at run time; a FUNCTION named `channels`; the silent clamps).

WHAT IT DOES NOT PROVE: these commands drive the HOST's sandbox. They put no
module into a device chain and render no audio - a module loaded here is not
heard - and driving a live `wasm_effect` device is still done with plugin.* (its
8 parameter models are project state). docs/KNOWN-LIMITATIONS.md and the ABI
doc's s13 say the same.

Registered as the `ControlWasmSandbox` ctest (tests/CMakeLists.txt), WASM-ON
builds only: the commands do not exist without the wasmtime C API, and a test
that could not run must not be registered as one that passed.

Usage: QT_QPA_PLATFORM=offscreen python3 control-wasm-sandbox.py <zene-binary>
Exit code 0 only when every check held.
"""

import itertools
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
#: Fixtures are handed to the instance as ABSOLUTE paths, so nothing here depends
#: on the instance's working directory.
ABI_DIR = os.path.join(REPO, "tests", "data", "wasm-effect-abi")
EXAMPLE = os.path.join(ABI_DIR, "softclip.wat")
PROBES = os.path.join(ABI_DIR, "probes")
PROBE_CHANNELS_FUNCTION = os.path.join(PROBES, "channels-function.wat")
PROBE_CHANNELS_CLAMPED = os.path.join(PROBES, "channels-clamped.wat")
PROBE_GROW = os.path.join(PROBES, "grow.wat")
PROBE_MUTABLE_CHANNELS = os.path.join(PROBES, "mutable-channels.wat")
#: The eight demo modules the build assembles (src/wasm/CMakeLists.txt).
DEMO_DIR = os.path.join(REPO, "modules", "wasm")
DEMO_COUNT = 8

WASM_IDS = ("wasm.get_state", "wasm.list", "wasm.load", "wasm.process",
            "wasm.set_param", "wasm.unload")
MUTATING_IDS = ("wasm.load", "wasm.unload", "wasm.set_param")
READ_IDS = ("wasm.list", "wasm.get_state", "wasm.process")
#: softclip's arithmetic is out[i] = clamp(in[i] * host_get_param(0), -1, 1).
GAIN = 0.5
FRAMES = 8
PROCESS_INPUT = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0]
#: The rate the second process() call uses, to show the argument arrives per call.
SECOND_RATE = 44100.0

REQUEST_IDS = itertools.count(1)


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id = next(REQUEST_IDS)
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        return (reply.get("result") or {}) if reply.get("ok") is True \
            else {"error": reply.get("error") or reply}

    def error_kind(self, command, args=None):
        """The typed error kind, or '' when the call succeeded."""
        reply = self.call(command, args)
        return "" if reply.get("ok") is True else ((reply.get("error") or {}).get("kind") or "")


class Recorder:
    """Collects the named checks and their evidence."""

    def __init__(self):
        self.results = []
        self.problems = H.Problems()

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))
        if not passed:
            self.problems.add("%s (%s)" % (name, evidence))


def clamped(sample, gain):
    """softclip's documented arithmetic: clamp(sample * gain, -1.0, 1.0)."""
    return min(max(sample * gain, -1.0), 1.0)


def close_enough(left, right):
    return abs(float(left) - float(right)) < 1e-6


# --- predicates: one clause each, so no check function carries a whole conjunction


def limits_are_the_abi_ones(limits):
    return (limits.get("channels") == 2 and limits.get("params") == 16
            and limits.get("instances") == 1 and limits.get("memories") == 1)
def exports_are_the_documented_ones(exports):
    named = {entry.get("name"): entry for entry in exports or []}
    required = (named.get("process") or {}, named.get("memory") or {})
    return (required[0].get("type") == "(i32 i32 i32 f32) -> i32"
            and required[0].get("required") is True and required[1].get("required") is True)
def imports_are_the_documented_ones(imports):
    names = {entry.get("name") for entry in imports or []}
    return names == {"env.host_get_param", "env.host_log", "env.host_get_transport_state"}
def demos_are_all_wat(listed):
    modules = listed.get("modules") or []
    return (listed.get("count") == DEMO_COUNT and len(modules) == DEMO_COUNT
            and all(entry.get("format") == "wat" for entry in modules))
def plane_is_the_clipped_ramp(output):
    expected = [round(clamped(sample, GAIN), 6) for sample in PROCESS_INPUT]
    return len(output) == FRAMES and [round(v, 6) for v in output] == expected
def block_ran_on_both_planes(ran):
    return (ran.get("status") == "ok" and ran.get("planes_run") == 2
            and ran.get("channels") == 2)
def call_result_was_recorded(ran):
    return close_enough(ran.get("return_value", -1), 0) and ran.get("trap_code") == -1
def load_reported_the_example(loaded, state):
    return (loaded.get("previous_path") == "" and state.get("loaded") is True
            and state.get("format") == "wat" and loaded.get("bytes", 0) > 0)
def the_example_is_stereo_and_undelayed(state):
    return (state.get("channels") == 2 and state.get("latency") == 0
            and state.get("has_process") is True)
def no_process_is_loadable(state):
    return state.get("loaded") is True and state.get("has_process") is False
def growth_kept_the_later_write(grown):
    output = grown.get("output") or []
    return (len(output) == 2 and close_enough(output[0], 42.0)
            and close_enough(output[1], 7.0))
def a_mutated_channels_global_was_not_reread(mutated):
    output = mutated.get("output") or []
    state = mutated.get("state") or {}
    return (state.get("channels") == 1 and mutated.get("planes_run") == 1
            and len(output) == 1 and close_enough(output[0], 2.0))
def rehost_used_the_recorded_inverse(undone, state):
    return (undone.get("undone") is True and undone.get("restored_by") == "wasm.load"
            and state.get("loaded") is True
            and str(state.get("path", "")).endswith("softclip.wat"))
def slots_were_invalidated_by_the_rehost(state):
    params = state.get("params") or []
    return len(params) == 16 and all(close_enough(value, 0.0) for value in params)
def fuel_and_slots_were_reported(ran):
    params = (ran.get("state") or {}).get("params") or []
    return ran.get("fuel_consumed", 0) > 0 and len(params) == 16
def the_log_arrived(log):
    return isinstance(log, str) and len(log) == 16
def the_last_sample_clipped(output):
    return close_enough(output[-1], 1.0) if output else False


def records_for(records, ids):
    return [entry for entry in records if entry.get("command") in ids]


def inverses_of(records):
    return [(entry.get("inverse") or {}) for entry in records_for(records, MUTATING_IDS)]


def a_replayable_inverse_exists(records):
    return any(inverse.get("applies") == "command"
               and str(inverse.get("op", "")).startswith("wasm.")
               for inverse in inverses_of(records))


def check_registered(session, recorder):
    """The group is in the surface an agent sees, not only in the source."""
    registered = [entry.get("id") for entry in
                  (session.result("control.commands_list").get("commands") or [])]
    missing = [entry for entry in WASM_IDS if entry not in registered]
    recorder.check("every wasm.* id is in control.commands_list", not missing,
                   "missing=%s" % missing)


def check_no_module(session, recorder):
    """Nothing hosted: the state says so and the two commands needing a module
    refuse, typed, instead of answering ok with an empty object."""
    state = session.result("wasm.get_state").get("state") or {}
    recorder.check("wasm.get_state reports nothing hosted",
                   state.get("loaded") is False and state.get("path") == "",
                   "state=%s" % json.dumps(state))
    process_kind = session.error_kind("wasm.process")
    unload_kind = session.error_kind("wasm.unload")
    recorder.check("wasm.process with nothing hosted is not_found",
                   process_kind == "not_found", "kind=%s" % process_kind)
    recorder.check("wasm.unload with nothing hosted is not_found",
                   unload_kind == "not_found", "kind=%s" % unload_kind)


def check_listing(session, recorder):
    """What the sandbox can host: its ABI surface and the committed fixtures."""
    demos = session.result("wasm.list", {"root": DEMO_DIR})
    recorder.check("wasm.list enumerates the eight demo modules",
                   demos_are_all_wat(demos), "count=%r" % demos.get("count"))
    recorder.check("wasm.list reports the ABI surface, not just files",
                   demos.get("import_module") == "env" and demos.get("abi_version") == 1
                   and limits_are_the_abi_ones(demos.get("limits") or {}),
                   "import_module=%r limits=%s" % (demos.get("import_module"),
                                                   json.dumps(demos.get("limits"))))
    recorder.check("the required exports are named with their wasm types",
                   exports_are_the_documented_ones(demos.get("exports")),
                   "exports=%s" % json.dumps(demos.get("exports")))
    recorder.check("the three host imports are named",
                   imports_are_the_documented_ones(demos.get("imports")),
                   "imports=%s" % json.dumps(demos.get("imports")))
    example = session.result("wasm.list", {"root": ABI_DIR})
    names = [entry.get("name") for entry in example.get("modules") or []]
    recorder.check("wasm.list finds the #614 example under tests/data",
                   names == ["softclip.wat"], "names=%s" % names)


def check_list_refusals(session, recorder):
    """A mis-typed path must not read as "this build can host nothing"."""
    bad_root = session.error_kind("wasm.list", {"root": "/no/such/wasm-dir"})
    bad_path = session.error_kind("wasm.load", {"path": "/no/such/module.wat"})
    recorder.check("a bad root and a bad module path are both not_found",
                   bad_root == "not_found" and bad_path == "not_found",
                   "root=%s path=%s" % (bad_root, bad_path))


def check_load_example(session, recorder):
    """The .wat is ASSEMBLED by the runtime and instantiated (ABI doc §1, §4, §5)."""
    loaded = session.result("wasm.load", {"path": EXAMPLE})
    state = loaded.get("state") or {}
    recorder.check("wasm.load assembles the .wat and instantiates it",
                   load_reported_the_example(loaded, state),
                   "bytes=%r format=%r" % (loaded.get("bytes"), state.get("format")))
    recorder.check("the example's own channels/latency globals are read (§4.1, §4.2)",
                   the_example_is_stereo_and_undelayed(state),
                   "channels=%r latency=%r has_process=%r"
                   % (state.get("channels"), state.get("latency"), state.get("has_process")))
    recorder.check("three pages of linear memory are exported (§5)",
                   state.get("memory_bytes", 0) >= 3 * 65536,
                   "memory_bytes=%r" % state.get("memory_bytes"))


def check_param(session, recorder):
    """A parameter slot, its previous value, and the refusal past the range."""
    first = session.result("wasm.set_param", {"index": 0, "value": GAIN})
    recorder.check("wasm.set_param reports the slot's previous value",
                   close_enough(first.get("value", -1), GAIN)
                   and close_enough(first.get("previous", -1), 0.0),
                   "value=%r previous=%r" % (first.get("value"), first.get("previous")))
    second = session.result("wasm.set_param", {"index": 0, "value": 0.25})
    recorder.check("the second write reports the first value as previous",
                   close_enough(second.get("previous", -1), GAIN),
                   "previous=%r" % second.get("previous"))
    out_of_range = session.error_kind("wasm.set_param", {"index": 99, "value": 1.0})
    recorder.check("an index past the 16 slots is invalid_args",
                   out_of_range == "invalid_args", "kind=%s" % out_of_range)
    # Back to GAIN, so the process() checks below read what they expect.
    session.result("wasm.set_param", {"index": 0, "value": GAIN})


def check_process_block(session, recorder):
    """The block: per-plane entry, the arithmetic, fuel, and the log."""
    ran = session.result("wasm.process", {"frames": FRAMES, "input": PROCESS_INPUT})
    output = ran.get("output") or []
    log = ran.get("log")
    recorder.check("a stereo module is entered once per channel plane (§3)",
                   block_ran_on_both_planes(ran),
                   "status=%r planes_run=%r channels=%r"
                   % (ran.get("status"), ran.get("planes_run"), ran.get("channels")))
    recorder.check("the call's own result object is recorded (§8)",
                   call_result_was_recorded(ran),
                   "return_value=%r trap_code=%r" % (ran.get("return_value"),
                                                     ran.get("trap_code")))
    recorder.check("plane 0 reads back clamp(in[i] * host_get_param(0)) (§5, §6)",
                   plane_is_the_clipped_ramp(output), "output=%s" % json.dumps(output))
    recorder.check("clipping actually happened, so the check is not vacuous",
                   the_last_sample_clipped(output), "output=%r" % (output[-1:],))
    recorder.check("the call consumed fuel and reported all 16 slots (§9)",
                   fuel_and_slots_were_reported(ran),
                   "fuel_consumed=%r" % ran.get("fuel_consumed"))
    recorder.check("host_log was wired: the module's 16 bytes arrived (§10)",
                   the_log_arrived(log), "log=%r" % (log,))


def check_process_arguments(session, recorder):
    """frames and sample_rate arrive per call: there is no setter and no global."""
    later = session.result("wasm.process", {"frames": 3, "sample_rate": SECOND_RATE})
    recorder.check("frames and sample_rate arrive per call (§7)",
                   later.get("frames") == 3
                   and close_enough(later.get("sample_rate", 0), SECOND_RATE),
                   "frames=%r sample_rate=%r" % (later.get("frames"), later.get("sample_rate")))


def check_probes(session, recorder):
    """What the document derived from the source, executed."""
    loaded = session.result("wasm.load",
                            {"path": PROBE_CHANNELS_FUNCTION})
    state = loaded.get("state") or {}
    recorder.check("a FUNCTION named channels is ignored, so the default 1 applies (§4.1)",
                   state.get("channels") == 1, "channels=%r" % state.get("channels"))
    recorder.check("a latency global of 64 is read (§4.2)",
                   state.get("latency") == 64, "latency=%r" % state.get("latency"))
    recorder.check("a module with no process() LOADS but is not an effect (§1)",
                   no_process_is_loadable(state),
                   "loaded=%r has_process=%r" % (state.get("loaded"),
                                                 state.get("has_process")))
    probe_kind = session.error_kind("wasm.process")
    recorder.check("wasm.process against that module refuses, typed",
                   probe_kind == "refused", "kind=%s" % probe_kind)

    session.result("wasm.load", {"path": PROBE_CHANNELS_CLAMPED})
    clamped_state = session.result("wasm.get_state").get("state") or {}
    recorder.check("a channels global of 6 is clamped to 2 (§4.1)",
                   clamped_state.get("channels") == 2,
                   "channels=%r" % clamped_state.get("channels"))
    recorder.check("a negative latency is clamped to 0 (§4.2)",
                   clamped_state.get("latency") == 0,
                   "latency=%r" % clamped_state.get("latency"))


def check_growth_and_mutable_globals(session, recorder):
    """Two of the document's UNKNOWNs, made observable instead of argued about.

    Each probe writes its evidence into its OWN output plane, so a check can tell
    "the module did the thing" from "the host ignored it".
    """
    session.result("wasm.load", {"path": PROBE_GROW})
    grown = session.result("wasm.process", {"frames": 2})
    recorder.check("a module may grow its memory inside process() and keep writing (UNKNOWN 2)",
                   growth_kept_the_later_write(grown),
                   "status=%r output=%s" % (grown.get("status"),
                                            json.dumps(grown.get("output"))))

    session.result("wasm.load", {"path": PROBE_MUTABLE_CHANNELS})
    mutated = session.result("wasm.process", {"frames": 1})
    recorder.check("a channels global mutated at run time is NOT re-read (UNKNOWN 4)",
                   a_mutated_channels_global_was_not_reread(mutated),
                   "channels=%r planes_run=%r output=%s"
                   % ((mutated.get("state") or {}).get("channels"), mutated.get("planes_run"),
                      json.dumps(mutated.get("output"))))

    # Put the example back: the checks that follow - the parameter undo and the
    # unload - are about the module this transcript is really about.
    session.result("wasm.load", {"path": EXAMPLE})


def check_undo_of_param(session, recorder):
    """control.undo puts the host's own slot value back."""
    session.result("wasm.set_param", {"index": 3, "value": 0.75})
    undone = session.result("control.undo")
    params = (session.result("wasm.get_state").get("state") or {}).get("params") or []
    restored = len(params) > 3 and close_enough(params[3], 0.0)
    recorder.check("control.undo restores the previous parameter value",
                   undone.get("undone") is True
                   and undone.get("restored_by") == "wasm.set_param" and restored,
                   "undone=%r restored_by=%r params[3]=%r"
                   % (undone.get("undone"), undone.get("restored_by"),
                      params[3] if len(params) > 3 else None))


def check_unload(session, recorder):
    """The dropped module's path is what the recorded inverse will replay."""
    unloaded = session.result("wasm.unload")
    state = unloaded.get("state") or {}
    dropped = str(unloaded.get("path", ""))
    recorder.check("wasm.unload reports what it dropped",
                   unloaded.get("unloaded") is True and state.get("loaded") is False
                   and dropped.endswith("softclip.wat"),
                   "unloaded=%r path=%r" % (unloaded.get("unloaded"), dropped))
    gone_kind = session.error_kind("wasm.process")
    recorder.check("a module that is not hosted cannot be run",
                   gone_kind == "not_found", "kind=%s" % gone_kind)


def check_rehost_undo(session, recorder):
    """control.undo replays the recorded inverse COMMAND, which hosts it again."""
    undone = session.result("control.undo")
    state = session.result("wasm.get_state").get("state") or {}
    recorder.check("control.undo RE-HOSTS the module through the recorded inverse",
                   rehost_used_the_recorded_inverse(undone, state),
                   "undone=%r restored_by=%r path=%r"
                   % (undone.get("undone"), undone.get("restored_by"), state.get("path")))
    # The inverse restores the PATH, not the module's memory: a fresh instantiation
    # has its parameter slots at their defaults again. That is what the A16 row's
    # mechanism string says, so it is checked rather than glossed over.
    recorder.check("the re-host is a COLD instantiation: the slots are back at 0 (§7)",
                   slots_were_invalidated_by_the_rehost(state),
                   "params=%s" % json.dumps(state.get("params")))


def check_transactions(session, recorder):
    """The A16 rows as the registry stamps them: snapshot, reversible."""
    records = session.result("control.transactions").get("transactions") or []
    mutating = records_for(records, MUTATING_IDS)
    print("wasm.* A16 records: %s" % json.dumps(
        [{key: entry.get(key) for key in ("command", "class", "reversible")}
         for entry in mutating], sort_keys=True))
    recorder.check("every mutating wasm.* call left a snapshot record",
                   len(mutating) >= 4 and all(entry.get("class") == "snapshot"
                                              and entry.get("reversible") is True
                                              for entry in mutating),
                   "%d records: %s" % (len(mutating), json.dumps(
                       [[entry.get("command"), entry.get("class"), entry.get("reversible")]
                        for entry in mutating])))
    recorder.check("a wasm record's inverse names a wasm.* command to replay",
                   a_replayable_inverse_exists(records),
                   "inverses=%s" % json.dumps(inverses_of(records)))
    read_only = records_for(records, READ_IDS)
    recorder.check("the read half records no transaction", not read_only,
                   "found=%s" % [entry.get("command") for entry in read_only])


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report_results(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s\nsocket:   %s\nexample:  %s" % (argv[1], instance.socket_path, EXAMPLE))
        for check in (check_registered, check_no_module, check_listing, check_list_refusals,
                      check_load_example, check_param, check_process_block,
                      check_process_arguments, check_probes, check_growth_and_mutable_globals,
                      check_undo_of_param, check_unload, check_rehost_undo, check_transactions):
            check(session, recorder)
        check_quit(session, instance, recorder)

    report_results(recorder)
    transcript.dump()
    if recorder.problems:
        print("")
        print("FAIL: wasm.* control-surface transcript")
        for item in recorder.problems.items:
            print("  - %s" % item)
        return 1
    H.ok("wasm.* control-surface transcript (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
