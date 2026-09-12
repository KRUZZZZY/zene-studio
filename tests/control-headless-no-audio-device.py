#!/usr/bin/env python3
"""BLOCKER 3 (task #625): 'audio device setup failed' must not block a headless run.

The defect: `MainWindow::finalize()` ends with

    else if( Engine::audioEngine()->audioDevStartFailed() ||
             !AudioEngine::isAudioDevNameValid( ConfigManager::inst()->value(
                 "audioengine", "audiodev" ) ) )
    {
        QMessageBox::critical(nullptr, "Audio device setup failed", ...);
        SetupDialog sd( SetupDialog::ConfigTab::AudioSettings );
        sd.exec();
    }

Both branches are modal, and they run BEFORE `app->exec()`: with an audio device
that cannot be opened, the instance never becomes ready - it sits in the box's
nested event loop, answers `control.ping` with engine_ready=false forever, and
nothing can dismiss it (measured on the base commit 6b01b98eb).

What this build does instead (the choice, stated): PROCEED on the fallback, and
report it.  `AudioEngine::tryAudioDevices()` already falls back to `AudioDummy`
and sets `m_audioDevStartFailed`, so an instance with no usable device is still
addressable and render/edit/save all work; refusing would be a lie about what
the engine can do.  The one thing an agent cannot hear is reported on stderr as
a typed line naming the configured backend, whether it failed to start, whether
the name is even a known backend, and which device the engine ended up on:

    MainWindow: audio-device-setup backend="..." device_start_failed=0|1 \
        name_known=0|1 device="..." (unattended run: no dialog, the engine continues)

Two scenarios are driven, one per condition of that `else if`, so neither half
can regress silently:

  1. "SDL (Simple DirectMedia Layer)" - a real, valid backend name that cannot
     open a device here (the SDL/ALSA probe prints "Host is down"), so
     `device_start_failed` is true and the engine must be on the DUMMY device;
  2. a name that matches no AudioDevice subclass, so `isAudioDevNameValid` is
     false and the branch is taken for that reason instead.

`--expect-blocked` is the pre-fix reproduction: it proves with the SAME binary
that a healthy configuration becomes ready in seconds, then asserts that each
scenario never becomes ready inside the bound, that the process is alive, and
that the typed line is absent.  On a fixed build that mode fails - the control.

Usage:
  QT_QPA_PLATFORM=offscreen python3 control-headless-no-audio-device.py \
      <lmms> [--expect-blocked]
Exit code 0 only when every assertion of the selected mode passed.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from headless_load_harness import (  # noqa: E402
    DUMMY_DEVICE, PING_TIMEOUT, READY_TIMEOUT, Blocked, Transcript, connect, diagnose_block, fail,
    healthy_control, ok, parse_args, report_pre_fix, start_instance, wait_ready,
)

USAGE = __doc__

SDL_BACKEND = "SDL (Simple DirectMedia Layer)"
BOGUS_BACKEND = "No Such Audio Backend (task #625)"
AUDIO_MARKER = "audio-device-setup"
PREFIX_BOUND = 25.0

SCENARIOS = (
    ("configured backend that cannot open a device", SDL_BACKEND),
    ("backend name that is not a known backend", BOGUS_BACKEND),
)


def audio_line(log):
    for line in log.splitlines():
        if AUDIO_MARKER in line:
            return line
    return None


def check_scenario_report(line, backend, must_be_dummy):
    if backend not in line:
        raise AssertionError("the report does not name the configured backend: %r" % line)
    if 'device=""' in line or 'device="' not in line:
        raise AssertionError("the report does not name the device the engine is using: %r" % line)
    if "device_start_failed=1" in line and "dummy" not in line.lower():
        raise AssertionError("the configured device failed to start but the engine is not on the "
                             "dummy fallback: %r" % line)
    if must_be_dummy:
        check_dummy_fallback(line, backend)


def check_dummy_fallback(line, backend):
    if "device_start_failed=1" not in line:
        raise AssertionError("expected the %r backend to fail to open and the engine to fall back, "
                             "but the report says otherwise: %r" % (backend, line))
    if DUMMY_DEVICE not in line:
        raise AssertionError("the %r backend could not open, so the engine must be on %r: %r"
                             % (backend, DUMMY_DEVICE, line))


def expect_blocked(binary):
    healthy = healthy_control(binary)
    print("control: the same binary with the dummy device became ready in %.1fs" % healthy)
    for label, backend in SCENARIOS:
        instance = start_instance(binary, audiodev=backend)
        transcript = Transcript()
        try:
            client = connect(instance)
            print("--- %s: backend %r, waiting for the instance to become ready" % (label, backend))
            try:
                wait_ready(instance, client, transcript, seconds=PREFIX_BOUND,
                           ping_timeout=PING_TIMEOUT)
            except Blocked:
                pass
            else:
                fail("expected the 'Audio device setup failed' modal to block the instance for "
                     "backend %r, but it became ready" % backend, instance, transcript)

            if not instance.alive():
                fail("the instance died instead of hanging (exit %s)"
                     % instance.process.returncode, instance, transcript)
            if audio_line(instance.read_log()) is not None:
                fail("the run reported the audio fallback without a dialog on a build that still "
                     "has the modal; this is not the pre-fix behaviour", instance, transcript)

            sentence = diagnose_block(instance, client, transcript)
            report_pre_fix("with backend %r (%s) the instance never became ready within %.0fs "
                           "while the 'Audio device setup failed' modal was up; %s"
                           % (backend, label, PREFIX_BOUND, sentence))
        finally:
            instance.kill()
    return 0


def expect_fixed(binary):
    for label, backend in SCENARIOS:
        instance = start_instance(binary, audiodev=backend)
        transcript = Transcript()
        try:
            try:
                client = connect(instance)
                wait_ready(instance, client, transcript, seconds=READY_TIMEOUT,
                           ping_timeout=PING_TIMEOUT)
            except Blocked as error:
                fail("the no-usable-audio-device case (%s: %r) blocked the instance: %s"
                     % (label, backend, error), instance, transcript)

            line = audio_line(instance.read_log())
            if line is None:
                fail("the instance proceeded but never said so: no '%s' line in the app log"
                     % AUDIO_MARKER, instance, transcript)
            try:
                check_scenario_report(line, backend, must_be_dummy=(backend == SDL_BACKEND))
            except AssertionError as error:
                fail(str(error), instance, transcript)

            print("\n---- audio report (%s) ----" % label)
            print(line)
            transcript.dump()
            ok("this test passed for %s: no dialog, the instance proceeded and reported it" % label)
        finally:
            instance.kill()
    return 0


def main():
    expect_blocked_mode, argv = parse_args(USAGE, minimum_argv=1)
    binary = os.path.abspath(argv[0])
    if not os.path.exists(binary):
        print("FAIL: no such file: %s" % binary)
        return 1
    if expect_blocked_mode:
        return expect_blocked(binary)
    return expect_fixed(binary)


if __name__ == "__main__":
    sys.exit(main())
