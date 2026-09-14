#!/usr/bin/env python3
"""midi_reconnect_flows - the measurements of the MIDI controller
re-connection proof (0.3.0 feature-list row 18, OWNER-31 item 7).

The entry point is tests/control-midi-reconnect.py (registered as the ctest
ControlMidiReconnect); this module holds the payload - the eight `check_*`
functions and the small readers they measure with - because the entry point plus
its payload does not fit the 500-line file ratchet, which is not moved for
convenience. The split is the same one `control_socket_flows.py` makes for the
socket tests: plumbing in one place, per-case evidence in another.

Every check takes the shared run context (`session`, `instance`, the `aconnect`
tool, the controller and the negative-control client) and a `Problems` to fill.
Nothing here asserts a success REPORT: each reading is an effect - the engine's
own state, the KERNEL's subscription table, the number of events the engine
RECEIVED, or the project file the engine wrote.
"""

import os
import subprocess
import time

STATUS = "midi.reconnect_status"
CLIENTS = "midi.clients_list"
ARM = "midi.reconnect_arm"
SET = "midi.reconnect_set"
CAPTURE_STATUS = "midi.retro_capture_status"

CONTROLLER = "Zene Reconnect Probe"
CONTROLLER_PORT = "controller"
BYSTANDER = "Zene Unbound Client"
IDENTITY = "%s:%s" % (CONTROLLER, CONTROLLER_PORT)
BYSTANDER_IDENTITY = "%s:%s" % (BYSTANDER, CONTROLLER_PORT)

# A burst is small and counted by DELTA, so nothing else on this box (a sibling
# instance's client, a stray announce event) can make the arithmetic pass.
BURST = 4
# The ALSA-sequencer client re-reads its port list once a second; every wait
# here is a multiple of that and BOUNDED, so a hang is a failure, never a pass.
POLL = 8.0
SHORT = 4.0


# ---------------------------------------------------------------------------
# the readers
# ---------------------------------------------------------------------------


def reading(session, cmd, args=None):
    """One successful command, as its own result object."""
    from control_socket_harness import ok_result
    return ok_result(session.call(cmd, args), session.last)


def binding_of(session, identity):
    """The assignment midi.reconnect_status reports for `identity`, or None."""
    for entry in reading(session, STATUS)["assignments"]:
        if entry["identity"] == identity:
            return entry
    return None


def port_entry(session, name, direction="read"):
    """The live-port entry midi.clients_list reports for `name`, or None."""
    for entry in reading(session, CLIENTS)["ports"]:
        if entry["name"] == name and entry["direction"] == direction:
            return entry
    return None


def captured(session):
    """The retro-capture window's retained-event count."""
    return int(reading(session, CAPTURE_STATUS)["events_buffered"])


def wait_for_port(session, name, present, seconds):
    """Wait (boundedly) for `name` to appear/disappear from the live list."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if (port_entry(session, name) is not None) == present:
            return True
        time.sleep(0.1)
    return False


def wait_for_binding(session, identity, live, seconds):
    """Wait (boundedly) for an assignment to report `live`."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        entry = binding_of(session, identity)
        if entry is not None and entry["live"] is live:
            return entry
        time.sleep(0.1)
    return binding_of(session, identity)


def engine_client_block(aconnect, pid):
    """The `aconnect -l` block of the client owned by `pid`, or None.

    The kernel's own subscription table is the reading that cannot be faked from
    inside the engine: it is what the sequencer will actually deliver through.
    Matching on the PID is what keeps this test away from sibling instances -
    the engine's client NAME is not unique, the pid is.
    """
    listing = subprocess.run([aconnect, "-l"], capture_output=True, text=True)
    if listing.returncode != 0:
        return None
    block = []
    inside = False
    for line in listing.stdout.splitlines():
        if line.startswith("client "):
            inside = ("pid=%d]" % pid) in line
        if inside:
            block.append(line)
    return "\n".join(block)


# ---------------------------------------------------------------------------
# the measurements
# ---------------------------------------------------------------------------


def check_attached(context, problems):
    """The engine attached to the external client, and the kernel agrees."""
    session = context["session"]
    controller = context["controller"]
    entry = wait_for_binding(session, IDENTITY, True, POLL)
    problems.require(entry is not None,
                     "the engine remembers no assignment for %r" % IDENTITY)
    if entry is None:
        return
    problems.require(entry["name"] == controller.full_name,
                     "the assignment holds %r, the external client is %r"
                     % (entry["name"], controller.full_name))
    problems.require(bool(entry["port"]), "the assignment names no engine port")
    problems.require(entry["reconnects"] == 0 and entry["lost"] is False,
                     "a fresh binding already reports reconnects=%r lost=%r"
                     % (entry["reconnects"], entry["lost"]))
    context["track"] = entry["port"]

    listed = port_entry(session, controller.full_name)
    problems.require(listed is not None,
                     "midi.clients_list does not list %r" % controller.full_name)
    if listed is not None:
        problems.require(listed["bound"] is True,
                         "the live port reports bound=%r" % listed["bound"])
        problems.require(listed["engine_port"] == entry["port"],
                         "the live port is bound to %r, the assignment names %r"
                         % (listed["engine_port"], entry["port"]))
        problems.require(listed["identity"] == IDENTITY,
                         "the live port's identity is %r, expected %r"
                         % (listed["identity"], IDENTITY))

    block = engine_client_block(context["aconnect"], context["instance"].process.pid)
    problems.require(block is not None, "aconnect -l did not report the engine's client")
    if block is not None:
        problems.require("Connected From: %s" % controller.address in block,
                         "the kernel's subscription table does not show %s as a "
                         "source for the engine's port:\n%s"
                         % (controller.address, block))
    print("  attached: engine port %s <- %s" % (entry["port"], controller.full_name))


