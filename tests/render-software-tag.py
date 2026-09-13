#!/usr/bin/env python3
"""The WAV software tag: a render this build writes must credit THIS product.

THE CLAIM THIS PROVES, in the release notes' own words: "The WAV files this build
renders carry `Zene Studio (libsndfile-…)` in their software tag where they used to
credit LMMS. It is a metadata string, not audio … Verified against this tree: the
tag is the `sf_set_string(m_sf, SF_STR_SOFTWARE, "Zene Studio")` call in the WAV
writer (`src/core/audio/AudioFileWave.cpp`) and its twin in the FLAC writer
(`src/core/audio/AudioFileFlac.cpp`), and the chunk was read back out of a real
render — `tests/integration-logs-3d/final/chunk-parse.log` records `LIST chunk:
INFOISFT Zene Studio (libsndfile-1.2.2)`" (docs/RELEASE-NOTES-v0.2.1-alpha.md).

So the claim rested on a committed LOG of a render that was parsed once by hand.
This is that parse, as a test: it renders a short project headlessly with the real
binary and reads the software tag out of the WAV's LIST/INFO/ISFT chunk.

WHAT IT CHECKS, and why each half can fail:
  * the method control - a WAV this script writes itself, carrying the PRE-FIX tag
    `LMMS (libsndfile-1.2.2)`, must be parsed successfully and must be REJECTED by
    the same check that accepts the real render. Without it, a parser that returned
    nothing at all would "pass" by finding no tag.
  * the real render - `zene render <project> -f wav` must exit 0, and the LIST/INFO
    chunk of its output must carry an ISFT entry that starts with "Zene Studio ("
    and names libsndfile, and must credit LMMS nowhere. A missing LIST/INFO chunk
    is a FAILURE, not a skip: the claim is that renders carry the tag.

SCOPE, stated because the claim sentence is longer than this file: the FLAC writer
has the same `sf_set_string` call (`AudioFileFlac.cpp:85`) and the notes name it,
but this test asserts the WAV half only. libsndfile writes its FLAC comments in a
layout that is not a plain Vorbis-comment block (the committed render's comment
block has no vendor-length prefix), so a FAITHFUL FLAC parser is a separate piece
of work; asserting it with a parser that had not been verified against libsndfile's
own output would be a green that proves nothing. The WAV half is what the notes
quote as the witness.

Usage: render-software-tag.py <lmms-binary> <plugin-dir> <project.mmp>
"""

import os
import struct
import subprocess
import sys
import tempfile

# The pre-fix string the rename replaced, kept verbatim (docs/RELEASE-NOTES-v0.2.1-alpha.md),
# so the method control can prove the check rejects it.
PRE_FIX_TAG = "LMMS (libsndfile-1.2.2)"
EXPECTED_PREFIX = "Zene Studio ("
EXPECTED_MARKER = "libsndfile-"
RENDER_TIMEOUT = 300.0


