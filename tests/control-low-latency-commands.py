#!/usr/bin/env python3
"""END-TO-END proof that low-latency monitoring takes an armed LATENT INSTRUMENT (Q050-174, MX-159).

THE CLAIM UNDER TEST: an agent connected to a real `zene` instance over its
control socket can switch transport.low_latency on, and the command applies its
policy to the path of a record-armed instrument whose instrument reports more
latency than 'max_ms' - the path is named in the result ('instruments': track,
channel, path, latency) - while an unarmed latent instrument is left alone under
the default scope 'armed'. Each call re-evaluates (disarm, call again, the path
is gone), 'scope' all takes both, off clears every path, and nothing of it is an
undo step.

THE SUBJECT is the in-tree CLAP fixture (tests/data/clap-test-plugin,
clap-test-instrument.clap), found by the real binary's own plug-in scan through
a config whose <paths vstdir> is the fixture's directory, and made LATENT by
CLAP_TEST_INSTRUMENT_LATENCY (every instance reports that many frames and
really delays its output by them). What the mode does to the rendered audio is
proven in process by InstrumentLatencyPdcTest
(lowLatencyMonitorsTheArmedLatentInstrumentWithoutAlignment); this transcript
proves the agent surface on the real binary.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-low-latency-commands.py <zene-binary> <clap-fixture-dir>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary or fixture at the path).
"""

from __future__ import annotations

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: Frames every fixture instance reports (and delays by): ~45 ms at 44.1 kHz, over the 3 ms default.
LATENCY = 2000
#: The fixture's CLAP id (clap-test-instrument.c).
CLAP_ID = "org.lmms.test.clap-instrument"
#: How long the host's 100 ms latency poll gets to report the fixture's latency.
POLL_S = 10.0


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
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the number."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))


def latent_track(session, name, device):
    """A new instrument track named @a name carrying the fixture, or None."""
    track = session.result("track.add", {"type": "instrument", "name": name}).get("track")
    if not track:
        return None
    loaded = session.result("plugin.load", {"target": track, "device": device})
    # plugin.load names the track after the instrument; the checks read it back by name.
    renamed = session.result("track.rename", {"track": track, "name": name})
    return None if "error" in loaded or "error" in renamed else track


def fixture_device(session):
    for device in session.result("plugin.list", {"format": "clap"}).get("devices") or []:
        if device.get("clap_id") == CLAP_ID:
            return device.get("id")
    return None


def reported_latency(session, name):
    for entry in session.result("pdc.report").get("track_inputs") or []:
        if entry.get("name") == name:
            return entry.get("latency_frames")
    return None


def wait_for_latency(session, names):
    deadline = time.time() + POLL_S
    while time.time() < deadline:
        if all(reported_latency(session, name) == LATENCY for name in names):
            return True
        time.sleep(0.1)
    return False


def tracks_of(result):
    return [entry.get("track") for entry in result.get("instruments") or []]


def check_armed_scope(session, recorder, armed, unarmed, take):
    """Default scope: only the armed latent instrument's path, named in the result."""
    recorder.check("track.set_arm arms the first latent track",
                   "error" not in session.result("track.set_arm", {"track": armed, "armed": True, "file": take}),
                   "track=%s" % armed)
    depth = session.result("control.undo_depth").get("depth")
    on = session.result("transport.low_latency", {"enabled": True})
    entry = (on.get("instruments") or [{}])[0]
    recorder.check("the default scope is armed with a 3 ms threshold",
                   on.get("scope") == "armed" and on.get("max_ms") == 3 and on.get("enabled") is True, "%r" % on)
    recorder.check("the result names the armed latent instrument's path and only it",
                   tracks_of(on) == [armed] and entry.get("latency_frames") == LATENCY
                   and str(entry.get("channel") or "").startswith("ch-")
                   and entry.get("path") == "%s -> %s" % (armed, entry.get("channel"))
                   and entry.get("name") == "low-latency-armed", "%r" % on)
    recorder.check("the unarmed latent instrument is not in the result", unarmed not in tracks_of(on), "%r" % on)
    recorder.check("turning the mode on records no undo step",
                   session.result("control.undo_depth").get("depth") == depth, "before=%r" % depth)


def check_scopes_and_off(session, recorder, armed, unarmed):
    """'all' takes both paths; disarming re-evaluates; off clears everything."""
    every = session.result("transport.low_latency", {"scope": "all"})
    recorder.check("scope all takes both latent instruments",
                   sorted(tracks_of(every)) == sorted([armed, unarmed]), "%r" % every)
    high = session.result("transport.low_latency", {"max_ms": 100})
    recorder.check("a threshold above the instruments' latency takes no path", tracks_of(high) == [], "%r" % high)
    session.result("transport.low_latency", {"scope": "armed", "max_ms": 3})
    session.result("track.set_arm", {"track": armed, "armed": False})
    disarmed = session.result("transport.low_latency")
    recorder.check("a call after disarming re-evaluates: no path is left", tracks_of(disarmed) == [], "%r" % disarmed)
    off = session.result("transport.low_latency", {"enabled": False})
    recorder.check("off answers enabled false and no path",
                   off.get("enabled") is False and tracks_of(off) == [], "%r" % off)
    bad = session.call("transport.low_latency", {"scope": "nowhere"})
    recorder.check("an unknown scope is a typed invalid_args",
                   bad.get("ok") is False and (bad.get("error") or {}).get("kind") == "invalid_args", "%r" % bad)


def run_checks(session, recorder, instance):
    device = fixture_device(session)
    recorder.check("the real binary's scan finds the CLAP fixture", device is not None, "device=%r" % device)
    if device is None:
        return
    armed = latent_track(session, "low-latency-armed", device)
    unarmed = latent_track(session, "low-latency-unarmed", device)
    recorder.check("two latent instrument tracks were created", armed and unarmed, "%r %r" % (armed, unarmed))
    if not (armed and unarmed):
        return
    recorder.check("both report the fixture's latency to PDC",
                   wait_for_latency(session, ["low-latency-armed", "low-latency-unarmed"]),
                   "armed=%r unarmed=%r" % (reported_latency(session, "low-latency-armed"),
                                           reported_latency(session, "low-latency-unarmed")))
    check_armed_scope(session, recorder, armed, unarmed, os.path.join(instance.tmp, "take.wav"))
    check_scopes_and_off(session, recorder, armed, unarmed)


def report(recorder):
    print("")
    print("==== checks ====")
    for name, passed, evidence in recorder.results:
        print("  %-66s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    return 0 if recorder.results and all(passed for _, passed, _ in recorder.results) else 1


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]) or not os.path.isdir(argv[2]):
        print("cannot run: no binary at %s or no fixture directory at %s" % (argv[1], argv[2]))
        return 2
    recorder = Recorder()
    transcript = H.Transcript()
    instance = H.Instance(argv[1], extra_xml='  <paths vstdir="%s/"/>' % os.path.abspath(argv[2]),
                          extra_env={"CLAP_TEST_INSTRUMENT_LATENCY": str(LATENCY)})
    instance.spawn()
    with instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        run_checks(session, recorder, instance)
        session.call("control.quit")
        client.close()
        instance.wait_for_exit(H.QUIT_TIMEOUT)
    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("transport.low_latency instrument-path socket proof (every check held)")
    else:
        instance.dump_log()
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
