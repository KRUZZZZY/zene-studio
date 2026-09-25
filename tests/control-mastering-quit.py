#!/usr/bin/env python3
"""BUG-MASTER-QUIT-CRASH: a `control.quit` while a mastering run is in flight
must reach exit 0. It used to ACK the quit and then die with SIGSEGV in the
run's teardown.

Measured 2026-09-25 (sample.generate -> mastering.run -> poll until
`state == "running"` -> control.quit -> the quit ACKs, then the process dies,
socket reset, -11). The teardown lambda connected to `aboutToQuit` did:

    run->process->kill();
    delete run;                 // ControlCommandsMasteringRun.cpp:306
    setMasteringPendingRun(nullptr);

Deleting the run deletes its QProcess, and deleting a QProcess whose child is
still running kills and REAPS it - which delivers the child's own completion to
completeMasteringRun on the same stack (measured with a breakpoint on
~MasteringPendingRun: HIT 1 from this lambda at :306, HIT 2 from
completeMasteringRun at ControlMasteringSupport.cpp:457 via
QProcess::waitForFinished). That completion's one guard compares the pending-run
slot with the run - still equal, because the slot is cleared only AFTER the
delete - so it passed and deleted the same run a second time. The second
`delete scratch` was the fault: SIGSEGV in ~QTemporaryDir on freed memory.

The fix clears the slot BEFORE the run is taken down, so the nested completion
the QProcess reaping delivers is the no-op the guard was written to be.

Usage: QT_QPA_PLATFORM=offscreen python3 control-mastering-quit.py <zene>
Exit code 0 only when the quit ACKed AND the process reached exit 0. Red before
the fix: exit -11 (or no exit at all).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None, extra_env=plugin_env(argv[1])) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % argv[1])
        H.ok_result(client.call(1, "control.version", transcript=transcript), 1)

        track = H.ok_result(client.call(
            2, "track.add", {"name": "Mastering QA", "type": "sample"}, transcript=transcript),
            2)["track"]
        # Long enough that the render child is still working when the quit
        # arrives: the defect needs a run IN FLIGHT, and the ACK is what says so.
        H.ok_result(client.call(3, "sample.generate", {
            "kind": "tone", "track": track, "position": 0, "length": 3840,
            "frequency": 440.0, "amplitude": 0.2}, transcript=transcript), 3)
        ack = H.ok_result(client.call(
            4, "mastering.run", {"out_dir": os.path.join(instance.tmp, "mastering")},
            transcript=transcript), 4)
        if ack.get("state") != "running":
            problems.add("mastering.run did not acknowledge a run in flight: %r" % ack)
        state = H.ok_result(client.call(5, "mastering.get_state", transcript=transcript), 5)
        if (state.get("state") or "running") != "running":
            print("note: the run finished before the quit (state %r); the teardown still has to "
                  "survive it" % state.get("state"))
        else:
            print("note: the mastering run is still in flight at the quit, as the defect needs")

        quoted = client.call(6, "control.quit", {"save": False}, transcript=transcript)
        if quoted.get("ok") is not True:
            problems.add("control.quit refused while a mastering run was in flight: %r" % quoted)
            transcript.dump()
            return H.finish([("quit during a mastering run", not problems, problems.items)])
        H.ok_result(quoted, 6)

        exited, code, waited = instance.wait_for_exit(90)
        if not exited:
            problems.add("the instance never exited after control.quit (%.1fs)" % waited)
        elif code != 0:
            problems.add("the instance exited with %r after a clean quit while a mastering run "
                         "was in flight (a teardown crash)" % code)
        else:
            print("PASS: the quit ACKed and the process reached exit 0")
    transcript.dump()
    return H.finish([("quit during a mastering run", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
