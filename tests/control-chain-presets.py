#!/usr/bin/env python3
"""END-TO-END proof that plugin chains as reusable presets are drivable by an agent.

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its control
socket can CAPTURE a track's effect chain as a named preset (the ordered device list
plus each device's own state), apply that preset to another track so the second track
ends up with the same ordered devices and the same parameter values, rename it, remove
it, and reverse every one of those - and the effect is real and measurable, not merely
a command that answered ok.

WHY IT IS A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/
ControlChainPresetTest.cpp already holds the surface, the contract rows, the document
and the apply to account IN PROCESS. What it cannot prove is the release contract's
section 3.1: that the feature is reachable THROUGH THE SOCKET by an agent, with the
schema validation, the wire numbers and the undo behaviour an external client actually
gets. So this test starts the REAL binary (the `ControlSocketIntegration` mould:
`$<TARGET_FILE:zene>`, `QT_QPA_PLATFORM=offscreen`, the shared `control_socket_harness`)
with its own HOME/XDG world - which is also the reason the preset store it writes is a
temporary one and not the developer's.

WHAT IT ASSERTS, in numbers read off the wire:

  * capture: a chain of two effects, one of them with a parameter moved away from its
    default, must read back through `chain.get_state` as exactly those two devices in
    that order, with the state document of each one;
  * apply: the preset applied to a SECOND track must give that track the same ordered
    device list AND the same parameter values read back through `dsp.get_state` - the
    comparison is against the source track's own report, never against the command's
    own answer - and `control.undo` must take it back off;
  * persistence: the applied chain survives a real `project.save` -> `project.open`
    round trip (it is project state on the target track), AND the preset itself is
    still in the store afterwards (it is NOT project state - it is per-user, which is
    what makes it usable in another project) and applies to a third track;
  * the store: `chain.rename` renames in place and `chain.remove` deletes, both
    reversible through `control.undo`; a rename onto an occupied name is refused and
    changes nothing;
  * refusals: every unknown name, bad target and empty name is a typed error that
    changes nothing.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-chain-presets.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The preset the whole proof uses, and the name a rename takes it to.
PRESET = "socket chain"
RENAMED = "socket chain renamed"

#: How many effects the captured chain carries.
DEVICES = 2


class Session:
    """The socket client, a running request id and the raw transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply, or {'error': ...} so a failed call is visible in a check."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}

    def typed_error(self, command, args=None):
        reply = self.call(command, args)
        if reply.get("ok") is False:
            return reply.get("error") or {}
        return {}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


# ---------------------------------------------------------------------------
# reading the wire
# ---------------------------------------------------------------------------


def channel_of(session, target):
    """dsp.get_state's entry for one target: its devices, in the chain's order."""
    for chain in session.result("dsp.get_state", {"target": target}).get("chains") or []:
        if chain.get("id") == target:
            return chain
    return {}


def signature(session, target):
    """(plugin, {parameter name: value}) of every device of a live target, in order.

    A tuple of tuples rather than a dict: the ORDER is half of what is under test.
    Values are rounded to six decimals, which is past float32's precision, so the
    comparison is exact for values that really round-tripped.
    """
    devices = []
    for device in channel_of(session, target).get("devices") or []:
        parameters = tuple(sorted(
            (parameter.get("name"), round(float(parameter.get("value", 0.0)), 6))
            for parameter in device.get("parameters") or []))
        devices.append((device.get("plugin"), parameters))
    return tuple(devices)


def store_names(session):
    return tuple(entry.get("name") for entry in
                 session.result("chain.list").get("presets") or [])


def preset_devices(session, name):
    return tuple((entry.get("index"), entry.get("plugin"), entry.get("format"))
                 for entry in
                 (session.result("chain.get_state", {"name": name}).get("devices") or []))


def make_track(session, instance, transcript, name):
    """A fresh instrument track, through the commands an agent has."""
    track = session.result("track.add", {"type": "instrument", "name": name})
    if not track.get("track"):
        H.fail("track.add returned no track id (%r)" % track, instance, transcript)
    return track.get("track")


def load_effects(session, target, wanted):
    """Loads \a wanted loadable effects onto \a target; returns their fx-<n> ids."""
    listed = session.result("plugin.list", {"kind": "effect", "loadable_only": True})
    ids = []
    for device in listed.get("devices") or []:
        if len(ids) >= wanted:
            break
        loaded = session.result("plugin.load", {"target": target, "device": device.get("id")})
        if loaded.get("id"):
            ids.append(loaded.get("id"))
    return ids


def find_track(session, name):
    """The trk-<n> id of the track called \a name, after a project has been opened."""
    for track in session.result("track.list").get("tracks") or []:
        if track.get("name") == name:
            return track.get("id")
    return None


