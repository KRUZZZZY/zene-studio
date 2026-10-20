# Cluster new-p16-146: frequency updates leave the dirty flag set

**Verdict: FIXED**

`NotePlayHandle::setFrequencyUpdate()` sets `m_frequencyNeedsUpdate`, and nothing ever cleared it. Once a note was marked, `play()` called `updateFrequency()` on every later period for the life of the note. `updateFrequency()` now spends the mark before it reads its inputs, so a note recomputes its frequency only when something marks it again.

## Evidence

- `src/core/NotePlayHandle.cpp:307` - `play()` tests the flag and calls `updateFrequency()` (the slide-glide clause is unchanged).
- `src/core/NotePlayHandle.cpp:620-624` - `updateFrequency()` clears `m_frequencyNeedsUpdate` before it reads `masterPitch`, `baseNote`, detuning, pitch, slide and MPE pitch. A mark set while the update runs survives to the next period.
- Before the change, the flag had no writer except `setFrequencyUpdate()` (`include/NotePlayHandle.h:336-339`) and no clear anywhere. The only reader is `NotePlayHandle.cpp:307`.
- Every caller of `updateFrequency()` now spends the mark: the constructor (`:117`), `play()` (`:309`), `processTimePos()` (`:715`) and the sub-note recursion (`:695`). The fix sits in the function, not in `play()`, so no caller is left out.
- Callers that mark (unchanged): `src/tracks/InstrumentTrack.cpp:440, 519, 803`, `src/tracks/InstrumentTrackNotes.cpp:92`, `include/ChordFollow.h:50`, `include/InstrumentTrack.h:135-139` (retune), `src/core/SessionTuning.cpp:315`.

### Regression test

`tests/src/core/MpePlaybackTest.cpp`, new slot `aFrequencyMarkIsSpentByItsUpdate()`:

1. `setFrequencyUpdate()` and one rendered block: the mark is applied.
2. `setMpePitchCents(1200)` with no mark, one more block: the frequency must stay the same (an unmarked input must not be picked up).
3. A new mark: the octave bend is applied on the next block (frequency doubles).

Step 2 fails before the fix: `261.626 Hz became 523.251 Hz`.

