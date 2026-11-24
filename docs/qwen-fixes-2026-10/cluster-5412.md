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
