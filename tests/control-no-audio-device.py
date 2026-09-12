#!/usr/bin/env python3
"""No usable audio device: the product must help, not answer `busy` forever.

Measured before this change (AGENT-TOOLING.md §4): with a configured audio device
that cannot open (SDL -> `Playback open error: Host is down`), the engine never
became ready and EVERY command answered `busy` for at least 92 s with no recovery.
The cause was a modal "Audio device setup failed" QMessageBox (plus the audio tab
of the setup dialog) in `MainWindow::finalize()`, i.e. a question for a human asked
in a run that has none.

The chosen behaviour (task #626, requirement 3): the engine keeps the dummy
fallback the audio layer already selects - rendering, editing and saving still
work, so the instance is genuinely usable - and says so, loudly, in two places:

  * `control.ping` -> `audio: {state: "dummy_fallback", requested: <backend>,
    device: "Dummy (no sound output)", start_failed: true, sound_output: false,
    message: <why>}`;
  * and `transport.play` - the one command that exists to make sound - refuses
    with the typed `requires` error naming the backend.

This test asserts both, plus that the instance is otherwise usable and still shuts
down cleanly. The device is made unopenable deterministically (an SDL audio driver
name that cannot exist), so the test does not depend on the machine having or
lacking a sound card. Bounded everywhere.

Usage: control-no-audio-device.py <lmms-binary>
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_flows import (  # noqa: E402
    check_audio_fallback, check_ping_shape, check_requires_device_refusal,
)
from control_socket_harness import (  # noqa: E402
    BROKEN_DEVICE, BROKEN_DEVICE_ENV, Client, Instance, Problems, Timeout, dump,
    finish,
)

CONNECT_TIMEOUT = 60.0
READY_TIMEOUT = 120.0
STDERR_DUMP_LIMIT = 4000


def wait_for_ready(client, problems):
    """Poll ping until ready. Returns (ready, last_reply, elapsed_s)."""
    started = time.time()
    deadline = started + READY_TIMEOUT
    ping = None
    while time.time() < deadline:
        ping = client.call(1, "control.ping")
        problems.extend(check_ping_shape(ping, 1))
        if (ping.get("result") or {}).get("engine_ready") is True:
            return True, ping, time.time() - started
        time.sleep(0.2)
    return False, ping, time.time() - started


def check_device_dependent_command(client, problems):
    """transport.play refuses, typed, naming the backend that failed."""
    play = client.call(2, "transport.play")
    problems.extend(check_requires_device_refusal(play))
    message = (play.get("error") or {}).get("message") or ""
    if BROKEN_DEVICE not in message:
        problems.add("the refusal does not name the failed backend %r: %r" % (BROKEN_DEVICE, message))
    print("transport.play: %r" % play)


def check_engine_still_usable(client, problems):
    """Everything that does not need to make sound keeps working."""
    for request_id, cmd in ((3, "mixer.get_state"), (4, "transport.get_state")):
        reply = client.call(request_id, cmd)
        if reply.get("ok") is not True:
            problems.add("%s answered %r on a dummy-fallback instance; only audible output "
                         "may refuse" % (cmd, reply))


def drive_no_audio_device(inst, problems):
    """The whole client side. Returns the quit reply, or None when never ready."""
    client = Client(inst.socket_path)
    try:
        ready, ping, elapsed = wait_for_ready(client, problems)
        problems.require(ready,
                         "the engine never became ready with '%s' configured (last ping: %r) - "
                         "this is the 92 s busy-forever defect" % (BROKEN_DEVICE, ping))
        if not ready:
            return None
        print("ready after %.2fs with audiodev=%s" % (elapsed, BROKEN_DEVICE))
        problems.extend(check_audio_fallback(ping or {}, BROKEN_DEVICE))
        print("ping report: %r" % (ping or {}).get("result"))
        check_device_dependent_command(client, problems)
        check_engine_still_usable(client, problems)
        return client.call(9, "control.quit")
    finally:
        client.close()


def check_shutdown(inst, quit_reply, problems):
    """The instance must still shut down cleanly with a broken audio device."""
    if quit_reply is None:
        inst.kill()
        return
    if (quit_reply.get("result") or {}).get("quitting") is not True:
        problems.add("control.quit answered %r" % quit_reply)
    exited, code, _ = inst.wait_for_exit(30.0)
    problems.require(exited, "the instance did not exit after control.quit")
    if exited and code != 0:
        problems.add("the instance exited with %s after control.quit" % code)


def check_stderr(inst, problems):
    """The same sentence must be in the log, not only on the wire."""
    stderr_text = inst.stderr_text()
    if BROKEN_DEVICE not in stderr_text:
        problems.add("stderr never mentions the failed device %r; the headless run must say what "
                     "happened on the log too" % BROKEN_DEVICE)
    if problems.items:
        dump("no-audio-device stderr", stderr_text[-STDERR_DUMP_LIMIT:])


def run_instance(binary, problems):
    """Start one instance with an unopenable device and run the whole check."""
    with Instance(binary, audiodev=BROKEN_DEVICE, extra_env=BROKEN_DEVICE_ENV) as inst:
        try:
            inst.spawn()
            inst.wait_for_socket(CONNECT_TIMEOUT)
            quit_reply = drive_no_audio_device(inst, problems)
            check_shutdown(inst, quit_reply, problems)
        except Timeout as exc:
            problems.add(str(exc))
            inst.kill()
        finally:
            check_stderr(inst, problems)
            inst.close()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print("FAIL: no lmms binary at %s" % binary)
        return 1

    problems = Problems()
    run_instance(binary, problems)
    return finish([("no-audio-device: usable + announced + typed refusal", not problems,
                    problems.items)])


if __name__ == "__main__":
    sys.exit(main())