def build_chain(session, instance, transcript, track):
    """\a track with DEVICES effects on it, one of them moved off its default.

    The device whose parameter is moved is the first one that EXPOSES one: a device
    with no parameter cannot show that a value round-tripped.
    """
    ids = load_effects(session, track, DEVICES)
    if len(ids) != DEVICES:
        H.fail("this build loaded only %d effect(s), the proof needs %d"
               % (len(ids), DEVICES), instance, transcript)
    devices = channel_of(session, track).get("devices") or []
    for index, fx in enumerate(ids):
        parameters = (devices[index] if index < len(devices) else {}).get("parameters") or []
        if not parameters:
            continue
        first = parameters[0]
        wanted = (float(first.get("min", 0.0)) + float(first.get("max", 1.0))) / 3.0
        session.result("plugin.param_set", {"target": track, "plugin": fx, "index": 0,
                                            "value": wanted})
        return ids, wanted
    H.fail("no loaded effect exposes a parameter, so 'the same values' cannot be tested",
           instance, transcript)


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_capture(session, source, recorder):
    """The chain is captured with its order and each device's own state."""
    saved = session.result("chain.save", {"target": source, "name": PRESET})
    reported = (saved.get("device_count") == DEVICES and saved.get("bytes", 0) > 0
                and saved.get("replaced") is False and bool(saved.get("sha256")))
    path = str(saved.get("path", ""))
    outside = "chainpresets" in path and "/projects/" not in path
    recorder.check("chain.save captures the chain and reports what it stored",
                   reported and path.endswith(".zcp"), "saved=%r" % (saved,))
    recorder.check("the capture is written in the preset store, not in the project",
                   outside, "path=%r" % (path,))

    kept = session.result("chain.get_state", {"name": PRESET})
    stored = preset_devices(session, PRESET)
    live = tuple(device.get("plugin") for device in
                 channel_of(session, source).get("devices") or [])
    recorder.check("chain.get_state keeps every device in the chain's own order",
                   tuple(entry[1] for entry in stored) == live,
                   "preset=%s live=%s" % (stored, live))
    recorder.check("every stored device names its own format",
                   all(entry[2] in ("builtin", "ladspa", "lv2") for entry in stored),
                   "devices=%s" % (stored,))
    recorder.check("chain.get_state reports the preset's device count",
                   int(kept.get("device_count", 0)) == len(live) == DEVICES,
                   "device_count=%r live=%s" % (kept.get("device_count"), live))
    recorder.check("chain.list reports the preset it just stored",
                   PRESET in store_names(session), "store=%s" % (store_names(session),))


def check_apply(session, source, target, recorder):
    """THE REAL EFFECT: the second track ends up with the same chain, measured."""
    before = signature(session, source)
    recorder.check("the source chain is the fixture the proof expects",
                   len(before) == DEVICES and bool(before[0][1]),
                   "source=%s" % (before,))
    recorder.check("the target starts empty", signature(session, target) == (),
                   "target=%s" % (signature(session, target),))

    applied = session.result("chain.apply", {"name": PRESET, "target": target})
    after = signature(session, target)
    recorder.check("chain.apply gives the target the SAME devices in the SAME order",
                   tuple(entry[0] for entry in after) == tuple(entry[0] for entry in before)
                   and len(after) == DEVICES,
                   "source=%s target=%s" % (before, after))
    recorder.check("chain.apply gives the target the SAME parameter values",
                   after == before, "source=%s target=%s" % (before, after))
    recorder.check("chain.apply reports the device list it built",
                   applied.get("device_count") == DEVICES and applied.get("replaced") == 0
                   and len(applied.get("devices") or []) == DEVICES,
                   "applied=%r" % (applied,))

    records = [record for record in
               session.result("control.transactions").get("transactions") or []
               if record.get("command") == "chain.apply"]
    recorder.check("the A16 record classes chain.apply true_inverse on the chain's own XML",
                   bool(records) and records[-1].get("class") == "true_inverse"
                   and records[-1].get("reversible") is True
                   and "<fxchain>" in str(records[-1].get("mechanism")),
                   "records=%s" % (records[-1:] or [],))

    session.result("control.undo")
    recorder.check("control.undo takes the applied chain back off the target",
                   signature(session, target) == (), "target=%s" % (signature(session, target),))


def check_cross_project(session, instance, source, target, recorder):
    """The applied chain survives save/open, and the preset is not project state."""
    session.result("chain.apply", {"name": PRESET, "target": target})
    recorder.check("the target carries the preset again before the round trip",
                   signature(session, target) == signature(session, source),
                   "target=%s" % (signature(session, target),))

    project = os.path.join(instance.tmp, "workspace", "chain-preset-proof.mmp")
    saved = session.result("project.save", {"path": project})
    opened = session.result("project.open", {"path": project})
    recorder.check("the project saved and opened cleanly",
                   saved.get("saved") is True and bool(opened.get("file"))
                   and opened.get("error_count") == 0,
                   "saved=%r opened=%r" % (saved.get("saved"), opened.get("file")))

    reopened = find_track(session, "Chain Preset Target")
    recorder.check("the applied chain survives a real save/open round trip",
                   reopened is not None and signature(session, reopened) == signature(
                       session, find_track(session, "Chain Preset Source")),
                   "target=%r other=%r" % (signature(session, reopened or ""), ()))

    # The preset is NOT project state: the store is still the same store, which is
    # the whole point of a preset (a second project can use it).
    recorder.check("the preset store survives the round trip (it is not project state)",
                   PRESET in store_names(session), "store=%s" % (store_names(session),))
    third = make_track(session, instance, None, "Chain Preset Third")
    applied = session.result("chain.apply", {"name": PRESET, "target": third})
    same = tuple(entry[0] for entry in signature(session, third))
    source_names = tuple(entry[0] for entry in signature(session, source))
    recorder.check("... and applies to a THIRD track after the project changed",
                   same == source_names and applied.get("device_count") == DEVICES,
                   "third=%s applied=%r" % (signature(session, third), applied))
    return third


