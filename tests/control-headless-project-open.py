#!/usr/bin/env python3
"""BLOCKER 1 (task #625): `project.open` on an error-carrying project, headless.

The defect: `Song::loadProject` ends with a modal "LMMS Error report"
QMessageBox whenever the load collected errors, gated only on `getGUI() !=
nullptr` - and an offscreen control-socket instance HAS a GuiApplication, so
`getGUI()` is not nullptr and the box goes up.  Measured on the base commit
6b01b98eb: the request never returns (the UI thread is inside the box's nested
event loop) while `control.ping` still answers `engine_ready:true`.

This test starts a real instance with `--control-socket`, opens two projects
that genuinely carry load errors, and asserts:

  * `project.open` answers inside a bounded timeout (a HANG is a failure);
  * the typed result carries the PER-ITEM error list - every entry has the
    failing item and the reason (`{message, count}`), not just a summary;
  * the instance is still alive and still answers after the load.

The error-carrying projects (both documented in tests/data/README-error-fixtures.md):
  * the shipped tutorial project, which references `samples/shapes/smooth_inv_saw.ogg`
    from a TripleOscillator; PathUtil cannot resolve that bare old-style relative
    path in this tree, so TripleOscillator::loadSettings records
    "Sample not found: samples/shapes/smooth_inv_saw.ogg";
  * tests/data/error-carrying-project.mmp, the control fixture with two
    user wave files pointing at files that do not exist (two DISTINCT items, so
    a summary string cannot pass the assertion by accident).

`--expect-blocked` is the pre-fix reproduction: it asserts that the bounded
`project.open` DOES expire, that the process is still alive, and that a later
`control.ping` is still answered (`engine_ready:true`) - the UI thread is alive
inside the modal's nested event loop with the open request still on its stack.
On a fixed build that mode fails, which is what makes it a control rather than
a tautology.

Usage:
  QT_QPA_PLATFORM=offscreen python3 control-headless-project-open.py \
      <lmms> <tutorial.mmp> <error-carrying.mmp> [--expect-blocked]
Exit code 0 only when every assertion of the selected mode passed.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from headless_load_harness import (  # noqa: E402
    OPEN_TIMEOUT, PING_TIMEOUT, Blocked, Transcript, connect, fail, ok, ok_result,
    parse_args, report_pre_fix, start_instance, wait_ready,
)

USAGE = __doc__

TUTORIAL_ERROR_ITEM = "Sample not found"
TUTORIAL_ERROR_PATH = "smooth_inv_saw.ogg"
FIXTURE_ERROR_PATHS = ("samples/shapes/nosuch_wave_625.ogg", "nosuchdir/missing_sample_625.wav")


def error_items(result):
    """The typed per-item list; raises when the shape is not {message, count}[]."""
    items = result.get("errors")
    if not isinstance(items, list):
        raise AssertionError("the result carries no 'errors' list: %r" % (result,))
    for item in items:
        if not isinstance(item, dict) or "message" not in item or "count" not in item:
            raise AssertionError("an error entry is not {message, count}: %r" % (item,))
    return items


def messages(result):
    return [str(item.get("message")) for item in error_items(result)]


def open_project(client, request_id, path, transcript):
    """One bounded project.open; returns the reply or reports a clean FAIL."""
    try:
        return client.call(request_id, "project.open", {"path": path},
                           timeout=OPEN_TIMEOUT, transcript=transcript)
    except Blocked as error:
        fail("project.open on %s did not return inside the %.0fs bound: %s - a modal still owns "
             "the UI thread" % (os.path.basename(path), OPEN_TIMEOUT, error))
    return None


def check_tutorial(result):
    if not result.get("file"):
        raise AssertionError("project.open did not report the loaded file: %r" % (result,))
    if not result.get("loaded_with_errors"):
        raise AssertionError("the tutorial project loaded without errors; the fixture no longer "
                             "carries an error and this test would prove nothing: %r" % (result,))
    texts = messages(result)
    if not any(TUTORIAL_ERROR_ITEM in text and TUTORIAL_ERROR_PATH in text for text in texts):
        raise AssertionError("the typed error list does not name the failing sample: %r" % (texts,))
    return texts


def check_fixture(result):
    texts = messages(result)
    if int(result.get("error_count", len(texts))) != len(texts):
        raise AssertionError("error_count disagrees with the list: %r" % (result,))
    if len(texts) != len(FIXTURE_ERROR_PATHS):
        raise AssertionError("expected %d error items, got %d: %r"
                             % (len(FIXTURE_ERROR_PATHS), len(texts), texts))
    for path in FIXTURE_ERROR_PATHS:
        if not any(path in text for text in texts):
            raise AssertionError("no error item names %s: %r" % (path, texts))
    if len(set(texts)) != len(texts):
        raise AssertionError("the error list repeats itself instead of listing items: %r" % (texts,))
    return texts


def expect_blocked(binary, tutorial, fixture):
    """Pre-fix reproduction: the modal owns the UI thread and nothing answers."""
    instance = start_instance(binary, workingdir=None)
    transcript = Transcript()
    try:
        client = connect(instance)
        wait_ready(instance, client, transcript, ping_timeout=PING_TIMEOUT)
        print("the instance became ready; now opening the tutorial project, which carries load errors")

        try:
            reply = client.call(1, "project.open", {"path": tutorial},
                                timeout=OPEN_TIMEOUT, transcript=transcript)
        except Blocked:
            pass
        else:
            fail("expected `project.open` to hang in the modal error report, but it answered: %r"
                 % (reply,), instance, transcript)

        if not instance.alive():
            fail("the instance died instead of hanging (exit %s)" % instance.process.returncode,
                 instance, transcript)

        sentence = stalled_open_diagnosis(client, transcript)
        report_pre_fix("project.open never returned inside %.0fs on the error-carrying project "
                       "(%s): %s" % (OPEN_TIMEOUT, os.path.basename(tutorial), sentence))
        transcript.dump()
        return 0
    finally:
        instance.kill()


def stalled_open_diagnosis(client, transcript):
    """Say which pre-fix signature the stall shows (measured on 6b01b98eb)."""
    try:
        ping = client.call(2, "control.ping", timeout=PING_TIMEOUT, transcript=transcript)
    except Blocked:
        return "and the socket stopped answering too (nothing is dispatching events)"
    result = ping.get("result") or {}
    if not result.get("pong"):
        raise AssertionError("control.ping answered without pong: %r" % (ping,))
    return ("while a later control.ping still answers pong=true engine_ready=%s, i.e. the UI "
            "thread is alive inside the modal's nested event loop and the open request never "
            "completes" % str(result.get("engine_ready")).lower())


def expect_fixed(binary, tutorial, fixture):
    instance = start_instance(binary, workingdir=None)
    transcript = Transcript()
    try:
        try:
            client = connect(instance)
            wait_ready(instance, client, transcript, ping_timeout=PING_TIMEOUT)
        except Blocked as error:
            fail("the instance never became ready: %s" % error, instance, transcript)

        tutorial_result = ok_result(open_project(client, 1, tutorial, transcript), 1)
        print("tutorial project: %d load error item(s): %s"
              % (len(messages(tutorial_result)), check_tutorial(tutorial_result)))

        fixture_result = ok_result(open_project(client, 2, fixture, transcript), 2)
        print("fixture project: %d distinct load error item(s): %s"
              % (len(messages(fixture_result)), check_fixture(fixture_result)))

        check_instance_survived(instance, client, transcript)
        transcript.dump()
        print("\n---- app log tail ----")
        print(instance.read_log()[-4000:])
        ok("this test passed (default mode): project.open returned a typed per-item error list "
           "inside the %.0fs bound" % OPEN_TIMEOUT)
        return 0
    finally:
        instance.kill()


def check_instance_survived(instance, client, transcript):
    if not instance.alive():
        fail("the instance died during project.open", instance, transcript)
    state = ok_result(client.call(3, "project.get_state", timeout=OPEN_TIMEOUT,
                                  transcript=transcript), 3)
    if not state.get("file"):
        fail("project.get_state lost the project file: %r" % (state,), instance, transcript)
    ping = client.call(4, "control.ping", timeout=PING_TIMEOUT, transcript=transcript)
    if not (ping.get("result") or {}).get("engine_ready"):
        fail("the instance stopped being ready after the load: %r" % (ping,), instance, transcript)
    if TUTORIAL_ERROR_ITEM not in instance.read_log():
        fail("the load errors are not reported anywhere a headless operator can read them "
             "(no '%s' in the app log)" % TUTORIAL_ERROR_ITEM, instance, transcript)


def main():
    expect_blocked_mode, argv = parse_args(USAGE, minimum_argv=3)
    binary = os.path.abspath(argv[0])
    tutorial = os.path.abspath(argv[1])
    fixture = os.path.abspath(argv[2])
    for path in (binary, tutorial, fixture):
        if not os.path.exists(path):
            print("FAIL: no such file: %s" % path)
            return 1
    if expect_blocked_mode:
        return expect_blocked(binary, tutorial, fixture)
    return expect_fixed(binary, tutorial, fixture)


if __name__ == "__main__":
    sys.exit(main())
