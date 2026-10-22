FIXED

new-p9-454: FIXED

new-p9-453: FIXED

Round 3 answers Luna's REWORK on `new-p9-454` (review
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-454.r2.md`).
The two findings keep their verdicts; the five blocking issues of the review are
fixed at their cause.

## What the review asked for, and where it is fixed

1. **One settings object per synth (crash-or-data-loss).** `createSynth()`
   (`plugins/Sf2Player/Sf2Player.cpp:314`) now builds a *new* `fluid_settings_t`
   for every synth (`Sf2Player.cpp:320`) and hands it to `new_fluid_synth()`
   (`:333`); the single shared `m_settings` member is gone. The settings object
   lives with its synth: `Sf2OwnedSynth` (`plugins/Sf2Player/Sf2Player.h:127`)
   pairs them, `m_ownedSynths` (`Sf2Player.h:306`) owns every synth the UI
   thread built, and `destroySynth()` (`Sf2Player.cpp:354`) deletes the synth
   *and* its settings. That covers the three paths the review named: the live
   synth, the candidate of `reloadSynth()`/`openFile()` (`:697`, `:462`) and the
   patch-dialog synth (`:749`), which the view now deletes through
   `destroySynth()` (`Sf2Player.cpp:1536`).
2. **No allocating or locking FluidSynth control call on the render path
   (realtime).** The whole control application moved to the UI thread, which
   owns the synth: `applyRequestedState()` (`Sf2Player.cpp:644`) sends the
   gain, the effects, the program and the tuning to `m_uiSynth` before the
   render thread adopts it, and the slots `updateGain`/`updateReverbOn`/
   `updateReverb`/`updateChorusOn`/`updateChorus`/`updatePatch`/`updateTuning`
   do the same for later changes (`Sf2Player.cpp:549-641`). The render thread
   keeps no control state at all: `applyControls()` and
   `resetAppliedControls()` are deleted, together with the `m_requested*` and
   `m_applied*` members. `playImpl()` (`Sf2Player.cpp:1025`) now only adopts the
   published synth, plays the notes and renders. The only FluidSynth call left
   on the render path is the pitch bend (`:1039`), which the baseline also had
   there: it follows the track's per-period bend value and allocates nothing.
3. **Equal temperament with the microtuner off (regression).** `updateTuning()`
   passes a *null* pitch table when the microtuner is off
   (`Sf2Player.cpp:634`: `m_tuningEnabled ? m_tuningCents.data() : nullptr`),
   which is what the pre-cluster implementation did; an all-zero table would
   pin every key to MIDI note 0.
4. **The requested effect state on every new synth (regression).**
   `createSynth()` writes `synth.reverb.active` and `synth.chorus.active` from
   the models into the new synth's own settings (`Sf2Player.cpp:330-331`),
   which is where FluidSynth reads the initial effect state from, and
   `applyRequestedState()` sends the state explicitly on top of it. The
   `USE_NEW_EFFECT_API` macro (the baseline's) was restored, so the current
   FluidSynth 2.x API is used again (`Sf2Player.cpp:64`).
5. **A note pool that holds a default-polyphony load (regression).**
   `PluginDataSlotCount` and `NoteEventCapacity` are 512 now
   (`Sf2Player.h:315`, `:319`), twice FluidSynth's default polyphony of 256.
   Exhaustion is defined: a note that finds no free slot keeps its plugin data
   empty and takes a slot in one of the next periods (`Sf2Player.cpp:851`), and
   a note whose event the bounded queue refuses gives the slot back and tries
   again next period (`Sf2Player.cpp:873`) - a full queue delays a note, it
   never drops it and never leaks a slot.

## Regressions (tests/src/plugins/Sf2PlayerRealtimeTest.cpp)

The test drives the product's own `sf2player` module through the plugin's
public interface. New cases: `renderPathAppliesNoState` (the state the player
asks for is at the synth before the first render - the render thread does not
configure the synth - and the render path allocates nothing),
`disabledTuningKeepsEqualTemperament` (the tuning at the synth is the equal
temperament, 100 cents per key, not an all-zero table),
`newSynthStartsWithTheRequestedEffectState` (the new synth's settings have the
effects off), `everySynthHasItsOwnSettings` (two synths never share a settings
object) and `moreNotesThanTheOldPoolGetSlots` (200 simultaneous notes all get
a plugin data slot). `playsWithARealSoundFont` is the collateral-damage guard:
it loads the SoundFont that ships with the tree, plays a note through the
player and checks that the rendered period is not silent (it is not a bug
regression - it passes before and after - but it proves that the rewritten
synth life cycle still makes sound). `notePathAllocatesNothing` and
`renderSurvivesAPendingReload` from round 2 are kept.

## Evidence

All logs are new numbered files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-454.r3/logs`.

