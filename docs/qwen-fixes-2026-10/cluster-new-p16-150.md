Verdict: FIXED (function-level hardening; the data path the finding names is already blocked, see below)

# Cluster new-p16-150: NaN velocity jitter propagates into the velocity factor

Finding: `src/core/NoteRandom.cpp:82-89` (BUG 9). `velocityFactor()` tested `jitter <= 0.f`, which is false for NaN, so NaN reached `std::min` and the function returned NaN.

## Decision

The function defect is real and is fixed. The reach the finding describes is not: no project file, command or in-tree caller can hand NaN to `velocityFactor()` today. The fix is therefore a hardening of the function's own contract, not a change to any reachable behaviour.

Evidence that the data path is closed (all on this branch, `37eaf07557`):

| Boundary | Code | What it does with non-finite jitter |
| --- | --- | --- |
| Project load | `src/core/Note.cpp:78-83` (`loadMidiDepthValue`) | Non-finite `veljit` is replaced by the fallback 0 before `setVelocityJitter` |
| Note setter | `src/core/Note.cpp:377-381` (`Note::setVelocityJitter`) | `std::isfinite(jitter) ? clamp(jitter, 0, 1) : 0` |
| Note command | `src/core/ControlCommandsNoteEdit.cpp:338` | Goes through the setter above |
| Randomize command | `src/core/ControlCommandsNoteRandom.cpp:213-218` (`readJitter`) | Refuses non-finite with `InvalidArgs` |
| Playback caller | `src/tracks/InstrumentTrackNotes.cpp:78-80` | Calls only under `velocityJitter() > 0.f` (false for NaN) |
| Randomize roll | `src/core/ControlCommandsNoteRandom.cpp:125-127` | Calls only under `velocityJitter > 0.0` (false for NaN) |
| Snapshot struct | `src/core/ControlCommandsNoteEdit.cpp:387` | Writes `NoteState::velocityJitter` on a copy used for the before/after comparison only; the note itself is written by the setter at line 338 |

So NaN is not reachable from data or commands. The only way to reach it is a direct call to the free function. The fix makes the function total over its input, matching the setter's policy (non-finite is not jitter, so the velocity is untouched).

## Change

- `src/core/NoteRandom.cpp:85-87`: the guard is now `!std::isfinite(jitter) || jitter <= 0.f` and returns `1.f`. Added `#include <cmath>` (line 27).
- Finite behaviour is unchanged. One deliberate behaviour change for **+inf** (it was clamped to jitter 1, giving a factor in [0, 2]; it now returns 1), which matches `Note::setVelocityJitter`'s treatment of +inf as "not jitter". -inf already returned 1.
- No header change (`include/NoteRandom.h`); the contract is documented in the `.cpp` comment. No new control command, no undo or audio-thread change: the added check is a branch on a float.

## Regression test

`tests/src/core/NoteRandomTest.cpp`: new slot `velocityJitterNonFiniteIsNeutral()` (comment lines 221-223, slot lines 224-238). For NaN, +inf and -inf jitter, it checks `velocityFactor(...) == 1.f` for every note in the fixture. Deterministic: fixed seed, no timing, no sleeps. Added `#include <limits>` (line 35). QTest discovers the slot automatically.

Audio-thread check: `velocityFactor` is called from `decorateNoteHandle` (`InstrumentTrackNotes.cpp:80`). The change adds only `std::isfinite` (a bit test) to the existing branch: no allocation, free, lock, wait, I/O or log.

## Commands and exit codes

Environment note: the machine's `ccache.conf` sets `temporary_dir` to a path outside this worktree's write area, so the first build failed inside ccache. Every build below after the first sets `CCACHE_TEMPDIR` to a directory under `$TMPDIR`; the compiler inputs are unchanged.

| Step | Command (run from the worktree unless stated) | EXIT |
| --- | --- | --- |
| Build attempt (ccache temp dir not writable) | `cmake --build build --target NoteRandomTest -j4 > logs/build-1.log 2>&1` | 2 (ccache permission error; no compile of the test) |
| RED build | `export CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p16-150; cmake --build build --target NoteRandomTest -j4 > logs/build-2.log 2>&1` | 0 |
| **RED run** (fix not yet applied) | `cd build/tests && ctest -R '^NoteRandomTest$' --output-on-failure > logs/test-2-red.log 2>&1` | **8** |
| RED assertion | `logs/test-2-red.log`: `FAIL! : NoteRandomTest::velocityJitterNonFiniteIsNeutral() 'f == 1.f' returned FALSE`, `Loc: [NoteRandomTest.cpp(235)]`; `Totals: 9 passed, 1 failed` | |
| GREEN build (zene + test) | `cmake --build build --target zene NoteRandomTest -j4 > logs/build-3-green.log 2>&1` | 0 |
| GREEN run | `cd build/tests && ctest -R '^NoteRandomTest$' --output-on-failure > logs/test-3-green.log 2>&1` | 0 |
| GREEN run, per slot | `ctest -R '^NoteRandomTest$' -V > logs/test-3b-green-verbose.log 2>&1` | 0; 10 `PASS :` lines, including `velocityJitterNonFiniteIsNeutral()` |
| **4b: save product diff** | `git diff -- src/core/NoteRandom.cpp > logs/fix.patch` | 0 |
| **4b: revert product change** | `git apply -R logs/fix.patch` | 0 |
| 4b: rebuild reverted | `cmake --build build --target zene NoteRandomTest -j4 > logs/build-4-revert.log 2>&1` | 0 |
| **4b: RED with fix reverted** | `cd build/tests && ctest -R '^NoteRandomTest$' --output-on-failure > logs/test-4-proof-red.log 2>&1` | **8** (`FAIL! ... returned FALSE`, `Totals: 9 passed, 1 failed`) |
| 4b: re-apply fix | `git apply logs/fix.patch` | 0 |
| 4b: rebuild with fix | `cmake --build build --target zene NoteRandomTest -j4 > logs/build-5-reapply.log 2>&1` | 0 |
| **4b: GREEN with fix restored** | `cd build/tests && ctest -R '^NoteRandomTest$' -V > logs/test-5-proof-green.log 2>&1` | **0** (`Totals: 10 passed, 0 failed`) |
| Family build | `cmake --build build --target MidiProbabilityPersistenceTest NoteRandomTest -j4 > logs/build-6-family.log 2>&1` | 0 |
| Family run | `cd build/tests && ctest -R '^(NoteRandomTest\|MidiProbabilityPersistenceTest)$' -V > logs/test-6-family.log 2>&1` | 0; 2/2 tests executed; 20 `PASS :` lines, 0 `FAIL!` |
| Gates (feature_check.py) | see note below | 0; `RESULT: PASS`, 8/8 rows PASS |

