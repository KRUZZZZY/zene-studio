#!/usr/bin/env python3
"""control-jack-capture.py - JACK feeds the capture path the modern recorder reads (relief plan R2.3).

Before 2026-09-29 only ALSA published captured audio into AudioInputPath (`publishCaptured`), so on
JACK the record routes saw nothing and `record.input_get_state` said the capture was not open. This
drives the REAL binary on the JACK backend against a private `jackd -d dummy` server (two capture
ports, silent - silence is still frames) and asserts, through the socket:

  1. `record.input_get_state` reports the capture CAPABLE and OPEN, with the channels JACK granted;
  (R6.3) `record.disarm_track` and `record.retro_capture_to_take` succeed here, the one host kind
     whose capture is real - their success replies are held to their schemas nowhere else;
  2. its captured-frame count RISES between two readings (frames are arriving);
  3. a record route armed on input channel 0 has its pushed-frame count RISE (a non-regression
     check only: routes were already fed through the stereo engine input before this change, so
     this one does not discriminate - measured against the pre-fix binary on 2026-09-29).

Measured against the pre-fix binary: checks 1 and 2 FAIL there (capture_capable/capture_open false,
capture_frames 0 -> 0) and pass here (63,232 -> 159,488 frames in two seconds).

Skipped (exit 77, ctest SKIP_RETURN_CODE) when `jackd` is not installed: an unverifiable device
path is reported as skipped, never as a pass.

Usage: QT_QPA_PLATFORM=offscreen python3 control-jack-capture.py <zene-binary>
"""
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import control_socket_harness as H  # noqa: E402

JACK_DEVICE = "JACK (JACK Audio Connection Kit)"
SKIP = 77


def number(state, *keys):
    for key in keys:
        if isinstance(state.get(key), (int, float)):
            return state[key]
    return None


def main():
    if len(sys.argv) < 2:
        print("usage: control-jack-capture.py <zene-binary>"); return 2
    jackd = shutil.which("jackd")
    if jackd is None:
        print("SKIP: jackd is not installed - the JACK capture path cannot be exercised here")
        return SKIP
    server = "zene-capture-%d" % os.getpid()
    env = dict(os.environ, JACK_DEFAULT_SERVER=server, JACK_NO_AUDIO_RESERVATION="1")
    daemon = subprocess.Popen([jackd, "-n", server, "-d", "dummy", "-r", "48000", "-p", "256",
                               "-C", "2", "-P", "2"], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    problems = []
    instance = None
    try:
        time.sleep(1.5)
        if daemon.poll() is not None:
            print("SKIP: jackd -d dummy did not stay up (exit %s)" % daemon.returncode)
            return SKIP
        instance = H.start_instance(os.path.abspath(sys.argv[1]), audiodev=JACK_DEVICE,
                                    extra_env={"JACK_DEFAULT_SERVER": server,
                                               "JACK_NO_AUDIO_RESERVATION": "1"})
        client = H.connect(instance)
        H.wait_ready(instance, client, None)

        def call(request_id, cmd, args=None):
            reply = client.call(request_id, cmd, args)
            if not reply.get("ok"):
                problems.append("%s failed: %r" % (cmd, reply.get("error")))
                return {}
            return reply.get("result", {})

        first = call(1, "record.input_get_state")
        print("record.input_get_state:", first)
        if first.get("capture_capable") is not True:
            problems.append("capture_capable is %r on JACK, expected true" % first.get("capture_capable"))
        if first.get("capture_open") is not True:
            problems.append("capture_open is %r on JACK, expected true (reason %r)"
                            % (first.get("capture_open"), first.get("capture_reason")))
        call(2, "record.arm_track", {"route": 0, "input_channel": 0})
        before = call(3, "record.get_state")
        time.sleep(2.0)
        second = call(4, "record.input_get_state")
        after = call(5, "record.get_state")
        a = number(first, "capture_frames")
        b = number(second, "capture_frames")
        print("captured frames: %s -> %s" % (a, b))
        if a is None or b is None or b <= a:
            problems.append("the captured-frame count did not rise on JACK (%r -> %r)" % (a, b))

        def pushed(state):
            routes = state.get("routes") or []
            return routes[0].get("frames_pushed") if routes and isinstance(routes[0], dict) else None
        p0, p1 = pushed(before), pushed(after)
        print("route 0 frames_pushed: %s -> %s" % (p0, p1))
        if p0 is None or p1 is None or p1 <= p0:
            problems.append("an armed route received no frames on JACK (%r -> %r)" % (p0, p1))
        # R6.3: two success paths only a host with a REAL capture reaches (the
        # Dummy device captures nothing, so control-record-inputs.py can only
        # prove their refusals). One route disarmed by name, then the retained
        # retrospective window written to a take.
        disarmed = call(6, "record.disarm_track", {"route": 0})
        if disarmed.get("armed") is not False:
            problems.append("record.disarm_track left route 0 armed: %r" % disarmed)
        call(7, "record.retro_capture_arm", {"armed": True})
        time.sleep(1.5)
        retained = number(call(8, "record.retro_capture_status"), "retained_frames")
        take = os.path.join(instance.tmp, "retro-take.wav")
        written = call(9, "record.retro_capture_to_take", {"file": take})
        print("retro window: %s frames retained, %s written" % (retained, written.get("frames_written")))
        if not retained or not written.get("frames_written") or not os.path.exists(take):
            problems.append("the JACK retro window was not written to a take (%r, %r)" % (retained, written))
        call(10, "record.retro_capture_arm", {"armed": False})
        call(11, "record.disarm_all")
    finally:
        if instance is not None:
            instance.close()
        daemon.terminate()
        try:
            daemon.wait(timeout=10)
        except subprocess.TimeoutExpired:
            daemon.kill()
    for problem in problems:
        print("FAIL:", problem)
    print("RESULT:", "PASS" if not problems else "FAIL (%d)" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