def check_store_edits(session, donor, recorder):
    """Rename and remove, both reversible; an occupied name is refused."""
    second = "socket chain other"
    session.result("chain.save", {"target": donor, "name": second})
    recorder.check("a second preset stores, so an occupied rename can be tested",
                   second in store_names(session), "store=%s" % (store_names(session),))

    before = store_names(session)
    refused = session.typed_error("chain.rename", {"name": PRESET, "to": second})
    unchanged = bool(refused.get("kind")) and store_names(session) == before
    recorder.check("a rename onto an occupied name is refused, typed, and changes nothing",
                   unchanged, "error=%r store=%s" % (refused, store_names(session)))

    renamed = session.result("chain.rename", {"name": PRESET, "to": RENAMED})
    kept = session.result("chain.get_state", {"name": RENAMED}).get("devices") or []
    in_place = (renamed.get("renamed_to") == RENAMED and RENAMED in store_names(session)
                and PRESET not in store_names(session) and len(kept) == DEVICES)
    recorder.check("chain.rename renames in place, keeping the devices",
                   in_place, "renamed=%r store=%s devices=%s"
                   % (renamed, store_names(session), kept))
    session.result("control.undo")
    back = PRESET in store_names(session) and RENAMED not in store_names(session)
    recorder.check("control.undo takes the rename back", back,
                   "store=%s" % (store_names(session),))

    removed = session.result("chain.remove", {"name": PRESET})
    gone = PRESET not in store_names(session) and removed.get("removed") == PRESET
    recorder.check("chain.remove deletes the preset", gone,
                   "removed=%r store=%s" % (removed, store_names(session)))
    session.result("control.undo")
    devices = session.result("chain.get_state", {"name": PRESET}).get("devices") or []
    recorder.check("control.undo brings the removed preset back with its devices",
                   PRESET in store_names(session) and len(devices) == DEVICES,
                   "store=%s devices=%s" % (store_names(session), devices))
    session.result("chain.remove", {"name": second})


def check_refusals(session, source, instance, transcript, recorder):
    """Every refusal is typed, and changes nothing."""
    before = store_names(session)
    empty = make_track(session, instance, transcript, "Chain Preset Empty")
    unknowns = [
        ("chain.get_state", {"name": "no such preset"}),
        ("chain.apply", {"name": "no such preset", "target": empty}),
        ("chain.rename", {"name": "no such preset", "to": "other"}),
        ("chain.remove", {"name": "no such preset"}),
        ("chain.save", {"target": empty, "name": PRESET, "overwrite": True}),
        ("chain.save", {"target": "trk-9999", "name": "nowhere"}),
        ("chain.save", {"target": source, "name": "../escape", "overwrite": True}),
        ("chain.apply", {"name": PRESET, "target": "not-a-target"}),
        ("chain.rename", {"name": PRESET, "to": "../escape"}),
        ("chain.list", {"name": "no such preset"}),
    ]
    failures = []
    for command, args in unknowns:
        error = session.typed_error(command, args)
        if not error.get("kind") or not error.get("message"):
            failures.append("%s -> %r" % (command, error))
    recorder.check("every refusal is typed and carries a message",
                   not failures, "; ".join(failures))
    recorder.check("the preset store is unchanged by every refusal",
                   store_names(session) == before,
                   "store=%s before=%s" % (store_names(session), before))


def check_quit(session, instance, recorder):
    reply = session.call("control.quit")
    if reply.get("ok") is not True:
        recorder.check("control.quit answered", False, "%r" % reply)
        return
    session.client.close()
    exited, code, waited = instance.wait_for_exit(H.QUIT_TIMEOUT)
    recorder.check("control.quit stops the instance", exited and code == 0,
                   "exited=%r code=%r after %.1fs" % (exited, code, waited))


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-58s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the chain.* socket proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one a measured number" % len(recorder.results))
    return 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        print("instance: %s" % argv[1])
        print("socket:   %s" % instance.socket_path)

        session.result("control.version")
        recorder.check("the store starts empty", store_names(session) == (),
                       "store=%s" % (store_names(session),))
        source = make_track(session, instance, transcript, "Chain Preset Source")
        target = make_track(session, instance, transcript, "Chain Preset Target")
        ids, wanted = build_chain(session, instance, transcript, source)
        print("source:   %s with %s" % (source, ids))
        check_capture(session, source, recorder)
        check_apply(session, source, target, recorder)
        third = check_cross_project(session, instance, source, target, recorder)
        check_store_edits(session, third, recorder)
        check_refusals(session, source, instance, transcript, recorder)
        check_quit(session, instance, recorder)

    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("chain.* socket proof (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
