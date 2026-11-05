#!/usr/bin/env python3
"""END-TO-END proof that an agent can generate a seeded melodic line in the active scale (Q050-221.1).

THE CLAIM UNDER TEST: over the control socket of a real `zene` instance, an agent sets a root and a
scale (scale.root_set / scale.set, and a custom one through scale.define), calls note.generate_melody
on a clip, and reads back - through note.list - a line of ordinary notes whose every key is in that
scale; the same seed on an identical empty clip gives the identical notes, another seed another pitch
line; control.undo takes the line back out; and with no scale set the call is refused, typed.
In process, tests/src/gui/NoteMelodyTest.cpp holds the same claims and the note editor's menu path;
this is the release contract's section 3.1 half: the command reachable THROUGH THE SOCKET.

Usage:
    QT_QPA_PLATFORM=offscreen python3 control-note-melody.py <zene-binary>

Exit codes: 0 every check held; 1 a check failed; 2 cannot run (no binary at the path).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402  (path set above)


class Session:
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


def notes_of(session, clip):
    """(position, key, length, velocity) of every note of the clip, in time order."""
    listed = session.result("note.list", {"clip": clip})
    rows = [(int(n.get("position", -1)), int(n.get("key", -1)), int(n.get("length", -1)), int(n.get("velocity", -1)))
            for n in listed.get("notes") or []]
    return sorted(rows)


def new_clip(session, instance, transcript, track):
    clip = session.result("clip.add", {"track": track, "position": 0})
    if not clip.get("clip"):
        H.fail("clip.add returned no clip id (%r)" % clip, instance, transcript)
    return clip.get("clip")


class Checks:
    """The named checks and their evidence, in H.finish's shape."""

    def __init__(self):
        self.rows = []

    def check(self, name, passed, evidence):
        self.rows.append((name, bool(passed), [] if passed else [evidence]))


def check_refusal(s, clip, checks):
    """With no scale set the call is refused, typed."""
    refused = s.call("note.generate_melody", {"clip": clip, "seed": 1})
    error = refused.get("error") or {}
    checks.check("no scale set: refused, typed", refused.get("ok") is False and error.get("kind") == "refused",
                 "%r" % refused)


def check_seeds(s, clips, checks):
    """D major: the same seed repeats, another seed changes the line, every key in scale; undo takes it back."""
    s.result("scale.root_set", {"root": "D"})
    s.result("scale.set", {"scale": "Major"})  # D E F# G A B C#
    d_major = {2, 4, 6, 7, 9, 11, 1}
    made = s.result("note.generate_melody", {"clip": clips[0], "seed": 7, "contour": "arch", "count": 12})
    s.result("note.generate_melody", {"clip": clips[1], "seed": 7, "contour": "arch", "count": 12})
    s.result("note.generate_melody", {"clip": clips[2], "seed": 8, "contour": "arch", "count": 12})
    first, again, other = notes_of(s, clips[0]), notes_of(s, clips[1]), notes_of(s, clips[2])
    first_keys = [row[1] for row in first]
    checks.check("the reply counts the notes and names its seed",
                 made.get("added") == 12 and made.get("seed") == 7, "%r" % made)
    checks.check("same seed, same empty clip: identical notes", len(first) == 12 and first == again,
                 "%r vs %r" % (first, again))
    checks.check("another seed: another pitch line", first_keys != [row[1] for row in other],
                 "%r vs %r" % (first, other))
    checks.check("every key in D major", all(row[1] % 12 in d_major for row in first + other), "%r" % (first + other,))
    checks.check("the reply's keys are the notes' keys", made.get("keys") == first_keys, "%r" % made.get("keys"))
    undone = s.call("control.undo")
    left = notes_of(s, clips[2])
    checks.check("control.undo takes the line back out", undone.get("ok") is True and left == [],
                 "%r / %r" % (undone, left))


def check_custom_scale(s, clip, checks):
    """A scale of one's own: every key in it."""
    s.result("scale.define", {"name": "Socket Mine", "degrees": [0, 3, 5, 10]})
    s.result("scale.root_set", {"root": "E"})
    s.result("scale.set", {"scale": "Socket Mine"})  # E G A D
    s.result("note.generate_melody", {"clip": clip, "seed": 3, "count": 48, "spread": 4})
    custom = notes_of(s, clip)
    checks.check("every key in the custom scale", len(custom) == 48 and all(row[1] % 12 in {4, 7, 9, 2} for row in custom),
                 "%r" % custom)


def check_quit(s, client, instance, checks):
    quit_reply = s.call("control.quit")
    client.close()
    exited, code, _ = instance.wait_for_exit(H.QUIT_TIMEOUT)
    checks.check("control.quit stops the instance", quit_reply.get("ok") is True and exited and code == 0,
                 "exited=%r code=%r" % (exited, code))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if not os.path.exists(argv[1]):
        print("cannot run: no binary at %s" % argv[1])
        return 2
    checks = Checks()
    transcript = H.Transcript()
    with H.start_instance(argv[1]) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        s = Session(client, transcript)
        track = s.result("track.add", {"type": "instrument", "name": "Melody"}).get("track")
        clips = [new_clip(s, instance, transcript, track) for _ in range(4)]
        check_refusal(s, clips[0], checks)
        check_seeds(s, clips[:3], checks)
        check_custom_scale(s, clips[3], checks)
        check_quit(s, client, instance, checks)
    transcript.dump()
    return H.finish(checks.rows)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
