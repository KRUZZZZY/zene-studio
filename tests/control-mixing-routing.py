#!/usr/bin/env python3
"""The 0.5.0 L09 routing surface, driven end to end over the control socket.

Starts the REAL `zene` binary headless (its own throwaway config) and proves:

  1. mixer.listen / listen_mode put channels on the listen bus (AFL / PFL) and take them off;
  2. mixer.send_many sends several channels to one as a single undo step;
  3. mixer.send_pan pans one send and one undo puts it back;
  4. vca.set_parent nests a group and refuses a loop; vca.set_links chooses the links;
  5. window.mixer_view sets and answers the sends-on-faders mode and the view options (L08);
  6. mixer.set_strip writes a channel's strip settings as one undo step, mixer.strip_get reads
     them and the meters, mixer.set_many clears every solo, mixer.solo_mode switches (L08).

Usage:  QT_QPA_PLATFORM=offscreen python3 tests/control-mixing-routing.py <zene>
Exit 0 only when every check held.
"""

import sys

import control_socket_harness as H
from freeze_bounce_evidence import Recorder, Session, report_results


def kind(reply):
    return (reply.get("error") or {}).get("kind")


def channels(session, count):
    return [session.result("mixer.add_channel").get("channel") for _ in range(count)]


def check_listen(session, recorder, chans):
    on = session.result("mixer.listen", {"channel": chans[0], "exclusive": True})
    recorder.check("mixer.listen puts a channel on the listen bus", on.get("listening") == [chans[0]] and on.get("active"), "reply=%r" % on)
    mode = session.result("mixer.listen_mode", {"mode": "pfl"})
    recorder.check("the listen point switches to PFL", mode.get("mode") == "pfl", "reply=%r" % mode)
    off = session.result("mixer.listen_mode", {"clear": True, "mode": "afl"})
    recorder.check("clearing the listen bus empties it", off.get("listening") == [] and not off.get("active"), "reply=%r" % off)


def check_sends(session, recorder, chans):
    made = session.result("mixer.send_many", {"channels": chans[:2], "to": chans[2], "amount": 0.5})
    recorder.check("mixer.send_many makes the sends", made.get("made") == 2, "reply=%r" % made)
    panned = session.call("mixer.send_pan", {"channel": chans[0], "to": chans[2], "pan": -0.5})
    recorder.check("mixer.send_pan pans one send", panned.get("ok") and (panned.get("result") or {}).get("pan") == -0.5, "reply=%r" % panned)
    session.result("control.undo")
    session.result("control.undo")
    gone = session.call("mixer.send_pan", {"channel": chans[0], "to": chans[2], "pan": 0.1})
    recorder.check("two undos take back the pan and both sends", kind(gone) == "not_found", "reply=%r" % gone)


def check_groups(session, recorder, chans):
    outer = session.result("vca.create", {"name": "Outer"}).get("id")
    inner = session.result("vca.create", {"name": "Inner"}).get("id")
    session.result("vca.assign", {"group": inner, "channel": chans[0]})
    nested = session.call("vca.set_parent", {"group": inner, "parent": outer})
    recorder.check("vca.set_parent nests a group", nested.get("ok") and (nested.get("result") or {}).get("parent") == outer, "reply=%r" % nested)
    loop = session.call("vca.set_parent", {"group": outer, "parent": inner})
    recorder.check("a nesting loop is refused, typed", kind(loop) == "refused", "reply=%r" % loop)
    links = session.result("vca.set_links", {"group": inner, "mute": False})
    recorder.check("vca.set_links chooses the links", (links.get("links") or {}).get("mute") is False, "reply=%r" % links)


def check_view(session, recorder, chans):
    view = session.result("window.mixer_view", {"sends_on_faders": chans[2]})
    recorder.check("window.mixer_view puts the faders on one return's sends", view.get("sends_on_faders") == chans[2], "reply=%r" % view)
    # 0.5.0 theme 6: the return is named, and every channel keeps a fader (a send, volume or no_send).
    drives = {f.get("channel"): f.get("drives") for f in view.get("faders") or []}
    recorder.check("the answer names the return and what every fader drives",
                   bool(view.get("sends_on_faders_name")) and drives.get(chans[2]) == "volume"
                   and set(drives.values()) <= {"volume", "send", "no_send"}, "reply=%r" % view)
    back = session.result("window.mixer_view", {"sends_on_faders": ""})
    recorder.check("and back to the channel faders", back.get("sends_on_faders") == "", "reply=%r" % back)
    narrow = session.result("window.mixer_view", {"strip_width": "narrow", "filter": "returns", "meter_mode": "pre_fader"})
    recorder.check("the view options are set and answered", narrow.get("strip_width") == "narrow" and narrow.get("filter") == "returns",
                   "reply=%r" % narrow)
    bad = session.call("window.mixer_view", {"filter": "everything"})
    recorder.check("an unknown filter is refused, typed", kind(bad) == "invalid_args", "reply=%r" % bad)
    session.result("window.mixer_view", {"strip_width": "normal", "filter": "all", "meter_mode": "peak"})


def check_strip(session, recorder, chans):
    strip = session.result("mixer.set_strip", {"channel": chans[1], "trim_db": -3.0, "polarity_left": True,
                                               "pan_mode": "width", "width": 0.5, "note": "bus check"})
    recorder.check("mixer.set_strip writes the strip", strip.get("trim_db") == -3.0 and strip.get("pan_mode") == "width"
                   and strip.get("note") == "bus check", "reply=%r" % strip)
    read = session.result("mixer.strip_get", {"channel": chans[1]})
    recorder.check("mixer.strip_get reads the settings and the meters", read.get("polarity_left") is True
                   and "clipped" in (read.get("meters") or {}), "reply=%r" % read)
    session.result("control.undo")
    after = session.result("mixer.strip_get", {"channel": chans[1]})
    recorder.check("one undo puts every strip setting back", after.get("trim_db") == 0 and after.get("pan_mode") == "balance"
                   and after.get("note") == "", "reply=%r" % after)
    cleared = session.result("mixer.set_many", {"all": True, "soloed": False})
    recorder.check("mixer.set_many clears every solo", cleared.get("count", 0) >= 3, "reply=%r" % cleared)
    mode = session.result("mixer.solo_mode", {"mode": "additive"})
    recorder.check("mixer.solo_mode switches to additive", mode.get("mode") == "additive", "reply=%r" % mode)
    session.result("mixer.solo_mode", {"mode": "exclusive"})


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
        chans = channels(session, 3)
        check_listen(session, recorder, chans)
        check_sends(session, recorder, chans)
        check_groups(session, recorder, chans)
        check_view(session, recorder, chans)
        check_strip(session, recorder, chans)
    report_results(recorder)
    if recorder.problems:
        transcript.dump()
        recorder.problems.report("mixing and routing control-surface driver")
        return 1
    H.ok("mixing and routing control-surface driver (every check held)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
