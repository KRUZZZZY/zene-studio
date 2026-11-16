IMPLEMENTED

## Q050-183.1 - show the running audio backend and play a test tone (stage 1/2)

## Acceptance

| criterion | test | what it proves | exit, log |
|---|---|---|---|
| C1: Audio Settings distinguishes the running backend or fallback from the configured one and shows the failure reason | AudioOutputStatusBoxTest: the real SetupDialog's Running output label, plus the describe() cases for a fallback with a reason, a fallback with no reason, and nothing configured | the Running output label and describe() report the running backend, the fallback state and the failure reason | exit 0; r1 033 |
| C1 | ControlNoAudioDevice: with a broken backend, requested, fallback, start_failed and failure_reason are set | the agent-side device state reports the broken backend and its failure | exit 0; r1 033, and r2 001 (run again) |
| C1 | ControlAudioTestTone step a: on a normal start, fallback is false and failure_reason is empty | a normal start is not reported as a fallback | exit 0; r1 033 |
| C2: a short tone plays and stops through the active output without changing project or transport | AudioTestToneTest: a real renderNextPeriod peak above 0.01 while playing and below 1e-4 after stop; isPlaying, play position, isModified and undo depth unchanged | the tone is audible while playing, silent after stop, and leaves the project, transport and undo history unchanged | exit 0; r1 033 |
| C2 | AudioOutputStatusBoxTest: clicks Play test tone and Stop | the visible GUI buttons start and stop the tone | exit 0; r1 033 |
| C2 | ControlAudioTestTone steps c-f: transport.get_state is unchanged, and invalid arguments are refused | the agent path does not touch transport, and bad arguments return invalid_args | exit 0; r1 033 |
| C2 (round 3) | AudioTestToneTest: aNaturallyFinishedToneIsRetiredNotFreedOnTheRenderPath, aStoppedToneIsRetiredByTheNextPeriod, repeatedTonesAreReclaimed; ControlAudioTestTone steps h and i | a naturally finished tone is retired by the render thread and freed by its owner, a stopped tone is reclaimed by the next period, and 12 tones fit 8 slots only because finished tones are reclaimed | exit 0; r3 010 |
| C3: a registered command reports current/configured backend and failure state | audio.device_list via ControlAudioTestTone, ControlNoAudioDevice and ControlRegistryTest | the registered command returns the current and configured backend and the failure state | exit 0; r1 033 |

## Commands and exit codes

- r1 029: RED zene build, exit 0. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-183.1.r1/logs/029-red-build-zene.log`
- r1 030: RED C++ test build, exit 2 (AudioTestTone.h missing). Log: `.../Q050-183.1.r1/logs/030-red-build-tests.log`
- r1 031: RED transcripts, exit 8 (no command 'audio.test_tone'). Log: `.../Q050-183.1.r1/logs/031-red-transcripts.log`
- r1 032: GREEN build, exit 0. Log: `.../Q050-183.1.r1/logs/032-green-build.log`
- r1 033: GREEN ctest of AudioTestToneTest, AudioOutputStatusBoxTest, ControlRegistryTest, ReversibilityContractTest, ControlAudioTestTone and ControlNoAudioDevice: 6/6, exit 0. Log: `.../Q050-183.1.r1/logs/033-green-tests.log`
- r1 034: live snapshot, exit 0. Log: `.../Q050-183.1.r1/logs/034-snapshot.log`
- r1 036: doc-figures-gate, exit 0. Log: `.../Q050-183.1.r1/logs/036-doc-figures.log`
- r2 001: ControlNoAudioDevice after the complexity split, exit 0. Log: `.../Q050-183.1.r2/logs/001-r2-noaudio.log`
- r2 002: feature_check, RESULT PASS, exit 0. Log: `.../Q050-183.1.r2/logs/002-r2-feature-check.log`
- r2 submit_check: the log from this packet's run, in `.../Q050-183.1.r2/logs/`.
- r3 006: RED zene build with fix 7a9ace57e3 reverted, exit 0. Log: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-183.1.r3/logs/006-r3-red-zene.log`
- r3 007: RED AudioTestToneTest build, exit 2 (audioTestToneHandlesHeld missing). Log: `.../Q050-183.1.r3/logs/007-r3-red-test-build.log`
- r3 008: RED ControlAudioTestTone, exit 8 (step h was_playing=True). Log: `.../Q050-183.1.r3/logs/008-r3-red-transcript.log`
- r3 009: GREEN zene build, exit 0. Log: `.../Q050-183.1.r3/logs/009-r3-green-build.log`
- r3 010: GREEN ctest, 6/6 passed, exit 0. Log: `.../Q050-183.1.r3/logs/010-r3-green-tests.log`
- r3 011: feature_check on round 3, RESULT FAIL (complexity: NEW lmms::reclaim@181-201 CCN 11). Log: `.../Q050-183.1.r3/logs/011-r3-feature-check.log`
- p5 012: build, exit not recorded in the log. Log: `.../Q050-183.1.r3/logs/012-r3-build-p5.log`
- p5 013: ctest, 6/6 passed, exit not recorded in the log. Log: `.../Q050-183.1.r3/logs/013-r3-test-p5.log`
- p5 014: feature_check, RESULT PASS, exit 0. Log: `.../Q050-183.1.r3/logs/014-r3-feature-check-p5.log`
- r3 submit_check (quiet-run label `r3-submit-check`, run after this commit): log in `.../Q050-183.1.r3/logs/`.