Affected family: `NoteRandomTest` (10 slots, 1 ctest) and `MidiProbabilityPersistenceTest` (10 slots, 1 ctest), both run from `build/tests`. Executed: 2 tests, 0 failed.

Gate note: `feature_check.py` writes its logs to `<worktree>-feature-check`, a sibling of the worktree that this lane may not write to (`mkdir` gave PermissionError; `logs/gates-1-feature-check.log`). I did not edit the script. I ran the unmodified script with only its log directory redirected into the LOG FOLDER (`logs/gates-2-feature-check.log`, EXIT=0). Re-run with this report included: `logs/gates-3-feature-check-final.log`, EXIT=0, 8/8 PASS. Its baseline for the merge-base `37eaf07557` was already cached, so the `git worktree add` path in the script was not taken.

## Requirements check

1. Decision with evidence: FIXED, with the reachability table above.
2. Regression test: `velocityJitterNonFiniteIsNeutral` FAILS with the fix reverted (exit 8) and PASSES with it (exit 0).
3. Fix at the cause: the only place that decides the neutral answer is the guard in `velocityFactor`. Its callers (`InstrumentTrackNotes.cpp:78-80`, `ControlCommandsNoteRandom.cpp:125-127`) and the setter/loader/command boundaries were checked and stay consistent.
4. No collateral damage: finite inputs are unchanged (`velocityJitterBounds` and `velocityJitterIsSeeded` pass). The only changed output is for non-finite jitter, which no reachable path produces.
5. Realtime, undo, commands: no new allocation or lock; no undo step; no command added.
6. Registration and gates: `src/core/NoteRandom.cpp` is already in `tests/fork-sources.txt:1828`; no new files. `feature_check.py` RESULT: PASS.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Reverting `src/core/NoteRandom.cpp` (saved as `logs/fix.patch`, `git apply -R`), rebuilding and running `ctest -R '^NoteRandomTest$'` from `build/tests` gives EXIT=8 with `FAIL! : NoteRandomTest::velocityJitterNonFiniteIsNeutral() 'f == 1.f' returned FALSE` (`logs/test-4-proof-red.log`). With the fix re-applied and rebuilt, the same command gives EXIT=0, `Totals: 10 passed, 0 failed` (`logs/test-5-proof-green.log`). The first RED run, before any fix, is the same (`logs/test-2-red.log`, EXIT=8).

2. **Deterministic.** The slot uses the fixed `s_notes` fixture and the fixed seed `5`, and calls only the pure `velocityFactor` function. There is no clock, sleep, thread or I/O. The inputs are the three values `NaN`, `+inf`, `-inf`.

3. **Fix at the cause, complete.** The only decision is the guard at `src/core/NoteRandom.cpp:87`. Callers checked: `src/tracks/InstrumentTrackNotes.cpp:78-80` and `src/core/ControlCommandsNoteRandom.cpp:125-127` both guard with `> 0` before calling, so they never pass NaN; the note boundaries (`Note.cpp:78-83`, `Note.cpp:377-381`, `ControlCommandsNoteRandom.cpp:213-218`, `ControlCommandsNoteEdit.cpp:338`) already refuse or map non-finite jitter. Sibling commands in the command files: the randomize command refuses non-finite input, and the note-edit command writes through the setter. Nothing else needs to change.

4. **No collateral damage.** Finite jitter takes the same arithmetic path as before (`velocityJitterBounds` and `velocityJitterIsSeeded` pass, 10/10 slots). The only changed output is for non-finite jitter, which no project load, command or in-tree caller produces (table above). No limit or check was added, so no caller is affected by one. `MidiProbabilityPersistenceTest` (10/10) passes.

5. **Verdict evidence (ALREADY-FIXED / FALSE-POSITIVE only).** Not applicable: the verdict is FIXED. The contract the fix follows is the one already written at `src/core/Note.cpp:379-381` (a non-finite value "is not velocity jitter ... the documented default is an untouched velocity").

## Submit-check note

`submit_check.py` runs `feature_check.py` as a subprocess without the log-directory redirect, so its gate row fails with the same `PermissionError` on the sibling directory `<worktree>-feature-check` (`logs/submit-check-1.log`). The gate itself, with its logs redirected into the LOG FOLDER, passes: `logs/gates-3-feature-check-final.log`, EXIT=0, 8/8 PASS.
