FIXED

# Cluster 5412: loading a preset silently removes a frozen take

**Verdict: REAL, fixed.** `Track::loadTrack()` cleared the frozen take on every call, including a preset load, but a preset load
neither restores a take nor unmutes the clips the take had muted. The track was left with muted clips and nothing covering them.

## Evidence

- **Cause.** Before the fix, `loadTrack()` called `clearFrozenTake()` then `loadFrozenTake(element)` unconditionally
  (`src/core/Track.cpp`, the "Reset-on-absence for the frozen take" block, now `src/core/Track.cpp:860-877`).
  `clearFrozenTake()` (`src/core/Track.cpp:~1445`) resets `m_frozen` and the buffer only; it does not touch clips. Only `unfreeze()`
  unmutes them (`src/core/Track.cpp:~1424-1431`).
- **A preset carries no frozen state.** `saveTrack(..., presetMode=true)` returns at `src/core/Track.cpp:736-739`, before the frozen
  attributes are written. So there is nothing for `loadFrozenTake()` to restore from a preset element.
- **A preset load leaves the clips alone.** The preset branch returns at `src/core/Track.cpp:~883-896`, before `deleteClips()`, so the
  clips the freeze muted stay muted. Result: silent track, as the finding says.
- **Fix.** `src/core/Track.cpp:866-877`: the clear and `loadFrozenTake()` now run only when `!presetMode`. A full-track load (project
  open, journal restore, clone) is unchanged: it still resets on absence, so the R6 reset-on-absence contract holds for every
  non-preset caller. Callers of the preset path: `Track::loadPreset` (`Track.cpp:~1057`), reached from
  `TrackContainerView.cpp:604` and `InstrumentTrack.cpp:1407`; all want the take left alone.
- **No header change**, nothing on the audio thread changes (`clearFrozenTake()` is merely not called; its render-lock use is
  untouched), no control command added, no saved-data format change.
- **Regression test.** `FreezeRegionRepairTest::loadingAPresetKeepsTheFrozenTakeAndItsMutedClips`
  (`tests/src/core/FreezeRegionRepairTest.cpp:249`). It takes a preset from an unfrozen sample track, freezes a region (one clip muted),
  calls `loadPreset`, and checks the take (window 60-120) and the muted clip are still there, then that `unfreeze()` still releases the
  clip. Deterministic: no timing, no audio rendering.

## Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5412.r1/logs)

| Step | Command | EXIT |
|---|---|---|
| RED build (test only) | `cmake --build build --target FreezeRegionRepairTest -j4 > build-1.log 2>&1` | 0 |
| RED run | `cd build/tests && ctest -R FreezeRegionRepairTest --output-on-failure > test-red-1.log 2>&1` | 8 (1 failed: the new slot, `'track->isFrozen()' returned FALSE`; 13 passed) |
| GREEN build | `cmake --build build --target zene FreezeRegionRepairTest TakeLaneTest FrozenTakeHandoffTest -j4 > build-2.log 2>&1` | 0 |
| GREEN run (family) | `ctest -R "FreezeRegionRepairTest\|TakeLaneTest\|FrozenTakeHandoffTest\|Freeze" --output-on-failure > test-green-1.log 2>&1` | 8, see note |
| 4b revert | `git diff -- src/core/Track.cpp > fix.patch && git apply -R fix.patch` | 0 |
| 4b build | `cmake --build build --target FreezeRegionRepairTest -j4 > build-3.log 2>&1` | 0 |
| 4b run (no fix) | `ctest -R FreezeRegionRepairTest --output-on-failure > test-red-2.log 2>&1` | 8 (new slot fails at `FreezeRegionRepairTest.cpp:271`; 13 passed, 1 failed) |
| 4b re-apply | `git apply fix.patch` | 0 |
| 4b build | `cmake --build build --target FreezeRegionRepairTest -j4 > build-4.log 2>&1` | 0 |
| 4b run (with fix) | `ctest -R FreezeRegionRepairTest --output-on-failure > test-green-2.log 2>&1` | 0 (100% passed) |
| Gates | `python3 .../feature_check.py <worktree> > gates-1.log 2>&1` | 0, RESULT: PASS (8 gates PASS) |