## Self-check

1. RED/GREEN: the product changes since base fa4a5f2e78 were reversed with `git apply -R`. With them reversed, the zene build exits 0 (029), the C++ test build exits 2 (030), and ControlAudioTestTone and ControlNoAudioDevice exit 8 (031). Restored and rebuilt, 6/6 pass, exit 0 (032/033).
   Round 3: with fix 7a9ace57e3 reverted, the zene build exits 0 (r3 006), the AudioTestToneTest build exits 2 (r3 007, audioTestToneHandlesHeld missing), and ControlAudioTestTone exits 8 (r3 008, step h was_playing=True). With the fix restored, 6/6 pass, exit 0 (r3 010).
2. Each criterion is proven by a test run here; see the acceptance table.
3. No edits to the project: audio.test_tone and audio.test_tone_stop are not_mutating and add no undo step, which AudioTestToneTest proves (undo depth unchanged). audio.device_list stays not_mutating. ReversibilityContractTest passes with the A16 figure re-taken at 944/412.
4. Audio path: the only code that runs on the render path is TestTonePlayHandle::isFinished (a relaxed atomic load plus the base check), TestTonePlayHandle::handOverToOwner (one release store), and AudioEngine::disposePlayHandle's new early return. Nothing on the render path deletes, allocates, locks or emits. The tone buffer, the Sample, the AudioBusHandle and the handle are created on the control thread and owned by a fixed 8-slot table. The control thread frees retired handles, buses and samples in reclaim() under requestChangesGuard at the next audio.test_tone or audio.test_tone_stop. TSan does not run on this box, so this is reasoned.
5. Projects: no project or config format change, and nothing is saved, so old projects load and render as before and there is no new data to round-trip.
6. Completeness and edges:
   - GUI path: the Settings dialog on the Audio tab; Play then Stop; describe() for a fallback with a reason, a fallback with no reason, empty configured, and configured equal to running.
   - Agent path: device_list on a normal start and on a broken backend; test_tone at the limits (frequency 20, seconds 0.1, level_db -60) and out of range (frequency 5/10/30000, seconds 0/11, level_db 0, frequency 'x') giving invalid_args; a second stop gives was_playing false; a new tone replaces the last.
   - Round 3 edges: natural completion on the dummy callback thread (step h), stop after completion, 20 and 12 repeated tones (repeatedTonesAreReclaimed, step i), a stopped tone retired by the next period, and a refused addPlayHandle retired through disposePlayHandle.
   - Stale generation: a superseded tone is retired by its owner's reclaim(), not destroyed on the render path. The destructor publishes no completion.
   - No engine: Requires.
7. GUI test: AudioOutputStatusBoxTest clicks the visible Play test tone and Stop buttons with QTest::mouseClick. The buttons call ControlRegistry::invoke('audio.test_tone' / 'audio.test_tone_stop'), and the test checks the command's effect, audioTestToneActive(), which only those commands change. Undo depth does not rise by one because the commands are honestly not_mutating (session audio); that the depth stays the same is asserted in AudioTestToneTest.
8. Portability: Qt API available in 5.15 and 6.x; QComboBox::textActivated was already in use; no windows.h macro names; no std::filesystem. The tests use std::span (C++20, the project standard).

## Known limits

- Live device switching is stage 2.
- The tone goes through master, so master effects apply.
- audible is false on the dummy device.
- ControlSocketIntegration's new audio block did not run on this build: it fails earlier at line 687 with no builtin plug-ins, which predates this unit. ControlAudioTestTone covers the same agent path.
- Commit 7fe2f60640 carries a 'Claude Haiku 5.5' co-author trailer instead of 'duet:opus-haiku'; amending is forbidden, so it stays.
- Round 3 trailer: commit 7a9ace57e3 also carries a 'Claude Haiku 5.5' trailer; amending is forbidden, so it stays.
- Round 3 leak: when its engine is destroyed, an unreclaimed tone slot is intentionally leaked and never freed after its engine is gone.
