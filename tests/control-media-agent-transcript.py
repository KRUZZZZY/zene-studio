#!/usr/bin/env python3
"""Live agent-surface evidence for the L11 media cluster (Q050-121).

Drives the REAL binary over the control socket through the shared harness and
prints the raw request/response transcript for each claim, in order:

  A. `project.media_usage` over a fixture project: one row per file, a usage
     count that adds every clip and instrument reference, the unused local file
     in `media/`, and the missing / external references kept distinct.
  B. `project.media_cleanup`: a dry run names the selection and writes nothing,
     a real removal without `confirm` is refused, and with `confirm` it removes
     the still-unused file while the referenced and external files survive.

Usage: QT_QPA_PLATFORM=offscreen python3 control-media-agent-transcript.py <lmms-binary>
Exit code 0 only when every assertion passed.
"""

import os
import shutil
import sys
import tempfile
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_harness import (  # noqa: E402
    Client, PING_TIMEOUT, READY_TIMEOUT, Transcript, connect, ok_result,
    start_instance, typed_error, wait_ready,
)

USAGE = __doc__

PROJECT = """<?xml version="1.0"?>
<zene-project creator="Zene Studio" version="1.0" type="song" creatorversion="0.5.0">
  <head bpm="120" timesig_numerator="4" timesig_denominator="4"/>
  <song><trackcontainer type="song"><track name="Audio" type="0">
    <instrumenttrack><instrument name="audiofileprocessor">
      <audiofileprocessor src="{used}" amp="100"/></instrument>
      <sampleclip pos="0" len="192" src="{used}"/>
      <sampleclip pos="192" len="192" src="{used}"/>
      <sampleclip pos="384" len="192" src="{missing}"/>
      <sampleclip pos="576" len="192" src="{external}"/>
    </instrumenttrack>
  </track></trackcontainer></song>
</zene-project>
"""


def write_wave(path, channels=2, rate=48000, frames=4800):
    with wave.open(path, "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(b"\x00\x00" * channels * frames)


class TranscriptTest:
    def __init__(self, client, transcript):
        self.client = client
        self.transcript = transcript
        self.next_id = 100
        self.problems = []

    def call(self, command, arguments=None, expected=1):
        self.next_id += 1
        return ok_result(self.client.call(self.next_id, command, arguments or {},
            timeout=PING_TIMEOUT, transcript=self.transcript), self.next_id)

    def call_error(self, command, arguments, kind):
        self.next_id += 1
        return typed_error(self.client.call(self.next_id, command, arguments,
            timeout=PING_TIMEOUT, transcript=self.transcript), self.next_id, kind)

    def require(self, condition, text):
        if not condition:
            self.problems.append(text)
        return bool(condition)


def media_usage(test, project, used, unused, external, missing):
    usage = test.call("project.media_usage", {"project": project})
    files = {entry["name"]: entry for entry in usage.get("files", [])}
    test.require(files.get("used.wav", {}).get("usage_count") == 3,
        "used.wav usage_count is %r, expected 3" % files.get("used.wav", {}).get("usage_count"))
    test.require(files.get("used.wav", {}).get("state") == "used", "used.wav is not 'used'")
    test.require(files.get("unused.wav", {}).get("state") == "unused", "unused.wav is not 'unused'")
    test.require(files.get("unused.wav", {}).get("cleanup_candidate") is True,
        "unused.wav is not a cleanup candidate")
    test.require(files.get("gone.wav", {}).get("state") == "missing", "gone.wav is not 'missing'")
    test.require(files.get("external.wav", {}).get("state") == "external", "external.wav is not 'external'")
    test.require(files.get("external.wav", {}).get("cleanup_candidate") is False,
        "external.wav is a cleanup candidate")
    test.require([entry["path"] for entry in usage.get("unused", [])] == [unused],
        "the unused list is %r" % [entry["path"] for entry in usage.get("unused", [])])
    return usage


def cleanup(test, project, unused, used, external):
    preview = test.call("project.media_cleanup",
        {"project": project, "paths": [unused], "dry_run": True})
    test.require([entry["path"] for entry in preview.get("removed", [])] == [unused],
        "the dry run did not name exactly %s" % unused)
    test.require(os.path.exists(unused), "the dry run removed the file")

    test.call_error("project.media_cleanup", {"project": project, "paths": [unused]}, "refused")
    test.require(os.path.exists(unused), "an unconfirmed cleanup removed the file")

    cleaned = test.call("project.media_cleanup",
        {"project": project, "paths": [unused, used, external], "confirm": True})
    test.require([entry["path"] for entry in cleaned.get("removed", [])] == [unused],
        "the confirmed cleanup did not remove exactly the unused file")
    test.require(len(cleaned.get("refused", [])) == 2, "the referenced/external paths were not refused")
    test.require(not os.path.exists(unused), "the confirmed cleanup left the unused file")
    test.require(os.path.exists(used) and os.path.exists(external),
        "the confirmed cleanup removed a referenced or external file")


def prepare_fixture(tmp):
    media = os.path.join(tmp, "media")
    os.makedirs(media)
    used = os.path.join(media, "used.wav")
    unused = os.path.join(media, "unused.wav")
    missing = os.path.join(media, "gone.wav")
    external = os.path.join(tmp, "external.wav")
    for path in (used, unused, external):
        write_wave(path)
    project = os.path.join(tmp, "song.mmp")
    with open(project, "w") as handle:
        handle.write(PROJECT.format(used=used, missing=missing, external=external))
    return {"project": project, "used": used, "unused": unused, "missing": missing,
            "external": external}


def run_claims(binary, tmp):
    fixture = prepare_fixture(tmp)
    transcript = Transcript()
    instance = start_instance(binary)
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript, seconds=READY_TIMEOUT, ping_timeout=PING_TIMEOUT)
        test = TranscriptTest(client, transcript)

        print("\n---- project.media_usage (an agent reads the inventory) ----")
        media_usage(test, fixture["project"], fixture["used"], fixture["unused"],
            fixture["external"], fixture["missing"])
        print("\n---- project.media_cleanup (preview, refuse, confirm) ----")
        cleanup(test, fixture["project"], fixture["unused"], fixture["used"], fixture["external"])
        try:
            ok_result(client.call(999, "control.quit", {"save": False}, timeout=PING_TIMEOUT,
                transcript=transcript), 999)
        except Exception:  # noqa: BLE001 - the reply need not arrive before exit
            pass
        transcript.dump()
        return 0 if not test.problems else report_failure(test.problems)
    except Exception as error:  # noqa: BLE001 - any failed claim is a failure, never a skip
        print("\nFAIL: %s" % error)
        transcript.dump()
        instance.dump_log()
        return 1
    finally:
        instance.close()


def main():
    if len(sys.argv) < 2:
        print(USAGE)
        return 2
    binary = os.path.abspath(sys.argv[1])
    if not os.path.exists(binary):
        print(USAGE)
        return 2
    tmp = tempfile.mkdtemp(prefix="zmedia-", dir="/tmp")
    try:
        return run_claims(binary, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def report_failure(problems):
    print("\nFAIL:")
    for item in problems:
        print("  - %s" % item)
    return 1


if __name__ == "__main__":
    sys.exit(main())
