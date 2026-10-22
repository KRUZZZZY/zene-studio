FIXED

new-p9-454: FIXED

new-p9-453: FIXED

Round 4 answers Luna's REWORK on `new-p9-454` (review
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-454.r3.md`).
The round-2 and round-3 fixes are kept; the three blocking issues of this
review are fixed at their cause.

## What the review asked for, and where it is fixed

1. **No render-path FluidSynth API may take or wait on a mutex, and the UI must
   not call into the playing synth (realtime).** Two changes:
   - Every synth is built with `synth.threadsafe-api = 0`
     (`plugins/Sf2Player/Sf2Player.cpp:335`). FluidSynth reads that setting
     into `synth->use_mutex` when the synth is created
     (fluidsynth v2.6.1 `src/synth/fluid_synth.c:745`) and only locks in
     `fluid_synth_api_enter()` when that flag is set (`:8246`). With the flag
     off, *no* FluidSynth call of the render path - `fluid_synth_get_gain()`,
     `fluid_synth_pitch_bend()`, `fluid_synth_noteon()` - can wait for another
     thread. The render thread is the only user of that synth, so the mutex
     bought nothing.
   - The UI thread no longer touches the synth the render thread plays. The
     update slots only publish the request (`Sf2Player.cpp:560-680`), and the
     render thread sends it to its own synth (`applyControls()`,
     `Sf2Player.cpp:1063`). The render path is the only place that calls into
     FluidSynth, and it calls a synth nobody else touches.
   - The microtuner is no longer a FluidSynth key tuning. That API allocates a
     tuning object and takes FluidSynth's API mutex, so it cannot run on the
     audio thread. The player now shifts the note with the voice's coarse tune
     (`Sf2Player.cpp:907` and `:972`), which is the same shift: the key
     tuning moves a note by `T[key] - 100 * key` cents, and the coarse tune
     moves it by the same amount. `theTuningOffsetMatchesFluidSynthsKeyTuning`
     proves the two are the same with FluidSynth 2.6.1 itself.
2. **A reload must not lose the loaded soundfont (regression).** The soundfont
   is loaded into a holder synth that keeps the font object alive
   (`loadFont()`, `Sf2Player.cpp:509`), and every synth that plays it gets the
   font from the holder (`addFontTo()`, `Sf2Player.cpp:538`). A reload
   therefore never reads the file again: the font survives, the playback
   survives and the preset list survives, also when the file behind the font
   is gone. This is what the base implementation did (it reused the loaded
   `m_font`).
3. **MidiHandoffQueueTest (regression).** The queue is a template over the
   event type, so the test looks for the generic `auto pop(Event& event)
   noexcept -> bool` now (`tests/src/core/MidiHandoffQueueTest.cpp:101`). Its
   checks that `pop()` has no loop, no retry and no compare-exchange are kept
   unchanged.

## Regressions (tests/src/plugins/Sf2PlayerRealtimeTest.cpp)

New in this round: `renderPathAppliesTheState` (the synth runs with
FluidSynth's thread-safe API off, the UI publishes the gain and the synth only
gets it with the next period - so the UI did not call into the playing synth -
and the render path allocates nothing), `reloadKeepsTheSoundFontWhenTheFileIsGone`
(a copy of the soundfont is loaded, the file is removed, a reload keeps the
sound and the preset list) and
`theTuningOffsetMatchesFluidSynthsKeyTuning` (FluidSynth's key tuning and the
coarse tune the player uses move a note to the same frequency). The cases of
the earlier rounds are kept: `notePathAllocatesNothing`,
`disabledTuningKeepsEqualTemperament` (now measured on the rendered audio: the
note plays the equal temperament), `newSynthStartsWithTheRequestedEffectState`,
`everySynthHasItsOwnSettings`, `moreNotesThanTheOldPoolGetSlots`,
`renderSurvivesAPendingReload` and `playsWithARealSoundFont`.

## Evidence

All logs are new numbered files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-454.r3/logs`.

### Whole-fix reversal (the brief's command)

- `git diff -- plugins/Sf2Player/Sf2Player.cpp plugins/Sf2Player/Sf2Player.h > logs/fix.patch` EXIT=0 (693 lines);
  `git apply -R logs/fix.patch` EXIT=0;
  `cmake --build build --target sf2player Sf2PlayerRealtimeTest -j12` EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest` EXIT=2,
  `Totals: 9 passed, 2 failed` (`test-red-full-r4.log`):
  `renderPathAppliesTheState` "Compared values are not the same" and
  `reloadKeepsTheSoundFontWhenTheFileIsGone` "the player lost the soundfont:
  the note plays at 0 Hz".
- `git apply logs/fix.patch` EXIT=0; the same build EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest` EXIT=0,
  `Totals: 12 passed, 0 failed` (`test-green-r4b.log`).

### One reversal per defect (each defect re-introduced on its own)

- The synth runs with FluidSynth's thread-safe API on again:
  `fluid_settings_setint(owned.settings, "synth.threadsafe-api", 1)` EXIT=0;
  build EXIT=0; test EXIT=1 - `renderPathAppliesTheState` "Compared values are
  not the same" (`test-red-r4a1.log`).
- The UI thread calls into the playing synth again:
  `fluid_synth_set_gain(m_synth, ...)` in `updateGain()` EXIT=0; build EXIT=0;
  test EXIT=1 - "Compared floats are not the same" (`test-red-r4a2.log`).
