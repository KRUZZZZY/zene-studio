#!/usr/bin/env python3
"""0.5.0 F7 (MX-060): the surround export through the agent surface and the CLI, on the REAL binary.

`render.render` takes an additive `channels` argument - "stereo" (absent: the render the command
has always made) or "surround" - and the CLI takes `--channels stereo|surround`. This driver proves,
through --control-socket and a child CLI process:

  - the binary's own schema advertises `channels` as ["stereo", "surround"];
  - a render without `channels` is the stereo render it was: a 2-channel PCM file, `channels` 2,
    `channel_layout` "stereo" in the reply;
  - `channels: "surround"` is refused typed (invalid_args), naming the reason and leaving no file, on a
    stereo master, for FLAC, and while the loudness report is on; an unknown name is refused too;
  - on a 5.1 master it renders a 6-channel WAVE_FORMAT_EXTENSIBLE file with mask 0x3F, and the reply
    says `channels` 6, `channel_layout` "5.1" (the render ran in the child CLI with --channels);
  - the CLI refuses `--channels surround` for FLAC (after loading: "Render refused"), for exportstems,
    and an unknown mode, each with a non-zero exit and no output.

The PCM content of a surround export (each source on its own speaker) is measured in-process by
SurroundExportTest; this driver is about the surfaces that reach it.

Usage: QT_QPA_PLATFORM=offscreen python3 control-render-surround.py <zene>
"""

import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402


class Session:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.last_id = 500

    def call(self, command, args=None):
        self.last_id += 1
        return self.client.call(self.last_id, command, args or {}, transcript=self.transcript)

    def ok(self, command, args=None):
        return H.ok_result(self.call(command, args), self.last_id)


def wav_header(path):
    """(format tag, channels, bits, channel mask or None) from the fmt chunk, read by hand."""
    with open(path, "rb") as handle:
        blob = handle.read()
    pos = 12
    while pos + 8 <= len(blob):
        chunk, size = blob[pos:pos + 4], struct.unpack_from("<I", blob, pos + 4)[0]
        if chunk == b"fmt ":
            tag, channels = struct.unpack_from("<HH", blob, pos + 8)
            bits = struct.unpack_from("<H", blob, pos + 22)[0]
            mask = struct.unpack_from("<I", blob, pos + 28)[0] if tag == 0xFFFE and size >= 40 else None
            return tag, channels, bits, mask
        pos += 8 + size + (size & 1)
    return None


def advertised_channels(session):
    for entry in session.ok("control.commands_list").get("commands", []):
        if entry.get("id") == "render.render":
            props = (entry.get("args_schema") or {}).get("properties") or {}
            return props.get("channels", {}).get("enum")
    return None


def render(session, args, note):
    """The render runs in a child process; retry once on a loaded box, as the other render drivers do."""
    reply = session.call("render.render", args)
    if not reply.get("ok") and (reply.get("error") or {}).get("kind") != "invalid_args":
        print("note: %s failed (%r); retrying once" % (note, reply.get("error")))
        reply = session.call("render.render", args)
    return reply


def expect_refused(session, args, reason, problems):
    reply = session.call("render.render", args)
    try:
        error = H.typed_error(reply, session.last_id, "invalid_args")
    except AssertionError as exc:
        problems.add("%r was not refused typed: %s" % (args, exc))
        return
    if reason not in error.get("message", ""):
        problems.add("the refusal of %r does not say %r: %r" % (args, reason, error.get("message")))
    if os.path.exists(args["out"]):
        problems.add("a refused render left a file behind: %s" % args["out"])


def expect_rendered(session, out, args, expected, problems):
    reply = render(session, dict(args, out=out), "the %s render" % expected[1])
    if not reply.get("ok"):
        problems.add("render.render %r failed: %r" % (args, reply.get("error")))
        return
    result = reply.get("result") or {}
    if (result.get("channels"), result.get("channel_layout")) != (expected[0], expected[1]):
        problems.add("render.render %r replied channels %r layout %r, expected %r"
                     % (args, result.get("channels"), result.get("channel_layout"), expected[:2]))
    check_header(out, expected, problems)
    if not result.get("frames") or result.get("frames") <= 0:
        problems.add("the %s render reports no frames: %r" % (expected[1], result))


