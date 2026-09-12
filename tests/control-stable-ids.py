#!/usr/bin/env python3
"""Live acceptance evidence for SPEC-stable-ids.md slice 1 (persistent trk-<n>).

Drives the REAL binary over the control socket through the merged harness and
prints the raw request/response transcript for each claim, in order:

  1. add-track -> project.save: the id track.add answered with, and the file it
     was written to (`id` on the track element, `next-id` on the root).
  2. a FRESH instance loads that file and addresses the track BY THE ID IT WAS
     GIVEN, with no positional guess and no renumbering.
  3. two loads of an id-less LEGACY project yield the SAME ids (deterministic
     assignment on load), and the legacy file itself is not written.
  4. the same legacy file loaded by our build reports the one-time upgrade
     (`ids_assigned`, `format_upgraded`) instead of doing it silently.

Usage: QT_QPA_PLATFORM=offscreen python3 control-stable-ids.py <lmms-binary>
Exit code 0 only when every assertion passed.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    Client, Instance, PING_TIMEOUT, READY_TIMEOUT, Transcript, connect, fail, ok,
    ok_result, start_instance, wait_ready,
)

USAGE = __doc__

LEGACY_PROJECT = """<?xml version="1.0"?>
<lmms-project version="1.0" type="song" creator="LMMS" creatorversion="1.2.0">
  <head/>
  <song>
    <trackcontainer type="song">
      <track type="0" name="A" muted="0">
        <instrumenttrack/>
      </track>
      <track type="0" name="B" muted="0">
        <instrumenttrack/>
      </track>
      <track type="0" name="C" muted="0">
        <instrumenttrack/>
      </track>
    </trackcontainer>
  </song>
</lmms-project>
"""


def open_instance(binary, transcript, label):
    instance = start_instance(binary)
    client = connect(instance)
    wait_ready(instance, client, transcript, seconds=READY_TIMEOUT, ping_timeout=PING_TIMEOUT)
    print("\n---- %s ----" % label)
    return instance, client


def quit_instance(instance, client, transcript, request_id):
    try:
        ok_result(client.call(request_id, "control.quit", {"save": False},
                              timeout=PING_TIMEOUT, transcript=transcript), request_id)
    except Exception as error:  # noqa: BLE001 - the reply may not arrive before exit
        print("control.quit: %s" % error)
    instance.close()


def track_ids(client, transcript, request_id):
    result = ok_result(client.call(request_id, "track.list", timeout=PING_TIMEOUT,
                                   transcript=transcript), request_id)
    return [entry.get("id") for entry in result.get("tracks", [])]


def main():
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print(USAGE)
        return 2

    tmp = tempfile.mkdtemp(prefix="zids-", dir="/tmp")
    failures = []
    try:
        saved = os.path.join(tmp, "with-ids.mmp")
        legacy = os.path.join(tmp, "legacy.mmp")
        with open(legacy, "w") as handle:
            handle.write(LEGACY_PROJECT)
        with open(legacy, "rb") as handle:
            legacy_bytes = handle.read()

        # ---- 1. add-track -> project.save -----------------------------------
        transcript = Transcript()
        instance, client = open_instance(binary, transcript, "instance 1: create and save")
        added = ok_result(client.call(1, "track.add", {"type": "instrument"},
                                      timeout=PING_TIMEOUT, transcript=transcript), 1)
        track_id = added.get("track")
        print("track.add answered: track=%s index=%s" % (track_id, added.get("index")))
        if not track_id or not str(track_id).startswith("trk-"):
            fail("track.add did not answer a trk-<n> id: %r" % (added,), instance, transcript)

        saved_reply = ok_result(client.call(2, "project.save", {"path": saved},
                                            timeout=PING_TIMEOUT, transcript=transcript), 2)
        print("project.save answered: %r" % (saved_reply,))
        quit_instance(instance, client, transcript, 3)
        transcript.dump()

        with open(saved, "r") as handle:
            saved_xml = handle.read()
        number = str(track_id).split("-", 1)[1]
        if not any('id="%s"' % number in line for line in saved_xml.splitlines()):
            failures.append("the saved file does not carry id=\"%s\" on a track element"
                            % number)
        if "next-id=" not in saved_xml:
            failures.append("the saved file does not carry next-id on the root")

        # ---- 2. a fresh instance addresses the track by that id -------------
        transcript = Transcript()
        instance, client = open_instance(binary, transcript, "instance 2: fresh load, address by id")
        opened = ok_result(client.call(1, "project.open", {"path": saved},
                                       timeout=PING_TIMEOUT, transcript=transcript), 1)
        print("project.open: ids_assigned=%s format_upgraded=%s"
              % (opened.get("ids_assigned"), opened.get("format_upgraded")))
        if opened.get("ids_assigned") != 0:
            failures.append("a file written by this build still needed id assignment: %r"
                            % opened.get("ids_assigned"))
        if opened.get("format_upgraded") is not False:
            failures.append("a file written by this build reported an upgrade")

        state = ok_result(client.call(2, "track.get_state", {"track": track_id},
                                      timeout=PING_TIMEOUT, transcript=transcript), 2)
        print("track.get_state %s answered id=%s index=%s name=%r"
              % (track_id, state.get("id"), state.get("index"), state.get("name")))
        if state.get("id") != track_id:
            failures.append("the id changed across save+load: %r -> %r"
                            % (track_id, state.get("id")))
        ids_after = track_ids(client, transcript, 3)
        if track_id not in ids_after:
            failures.append("%s is not in track.list after the reload: %r" % (track_id, ids_after))
        quit_instance(instance, client, transcript, 4)
        transcript.dump()

        # ---- 3 + 4. two loads of an id-less legacy project ------------------
        loads = []
        for index, request_id in enumerate((1, 2)):
            transcript = Transcript()
            instance, client = open_instance(
                binary, transcript, "instance %d: load the id-less legacy project" % (index + 3))
            opened = ok_result(client.call(1, "project.open", {"path": legacy},
                                           timeout=PING_TIMEOUT, transcript=transcript), 1)
            print("legacy load %d: track_count=%s ids_assigned=%s format_upgraded=%s"
                  % (index + 1, opened.get("track_count"), opened.get("ids_assigned"),
                     opened.get("format_upgraded")))
            if opened.get("format_upgraded") is not True:
                failures.append("the legacy load did not report the one-time id upgrade")
            if opened.get("ids_assigned") != 3:
                failures.append("the legacy load assigned %r ids, expected 3"
                                % opened.get("ids_assigned"))
            loads.append(track_ids(client, transcript, 2))
            quit_instance(instance, client, transcript, 3)
            transcript.dump()

        print("\nlegacy load 1 ids: %r\nlegacy load 2 ids: %r" % (loads[0], loads[1]))
        if loads[0] != loads[1]:
            failures.append("two loads of the same id-less project gave different ids: %r vs %r"
                            % (loads[0], loads[1]))
        if loads[0] != ["trk-0", "trk-1", "trk-2"]:
            failures.append("legacy ids are not assigned in document order: %r" % (loads[0],))
        with open(legacy, "rb") as handle:
            if handle.read() != legacy_bytes:
                failures.append("the load WROTE to the legacy file (it must be in-memory only)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("\n=== FAIL ===")
        for item in failures:
            print("  - %s" % item)
        return 1
    ok("stable ids: creation-assigned, persisted, deterministic on load, reported on upgrade")
    return 0


if __name__ == "__main__":
    sys.exit(main())