### Whole-fix reversal (the brief's command)

- `git diff -- plugins/Sf2Player/Sf2Player.cpp plugins/Sf2Player/Sf2Player.h > logs/fix.patch` EXIT=0 (909 lines);
  `git apply -R logs/fix.patch` EXIT=0;
  `cmake --build build --target sf2player Sf2PlayerRealtimeTest -j12` EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest` EXIT=5,
  `Totals: 4 passed, 5 failed` (`test-red-full.log`): `renderPathAppliesNoState`,
  `disabledTuningKeepsEqualTemperament`, `newSynthStartsWithTheRequestedEffectState`
  and `everySynthHasItsOwnSettings` cannot reach the synth (the seam is part of
  the fix), `moreNotesThanTheOldPoolGetSlots` reports
  "72 of 200 notes did not get a plugin data slot".
- `git apply logs/fix.patch` EXIT=0; the same build EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest` EXIT=0,
  `Totals: 9 passed, 0 failed` (`test-green-final.log`); with the end-to-end
case added: `Totals: 10 passed, 0 failed` (`test-e2e.log`).

### One reversal per defect (each defect re-introduced on its own, the seam kept)

- All-zero tuning table: `const double* pitch = m_tuningCents.data();` EXIT=0;
  build EXIT=0; test EXIT=1 - "key 1 is tuned to 0 cents, the equal temperament
  wants 100" (`test-red-tuning.log`).
- Effect defaults: `fluid_settings_setint(..., "synth.reverb.active", 1)` and
  the same for the chorus EXIT=0; build EXIT=0; test EXIT=1 -
  `newSynthStartsWithTheRequestedEffectState` "Compared values are not the same"
  (`test-red-effects.log`).
- Note pool: `PluginDataSlotCount = 128` EXIT=0; build EXIT=0; test EXIT=1 -
  "72 of 200 notes did not get a plugin data slot" (`test-red-pool.log`).
- The state not applied by the UI: `updateGain()` returns before
  `fluid_synth_set_gain()` EXIT=0; build EXIT=0; test EXIT=1 -
  "the synth plays with the gain 0.2, the player asks for 1.0"
  (`test-red-state-3.log`).
- Shared settings object: `createSynth()` hands every synth one
  `m_sharedSettings` EXIT=0; build EXIT=0; test EXIT=1 - "the two synths share
  one FluidSynth settings object" (`test-red-settings.log`); the run then
  aborted in the cleanup, which is the undefined behaviour FluidSynth warns
  about (one settings object deleted twice).

### Affected family, sweep and gates

- `cd build/tests && ctest -R '^(Sf2PlayerRealtimeTest|RtSafetySweep|RtSafetySelfTest)$' --output-on-failure`
  EXIT=0, 3/3 tests passed (`test-family-1.log`).
- `python3 tests/rt-safety-sweep.py` EXIT=0 - "PASS: every hit on a declared
  audio-thread path is allowlisted with a reason and at its allowed count"
  (`rt-sweep-1.log`). The scope file lost the two functions that no longer
  exist (`applyControls`, `resetAppliedControls`) and the `playImpl` entry now
  describes the render path without a control application.
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  EXIT=0 - fork-sources PASS, all-sources-reproduce PASS,
  no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
  doc-figures PASS, complexity PASS ("no new item in your files"), file-length
  PASS ("no new item in your files"), RESULT PASS (`feature-check-1.log`).
