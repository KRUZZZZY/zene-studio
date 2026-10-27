FALSE-POSITIVE

# Cluster new-p21-126: note position and duration "swapped" on DAWproject import

## Verdict

The finding's premise is wrong. `Note`'s first constructor parameter is the **length**, and its second is the **position**. The import at `src/core/DawProjectSession.cpp:583-586` passes `duration` first and `time` second, which is the correct order. No code change is needed.

The finding's suggested fix ("pass `TimePos(time)` first and `TimePos(duration)` second") would introduce the bug it describes. Do not apply it.

## Evidence

- `include/Note.h:106-107`: `Note( const TimePos & length = TimePos( 0 ), const TimePos & pos = TimePos( 0 ), int key = DefaultKey, ...)`. Length comes first.
- `src/core/Note.cpp:151-152`: the definition matches (`Note( const TimePos & length, const TimePos & pos, ...)`).
- `src/core/DawProjectSession.cpp:583-586` (the flagged call): `Note note(TimePos(...noteEntry.duration...), TimePos(...noteEntry.time...), noteEntry.key, ...)`. This is length first, then position, which matches the header.
- Every other `Note` construction in the tree uses the same order. Examples: `src/core/ControlCommandsClipImportFile.cpp:245`, `src/core/ControlCommandsNoteShape.cpp:218`, `src/core/ScriptBindings.cpp:496`, `src/core/ScriptEngine.cpp:483`, `src/core/ControlCommandsAudioSlice.cpp:238`.
- The export side agrees: `src/core/DawProjectSession.cpp:339-340` writes `time = note->pos()` and `duration = note->length()`.
- The reader agrees: `src/core/DawProjectReadTracks.cpp:124-138` reads the `Note` attributes by name: `duration` at :124 (checked for negatives), `time` at :135, and both are assigned to `note.time` and `note.duration`.

The finding's claim that "LMMS Note's constructor takes position then length" is the inverse of `Note.h:106-107`.

## Gap found and closed (test only)

The existing import test `releaseVelocitySurvivesAnImportAndExport` (`tests/src/core/DawProjectInterchangeRoundTripTest.cpp`) uses `time = 0`. It asserts velocity and release velocity, but not position or length, so a swap would not fail it. I added one guard test next to it:

- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`: new slot `noteTimeIsPositionAndDurationIsLength()`. It applies a note at `time = 1.0`, `duration = 2.0` through `applyDawProjectModel`, reads it back through `dawProjectModelFromSong`, and asserts `time == 1.0` and `duration == 2.0`. The asymmetric values are chosen so that a swap cannot pass.

The test passes on the unmodified code, as it should: the code is correct. Its regression proof comes from the mutation check below.

## Commands and results

All commands ran from the worktree `zene-qwen-new-p21-126`. The configured build tree is `build/`.

Environment note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied` under `~/.local/share/zene-bunny-fix/dsh/...`. The write-jail blocks that path. Every build after the first used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny-tmp` (environment only, no repo change). The first build log is kept as `build-1.log` for the record.

| Step | Command | EXIT | Log (LOG folder) |
|---|---|---|---|
| Build, failed (ccache permission) | `cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 2 | build-1.log |
| Build, after ccache temp fix | `CCACHE_TEMPDIR=... cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | build-2.log |
| Count tests in family | `cd build/tests && ctest -N -R DawProjectInterchangeRoundTripTest` | 0 (1 test) | ctest-list-1.log |
| Family, baseline (unmodified code) | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 (100%, 1/1) | test-1.log |
| New test by name, baseline | `cd build/tests && ./DawProjectInterchangeRoundTripTest noteTimeIsPositionAndDurationIsLength` | 0 (3 passed, 0 failed: init, the new test, cleanup) | test-2-green-baseline.log |
| Mutation: swap the two args in `DawProjectSession.cpp:583-586` (reproduces the finding), rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | build-4-mutant.log |
| **RED** (mutant): new test | `cd build/tests && ./DawProjectInterchangeRoundTripTest noteTimeIsPositionAndDurationIsLength` | **1** (`FAIL!` at `DawProjectInterchangeRoundTripTest.cpp:1832`: `roundTripped.time` is `2`, expected `1`) | test-4-red-mutant.log |
| Restore original `DawProjectSession.cpp` (Edit), rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene DawProjectInterchangeRoundTripTest -j4` | 0 | build-5-restored.log |
| **GREEN** (restored): new test | `cd build/tests && ./DawProjectInterchangeRoundTripTest noteTimeIsPositionAndDurationIsLength` | **0** (3 passed, 0 failed) | test-5-green-restored.log |
| Family after restore | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 (100%, 1/1) | test-6-family.log |
| Product tree clean after restore | `git diff --stat -- src/` | empty output | (shell) |