def check_delivery(context, problems):
    """Events played by the attached client arrive, THROUGH the subscription.

    The probe sends to SUBSCRIBERS - no destination address - so the kernel
    delivers only to whatever has subscribed. The negative control is a second
    client nothing is bound to: its identical burst must arrive NOWHERE, which
    is what proves the count is gated on the binding and not on "a client
    exists".
    """
    session = context["session"]
    before = captured(session)
    context["controller"].burst(BURST, problems)
    after = captured(session)
    problems.require(after - before == BURST,
                     "the engine received %d of the %d events the BOUND client "
                     "played through the subscription" % (after - before, BURST))

    bystander = context["bystander"]
    if bystander is not None:
        bystander.burst(BURST, problems)
        control = captured(session)
        problems.require(control - after == 0,
                         "a client NOTHING is bound to delivered %d event(s) to the "
                         "engine: the count above is not gated on the binding"
                         % (control - after))
    print("  +%d event(s) through the binding, 0 from the unbound client"
          % (after - before))


def check_loss(context, problems):
    """The device is unplugged: the loss is recorded ONCE and the memory stays."""
    session = context["session"]
    controller = context["controller"]
    code = controller.kill()
    problems.require(code == -9,
                     "the external client exited with %r, expected SIGKILL (-9): the "
                     "device was not actually destroyed" % code)
    context["first_address"] = controller.address

    entry = wait_for_binding(session, IDENTITY, False, POLL)
    problems.require(entry is not None,
                     "the assignment for %r was DROPPED when the device went away: "
                     "the engine must remember its identity" % IDENTITY)
    if entry is None:
        return
    problems.require(entry["lost"] is True,
                     "the assignment reports lost=%r after the device went away"
                     % entry["lost"])
    status = reading(session, STATUS)
    problems.require(status["lost_count"] >= 1,
                     "lost_count is %r after a real loss" % status["lost_count"])
    first = status["lost"]
    time.sleep(2.0)
    again = reading(session, STATUS)
    problems.require(again["lost"] == first,
                     "the loss was counted again on a later poll (%r -> %r): it must "
                     "be counted on the transition only" % (first, again["lost"]))
    problems.require(wait_for_port(session, controller.full_name, False, SHORT),
                     "the dead client is still in the live port list")
    print("  loss: %s gone, assignment kept, lost=%d"
          % (controller.address, again["lost"]))


def check_reattach(context, problems):
    """THE CLAIM: the device is back at a NEW address and the engine re-attaches."""
    session = context["session"]
    instance = context["instance"]
    second = context["restart"]()
    if second is None:
        problems.add("could not start the second external client")
        return
    problems.require(second.address != context["first_address"],
                     "the recreated client came back at the SAME address %s: this run "
                     "cannot show a re-attachment to a new one" % second.address)

    entry = wait_for_binding(session, IDENTITY, True, POLL)
    problems.require(entry is not None, "the assignment vanished")
    if entry is None:
        return
    problems.require(entry["live"] is True,
                     "the engine did NOT re-attach %r: it reports live=%r"
                     % (IDENTITY, entry["live"]))
    problems.require(entry["name"] == second.full_name,
                     "the binding still holds %r; the device is back at %r"
                     % (entry["name"], second.full_name))
    problems.require(entry["reconnects"] >= 1,
                     "the re-attachment was not counted (reconnects=%r)"
                     % entry["reconnects"])

    block = engine_client_block(context["aconnect"], instance.process.pid)
    problems.require(block is not None and "Connected From: %s" % second.address in block,
                     "the kernel's subscription table does not show the NEW address "
                     "%s as a source for the engine's port:\n%s"
                     % (second.address, block))

    listed = port_entry(session, second.full_name)
    problems.require(listed is not None and listed["bound"] is True,
                     "the new live port is not reported as bound: %r" % (listed,))
    check_saved_project(context, second, problems)
    print("  reattached: %s -> %s (reconnects=%d)"
          % (context["first_address"], second.full_name, entry["reconnects"]))