- The reload reads the file again and publishes a synth without the font:
  EXIT=0; build EXIT=0; test EXIT=1 - "the player lost the soundfont: the
  note plays at 0 Hz" (`test-red-r4b2.log`).
- The queue test looks for the old signature again: EXIT=0; build EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./MidiHandoffQueueTest` EXIT=1 -
  `popHasNoRetryLoop()` "start != std::string::npos" returned FALSE
  (`test-red-queue.log`).

### Affected family, sweep and gates

- `cd build/tests && ctest -R '^(Sf2PlayerRealtimeTest|RtSafetySweep|RtSafetySelfTest|MidiHandoffQueueTest)$' --output-on-failure`
  EXIT=0, 4/4 tests passed (`test-family-r4b.log`).
- The four tests the review named:
  `cd build/tests && ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest|RemotePluginAudioPortsTest|RemotePluginClientE2ETest)$' --output-on-failure`
  EXIT=0, 4/4 tests passed (`test-queue-1.log`).
- `python3 tests/rt-safety-sweep.py` EXIT=0 - "PASS: every hit on a declared
  audio-thread path is allowlisted with a reason and at its allowed count"
  (`rt-sweep-2.log`). The scope declares `applyControls()` next to the render
  path again, because the render thread applies the state now.
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  EXIT=0 - fork-sources PASS, all-sources-reproduce PASS,
  no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
  doc-figures PASS, complexity PASS ("no new item in your files"), file-length
  PASS ("no new item in your files"), RESULT PASS (`feature-check-r4.log`).
- Build: `cmake --build build --target sf2player Sf2PlayerRealtimeTest MidiHandoffQueueTest zene -j12`
  EXIT=0 (`build-final-r4b.log`).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** The whole-fix reversal gives EXIT=2 with two failing
  cases, the restored fix EXIT=0 with `Totals: 12 passed, 0 failed`; each of
  the three defects was also re-introduced on its own and made exactly its
  case fail (EXIT=1 each, the messages quoted above).
- **The test is deterministic.** No wall-clock timing, no sleeps, no races:
  the allocation count uses a replacement global `operator new` armed around
  one call, the state is read back through FluidSynth's own
  `fluid_synth_get_gain()` and `fluid_settings_getint()`, the note pool
  through the note handles' `m_pluginData`, the soundfont through the
  rendered audio (a peak and an autocorrelation of the rendered period, both
  arithmetic on the samples) and the missing file through a copy of the
  soundfont in a `QTemporaryDir`. Everything runs in the test thread.
- **The fix is at the cause and complete.** The cause of the review's first
  item is that the synth was shared between two threads while FluidSynth's API
  was thread safe. It is fixed once, where every path goes through: the synth
  is built with the thread-safe API off, so the render path cannot wait, and
  the UI thread only publishes numbers, so it cannot touch the synth the
  render thread plays. Every caller was checked: the live synth, the
  `reloadSynth()` candidate, the `openFile()` candidate, the failed-load path
  (the previous synth keeps playing), the patch-dialog synth (it gets the font
  from the holder), the font holder and the destructor (every owned synth is
  deleted with its settings, the font with the holder).
- **No collateral damage.** The saved attributes (`src`, `bank`, `patch`, the
  effect values, the gain) are untouched, so old projects load as before; a
  failed font load still keeps the current font and the current presets; the
  note pool grew, so no legitimate caller is refused. The microtuner still
  plays the scale of the track: the shift per key is the same amount as
  before, only applied through the voice instead of a FluidSynth tuning
  object, so the equal temperament with the microtuner off is unchanged.
- **Nothing new runs on the audio thread that allocates, frees, locks, waits or
  logs.** The render path is `playImpl` -> `serviceSynthHandoff` ->
  `applyControls` -> `playNoteImpl`/`noteOn`/`noteOff`/`renderFrames`: the
  state it sends is a number, the note data comes from the preallocated slots,
  the events from the bounded preallocated queue, the synth hand-off from the
  fixed-capacity queues, and every FluidSynth call it makes is lock free
  because the synth runs with the thread-safe API off. The RT-safety sweep
  over the declared audio paths passes (EXIT=0).
- **Qt 5.15 / MSVC / macOS portability.** No new Qt API is used (the test
  seams are `quintptr` and `QString` slots, the settings read is
  `fluid_settings_getint`, which exists in FluidSynth 1.x as well), no
  `windows.h` macro names, no `std::filesystem`. The `USE_NEW_EFFECT_API`
  switch keeps the FluidSynth 1.x path compiling, as in the baseline.
- **Verdict.** Both findings are REAL and FIXED; neither is ALREADY-FIXED or
  FALSE-POSITIVE. The round-3 review's blocking items (a render path that can
  wait on FluidSynth's API mutex while the UI configures the same synth, a
  reload that loses the loaded soundfont, and the queue test that no longer
  finds the method) are each fixed and each has a regression that fails
  without its fix.

Files committed: `plugins/Sf2Player/Sf2Player.cpp`,
`plugins/Sf2Player/Sf2Player.h`, `tests/src/plugins/Sf2PlayerRealtimeTest.cpp`,
`tests/src/core/MidiHandoffQueueTest.cpp`, `tests/rt-safety-scope.txt`,
`tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p9-454.md`.
