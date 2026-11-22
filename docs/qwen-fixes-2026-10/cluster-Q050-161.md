IMPLEMENTED
(five of the six batch units; Q050-330 is BLOCKED - its line below says why; none of its code is on the branch)

Q050-161: IMPLEMENTED
Q050-331: IMPLEMENTED
Q050-330: BLOCKED - every criterion needs a Windows build, a WASAPI/ASIO device and hosted Windows CI; this lane is Linux-only and may not push. Details: docs/050-features/Q050-330.md
Q050-081: IMPLEMENTED
Q050-246: IMPLEMENTED
Q050-205: IMPLEMENTED

Branch `feat/050-Q050-161-oc`, cut from `050/m0-train-21` (`37eaf0755`). Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-161.r1/logs/`
(build-N / test-N). Every exit code below is the unpiped exit of the command named. Feature records:
`docs/050-features/Q050-161.md`, `-331.md`, `-330.md`, `-081.md`, `-246.md`, `-205.md`.

Shared design: the units touch the control surface in three places, each built once - one place where devices'
MIDI bindings are named relative to their device (`PresetBindings`, Q050-331), one MIDI-port-bound runtime with a
lock-free inbox (`ControllerScript`, Q050-081), one registry adapter for a network transport (`OscBridge`, Q050-205);
all three dispatch through `ControlRegistry::invoke`, so undo, refusals and records are the registry's. Both new
command groups (`osc.*`, `controller.script_*`) are device state, `not_mutating`, the `surface.mcu_connect` precedent,
and both refuse each other's lifecycle commands (a script cannot start scripts; OSC cannot start scripts).

## Q050-161 - per-period automation ramp locking
| Criterion | Test (command) | Exit |
|---|---|---|
| A concurrent ramp test proves cache hits avoid the mutex while recomputation remains correct under updates. | `ctest -R '^PhaseDSidechainTest$'` (`cachedRampHitsTakeNoLockAndRecomputeAfterConcurrentUpdates`: 256 realtime hits under the held mutex and a blocked writer, then bit-exact recomputation and a new ramp) | 0 (test-07, test-37-final) |
| An audio-thread instrumentation test confirms no lock is taken on a cache hit and automation output matches the existing render. | same target (`instrumentedRampRenderHitsCacheWithoutLockAndMatchesReference`: 10 counted hits, 0 allocations, SHA-256 `a7348883...5a69` = `AutomationPrepassProofTest`'s reference) | 0 |
Negative controls: a `QMutexLocker` on the cache-hit return -> both cases FAIL (exit 1, test-05-mutation-*);
`consumePublishedValuePair()` removed -> the concurrent case FAILS (exit 1, test-06-mutation2).

## Q050-331 - controller bindings with presets
| Criterion | Test (command) | Exit |
|---|---|---|
| Track and chain preset round-trip tests preserve valid bindings and rebind them to the loaded devices. | `ctest -R '^PresetBindingsTest$'` (`trackPresetRestoresBindingsOnTheLoadedDevices`, `chainPresetRebindsToTheNewEffect`) | 0 (test-22, test-37-final) |
| Missing targets are reported and skipped without creating phantom mappings. | same (`aMissingTargetIsReportedAndSkipped`, `anOlderPresetLoadsAsBefore`) | 0 |
| Preset UI and commands show which bindings were restored. | `ctest -R '^PresetBindingsGuiTest$'` (chain menu Apply and Track Preset > Load Preset... form, both triggered) + the command results above | 0 |
Negative controls: manifest writer no-op -> `PresetBindingsTest` exit 4, `PresetBindingsGuiTest` exit 2 (test-21-*);
`ConnectionRestoreScope` removed from `JournallingObject::restoreState` -> the undo check FAILS, exit 1 (test-20).
Neighbours still pass: `ControlChainPresetTest`, `ChainPresetMenuTest`, `LibraryProjectsTest`, `AutomatableModelTest` (exit 0).

## Q050-330 - Windows ASIO device selection: BLOCKED
No test could be run: the criteria need the MSVC build, a WASAPI/ASIO endpoint and the hosted Windows job. Nothing of
it is on the branch. The record names the path a Windows lane should take (WASAPI first; the ASIO SDK is GPLv3).

## Q050-081 - controller scripting and a mapping editor
| Criterion | Test (command) | Exit |
|---|---|---|
| A Lua example receives MIDI from a bound port, invokes a registered command and sends feedback to that port. | `ctest -R '^ControllerScriptTest$'` (`theExampleRunsACommandAndSendsFeedbackToItsPort`, `theCommandsStartReadAndStopAScript`) | 0 (test-36, test-37-final) |
| The mapping editor lists, edits and persists bindings; its actions have stable command IDs and headless tests. | `ctest -R '^ControllerMappingEditorTest$'` (MIDI Mappings... triggered, list action run, project.save/open round trip; Controller Scripts menu and forms driven) | 0 |
| Script errors and port loss are bounded and reported without blocking the audio thread. | `ctest -R '^ControllerScriptTest$'` (`scriptErrorsAreBoundedAndReported`, `deliveryNeverBlocksTheAudioThreadAndPortLossIsReported`, `theSandboxHoldsAndScriptsCannotStartScripts`) | 0 |
Negative controls: a growing vector in `processInEvent` -> "allocations 15, expected 0", exit 1 (test-33);
no fault after 8 errors -> `scriptErrorsAreBoundedAndReported` FAILS, exit 1 (test-29). `ScriptCommandBridgeTest`,
`ScriptEngineTest` still pass.

## Q050-246 - external MIDI output timing
| Criterion | Test (command) | Exit |
|---|---|---|
| A MIDI loopback test measures repeated 1/32-note output timing and reports jitter in milliseconds. | `ctest -R '^MidiOutTimingTest$'` (ALSA kernel loopback, delivery stamps; prints `Q050_246_MIDI_JITTER` with target, mean, max/mean abs error, setup) | 0 (test-24-run1..3, test-37-final) |
| If measured jitter exceeds 1 ms, the regression test proves sub-tick scheduling fixes it; otherwise the measured pass and hardware setup are recorded. | same: baseline 4.46-4.61 ms (> 1 ms), product 0.25 ms (<= 1 ms, asserted) | 0 |
Negative control: the override made to drop the offset -> "1/32-note output jitter 4.638 ms > 1 ms", exit 1 (test-25).
`MidiOutQueueTest` still passes. Setup: Linux ALSA sequencer client-to-client loopback, no hardware interface.

## Q050-205 - opt-in OSC control server
| Criterion | Test (command) | Exit |
|---|---|---|
| An OSC loopback test maps /zene/<command-id> messages to registered commands and verifies transport/fader feedback. | `ctest -R '^ControlOscServerTest$'` (`transportAndFaderRoundTripOverLoopback`) | 0 (test-10, test-37-final) |
| The server is off by default, binds only after explicit opt-in and documents its address map. | same (`offByDefaultBindsOnOptInAndStopCloses`, `addressMapIsDocumented`) + `ctest -R '^OscServerMenuTest$'` (menu and form driven) | 0 / 0 |
| Malformed and unsupported OSC messages return bounded errors without terminating the DAW. | same (`malformedAndUnsupportedRequestsGetBoundedErrors`, `aBurstIsAnsweredInBoundedTurns`, `fullScopeRunsOtherCommandsButNotScripts`) | 0 |
The tests do not build without the product change (all of `osc.*`, `OscBridge`, `OscServer` are new).

## Shared checks
- Command snapshot regenerated from a live instance: `snapshot_live.py` exit 0 (877 ids / 77 groups); README canary
  updated; `python3 tests/doc-figures-gate.py` exit 0; `ctest -R '^ControlCommandsSnapshot$'` exit 0.
- A16: six new `not_mutating` rows; `ReversibilityContractTest` exit 0 after the published block moved to
  `rows=886 true_inverse=434 snapshot=42 irreversible=25 not_mutating=385` (release notes line added).
- Gates (`feature_check.py`, run from a /tmp copy because the original writes its logs beside the worktree, outside
  this lane's write-jail): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
  no-tautology, doc-figures 0; complexity and file-length: no new item. RESULT PASS (gates-04).
- `ControllerSurfaceTest::TemplatesSaveListReadAndApply` fails in this lane only because it writes into the real
  user preset directory, which the write-jail blocks; with `HOME=/tmp/q161home` the whole test passes (15/15, exit 0,
  test-27). Not caused by this branch.
- Last edit not rebuilt: `include/ControlRegistryGroups.h` changed a comment only (the stale "OSC IS OUT" wording);
  a full lmmsobjs rebuild is about an hour on this shared 4-core box, so it was not re-run after that comment.

## Self-check
1. **Each new test fails with the product change reverted, passes restored.** Q050-161: the product change is a
   test seam, so the controls are mutations of the hit path (lock injected: both cases exit 1; recompute skipped:
   exit 1); restored exit 0. Q050-331: manifest no-op exit 4/2, restore scope removed exit 1; restored 0.
   Q050-081: allocation on delivery exit 1, no fault exit 1; restored 0. Q050-246: offset dropped exit 1; restored 0.
   Q050-205: new commands/classes - the tests cannot build without them (stated, not a mutation).
2. **Every criterion proven by a test I ran:** the tables above, each with its ctest target and exit code; the final
   batch `test-37-final` is 21/21 exit 0.
3. **One undo step / honest classes.** No new mutating command. `osc.*` and `controller.script_*` are `not_mutating`
   (device state); what they run records as the registered command (`ControlOscServerTest` checks
   `mixer.set_volume` is recorded `true_inverse` and one `control.undo` reverts it; `ControllerScriptTest` undoes the
   script's fader write). `track.preset_load` / `chain.apply` stay `true_inverse`, and their single undo now also
   takes the loaded bindings back (`PresetBindingsTest`).
4. **Audio thread.** Touched audio-path code: `MidiAlsaSeq::processOutEventAt` (one atomic load, one
   `steady_clock::now`, integer arithmetic, then the existing bounded lock-free push - no allocation, lock, wait or
   signal; the wake-pipe write is the pre-existing one), `MidiOutQueue::push` (one more POD field),
   `RenderPeriodClock::inPeriod` (an atomic load), `ControllerScript::processInEvent` (fixed-slot copy plus atomics;
   measured 0 allocations / 0 frees over 10,000 events in a `RealtimeScope`), `AutomatableModel::valueBuffer` hit (a
   null-pointer test in test builds only). OSC, scripts and preset reconciliation run on the UI thread. TSan does not
   run on this box; the race reasoning is in each feature record.
5. **Old projects / round trips.** No project format change except an optional `<zenebindings>` in presets (absent =
   older preset, loaded as before - `anOlderPresetLoadsAsBefore`); the ramp render hash equals the pinned reference;
   MIDI mappings survive `project.save`/`project.open` (`ControllerMappingEditorTest`). The restore-scope change only
   acts inside `JournallingObject::restoreState`; a normal load still only adds a connection.
6. **Portability.** No Qt-6-only API (QUdpSocket/QNetworkDatagram exist in Qt 5.15; `receiveDatagram`,
   `QSaveFile` unchanged); OSC server code is behind `ZENE_HAVE_COLLAB` and its headers are skipped from AUTOMOC
   without Qt Network; ALSA code stays inside the existing `LMMS_HAVE_ALSA` file and the timing test QSKIPs without
   it; no `std::filesystem`, no `small/near/far/interface/min/max` identifiers, no POSIX call outside guarded files;
   `thread_local` and `std::bit_cast` were already in use in these files. Not compiled for MSVC/macOS here.
