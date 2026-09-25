#!/usr/bin/env python3
"""END-TO-END proof that an ACTIVATED tempo map survives the project round trip.

THE DEFECT UNDER TEST (BUG-D4c-8402, certification wave 4, seed 8402). Step 44 of
that run called `transport.tempo_map_set_active {"active":true}` - the documented
way to switch the timeline onto the tempo map WITHOUT editing its events - and the
saved document carried `<tempo-map max-events="128" active="1" events="0"/>`.
One save -> open -> save later that element was GONE, and the delta was outside
the documented per-save strips (docs/SAVE-CANONICAL-STABILITY.md section 3/4 -
only the <z:provenance> block and its index digest), so it was silent project
state loss of the BUG-A4-3 class. The READER dropped it, not the writer:
`TempoMap::loadSettings()` emptied a map whose element held no event AND reset
its `active` flag, while `TempoMap::shouldPersist()` is `active || size() > 0` -
so the next save had nothing to write.

WHAT IT ASSERTS, read back out of the documents the application itself wrote:

  * an empty-but-active map is written as `<tempo-map ... active="1" events="0"
    max-events="128"/>`, and that element with those attributes is still in the
    document after save -> open -> save, and again after a second open -> save;
  * `transport.tempo_map_get` reports the map ACTIVE after the reload too - the
    same fact on the wire, so a byte check cannot pass while the state is wrong;
  * a POPULATED map (a tempo at tick 0, a 3/4 metre at 384) keeps every event and
    its attributes across the same round trip. This is the control the first half
    needs: the fix must not change what a non-empty map does.

WHY A REGISTERED CTEST AND NOT A UNIT TEST. tests/src/core/TempoMapPersistenceTest.cpp
holds the map's own file claim IN PROCESS. What only the real binary can show is
the release contract's section 3.1 path: an external client activates the map over
`--control-socket`, saves it with `project.save`, reloads it with `project.open`,
and the bytes are read back from the file the application wrote.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-tempo-map-persistence.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed (the app log is printed);
2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import re
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)

#: The tempo the populated half authors at tick 0, and the metre it authors at
#: 384 - the metre of a 192-tick 4/4 bar, so the event is one bar in.
MAPPED_TEMPO = 90
METRE_TICK = 384
METRE = (3, 4)

#: An empty-but-active map is written exactly this way (TempoMap::saveSettings):
#: the declared authority, a zero event count and the fixed capacity. The
#: attributes are compared as a set, because the save canonicaliser orders them.
EMPTY_ACTIVE = {"active": "1", "events": "0", "max-events": "128"}

ELEMENT = re.compile(r"<tempo-map\b([^>]*?)/?>", re.S)
INNER = re.compile(r"<tempo-map\b[^>]*?(?:/>|>(.*?)</tempo-map>)", re.S)
EVENT = re.compile(r"<event\b([^>]*?)/?>", re.S)
ATTRIBUTE = re.compile(r'([A-Za-z][A-Za-z-]*)="([^"]*)"')


# ---------------------------------------------------------------------------
# reading the wire
# ---------------------------------------------------------------------------


class Session:
    """One request per command, with the reply kept for the transcript."""

    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 0

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args, transcript=self.transcript)

    def result(self, command, args=None):
        """The reply's result, or {'error': ...} so a failed call is visible."""
        reply = self.call(command, args)
        if reply.get("ok") is True:
            return reply.get("result") or {}
        return {"error": reply.get("error") or reply}


class Recorder:
    """Collects the named checks and their evidence, so a failure names the bytes."""

    def __init__(self):
        self.results = []

    def check(self, name, passed, evidence):
        self.results.append((name, bool(passed), evidence))

    def problems(self):
        return [(name, evidence) for name, passed, evidence in self.results if not passed]


def read_project_xml(path):
    """The project's XML as text. `project.save` writes .mmpz as Qt's qCompress
    output (a big-endian length, then zlib), and anything else as plain XML."""
    with open(path, "rb") as handle:
        blob = handle.read()
    try:
        return zlib.decompress(blob[4:]).decode("utf-8", "replace")
    except zlib.error:
        return blob.decode("utf-8", "replace")


def tempo_map_element(text):
    """The `<tempo-map ...>` element's attributes as a dict, or None when absent."""
    match = ELEMENT.search(text)
    return None if match is None else dict(ATTRIBUTE.findall(match.group(1)))


def tempo_map_events(text):
    """Every `<event ...>` attribute dict inside the tempo-map element, in order.

    The element's own tag is self-closing when the set is empty and opens a
    child list otherwise, so the body is taken up to `</tempo-map>` - never up
    to the first `/>`, which is the FIRST EVENT's own tag."""
    block = INNER.search(text)
    if block is None or not block.group(1):
        return ()
    return tuple(dict(ATTRIBUTE.findall(chunk)) for chunk in EVENT.findall(block.group(1)))


def save_and_read(session, path):
    """`project.save` to `path`, then the file's own bytes: (result, XML text)."""
    saved = session.result("project.save", {"path": path})
    text = read_project_xml(path) if os.path.exists(path) else ""
    return saved, text


def reopen(session, path):
    """`project.open` of `path`, then the live map state off the wire."""
    opened = session.result("project.open", {"path": path})
    return opened, session.result("transport.tempo_map_get")