def check_saved_project(context, second, problems):
    """The project's OWN record moved: a save carries the new address."""
    instance = context["instance"]
    saved = os.path.join(instance.tmp, "reconnected.mmp")
    reading(context["session"], "project.save", {"path": saved})
    problems.require(os.path.exists(saved), "project.save wrote no file at %r" % saved)
    if not os.path.exists(saved):
        return
    with open(saved) as handle:
        text = handle.read()
    problems.require('inports="%s"' % second.full_name in text,
                     "the saved project does not bind %r: the engine's own "
                     "serialized record did not move" % second.full_name)


def check_delivery_again(context, problems):
    """The RESTORED subscription carries MIDI: the reconnected client plays."""
    session = context["session"]
    before = captured(session)
    context["controller"].burst(BURST, problems)
    after = captured(session)
    problems.require(after - before == BURST,
                     "the engine received %d of the %d events played by the "
                     "RECONNECTED client: the restored binding is not live"
                     % (after - before, BURST))
    print("  +%d event(s) through the restored subscription" % (after - before))


def check_mode_switch(context, problems):
    """Disarmed the same kill-and-return leaves it dead; armed it comes back."""
    session = context["session"]
    instance = context["instance"]
    armed = reading(session, ARM, {"enabled": False})
    problems.require(armed["enabled"] is False and armed["changed"] is True,
                     "disarming reported enabled=%r changed=%r"
                     % (armed["enabled"], armed["changed"]))
    problems.require(armed["persisted"] is False,
                     "the config file's midi/reconnect key still says %r"
                     % armed["persisted"])
    with open(instance.config_path) as handle:
        config = handle.read()
    problems.require('reconnect="0"' in config,
                     "the config file does not carry the disarmed key: %s" % config)

    problems.require(context["controller"].kill() == -9, "the client was not killed")
    third = context["restart"]()
    if third is None:
        problems.add("could not start the third external client")
        return
    problems.require(wait_for_port(session, third.full_name, True, SHORT),
                     "the recreated client never appeared in the live port list")
    time.sleep(2.0)
    entry = binding_of(session, IDENTITY)
    problems.require(entry is not None, "the assignment vanished while disarmed")
    if entry is not None:
        problems.require(entry["live"] is False,
                         "the engine re-attached while DISARMED (live=%r)" % entry["live"])
        context["reconnects_while_off"] = entry["reconnects"]

    rearmed = reading(session, ARM)
    problems.require(rearmed["enabled"] is True and rearmed["changed"] is True,
                     "re-arming reported enabled=%r changed=%r"
                     % (rearmed["enabled"], rearmed["changed"]))
    entry = wait_for_binding(session, IDENTITY, True, POLL)
    problems.require(entry is not None and entry["live"] is True,
                     "arming again did not re-attach the device: %r" % (entry,))
    if entry is not None:
        problems.require(entry["reconnects"] > context.get("reconnects_while_off", 0),
                         "the re-attachment after arming was not counted (%r)"
                         % entry["reconnects"])
    print("  mode: disarmed left it dead, armed brought it back (reconnects=%s)"
          % (None if entry is None else entry["reconnects"]))


def check_inverse(context, problems):
    """SPEC A16: midi.reconnect_set binds, and ONE control.undo takes it back off."""
    from control_socket_harness import typed_error
    session = context["session"]
    track = context["track"]
    bystander = context["bystander"]
    problems.require(bystander is not None, "the negative-control client is gone")
    if bystander is None:
        return

    bound = reading(session, SET, {"port": track, "identity": BYSTANDER_IDENTITY})
    problems.require(bound["port"] == track and bound["identity"] == BYSTANDER_IDENTITY,
                     "midi.reconnect_set reported %r" % bound)
    problems.require(bound["changed"] is True and bound["live"] is True,
                     "binding an unbound port reported changed=%r live=%r"
                     % (bound["changed"], bound["live"]))
    listed = port_entry(session, bystander.full_name)
    problems.require(listed is not None and listed["bound"] is True,
                     "after the bind the live port reports %r" % (listed,))

    undone = reading(session, "control.undo")
    problems.require(undone.get("undone_command") == SET,
                     "control.undo unwound %r, not the binding"
                     % undone.get("undone_command"))
    after = port_entry(session, bystander.full_name)
    problems.require(after is not None and after["bound"] is False,
                     "after ONE control.undo the binding is still there: %r" % (after,))
    problems.require(binding_of(session, BYSTANDER_IDENTITY) is None,
                     "the assignment survived the undo")

    typed_error(session.call(SET, {"port": track, "identity": "no such client:p"}),
                session.last, "not_found")
    typed_error(session.call(SET, {"port": "trk-9999", "identity": BYSTANDER_IDENTITY}),
                session.last, "not_found")
    typed_error(session.call(SET, {"port": track}), session.last, "invalid_args")
    print("  inverse: bound %s, ONE control.undo removed it" % bystander.full_name)


def check_alive(context, problems):
    """The re-connections are a behaviour, not a crash: the engine still answers."""
    session = context["session"]
    instance = context["instance"]
    problems.require(instance.alive(),
                     "the instance exited (code %r) during the measurement"
                     % instance.process.returncode)
    ping = reading(session, "control.ping")
    problems.require(ping.get("pong") is True,
                     "control.ping answered %r after the re-connections" % ping)
