#!/usr/bin/env python3
"""BUG-RENDER-MP3: the render command's advertised format list must agree with
what THIS BUILD can encode.

Reproduced 2026-09-25: `render.render {format: "mp3"}` was accepted - the schema
at src/core/ControlCommandsProject.cpp advertised `mp3` unconditionally - on a
build with no LAME encoder, and then failed with the generic
`refused: "the render failed (exit 1); the session is unchanged"` and no output
file. The capability was promised by the API and could not be delivered by the
engine, and the failure a caller saw named neither.

ProjectRenderer::fileEncodeDevices already answers this: a device whose
instantiation function is null (no LAME / no libvorbis in this configuration)
reports `isAvailable() == false`, and that is the filter the export dialog
applies to its file-type list (ExportProjectDialog.cpp:84). The fix makes the
control surface use the same authority: the schema's `format` enum is BUILT from
the available devices at registration time, and a format name whose device is
unavailable is refused typed in the handler (naming the encoder) rather than
being handed to the render child.

This test drives the REAL binary over --control-socket and reads the schema the
binary itself advertises: `wav` must be offered and must render (the capability
half), and `mp3` must be refused typed - never the generic render failure - when
it is not offered. On a build WITH LAME the branch inverts: mp3 is offered and
has to render a file. Red before the fix (this configuration): `mp3` is in the
enum and the request answers `refused: the render failed (exit 1)...`.

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-format-capability.py <zene>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 300

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)

    def render(self, args):
        return self.call("render.render", args)


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def advertised_formats(session):
    """What the binary's OWN schema promises, not what the source says."""
    for entry in session.ok("control.commands_list").get("commands", []):
        if entry.get("id") == "render.render":
            schema = entry.get("args_schema") or {}
            return (schema.get("properties") or {}).get("format", {}).get("enum", [])
    return None


def render_until_answered(session, args, note):
    """The render runs in a child process; retry once on a loaded box, the way
    this tree's own render tests do (control-socket-integration.py)."""
    reply = session.render(args)
    if reply.get("ok"):
        return reply
    print("note: %s failed (%r); retrying once" % (note, reply.get("error")))
    return session.render(args)


def written(path):
    return os.path.isfile(path) and os.path.getsize(path) > 0


def check_wav_is_offered_and_renders(session, advertised, out_dir, problems):
    if "wav" not in advertised:
        problems.add("render.render does not advertise wav, which every build can write: %r"
                     % advertised)
        return
    wav_out = os.path.join(out_dir, "format-probe.wav")
    reply = render_until_answered(session, {"out": wav_out, "format": "wav"}, "the wav render")
    if not reply.get("ok"):
        problems.add("render.render refused the format it advertises (wav): %r"
                     % reply.get("error"))
        return
    result = H.ok_result(reply, reply.get("id"))
    if not result.get("sha256") or not written(wav_out):
        problems.add("the wav render wrote no file or no hash: %r" % result)


def check_advertised_mp3_renders(session, mp3_out, reply, problems):
    for _ in range(2):
        if reply.get("ok"):
            break
        print("note: the advertised mp3 render failed (%r); retrying once" % reply.get("error"))
        reply = session.render({"out": mp3_out, "format": "mp3"})
    if not reply.get("ok"):
        problems.add("render.render advertises mp3 but the render failed twice: %r"
                     % reply.get("error"))
    elif not written(mp3_out):
        problems.add("render.render accepted mp3 and wrote no file: %r" % reply.get("result"))


def check_unadvertised_mp3_is_refused(reply, mp3_out, problems):
    error = reply.get("error") or {}
    if reply.get("ok"):
        problems.add("this build does not advertise mp3 but the render was accepted: %r"
                     % reply.get("result"))
    if not error.get("kind"):
        problems.add("the refused mp3 render carried no typed kind: %r" % reply)
    if "the render failed" in str(error.get("message", "")):
        problems.add("an unavailable format still failed as a generic render failure: %r" % error)
    if os.path.exists(mp3_out):
        problems.add("the refused mp3 render left a file behind: %s" % mp3_out)


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    with H.start_instance(binary, workingdir=None, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        # A session with something to render in it: an empty session refuses the
        # render for a different reason ("there is nothing to render") and would
        # hide the capability this test is about.
        track = session.ok("track.add", {"name": "Format QA", "type": "instrument"})["track"]
        session.ok("clip.add", {"track": track, "position": 0, "length": 192})
        advertised = advertised_formats(session)
        if advertised is None:
            problems.add("control.commands_list does not know render.render")
            transcript.dump()
            return
        print("advertised render formats: %r" % advertised)
        mp3_out = os.path.join(instance.tmp, "format-probe.mp3")
        reply = session.render({"out": mp3_out, "format": "mp3"})
        if "mp3" in advertised:
            check_advertised_mp3_renders(session, mp3_out, reply, problems)
        else:
            check_unadvertised_mp3_is_refused(reply, mp3_out, problems)
        check_wav_is_offered_and_renders(session, advertised, instance.tmp, problems)
        if instance.alive():
            session.ok("control.quit", {"save": False})
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("render formats match the build", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