**Note on the family run (EXIT 8).** Of 9 matched tests, FrozenTakeHandoffTest, FreezeRegionRepairTest and TakeLaneTest passed. Three
(BatchFreezeTest, MixerBusFreezeTest, MixerBusFreezeMenuTest) were "Not Run" because I did not build their targets. I did not
run them. ControlFreezeCommandsTranscript reported Failed; its log ends with
`PASS: no audible instrument in this build: Skipped, never Passed` right after `plugin.list` returned `loadable_count:0`, before any
freeze or preset step. It exits 77 for that environment reason and its `set_tests_properties` has no `SKIP_RETURN_CODE`
(tests/CMakeLists.txt:7364), so ctest counts it as failed. I did not run it on the baseline, so "unrelated to this change" is
inferred from where the log stops, not measured. ControlFreezeTracksTranscript and ControlBusFreezeTranscript were Skipped.

## Other findings

- `ControlFreezeCommandsTranscript` (tests/CMakeLists.txt:7360) lacks `SKIP_RETURN_CODE 77`, unlike its two sibling transcripts, so on
  a build with no loadable instrument it shows as Failed instead of Skipped. Not fixed here.
- The helper assistant used `awk` in a shell once despite the shell-search restriction; no result depended on it.

Delegation: Haiku ran the RED build/test, the GREEN build/ctest family, and the 4b revert/rebuild/rerun sequence and reported exit codes
and failing slot names; I checked the failing assertion line, the `git diff --stat` and the gate result myself. No DeepSeek (unavailable).

## Self-check

- **Fails reverted, passes fixed.** With `src/core/Track.cpp` reverted (`git apply -R fix.patch`, EXIT=0) the new slot fails
  (`ctest` EXIT=8, `test-red-2.log`, `FreezeRegionRepairTest.cpp:271`, 13 passed / 1 failed); with the fix re-applied it passes
  (`ctest` EXIT=0, `test-green-2.log`). The first RED (before any fix) was EXIT=8 too (`test-red-1.log`).
- **Deterministic.** No sleeps, clocks or render loop: it drives `savePreset`, `isolateRegion`, `freezeTo`, `loadPreset` and
  `unfreeze` directly on the control thread, with fixed ticks (60/120) and a generated WAV in a `QTemporaryDir`.
- **Cause, callers, siblings.** The cause is the unconditional `clearFrozenTake()` in `loadTrack()`. `loadTrack` has two callers,
  `Track::loadPreset` (`presetMode=true`, `Track.cpp:~1059`) and `Track::loadSettings` (`false`, `Track.cpp:~1077`). Only the
  preset caller changes. `loadPreset` is reached from `TrackContainerView.cpp:604` and `InstrumentTrack.cpp:1407`
  (instrument-preset load), and from the control restore path noted in `tests/control-socket-integration.py:887`. The sibling
  restore of clips (`deleteClips`, rejected-take unmute, take lanes) already sits after the preset return, so a preset never touched
  it; the frozen reset was the one piece of full-track state that ran for presets. Other state in the preset block (name, mute,
  solo, monitor, colour) is outside this bug and unchanged.
- **No collateral damage.** `!presetMode` is the full-track path as before, so project open, journal restore and clone behave
  exactly as they did (reset on absence kept). Presets contain no frozen attributes (`Track.cpp:736-739`), so no saved data is read
  differently. No limit or check added.
- **Realtime / undo / commands.** No audio-thread code touched, no new control command, no undo change.
- **Verdict is FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.
- **Sibling commands in command files.** I changed no command file (`git diff` of the fix touches only `src/core/Track.cpp` and
  `tests/src/core/FreezeRegionRepairTest.cpp`), so there are no sibling commands to reconcile. The "tests added or changed" list in
  the check names other test files; those come from the branch's merge history, not from this change.
