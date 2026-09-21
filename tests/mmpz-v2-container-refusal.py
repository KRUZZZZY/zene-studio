#!/usr/bin/env python3
"""The `.mmpz` verbs on the command line tell the two container formats apart by
CONTENT, and refuse a v2 container BY NAME rather than answering it wrongly.

Reproduced on this branch (commit 3ad404e98, 2026-09-21), with a v2 container
built by hand - a STORE ZIP whose `project.xml` entry holds a real document:

    $ ./build/zene --dump v2.mmpz            # 1 byte of stdout, exit 0
    $ ./build/zene --compress v2.mmpz > out  # a qCompress frame WRAPPING THE ZIP
    $ python3 -c "import zlib; print(zlib.decompress(open('out','rb').read()[4:])[:4])"
    b'PK\\x03\\x04'

Neither answer is a refusal. `--dump` passed qUncompress()'s empty string to
printf, so a caller got a bare newline AND `EXIT_SUCCESS` - a script reading that
stdout sees an empty document and a zero exit, which is the one combination it
cannot detect. `--compress` is worse: it is a WRITER, and it took a container and
produced a file that is neither format - v1's framing around v2's bytes, which
nothing in the tree reads back. Both are silent, and the second is lossy.

What the fix pins, and what this test exists to keep pinned:
  * the discriminator is the file's own bytes (projectcontainer::isContainer,
    SPEC-ARCH-4 risk 5: never the extension). A test built on the extension would
    pass while `nameWithExtension` could still make one name mean two formats;
  * a v2 container is refused on stderr, by name, with a non-zero exit, and
    `--compress` writes NOTHING to stdout when it refuses - a partial write on a
    refusal is how a caller ends up with half a file;
  * the shapes that are NOT v2 are untouched: `--dump` still prints a v1
    document, and `--compress` still frames an `.mmp`. Those two cases are the
    controls, and a guard that refused everything would fail them - which is the
    only thing that makes this suite evidence rather than a description.

The v2 fixture is written by Python's own `zipfile` in STORE mode rather than by
the product's writer, on purpose: this test is about what the CLI does with ANY
container it is handed, so it must not be able to pass by agreeing with one
particular writer's layout. `isContainer` matches the ZIP local-header magic,
which every writer emits.

Usage:
  QT_QPA_PLATFORM=offscreen python3 mmpz-v2-container-refusal.py <zene>
Exit code 0 only when every case passed.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

# The document the fixtures are built from: a real, minimal project file.
DOCUMENT = (b'<?xml version="1.0"?>\n'
            b'<multimedia-project version="31" type="song">\n'
            b'\t<head><bpm>140</bpm></head>\n'
            b'\t<song><track type="instrument" name="lead"/></song>\n'
            b'</multimedia-project>\n')

# A CLI verb returns before the engine comes up (measured: --dump on a v1 file
# emits no plugin-scan output), so this bound is not a startup budget - it is the
# bound that keeps a HUNG or silently-waiting binary from hanging the suite.
VERB_TIMEOUT = 60.0


def qcompress(xml):
    """Byte-identical to Qt's qCompress: a big-endian length, then zlib."""
    return struct.pack(">I", len(xml)) + zlib.compress(xml, 6)


def quncompress(blob):
    """The inverse, so a case can prove the OUTPUT is a v1 container."""
    declared = struct.unpack(">I", blob[:4])[0]
    body = zlib.decompress(blob[4:])
    if len(body) != declared:
        raise ValueError("declared %d != inflated %d" % (declared, len(body)))
    return body


def write_v2(path, entries):
    """A STORE v2 container: the ZIP shape, with no compression on any entry."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED) as archive:
        for name, body in entries:
            archive.writestr(name, body)
    return open(path, "rb").read()


def run(zene, args, env):
    """One verb, unpiped: the exit code is the binary's, never a reader's."""
    return subprocess.run([zene] + args, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=VERB_TIMEOUT, env=env)


def check(cases, name, condition, detail):
    """Record one case's verdict. Every case reports, passing or not."""
    cases.append((name, bool(condition), detail))