- Build: `cmake --build build --target sf2player Sf2PlayerRealtimeTest zene -j12`
  EXIT=0 (`build-green-final.log`).
- End-to-end sound: `playsWithARealSoundFont` loads
  `plugins/LadspaEffect/calf/veal/sf2/Hum.sf2` through the player, plays a note
  and finds a peak of 0.42 in the rendered period (`test-e2e.log`), so the
  rewritten synth life cycle (own settings, font load, program selection,
  note-on, rendering with the requested effect state) still makes sound.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** The whole-fix reversal gives EXIT=5 with five failing
  cases, the restored fix EXIT=0 with `Totals: 9 passed, 0 failed`; each of the
  five defects was also re-introduced on its own and made exactly its case
  fail (EXIT=1 each, messages quoted above).
- **The test is deterministic.** No wall-clock timing, no sleeps, no races: the
  allocation count uses a replacement global `operator new` armed around one
  call, the tuning is read back through FluidSynth's own
  `fluid_synth_tuning_dump()`, the effect state through
  `fluid_settings_getint()` on the synth's own settings, and the note pool
  through the note handles' `m_pluginData`. Everything runs in the test thread.
- **The fix is at the cause and complete.** The cause of all five findings is
  the same: state that belongs to the FluidSynth synth was split between the UI
  thread and the render thread. It is fixed once, in the place all the paths go
  through: the synth and its settings are owned by the UI thread
  (`m_ownedSynths`, `destroySynth()`), the state is applied by the UI thread
  (`applyRequestedState()` and the update slots), and the render thread only
  borrows the synth, plays notes and renders. Every caller was checked: the
  live synth, the `reloadSynth()` candidate, the `openFile()` candidate, the
  failed-load path (the previous synth is restored), the patch-dialog synth and
  the destructor (every owned synth is deleted with its settings).
- **No collateral damage.** The saved attributes (`src`, `bank`, `patch`, the
  effect values, the gain) are untouched, so old projects load as before; a
  failed font load still keeps the current font (the candidate is built and
  destroyed off the audio thread and the old synth is restored); the note pool
  grew, so no legitimate caller is refused; the queue-exhaustion behaviour is
  documented and never drops a note. The only behaviour that changes is the one
  the review asked for: the effects of a new synth follow the models, and the
  microtuner off plays the equal temperament again.
- **Nothing new runs on the audio thread that allocates, frees, locks, waits or
  logs.** The render path is `playImpl` -> `serviceSynthHandoff` ->
  `playNoteImpl`/`noteOn`/`noteOff`/`renderFrames`: the note data comes from
  the preallocated slots, the events from the bounded preallocated queue, the
  synth hand-off from the fixed-capacity queues, and the only FluidSynth call
  left there is the pitch bend (no allocation, no wait for another thread
  beyond FluidSynth's own uncontended API mutex). The control application -
  the tuning activation, which allocates a tuning object inside FluidSynth -
  runs on the UI thread. The RT-safety sweep over the declared audio paths
  passes (EXIT=0).
- **Qt 5.15 / MSVC / macOS portability.** No new Qt API is used (the test seam
  is a `quintptr` slot, the settings read is `fluid_settings_getint`, which
  exists in FluidSynth 1.x as well), no `windows.h` macro names, no
  `std::filesystem`. The `USE_NEW_EFFECT_API` switch keeps the FluidSynth 1.x
  path compiling, as in the baseline.
- **Verdict.** Both findings are REAL and FIXED; neither is ALREADY-FIXED or
  FALSE-POSITIVE. The round-2 review's blocking items (shared settings object,
  allocating/locking control call on the render path, all-zero tuning table,
  effect defaults, 128-note pool) are each fixed and each has a regression that
  fails without its fix.

Files committed: `plugins/Sf2Player/Sf2Player.cpp`,
`plugins/Sf2Player/Sf2Player.h`, `tests/src/plugins/Sf2PlayerRealtimeTest.cpp`,
`tests/CMakeLists.txt`, `tests/rt-safety-scope.txt`,
`tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p9-454.md`.