Registration: `tests/upstream-modifications.txt` (last line) for `src/core/NotePlayHandle.cpp`. `MpePlaybackTest.cpp` is an existing test file and was not re-registered.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-146.r1/logs/`.

| # | Command (run from the worktree or `build/tests` as noted) | Log | EXIT | Result |
|---|---|---|---|---|
| 1 | `cmake --build build --target MpePlaybackTest -j4` | build-1.log | 2 | ccache could not write under `/run/user/1000`; no code compiled |
| 2 | `CCACHE_TEMPDIR=/tmp/zene-new-p16-146-ccache-tmp cmake --build build --target MpePlaybackTest -j4` (RED build) | build-2.log | 0 | built with the test, without the fix |
| 3 | `ctest -R '^MpePlaybackTest$' --output-on-failure` (from `build/tests`, **RED**) | test-red-1.log | 8 | 7 passed, 1 failed: `aFrequencyMarkIsSpentByItsUpdate` - `the spent mark recomputed the frequency: 261.626 Hz became 523.251 Hz` |
| 4 | `git diff -- src/core/NotePlayHandle.cpp > fix.patch` | - | 0 | fix saved to the logs folder |
| 5 | `git apply -R fix.patch` (step 4b, revert) | - | 0 | product file back to unfixed |
| 6 | `cmake --build build --target zene MpePlaybackTest -j4` (with `CCACHE_TEMPDIR`) | build-4.log | 0 | |
| 7 | `ctest -R '^MpePlaybackTest$' -V` (unfixed, step 4b) | test-4b-unfixed.log | 8 | same FAIL as row 3 |
| 8 | `git apply fix.patch` (re-apply) | - | 0 | |
| 9 | `cmake --build build --target zene MpePlaybackTest -j4` (with `CCACHE_TEMPDIR`) | build-5.log | 0 | |
| 10 | `ctest -R '^MpePlaybackTest$' -V` (**GREEN**, step 4b) | test-4b-fixed.log | 0 | `PASS : MpePlaybackTest::aFrequencyMarkIsSpentByItsUpdate()`; 100% passed |
| 11 | `cmake --build build --target zene MpePlaybackTest -j4` (after the final edit) | build-3.log | 0 | |
| 12 | `ctest -R '^MpePlaybackTest$'` (GREEN) | test-green-1.log | 0 | 1/1 passed |

### Related family (regression check of the behaviour change)

The fix changes when a note recomputes, so I ran the other tests that cover note pitch, MPE, slides, tuning and articulation.

| # | Command | Log | EXIT | Result |
|---|---|---|---|---|
| 13 | build of the family targets, first attempt (`MpeExpressionTest MpeInputPathTest MpeNoteStorageTest SessionTuningTest SlideNotesTest ClipTransposeTest NotePlaybackTest ExprSynthTest ArticulationPlaybackTest MpePlaybackTest`) | build-6.log | 2 | `No rule to make target 'ExprSynthTest'`; the CMake target is `ExprSynth` |
| 14 | `cmake --build build --target InstrumentLayerZonePublishTest AudioFileProcessorPlayheadTest ...` | build-7.log | 0 | |
| 15 | `cmake --build build --target ExprSynth` | build-8.log | 0 | |
| 16 | `cmake --build build --target ExprSynthAllocation` | build-9.log | 2 | no such target; the target is `ExprSynthAllocationTest` |
| 17 | `cmake --build build --target ExprSynthAllocationTest` | build-10.log | 0 | |
| 18 | `ctest -R '^(...family...)$'` (first run) | test-family-1.log | 8 | 2 failures from missing plugins, see below |
| 19 | `cmake --build build --target monstro audiofileprocessor` (with `env -u MAKEFLAGS`) | build-12.log | 0 | plugins built |
| 20 | `ctest -R '^(NotePlaybackTest\|AudioFileProcessorPlayheadTest)$' -V` | test-family-4.log | 0 | 23/23 and 3/3 pass |
| 21 | `ctest -R '^(SessionTuningTest\|MpeExpressionTest\|MpeInputPathTest\|MpeNoteStorageTest\|MpePlaybackTest\|ClipTransposeTest\|SlideNotesTest\|NotePlaybackTest\|ArticulationPlaybackTest\|ExprSynth\|ExprSynthAllocation\|InstrumentLayerZonePublishTest\|AudioFileProcessorPlayheadTest)$' --output-on-failure` | test-final-family.log | 0 | **13 tests executed, 13 passed, 100%** |

Rows 18-20 are environmental. The `monstro` and `audiofileprocessor` plugins were not built in the targeted tree (`Plugin not found`, as the QWARN lines in test-family-1.log show). Building them and rerunning passes (row 20). No product code is involved in that failure.

Affected-family test count for the final run (row 21): 13 executed, 13 passed. The regression lives in `MpePlaybackTest` (ctest family of 1, plus the new slot).

### Gates (step 5)

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (row 22): **EXIT=1**. The script creates its log directory beside the worktree (`…/zene-qwen-new-p16-146-feature-check`), which is outside the write jail (`PermissionError`, `gates-1.log`). I did not edit the script.
- The same gate functions, command lists and pass criteria were run with `/tmp/zene-new-p16-146-gates/run_gates.py` (it imports `feature_check.py` unchanged and only redirects the per-gate logs to `logs/gates-1/`): **EXIT=0, RESULT: PASS**. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all PASS (exit 0); complexity and file-length PASS ("no new item in your files"). Summary in `gates-1-summary.log`.

## Behaviour consequence (reviewer decision)

`masterPitch` is read inside `updateFrequency()` (`src/core/NotePlayHandle.cpp:623`), and its change does not mark notes: the `dataChanged` connection in `src/core/Song.cpp:194-195` is commented out. Before this fix, a note that had been marked once recomputed every period, so it also followed master-pitch automation from then on. After the fix it follows master-pitch changes only when it is marked again, like a note that was never marked. This is by reading the code; I did not add a test for it. The consistent behaviour (master-pitch changes marking the handles) would be a separate change, and I did not make it here.

## Self-check

(filled in below after `submit_check.py`)
