#!/usr/bin/env python3
"""ControlMidiReconnect - the registered proof for MIDI controller
auto-reconnection (0.3.0 feature-list row 18, OWNER-31 item 7).

The claim this file exists to check is the one the feature was built for: a
controller that was BOUND and then went away is BOUND AGAIN, without the user
doing anything, when the same device comes back at a new sequencer address.

Nothing here can be proved inside the engine, so every reading is outside it:

  * a REAL external ALSA-sequencer client (tests/midi_reconnect_probe.py, a
    separate process, exactly like a keyboard) is created, and the engine's
    binding to it is read back from the KERNEL's own subscription table
    (`aconnect -l`) as well as from the engine's report;
  * the client is KILLED BY PID - the device being unplugged - and the engine's
    own numbers have to show the loss: the assignment stays, marked not live,
    with the loss counted ONCE;
  * the client is created again under the SAME name at a NEW address (asserted
    different), and the engine has to re-attach by itself: the binding's name
    moves, the kernel's table shows the new subscription, and the project's own
    serialized <midiport inports> carries the new name after a save;
  * MIDI is then PLAYED BY THE RECONNECTED CLIENT and has to arrive: the probe
    sends to SUBSCRIBERS (no destination address), so the kernel delivers it
    ONLY IF the engine's subscription is live again, and the retro-capture
    window counts what arrives;
  * a NEGATIVE CONTROL runs against that: a second client nothing is bound to
    sends the same burst and the engine must receive NOTHING - so "the events
    arrived" cannot be true merely because a client exists;
  * the mode switch is measured in BOTH directions: disarmed, the same
    kill-then-return leaves the binding dead; armed again, it comes back;
  * and the A16 inverse: midi.reconnect_set binds a track's MIDI port, and ONE
    control.undo takes the binding back off.

It reports ctest *Skipped* (exit 77) - never *Passed* - when this host cannot
supply a real MIDI controller (no ALSA-sequencer tooling, or an engine that came
up on the dummy MIDI client), because a test that cannot make its measurement
must not report that it did.

The measurements themselves live in tests/midi_reconnect_flows.py (the Gate 7
split this repository already makes for the socket tests: plumbing here, per-case
evidence there).

Usage: QT_QPA_PLATFORM=offscreen python3 control-midi-reconnect.py <zene-binary>
"""

import os
import subprocess
import sys
import time

from control_socket_harness import (Instance, Problems, Transcript, connect,
                                    finish, wait_ready)

import midi_reconnect_flows as flows

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE = os.path.join(HERE, "midi_reconnect_probe.py")
FIXTURE = os.path.join(HERE, "data", "agent-control-fixture.mmp")

# The engine's name for the ALSA-sequencer client (include/MidiAlsaSeq.h).
SEQUENCER = "ALSA-Sequencer"
CAPTURE_ARM = "midi.retro_capture_arm"


def write_text(path, text):
    with open(path, "w") as handle:
        handle.write(text)


def read_text(path):
    try:
        with open(path) as handle:
            return handle.read().strip()
    except OSError:
        return None


class Controller:
    """One external ALSA-sequencer client, owned by this test."""

    def __init__(self, tmp, name, port_name, tag):
        self.name = name
        self.port_name = port_name
        self.ready_file = os.path.join(tmp, "probe-%s.ready" % tag)
        self.burst_file = os.path.join(tmp, "probe-%s.burst" % tag)
        self.log_path = os.path.join(tmp, "probe-%s.log" % tag)
        self.argv = [sys.executable, PROBE, "--name", name, "--port", port_name,
                     "--ready-file", self.ready_file, "--burst-file", self.burst_file]
        self.process = None
        self.client = None
        self.port = None
        self.address = None
        self.full_name = None

    def start(self):
        self.process = subprocess.Popen(self.argv, stdout=open(self.log_path, "wb"),
                                        stderr=subprocess.STDOUT)
        return self.process

    def wait_ready(self, problems, seconds=20.0):
        """Wait for the probe's own report of the address it was given."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            reported = read_text(self.ready_file)
            if reported:
                self.client, self.port = (int(part) for part in reported.split())
                self.address = "%d:%d" % (self.client, self.port)
                self.full_name = "%s %s:%s" % (self.address, self.name, self.port_name)
                return True
            if self.process.poll() is not None:
                problems.add("the external client exited (%s) before it opened a "
                             "port: %s" % (self.process.returncode,
                                           read_text(self.log_path)))
                return False
            time.sleep(0.05)
        problems.add("the external client never reported a port address")
        return False

    def burst(self, count, problems, seconds=10.0):
        """Ask the client to PLAY: it sends `count` note-ons TO SUBSCRIBERS."""
        write_text(self.burst_file, "go:%d" % count)
        deadline = time.time() + seconds
        while time.time() < deadline:
            if read_text(self.burst_file) == "done:%d" % count:
                return True
            time.sleep(0.05)
        problems.add("the external client never answered a burst of %d" % count)
        return False

    def kill(self):
        """The device being unplugged: SIGKILL, by this object's OWN pid.

        Never a pattern: `pkill -f` has already matched and killed a lane's own
        build on this box, so the kill names the exact process this test
        started, and its exit code is reported - a kill that did not happen
        makes every measurement meaningless.
        """
        self.process.kill()
        return self.process.wait()

    def close(self):
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
            self.process.wait()


class Session:
    """A control client that numbers its own requests and keeps a transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last = 0

    def call(self, cmd, args=None):
        self.last += 1
        return self.client.call(self.last, cmd, args or {}, transcript=self.transcript)

    def ok(self, cmd, args=None):
        from control_socket_harness import ok_result
        return ok_result(self.call(cmd, args), self.last)