# ---------------------------------------------------------------------------
# the checks
# ---------------------------------------------------------------------------


def check_empty_active_round_trip(session, instance, recorder):
    """Activate an empty map, then two save -> open -> save cycles on it."""
    start = session.result("transport.tempo_map_get")
    recorder.check("the map starts empty and inactive",
                   start.get("active") is False and start.get("event_count") == 0,
                   "active=%r event_count=%r" % (start.get("active"), start.get("event_count")))

    activated = session.result("transport.tempo_map_set_active", {"active": True})
    recorder.check("activating an empty map reports the empty-but-active state",
                   activated.get("active") is True and activated.get("event_count") == 0,
                   "reply=%r" % (activated,))

    first_path = os.path.join(instance.workspace, "tempo-map-proof.mmpz")
    saved, first = save_and_read(session, first_path)
    first_element = tempo_map_element(first)
    recorder.check("the first save writes the empty-but-active element",
                   saved.get("saved") is True and first_element == EMPTY_ACTIVE,
                   "saved=%r element=%r" % (saved.get("saved"), first_element))

    opened, state = reopen(session, first_path)
    recorder.check("the reloaded instance still reports the map active",
                   bool(opened.get("file")) and opened.get("error_count") == 0
                   and state.get("active") is True and state.get("event_count") == 0,
                   "opened=%r errors=%r active=%r event_count=%r"
                   % (opened.get("file"), opened.get("error_count"),
                      state.get("active"), state.get("event_count")))

    second_path = os.path.join(instance.workspace, "tempo-map-proof-2.mmpz")
    _, second = save_and_read(session, second_path)
    recorder.check("the element survives save -> open -> save",
                   tempo_map_element(second) == first_element and first_element is not None,
                   "after the reload=%r before it=%r"
                   % (tempo_map_element(second), first_element))

    reopened, state = reopen(session, second_path)
    third_path = os.path.join(instance.workspace, "tempo-map-proof-3.mmpz")
    _, third = save_and_read(session, third_path)
    recorder.check("and a second open -> save keeps it, on the wire as well",
                   tempo_map_element(third) == first_element
                   and reopened.get("error_count") == 0
                   and state.get("active") is True,
                   "after two cycles=%r active=%r" % (tempo_map_element(third), state.get("active")))
    return third_path


def check_populated_round_trip(session, instance, recorder):
    """The control: a map WITH events keeps every event across the same trip."""
    tempo = session.result("transport.tempo_map_add", {"tick": 0, "bpm": MAPPED_TEMPO})
    metre = session.result("transport.tempo_map_add",
                           {"tick": METRE_TICK, "numerator": METRE[0], "denominator": METRE[1]})
    authored = session.result("transport.tempo_map_get")
    recorder.check("two events are authored and the map is in force",
                   tempo.get("has_tempo") is True and metre.get("has_time_signature") is True
                   and authored.get("active") is True and authored.get("event_count") == 2,
                   "tempo=%r metre=%r active=%r event_count=%r"
                   % (tempo.get("tick"), metre.get("tick"),
                      authored.get("active"), authored.get("event_count")))

    first_path = os.path.join(instance.workspace, "tempo-map-populated.mmpz")
    _, first = save_and_read(session, first_path)
    first_events = tempo_map_events(first)
    recorder.check("the populated save carries both events and their attributes",
                   tempo_map_element(first) == {"active": "1", "events": "2", "max-events": "128"}
                   and first_events == ({"tick": "0", "bpm": str(MAPPED_TEMPO)},
                                        {"tick": str(METRE_TICK), "numerator": str(METRE[0]),
                                         "denominator": str(METRE[1])}),
                   "element=%r events=%r" % (tempo_map_element(first), first_events))

    opened, state = reopen(session, first_path)
    second_path = os.path.join(instance.workspace, "tempo-map-populated-2.mmpz")
    _, second = save_and_read(session, second_path)
    recorder.check("the populated map's events survive save -> open -> save",
                   tempo_map_events(second) == first_events
                   and tempo_map_element(second) == tempo_map_element(first),
                   "after the reload=%r before it=%r"
                   % (tempo_map_events(second), first_events))
    recorder.check("the reloaded map's own query answers the authored tempo",
                   opened.get("error_count") == 0
                   and state.get("event_count") == 2
                   and state.get("active") is True
                   and state.get("tempo_at_position") == MAPPED_TEMPO,
                   "errors=%r active=%r event_count=%r tempo_at_position=%r"
                   % (opened.get("error_count"), state.get("active"),
                      state.get("event_count"), state.get("tempo_at_position")))


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
        print("  %-62s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            print("      %s" % evidence)
    problems = recorder.problems()
    if problems:
        print("")
        print("FAIL: the tempo-map persistence proof has %d failed check(s)" % len(problems))
        return 1
    print("")
    print("PASS: %d checks, every one read out of a saved document" % len(recorder.results))
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
        print("workspace: %s" % instance.workspace)

        session.result("control.version")
        check_empty_active_round_trip(session, instance, recorder)
        check_populated_round_trip(session, instance, recorder)
        check_quit(session, instance, recorder)
    transcript.dump()
    code = report(recorder)
    if code == 0:
        H.ok("the tempo map survives the project round trip (every check held)")
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