def fixture_cases(v2_bytes, cases):
    """The fixture has to BE a v2 container, and one whose bytes would fool a text
    probe: a STORE container holds its skeleton verbatim. Both are asserted,
    because a fixture that was secretly v1 would make every refusal below pass for
    the wrong reason."""
    check(cases, "fixture is a ZIP container", v2_bytes[:4] == b"PK\x03\x04",
          "first four bytes are %r" % v2_bytes[:4])
    check(cases, "fixture holds its skeleton verbatim", b"<?xml" in v2_bytes,
          "%d bytes" % len(v2_bytes))


def dump_cases(zene, v1, v2, env, cases):
    """`--dump`: the verb that printed a bare newline and called it success."""
    control = run(zene, ["--dump", v1], env)
    check(cases, "CONTROL: --dump still prints a v1 document",
          control.returncode == 0 and control.stdout == DOCUMENT + b"\n",
          "exit %d, stdout %d bytes" % (control.returncode, len(control.stdout)))

    refused = run(zene, ["--dump", v2], env)
    check(cases, "--dump refuses a v2 container", refused.returncode != 0,
          "exit %d" % refused.returncode)
    check(cases, "--dump writes no document when it refuses", refused.stdout == b"",
          "stdout is %d bytes: %r" % (len(refused.stdout), refused.stdout[:24]))
    check(cases, "--dump names the container it refused", b"v2 container" in refused.stderr,
          "stderr: %r" % refused.stderr[:120])


def compress_cases(zene, plain, v2, env, cases):
    """`--compress`: the verb that is a WRITER, so its refusal must write nothing.

    Its contract is "frame THIS FILE", not "frame this XML": the output's inflated
    body is the input read back, so the control feeds it a `.mmp` and compares
    against the document. (Feeding it a v1 `.mmpz` would inflate to that blob
    again - which is why the control for `--dump`, not this one, uses v1.)"""
    control = run(zene, ["--compress", plain], env)
    framed = False
    if control.returncode == 0:
        try:
            framed = quncompress(control.stdout) == DOCUMENT
        except (ValueError, zlib.error) as exc:
            framed = "raise: %s" % exc
    check(cases, "CONTROL: --compress still frames an .mmp", framed is True,
          "exit %d, round-trip %s" % (control.returncode, framed))

    refused = run(zene, ["--compress", v2], env)
    check(cases, "--compress refuses a v2 container", refused.returncode != 0,
          "exit %d" % refused.returncode)
    check(cases, "--compress writes nothing when it refuses", refused.stdout == b"",
          "stdout is %d bytes" % len(refused.stdout))
    check(cases, "--compress names the container it refused",
          b"v2 container" in refused.stderr, "stderr: %r" % refused.stderr[:120])


def report(cases):
    failed = 0
    for name, passed, detail in cases:
        print("%-52s %s  (%s)" % (name, "PASS" if passed else "FAIL", detail))
        if not passed:
            failed += 1
    print()
    print("mmpz v2 CLI refusal: %d case(s), %d failed" % (len(cases), failed))
    return 1 if failed else 0


def main(argv):
    if len(argv) != 2:
        print("usage: %s <zene>" % argv[0], file=sys.stderr)
        return 2
    zene = argv[1]
    if not os.path.exists(zene):
        print("error: no binary at %s" % zene, file=sys.stderr)
        return 2

    cases = []
    scratch = tempfile.mkdtemp(prefix="mmpz-v2-cli.")
    # A scratch HOME and XDG pair: these verbs return before any of it is read,
    # but a verb that one day does not must not write the caller's settings.
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["HOME"] = scratch
    env["XDG_CONFIG_HOME"] = os.path.join(scratch, "config")
    env["XDG_DATA_HOME"] = os.path.join(scratch, "data")

    v1 = os.path.join(scratch, "v1.mmpz")
    open(v1, "wb").write(qcompress(DOCUMENT))
    plain = os.path.join(scratch, "plain.mmp")
    open(plain, "wb").write(DOCUMENT)
    v2 = os.path.join(scratch, "v2.mmpz")
    v2_bytes = write_v2(v2, [("project.xml", DOCUMENT),
                             ("sections/0000-track", b'<track type="instrument"/>')])

    fixture_cases(v2_bytes, cases)
    dump_cases(zene, v1, v2, env, cases)
    compress_cases(zene, plain, v2, env, cases)
    return report(cases)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
