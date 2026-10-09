#!/usr/bin/env python3
"""Q050-007: the opt-in v2 project container, driven over the control socket of
the REAL binary.

What this pins, each against the bytes on disk and never against the reply alone:

  * an ordinary `project.save` (no `format`, no setting) still writes the legacy
    single-blob `.mmpz` - a qCompress frame, not a ZIP - and says so;
  * `project.save {format: "v2"}` writes a ZIP container, the reply names the
    format and carries the compatibility note (a larger file, older builds cannot
    open it), and the file really IS larger than the legacy one;
  * `project.open` takes both files with no hint and lands on the same project
    (track names, order, tempo), and `project.get_state` names the file;
  * the global opt-in `app/savecontainerv2` (settings.set) turns a BARE save of
    a `.mmpz` into a container, leaves a `.mmp` plain, and an explicit
    `format: "legacy"` still wins over it; switching it off puts the default back;
  * `format: "v2"` for a path that is not `.mmpz` is refused, writes no file.

The container is recognised by its leading ZIP local-header magic (PK\\x03\\x04)
and the legacy blob by qCompress's 4-byte length followed by a zlib header - the
same content test the loader applies (never the extension).

Usage: QT_QPA_PLATFORM=offscreen python3 control-container-v2.py <zene>
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import control_socket_harness as H  # noqa: E402

ZIP_MAGIC = b"PK\x03\x04"
SETTING = "app/savecontainerv2"


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


def plugin_env(binary):
    plugin_dir = os.path.join(os.path.dirname(os.path.abspath(binary)), "plugins")
    return {"LMMS_PLUGIN_DIR": plugin_dir} if os.path.isdir(plugin_dir) else None


def head(path, count=8):
    with open(path, "rb") as handle:
        return handle.read(count)


def is_container(path):
    return head(path, 4) == ZIP_MAGIC


def is_legacy_blob(path):
    """qCompress: a 4-byte big-endian length, then a zlib stream (0x78 header)."""
    first = head(path, 8)
    return len(first) == 8 and first[:4] != ZIP_MAGIC and first[4] == 0x78


def is_plain_xml(path):
    return head(path, 5).lstrip().startswith(b"<")


def project_shape(session):
    tracks = session.ok("track.list")
    names = [t.get("name") for t in tracks.get("tracks", [])]
    return {"tracks": names, "tempo": session.ok("transport.get_state").get("tempo")}


def build_the_project(session):
    session.ok("transport.set_tempo", {"bpm": 133})
    for name in ("Drums", "Loops", "Vox"):
        session.ok("track.add", {"type": "sample", "name": name})


def check_the_default_is_legacy(session, tmp, problems):
    path = os.path.join(tmp, "plain.mmpz")
    saved = session.ok("project.save", {"path": path})
    if not is_legacy_blob(path):
        problems.add("an ordinary project.save did not write the legacy qCompress blob: %r" % head(path))
    if saved.get("format") != "legacy":
        problems.add("an ordinary project.save reported format %r, not 'legacy'" % saved.get("format"))
    if "format_note" in saved:
        problems.add("an ordinary project.save carried the v2 compatibility note: %r" % saved)
    return path


def check_the_opt_in(session, tmp, legacy_path, shape, problems):
    v2_path = os.path.join(tmp, "container.mmpz")
    saved = session.ok("project.save", {"path": v2_path, "format": "v2"})
    if not is_container(v2_path):
        problems.add("project.save {format: v2} did not write a ZIP container: %r" % head(v2_path))
        return v2_path
    if saved.get("format") != "v2":
        problems.add("the v2 save reported format %r" % saved.get("format"))
    note = saved.get("format_note", "")
    if "larger" not in note or "older" not in note:
        problems.add("the v2 save did not carry the larger-file / older-build note: %r" % note)
    if os.path.getsize(v2_path) <= os.path.getsize(legacy_path):
        problems.add("the container (%d bytes) is not larger than the legacy file (%d bytes), "
                     "so the note's claim is false"
                     % (os.path.getsize(v2_path), os.path.getsize(legacy_path)))
    if saved.get("bytes") != os.path.getsize(v2_path):
        problems.add("the reply's bytes %r differ from the file's %d" % (saved.get("bytes"), os.path.getsize(v2_path)))
    return v2_path


def check_open_detects_both(session, paths, shape, problems):
    for path in paths:
        session.ok("project.new")
        opened = session.ok("project.open", {"path": path})
        if opened.get("file") != path:
            problems.add("project.open of %s named %r" % (path, opened.get("file")))
        if project_shape(session) != shape:
            problems.add("project.open of %s landed on %r, not %r" % (path, project_shape(session), shape))


def check_the_global_setting(session, tmp, problems):
    session.ok("settings.set", {"key": SETTING, "value": "1"})
    mmpz = os.path.join(tmp, "global.mmpz")
    saved = session.ok("project.save", {"path": mmpz})
    if not is_container(mmpz) or saved.get("format") != "v2":
        problems.add("with %s=1 a bare .mmpz save was not a container: %r" % (SETTING, head(mmpz)))
    xml = os.path.join(tmp, "global.mmp")
    session.ok("project.save", {"path": xml})
    if not is_plain_xml(xml):
        problems.add("with the setting on, a .mmp save stopped being plain XML: %r" % head(xml))
    forced = os.path.join(tmp, "forced.mmpz")
    session.ok("project.save", {"path": forced, "format": "legacy"})
    if not is_legacy_blob(forced):
        problems.add("format 'legacy' did not beat the global setting: %r" % head(forced))
    session.ok("settings.set", {"key": SETTING, "value": "0"})
    session.ok("project.new")
    off = os.path.join(tmp, "off.mmpz")
    session.ok("project.save", {"path": off})
    if not is_legacy_blob(off):
        problems.add("with the setting off again, a bare save was not legacy: %r" % head(off))


def check_the_refusals(session, tmp, problems):
    bad = os.path.join(tmp, "not-container.mmp")
    reply = session.call("project.save", {"path": bad, "format": "v2"})
    if reply.get("ok"):
        problems.add("format v2 for a .mmp path was accepted: %r" % reply)
    if os.path.exists(bad):
        problems.add("the refused v2 save left %s behind" % bad)
    reply = session.call("project.save", {"path": os.path.join(tmp, "x.mmpz"), "format": "zip"})
    if reply.get("ok"):
        problems.add("an unknown format was accepted: %r" % reply)


def check_the_instance(binary, problems):
    transcript = H.Transcript()
    with H.start_instance(binary, workingdir=None, extra_env=plugin_env(binary)) as instance:
        H.wait_for_socket(instance)
        client = H.connect(instance)
        H.wait_ready(instance, client, transcript)
        print("instance: %s" % binary)
        session = Session(client, transcript)
        session.ok("control.version")
        build_the_project(session)
        shape = project_shape(session)
        if len(shape["tracks"]) < 3:
            problems.add("the fixture project has %r tracks" % shape["tracks"])
        legacy_path = check_the_default_is_legacy(session, instance.tmp, problems)
        v2_path = check_the_opt_in(session, instance.tmp, legacy_path, shape, problems)
        check_open_detects_both(session, [legacy_path, v2_path], shape, problems)
        # The project is the container now: restore the fixture state for the rest.
        session.ok("project.new")
        build_the_project(session)
        check_the_global_setting(session, instance.tmp, problems)
        build_the_project(session)
        check_the_refusals(session, instance.tmp, problems)
        session.ok("settings.set", {"key": SETTING, "value": "0"})
    transcript.dump()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = H.Problems()
    check_the_instance(argv[1], problems)
    return H.finish([("the opt-in v2 container over project.save / project.open", not problems, problems.items)])


if __name__ == "__main__":
    sys.exit(main(sys.argv))
