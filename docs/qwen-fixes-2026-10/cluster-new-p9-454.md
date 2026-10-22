FIXED

new-p9-454: FIXED

new-p9-453: FIXED

Round 5 answers Luna's REWORK on `new-p9-454` (review
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/qa/reviews/new-p9-454.r4.md`).
The round-2, round-3 and round-4 fixes are kept; the blocking issue of this
review - the SoundFont that two synths shared - is fixed at its cause.

## The finding that started the cluster

`m_playingNotes` was a `QVector` whose `append()` could grow on the audio path.
That part was fixed in round 2/3 (`370df7573`, `d73caeaca`): the player now
keeps its notes in preallocated slots and hands the synth over to the render
thread through a fixed-capacity queue, so nothing on the audio thread
allocates. `notePathAllocatesNothing` and the RT-safety sweep prove it. The
round-4 review accepted those parts and sent the cluster back for the
SoundFont lifetime, which is what this round fixes.

## The blocking issue of the round-4 review, and where it is fixed

**The holder and the candidate synths shared one `fluid_sfont_t`.**
`fluid_synth_add_sfont()` *transfers the ownership* of the font to the synth it
is added to (fluidsynth v2.6.1 `src/synth/fluid_synth.c:5798`, the comment says
"ownership is transferred to @p synth"), and `delete_fluid_synth()` frees every
font of the synth it deletes (`fs.c:1216`, the loop that calls
`fluid_sfont_delete_internal()`). The old code loaded the font into a holder
synth and handed the *same pointer* to every playing synth
(`addFontTo()`, `Sf2Player.cpp:543-555` before this change), so deleting the
holder or a retired synth freed a font another live synth was still using.

The fix gives **every synth a font instance of its own**:

- The instrument keeps the font's bytes (`m_fontData`,
  `plugins/Sf2Player/Sf2Player.h:311`), read from the file once
  (`loadFont()`, `plugins/Sf2Player/Sf2Player.cpp:615`). The bytes are the
  instrument's own copy, so a synth can still be built when the file behind
  the font is gone - the reload keeps working without the file.
- Every synth gets a FluidSynth loader of its own that reads those bytes
  (`createSynth()`, `plugins/Sf2Player/Sf2Player.cpp:431`,
  `new_fluid_defsfloader()` + `fluid_sfloader_set_callbacks()`, the supported
  way to load a soundfont from memory). The loader is owned by the synth and
  dies with it.
- Every synth parses the copy into a font of its own (`addFontTo()`,
  `plugins/Sf2Player/Sf2Player.cpp:633`, `fluid_synth_sfload()`), so no
  `fluid_sfont_t` is ever added to two live synths. The holder synth and the
  shared `m_font` pointer are gone.
- The bytes are read on the UI thread only, and the render path is untouched:
  the font of the synth the render thread plays is loaded before the synth is
  published.

Everything that used the font goes through the two functions above: the live
synth, the `reloadSynth()` candidate, the `openFile()` candidate, the
patch-dialog synth (`createPatchDialogSynth()`,
`plugins/Sf2Player/Sf2Player.cpp:836`) and the destructor (every owned synth is
deleted with its settings, and with it its own font).

## Regressions (tests/src/plugins/Sf2PlayerRealtimeTest.cpp)

New in this round: `everySynthHasItsOwnSoundFont` - it loads the soundfont,
plays it, reloads while the old synth is still alive, reads the font object of
both synths through FluidSynth's own API (`fluid_synth_get_sfont()`), and
proves that the two synths do not share one font object. Then it retires the
old synth (the `updatePatch()` drain), plays again, checks the preset list and
lets the instrument destroy the retired synth. The cases of the earlier rounds
are kept: `notePathAllocatesNothing`, `disabledTuningKeepsEqualTemperament`,
`newSynthStartsWithTheRequestedEffectState`, `everySynthHasItsOwnSettings`,
`moreNotesThanTheOldPoolGetSlots`, `renderSurvivesAPendingReload`,
`renderPathAppliesTheState`, `playsWithARealSoundFont`,
`reloadKeepsTheSoundFontWhenTheFileIsGone` and
`theTuningOffsetMatchesFluidSynthsKeyTuning` (13 cases in the binary).

## Evidence

All logs are new numbered files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-454.r5/logs`.

### The regression test before the fix (RED)

- `cmake --build build --target Sf2PlayerRealtimeTest sf2player` EXIT=0
  (`build-red-1.log`).
- `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest everySynthHasItsOwnSoundFont`
  EXIT=1 (`test-red-1.log`):
  `FAIL! : Sf2PlayerRealtimeTest::everySynthHasItsOwnSoundFont() 'secondFont != firstFont' returned FALSE. (the two synths share one SoundFont object: deleting one of them leaves the other with a freed font)`.

### The whole-fix reversal (the brief's command in step 4b)

- `git diff -- plugins/Sf2Player/Sf2Player.cpp plugins/Sf2Player/Sf2Player.h > logs/fix.patch`
  EXIT=0 (245 lines);