A first mutant attempt (build-3-mutant.log) exited 2 because the same ccache permission error hit it. Its test run (test-3-red-mutant.log) used the stale binary and passed, so I discarded it and repeated the mutant build with the ccache fix. The result that counts is test-4-red-mutant.log, which ran against a rebuilt binary.

Affected-family test count: 1 ctest test, 1 test binary; the new function runs inside it.

Gates. `feature_check.py` could not run as installed. It creates its output folder at `<worktree>/../zene-qwen-new-p21-126-feature-check`, which is outside the write-jail (`PermissionError`, output in the session transcript). I ran a copy of the same driver with only two paths changed: the output folder points at the LOG folder, and the baseline cache root points at the original `zene-bunny-fix/qwen` directory. The gate commands and comparison logic are unchanged.

| Command | EXIT | Log |
|---|---|---|
| `python3 feature_check.py <worktree>` (as installed) | 1 (PermissionError, no gate ran) | gates-1.log |
| `python3 <LOG>/feature_check_logdir.py <worktree>` (copy, logs redirected) | 0 (RESULT: PASS: all 8 rows PASS) | gates-2.log, feature-check/ |

## Files

- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`: the new guard test.
- `docs/qwen-fixes-2026-10/cluster-new-p21-126.md`: this report.

No product file changes. The test file was already registered (`tests/CMakeLists.txt:1011`, `tests/all-sources.txt:2684`), so no registration change is needed.

## Self-check

`submit_check.py` output: `<LOG>/submit-check-1.log`, EXIT 1 before the commit. Items:

- **Commit on the branch / working tree clean: FAIL before the commit, fixed by the commit that adds this report and the test.** Both were expected at that point; the work was not committed yet.
- **Gates (`feature_check.py`): FAIL, environment only.** The installed script exits with `PermissionError` when it creates `<worktree>/../zene-qwen-new-p21-126-feature-check` (`gates-1.log`), which is outside the write-jail. The same driver with only the output folder redirected passes all 8 rows, EXIT 0 (`gates-2.log`, `feature-check/`). Gate commands and comparison logic are unchanged, but the official run has not been executed in this session, so this FAIL stands until someone runs it from a session that can write the sibling folder.
- **Report, tests added or changed: PASS.**

Judgement items:

1. *Regression test fails with the product change reverted and passes with it:* this is a false-positive verdict, so there is no product fix to revert. The product code is already correct. The test is a guard, and its proof is the mutation: swapping the two arguments at `DawProjectSession.cpp:583-586` (the finding's bug) gives `FAIL!` at `DawProjectInterchangeRoundTripTest.cpp:1832`, EXIT 1 (`test-4-red-mutant.log`). The original code gives EXIT 0, 3 passed (`test-5-green-restored.log`). The product file was restored, and `git diff --stat -- src/` is empty.
2. *Deterministic:* yes. The test uses fixed inputs (1.0 and 2.0 beats) and the in-memory model path `applyDawProjectModel` and `dawProjectModelFromSong`. It has no clock, sleep, thread, or file I/O.
3. *Fix at the cause, every caller and sibling checked:* there is no product change. I checked every `Note` construction in `src/` (see Evidence), the export at `DawProjectSession.cpp:339-340`, and the reader at `DawProjectReadTracks.cpp:124-138`. All use the same order and agree.
4. *No collateral damage:* no product code, data format, or limit was touched. The old-project load path is unchanged. The only file changed is the test, plus this report.
5. *FALSE-POSITIVE evidence:* `include/Note.h:106-107` and `src/core/Note.cpp:151-152` (length first), and `src/core/DawProjectSession.cpp:583-586` (the flagged call, length first). The suggested fix in the finding would break the import, so it was not applied.
