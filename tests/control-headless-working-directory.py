#!/usr/bin/env python3
"""BLOCKER 2 (task #625): a non-existent working directory must not block.

The defect: `GuiApplication::GuiApplication()` asks
`QMessageBox::question(nullptr, "Working directory", "... %1 does not exist.
Create it now? ...")` when `ConfigManager::hasWorkingDir()` is false.  That is
BEFORE `app->exec()`, and in a `--control-socket` instance nobody can click it,
so the process never reaches its event loop.  Measured on the base commit
6b01b98eb: the box's nested event loop keeps dispatching socket events, so
`control.ping` answers `pong:true, engine_ready:false` for as long as the box is
up and every engine command is refused with the typed `busy` - the instance is
alive and useless, and it stays that way until someone clicks.

What this build does instead (the choice is stated here and in the audit):
CREATE THE DIRECTORY and say so on stderr.  The prompt's own default button is
"Yes"; `ConfigManager::createWorkingDir()` is exactly what that button does, and
the working directory is where the app keeps samples, presets and projects, so a
headless instance that refused to start over a missing directory would be
useless for a reason the modal itself proposes to fix.  If creation fails the
run reports a typed line naming the path (`working-directory-error path=...`)
instead of waiting on a click.

This test starts an instance whose config points `paths/workingdir` at a path
that does not exist and asserts, inside a bounded timeout:
  * the instance becomes ready and answers `control.ping` (no block);
  * the directory now exists on disk;
  * the app log names the directory and says it was created.

`--expect-blocked` is the pre-fix reproduction: it asserts the socket appears,
the instance never becomes ready inside the bound, the process is still alive,
and the creation line is absent.  On a fixed build that mode fails - the control.

Usage:
  QT_QPA_PLATFORM=offscreen python3 control-headless-working-directory.py \
      <lmms> [--expect-blocked]
Exit code 0 only when every assertion of the selected mode passed.
"""

import os
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from control_socket_flows import (  # noqa: E402
    diagnose_block, healthy_control, parse_args, report_pre_fix,
)
from control_socket_harness import (  # noqa: E402
    PING_TIMEOUT, READY_TIMEOUT, Blocked, Transcript, connect, fail, ok,
    start_instance, wait_ready,
)

USAGE = __doc__

# Pre-fix the block is total and immediate, so the reproduction does not need
# the full engine budget: a ping that never answers is the whole signal.
PREFIX_BOUND = 25.0

CREATED_MARKER = "working-directory"


def expect_blocked(binary, missing):
    healthy = healthy_control(binary)
    print("control: the same binary with an existing working directory became ready in %.1fs" % healthy)
    instance = start_instance(binary, workingdir=missing)
    transcript = Transcript()
    try:
        client = connect(instance)
        print("the socket is up; waiting for the instance to become ready")
        try:
            wait_ready(instance, client, transcript, seconds=PREFIX_BOUND, ping_timeout=PING_TIMEOUT)
        except Blocked:
            pass
        else:
            fail("expected the missing-working-directory prompt to block the instance, but it "
                 "became ready", instance, transcript)

        if not instance.alive():
            fail("the instance died instead of hanging (exit %s)" % instance.process.returncode,
                 instance, transcript)
        if CREATED_MARKER in instance.read_log():
            fail("the directory was created without a prompt on a build that still has the modal; "
                 "this is not the pre-fix behaviour", instance, transcript)

        sentence = diagnose_block(instance, client, transcript)
        report_pre_fix("the instance never became ready within %.0fs while the 'Working directory "
                       "%s does not exist. Create it now?' modal was up; %s"
                       % (PREFIX_BOUND, missing, sentence))
        transcript.dump()
        return 0
    finally:
        instance.close()


def expect_fixed(binary, missing):
    instance = start_instance(binary, workingdir=missing)
    transcript = Transcript()
    try:
        try:
            client = connect(instance)
            wait_ready(instance, client, transcript, seconds=READY_TIMEOUT, ping_timeout=PING_TIMEOUT)
        except Blocked as error:
            fail("the missing-working-directory case blocked the instance: %s" % error,
                 instance, transcript)

        if not os.path.isdir(missing):
            fail("the instance became ready but the working directory %s still does not exist"
                 % missing, instance, transcript)

        log = instance.read_log()
        named = missing.rstrip("/")
        if CREATED_MARKER not in log or named not in log:
            fail("nothing in the app log names the created directory (%s): a headless operator "
                 "cannot tell that it happened" % missing, instance, transcript)

        print("\n---- app log lines about the working directory ----")
        for line in log.splitlines():
            if CREATED_MARKER in line:
                print(line)

        transcript.dump()
        ok("this test passed (default mode): a missing working directory did not block the "
           "instance; it was created and reported")
        return 0
    finally:
        instance.close()


def main():
    expect_blocked_mode, argv = parse_args(USAGE, minimum_argv=1)
    binary = os.path.abspath(argv[0])
    if not os.path.exists(binary):
        print("FAIL: no such file: %s" % binary)
        return 1

    outer = tempfile.mkdtemp(prefix="z625-wd-", dir="/tmp")
    missing = os.path.join(outer, "workspace-that-does-not-exist")
    try:
        if expect_blocked_mode:
            return expect_blocked(binary, missing)
        return expect_fixed(binary, missing)
    finally:
        shutil.rmtree(outer, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