- `git apply -R logs/fix.patch` EXIT=0;
  `cmake --build build --target Sf2PlayerRealtimeTest sf2player` EXIT=0
  (`build-revert-1.log`);
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest everySynthHasItsOwnSoundFont`
  EXIT=1 (`test-revert-1.log`): the same failure,
  `'secondFont != firstFont' returned FALSE`.
- `git apply logs/fix.patch` EXIT=0;
  `cmake --build build --target Sf2PlayerRealtimeTest sf2player zene` EXIT=0
  (`build-green-3.log`);
  `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest everySynthHasItsOwnSoundFont`
  EXIT=0 (`test-green-3.log`): `PASS`.

### Affected family, sweep and gates

- `cd build/tests && QT_QPA_PLATFORM=offscreen ./Sf2PlayerRealtimeTest` EXIT=0,
  `Totals: 13 passed, 0 failed` (`test-green-4.log`).
- `cd build/tests && ctest -R '^(Sf2PlayerRealtimeTest|MidiHandoffQueueTest|CarlaMidiHandoffTest|RemotePluginAudioPortsTest|RemotePluginClientE2ETest|RtSafetySweep|RtSafetySelfTest)$' --output-on-failure`
  EXIT=0, 7/7 tests passed (`ctest-focused-3.log`).
- `cd build/tests && ctest -R 'PluginPortsMigrationTest' --output-on-failure`
  EXIT=0, 1/1 test passed (`ctest-pluginports-3.log`). (The first run of this
  test failed with "SWH Decimator test fixture was not loaded" - the LADSPA
  fixture was not built in this build tree; after
  `cmake --build build --target decimator_1202` EXIT=0 the test passes. The
  failure is unrelated to this change.)
- `python3 tests/rt-safety-sweep.py` EXIT=0 - "PASS: every hit on a declared
  audio-thread path is allowlisted with a reason and at its allowed count"
  (`rt-sweep-r5.log`).
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  EXIT=0 - fork-sources PASS, all-sources-reproduce PASS,
  no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
  doc-figures PASS, complexity PASS ("no new item in your files"), file-length
  PASS ("no new item in your files"), RESULT PASS
  (`feature-check-r5-3.log`).
- Build: `cmake --build build --target zene sf2player Sf2PlayerRealtimeTest`
  EXIT=0 (`build-final-1.log`).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Before the fix: EXIT=1 with
  `'secondFont != firstFont' returned FALSE` (`test-red-1.log`). With the fix
  reverted (`git apply -R logs/fix.patch`): EXIT=1, the same failure
  (`test-revert-1.log`). With the fix restored (`git apply logs/fix.patch`):
  EXIT=0 (`test-green-3.log`).
- **The test is deterministic: no wall-clock timing, no sleeps, no races.** It
  uses no timing at all: the font object of a synth is read through
  FluidSynth's own `fluid_synth_get_sfont(synth, 0)` on the synth pointer the
  fixture gets from the player's own `currentSynthAddress()` seam, and the
  playback is read back from the rendered samples (a frequency estimate of the
  rendered period). The reload is driven through the player's `reloadSynth()`
  slot with `Qt::DirectConnection`, so it runs in the test thread, and the
  retired synth is drained by the player's own `updatePatch()` slot. Nothing
  waits for wall-clock time.
- **The fix is at the cause and complete: every caller and sibling was
  checked.** The cause is the ownership transfer of `fluid_synth_add_sfont()`;
  the fix removes the sharing itself, so every path that wanted the font gets
  its own instance: the live synth, the `reloadSynth()` candidate, the
  `openFile()` candidate, the patch-dialog synth and the failed-load path (the
  current font and the current presets survive a failed load, and the reload
  still keeps the old synth when the font cannot be loaded). The holder synth
  and the shared `m_font` member are gone, so there is no second way to reach
  a font. `fluid_synth_remove_sfont()` and `fluid_synth_add_sfont()` are no
  longer called anywhere in the plugin.
- **No collateral damage.** The saved attributes (`src`, `bank`, `patch`, the
  effect values, the gain) are untouched, so old projects load as before. The
  reload still works when the file behind the font is gone
  (`reloadKeepsTheSoundFontWhenTheFileIsGone` passes), because the instrument
  keeps its own copy of the font's bytes. No new limit or check was added that
  could refuse another caller: the memory loader only refuses to open when
  there is no font data, and the default FluidSynth loader is still registered
  on every synth, so a font can still be loaded from a file path. The only
  behaviour that changed is where the bytes of the font come from: the file is
  read once into the instrument's own copy instead of being read by
  FluidSynth's file loader for every synth.
- **Nothing new runs on the audio thread that allocates, frees, locks, waits or
  logs.** The font is loaded on the UI thread only (`loadFont()`,
  `addFontTo()`, both called from the UI slots `openFile()`, `reloadSynth()`
  and the patch-dialog slot). The render path is unchanged since round 4:
  `playImpl` -> `serviceSynthHandoff` -> `applyControls` ->
  `playNoteImpl`/`noteOn`/`noteOff`/`renderFrames`, with preallocated slots,
  bounded queues and a synth that runs with FluidSynth's thread-safe API off.
  The RT-safety sweep over the declared audio paths passes (EXIT=0). No new
  control command was added.
- **Verdict.** The round-4 blocking finding (a `fluid_sfont_t` shared between
  the holder and the candidate synths) is REAL and is FIXED: no font object is
  shared any more, every synth owns a font instance, and the regression that
  proves it fails without the fix. The verdict for this cluster is therefore
  FIXED, not ALREADY-FIXED and not FALSE-POSITIVE: the code in this branch
  before this round did share one `fluid_sfont_t` between two live synths
  (`addFontTo()` handed the holder's pointer to every candidate), which is
  what the RED run above shows. The original finding of the cluster (a
  `QVector` that could allocate on the audio path) was fixed in the earlier
  rounds of this branch and stays fixed.

Files committed: `plugins/Sf2Player/Sf2Player.cpp`,
`plugins/Sf2Player/Sf2Player.h`, `tests/src/plugins/Sf2PlayerRealtimeTest.cpp`,
`tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-new-p9-454.md`.
