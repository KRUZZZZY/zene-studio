#!/usr/bin/env python3
# control-success-paths.py - the socket-side success paths R6.3 could not reach in-process
#
# Copyright (c) 2026 Zene Studio contributors
#
# This file is part of LMMS - https://lmms.io
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
"""Success paths that need a RUNNING instance, held to their result schemas.

tests/src/core/ControlSuccessPathsTest.cpp drives most of the commands the suite had only
ever refused (gate 16, tests/checked-coverage-unreached.txt). Two need what an in-process
test does not have:

  1. `session.arrangement_record_land` needs a PERFORMANCE: the audio thread pushes one event
     per launch and per stop while Arrangement Record is armed, so the engine must be running.
     A slot is launched and stopped while armed, then the land pass must succeed and write the
     clip the pair describes;
  2. `project.audible_diff` renders both files through the running binary as child processes,
     and in-process that binary is the test executable. A project compared with ITSELF must
     come back `differ: false` - an answer, not only a success.

The instance is the shared harness's headless one, so every reply is checked against its own
resultSchema (ZENE_CONTROL_CHECK_RESULTS) and a reply that breaks it arrives as a typed error.

Usage: QT_QPA_PLATFORM=offscreen python3 control-success-paths.py <zene>
"""

import os
import sys
import time

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def check_arrangement_record_land(session, recorder):
    session.result("track.add", {"type": "instrument", "name": "Performed"})
    session.result("session.set_grid", {"tracks": 1, "scenes": 1})
    slot = session.result("session.set_slot", {"track": 0, "scene": 0, "type": "midi", "pattern": 1,
                                               "name": "take", "mode": "trigger",
                                               "quantisation": "none"})
    recorder.check("a slot is built to perform", "error" not in slot, "reply=%r" % slot)
    session.result("session.set_quantisation", {"quantisation": "none"})
    session.result("transport.play")
    armed = session.result("session.arrangement_record_arm", {"armed": True})
    recorder.check("Arrangement Record arms", "error" not in armed, "reply=%r" % armed)
    session.result("session.launch_slot", {"track": 0, "scene": 0, "quantisation": "none"})
    time.sleep(1.0)
    session.result("session.stop_all")
    time.sleep(0.5)
    status = session.result("session.arrangement_record_status")
    print("arrangement record status: %r" % status)
    landed = session.result("session.arrangement_record_land")
    print("land: %r" % landed)
    recorder.check("the land pass writes the one clip the launch/stop pair describes",
                   landed.get("clips") == 1 and landed.get("pairs") == 1, "reply=%r" % landed)
    session.result("session.arrangement_record_arm", {"armed": False})
    session.result("transport.stop")


def check_audible_diff(session, recorder, outdir):
    path = os.path.join(outdir, "same.mmp")
    session.result("project.save", {"path": path})
    same = session.result("project.audible_diff", {"a": path, "b": path, "mix_only": True})
    print("audible_diff(a, a): %r" % same)
    recorder.check("project.audible_diff succeeds", "error" not in same, "reply=%r" % same)
    recorder.check("a project compared with itself does not differ", same.get("differ") is False,
                   "differ=%r" % same.get("differ"))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    with H.start_instance(argv[1], workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        check_arrangement_record_land(session, recorder)
        check_audible_diff(session, recorder, instance.tmp)
        session.call("control.quit")
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("socket-side success paths")
        return 1
    H.ok("socket-side success paths (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