def check_header(out, expected, problems):
    """expected: (channels, layout, format tag, mask or None)."""
    header = wav_header(out) if os.path.isfile(out) else None
    if header is None or (header[1], header[0], header[3]) != (expected[0], expected[2], expected[3]):
        problems.add("%s: header %r, expected tag 0x%x, %d channels, mask %r"
                     % (out, header, expected[2], expected[0], expected[3]))


def check_cli(instance, project, problems):
    """The CLI's own refusals: a non-zero exit, the reason on stderr, no output file."""
    cases = [
        (["render", project, "-f", "flac", "-o", os.path.join(instance.tmp, "cli.flac"), "--channels", "surround"],
         "Render refused", os.path.join(instance.tmp, "cli.flac")),
        (["render", project, "-o", os.path.join(instance.tmp, "cli-bad.wav"), "--channels", "9.1"],
         "--channels takes", os.path.join(instance.tmp, "cli-bad.wav")),
        (["exportstems", project, "-o", os.path.join(instance.tmp, "stems"), "--channels", "surround"],
         "--channels surround is for", os.path.join(instance.tmp, "stems")),
    ]
    for argv, reason, output in cases:
        run = subprocess.run([instance.binary, "--config", instance.config_path] + argv, env=instance.env(),
                             cwd=instance.tmp, capture_output=True, timeout=300)
        text = run.stderr.decode(errors="replace") + run.stdout.decode(errors="replace")
        if run.returncode == 0:
            problems.add("the CLI accepted %r" % argv)
        if reason not in text:
            problems.add("the CLI's refusal of %r does not say %r: %s" % (argv, reason, text[-400:]))
        if os.path.exists(output):
            problems.add("the CLI's refused %r left %s behind" % (argv, output))


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    with H.start_instance(binary, workingdir=None) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        session = Session(client, transcript)
        offered = advertised_channels(session)
        if offered != ["stereo", "surround"]:
            problems.add("render.render does not advertise channels [stereo, surround]: %r" % offered)
        track = session.ok("track.add", {"name": "Surround QA", "type": "instrument"})["track"]
        session.ok("clip.add", {"track": track, "position": 0, "length": 192})
        tmp = instance.tmp

        expect_refused(session, {"out": os.path.join(tmp, "stereo-master.wav"), "channels": "surround"},
                       "master is stereo", problems)
        expect_rendered(session, os.path.join(tmp, "default.wav"), {}, (2, "stereo", 1, None), problems)

        master = session.ok("mixer.get_state")["channels"][0]["id"]
        session.ok("mixer.channel_format", {"channel": master, "format": "5.1"})
        expect_refused(session, {"out": os.path.join(tmp, "s.flac"), "format": "flac", "channels": "surround"},
                       "only WAV", problems)
        expect_refused(session, {"out": os.path.join(tmp, "bad.wav"), "channels": "9.1"}, "", problems)
        session.ok("export.set_loudness_report", {"enabled": True})
        expect_refused(session, {"out": os.path.join(tmp, "loud.wav"), "channels": "surround"}, "loudness", problems)
        session.ok("export.set_loudness_report", {"enabled": False})
        expect_rendered(session, os.path.join(tmp, "surround.wav"), {"channels": "surround"},
                        (6, "5.1", 0xFFFE, 0x3F), problems)
        expect_rendered(session, os.path.join(tmp, "explicit-stereo.wav"), {"channels": "stereo"},
                        (2, "stereo", 1, None), problems)

        project = os.path.join(tmp, "surround.mmp")
        session.ok("project.save", {"path": project})
        check_cli(instance, project, problems)
        if instance.alive():
            session.ok("control.quit", {"save": False})
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("surround export through render.render and the CLI", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