def readable_fixture(instance, full_name):
    """The committed fixture, with ONE track's MIDI port bound to `full_name`.

    A project's instrument track only opens an ALSA-sequencer INPUT port when
    its <midiport> is readable (src/core/midi/MidiPort.cpp: loadSettings ->
    updateMidiPortMode -> applyPortMode), and that same element's `inports` is
    what the engine subscribes to - so this is the user's own record of their
    controller, written where the engine reads it.
    """
    with open(FIXTURE) as handle:
        text = handle.read()
    bound = text.replace('readable="0"',
                         'readable="1" inports="%s"' % full_name, 1)
    bound = bound.replace('readable="0"', 'readable="1"')
    path = os.path.join(instance.tmp, "midi-reconnect.mmp")
    write_text(path, bound)
    return path


def host_aconnect():
    """The ALSA-sequencer tool this test reads the kernel's table with, or None."""
    which = subprocess.run(["sh", "-c", "command -v aconnect"],
                           capture_output=True, text=True)
    return which.stdout.strip() or None


def skipped(reason, *instances):
    for instance in instances:
        instance.close()
    print("SKIPPED: %s" % reason)
    sys.exit(77)


def step(name, function, context):
    """Run one measurement, print its problems, and return its result tuple."""
    problems = Problems()
    function(context, problems)
    print("")
    print("%-58s %s"
          % (name, "ok" if not problems else "FAILED (%d)" % len(problems.items)))
    for item in problems.items:
        print("      %s" % item)
    return (name, not problems, problems.items)


def prepare(session, instance, controller):
    """Everything the measurements need, or a 77 exit naming what is missing."""
    running = session.ok(flows.STATUS)
    if not str(running["client"]).startswith(SEQUENCER):
        skipped("the engine is running the %r MIDI client, not the %r one: no "
                "external sequencer client can be bound, and nothing can be noticed "
                "when it goes away" % (running["client"], SEQUENCER), instance)
    path = readable_fixture(instance, controller.full_name)
    opened = session.ok("project.open", {"path": path})
    if opened.get("loaded_with_errors"):
        skipped("the fixture did not load cleanly (%d error(s))"
                % opened.get("error_count", 0), instance)
    session.ok(CAPTURE_ARM)
    return running


def start_controller(instance, controllers, tag):
    """Start one more external client, reusing the controller name."""
    controller = Controller(instance.tmp, flows.CONTROLLER, flows.CONTROLLER_PORT, tag)
    controllers.append(controller)
    controller.start()
    problems = Problems()
    if not controller.wait_ready(problems):
        if controller.process.poll() == 77:
            return None, "this host cannot open an ALSA sequencer at all"
        return None, "the external client did not start: %s" % problems.items
    return controller, None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    aconnect = host_aconnect()
    if aconnect is None:
        skipped("no ALSA-sequencer tooling on this host (aconnect=%r): this test "
                "reads the kernel's own subscription table" % aconnect)

    instance = Instance(sys.argv[1])
    transcript = Transcript()
    controllers = []
    results = []
    instance.spawn()
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript)
        session = Session(client, transcript)

        controller, failure = start_controller(instance, controllers, "first")
        if controller is None:
            skipped(failure, instance)
        bystander = Controller(instance.tmp, flows.BYSTANDER, flows.CONTROLLER_PORT,
                               "bystander")
        controllers.append(bystander)
        bystander.start()
        if not bystander.wait_ready(Problems()):
            skipped("the negative-control client did not start", instance)

        running = prepare(session, instance, controller)
        print("")
        print("engine     : %r" % running["client"])
        print("controller : %s (%s), unbound control: %s (%s)"
              % (controller.full_name, controller.address, bystander.full_name,
                 bystander.address))

        restarts = {"tag": 0}

        def restart():
            """Start one more client under the SAME name (a replug)."""
            restarts["tag"] += 1
            started, why = start_controller(instance, controllers, "again%d"
                                            % restarts["tag"])
            if started is None:
                print("      %s" % why)
            else:
                controller_context["controller"] = started
            return started

        context = {"session": session, "instance": instance, "aconnect": aconnect,
                   "controller": controller, "bystander": bystander,
                   "restart": restart}
        controller_context = context
        results.append(step("the engine attached to the external client",
                            flows.check_attached, context))
        results.append(step("events played through the binding arrive, and only "
                            "through it", flows.check_delivery, context))
        results.append(step("the device is unplugged: the loss is recorded once",
                            flows.check_loss, context))
        results.append(step("the device returns at a NEW address and the engine "
                            "re-attaches", flows.check_reattach, context))
        results.append(step("the restored subscription carries MIDI again",
                            flows.check_delivery_again, context))
        results.append(step("the mode switch, measured in both directions",
                            flows.check_mode_switch, context))
        results.append(step("midi.reconnect_set binds, and ONE control.undo "
                            "removes it", flows.check_inverse, context))
        results.append(step("the instance is still answering afterwards",
                            flows.check_alive, context))
    finally:
        for controller in controllers:
            controller.close()
        instance.close()
    return finish(results)


if __name__ == "__main__":
    sys.exit(main())