def parse_wav_info_chunks(path):
    """{chunk id: value} for the LIST/INFO sub-chunks of a RIFF/WAVE file.

    A LIST chunk whose form type is "INFO" carries the metadata entries libsndfile
    writes for SF_STR_*; each entry is an id, a little-endian size and the value,
    NUL-padded to an even length. Values are returned with their NUL padding and
    surrounding whitespace stripped.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("%s is not a RIFF/WAVE file" % path)
    info = {}
    offset = 12
    while offset + 8 <= len(data):
        chunk_id = data[offset:offset + 4]
        size = struct.unpack("<I", data[offset + 4:offset + 8])[0]
        body = data[offset + 8:offset + 8 + size]
        if chunk_id == b"LIST" and body[:4] == b"INFO":
            inner = 4
            while inner + 8 <= len(body):
                sub_id = body[inner:inner + 4]
                sub_size = struct.unpack("<I", body[inner + 4:inner + 8])[0]
                value = body[inner + 8:inner + 8 + sub_size]
                info[sub_id.decode("latin-1")] = value.rstrip(b"\0").strip().decode(
                    "utf-8", "replace")
                inner += 8 + sub_size + (sub_size % 2)
        offset += 8 + size + (size % 2)
    return info


def write_wav_with_tag(path, tag, frames=8, sample_rate=44100):
    """A minimal, valid RIFF/WAVE file carrying one LIST/INFO/ISFT entry."""
    def chunk(chunk_id, payload):
        body = chunk_id + struct.pack("<I", len(payload)) + payload
        return body + (b"\0" if len(payload) % 2 else b"")

    fmt = struct.pack("<HHIIHH", 1, 2, sample_rate, sample_rate * 4, 4, 16)
    data = b"\0" * (frames * 4)
    isft = tag.encode("utf-8") + b"\0"
    info = b"INFO" + chunk(b"ISFT", isft)
    body = chunk(b"fmt ", fmt) + chunk(b"LIST", info) + chunk(b"data", data)
    with open(path, "wb") as handle:
        handle.write(b"RIFF" + struct.pack("<I", len(body)) + b"WAVE" + body)


def tag_problems(info, where):
    """The assertion itself: `info` (a parsed INFO chunk map) must credit THIS product."""
    problems = []
    if "ISFT" not in info:
        problems.append(
            "%s has no LIST/INFO/ISFT software tag at all (INFO chunks present: %r). The "
            "release documents that renders carry one, so its absence is a failure"
            % (where, sorted(info)))
        return problems
    value = info["ISFT"]
    if not value.startswith(EXPECTED_PREFIX):
        problems.append(
            "%s carries the software tag %r; the release documents %s… as the tag this "
            "build writes" % (where, value, EXPECTED_PREFIX))
    if EXPECTED_MARKER not in value:
        problems.append(
            "%s carries the software tag %r; the documented shape names the writer, "
            "%r" % (where, value, EXPECTED_MARKER))
    if "LMMS" in value:
        problems.append(
            "%s carries the PRE-FIX software tag %r - a render that still credits LMMS"
            % (where, value))
    return problems


def method_control(workdir, problems):
    """The check above must reject the pre-fix tag, and the parser must read it."""
    control = os.path.join(workdir, "control-pre-fix-tag.wav")
    write_wav_with_tag(control, PRE_FIX_TAG)
    try:
        found = parse_wav_info_chunks(control)
    except ValueError as exc:
        problems.append("the parser could not read the WAV this script wrote itself: %s" % exc)
        return
    if found.get("ISFT") != PRE_FIX_TAG:
        problems.append(
            "the parser read %r out of a file this script wrote with the tag %r, so a "
            "negative result on the real render would prove nothing"
            % (found.get("ISFT"), PRE_FIX_TAG))
        return
    rejected = tag_problems(found, "the method-control WAV")
    if not rejected:
        problems.append(
            "the check ACCEPTED the pre-fix tag %r: it cannot detect the defect it "
            "targets" % PRE_FIX_TAG)
    else:
        print("  method control: the parser read %r and the check rejected it: %s"
              % (PRE_FIX_TAG, rejected[0]))


def render_and_check(binary, plugin_dir, project, workdir, problems):
    out = os.path.join(workdir, "software-tag-render.wav")
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["LMMS_PLUGIN_DIR"] = plugin_dir
    command = [binary, "render", project, "-f", "wav", "-s", "44100", "-o", out]
    try:
        done = subprocess.run(command, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=RENDER_TIMEOUT)
    except subprocess.TimeoutExpired:
        problems.append("the render did not finish within %.0fs: %r"
                        % (RENDER_TIMEOUT, command))
        return
    output = done.stdout.decode("utf-8", "replace")
    if done.returncode != 0:
        problems.append("the render exited %d: %r\n%s"
                        % (done.returncode, command, output[-2000:]))
        return
    if not os.path.exists(out):
        problems.append("the render exited 0 but wrote no file at %s\n%s"
                        % (out, output[-2000:]))
        return
    try:
        info = parse_wav_info_chunks(out)
    except ValueError as exc:
        problems.append("the render's output could not be parsed: %s" % exc)
        return
    print("  render: %s -> %s, INFO chunks: %r"
          % (os.path.basename(project), os.path.basename(out), info))
    problems.extend(tag_problems(info, "the rendered WAV"))


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    binary = os.path.abspath(sys.argv[1])
    plugin_dir = os.path.abspath(sys.argv[2])
    project = os.path.abspath(sys.argv[3])
    for path, what in ((binary, "lmms binary"), (project, "project")):
        if not os.path.exists(path):
            print("FAIL: no %s at %s" % (what, path))
            return 1
    if not os.path.isdir(plugin_dir):
        print("FAIL: no plugin directory (LMMS_PLUGIN_DIR) at %s" % plugin_dir)
        return 1

    problems = []
    workdir = tempfile.mkdtemp(prefix="zene-tag-", dir="/tmp")
    print("software tag: binary=%s plugins=%s project=%s" % (binary, plugin_dir, project))
    method_control(workdir, problems)
    render_and_check(binary, plugin_dir, project, workdir, problems)

    print("")
    if problems:
        print("=== FAIL ===")
        for item in problems:
            print("  - %s" % item)
        return 1
    print("=== PASS ===")
    print("  the rendered WAV's LIST/INFO/ISFT tag credits this build, and the check "
          "rejects the pre-fix tag")
    return 0


if __name__ == "__main__":
    sys.exit(main())
