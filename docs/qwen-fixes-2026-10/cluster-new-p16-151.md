FIXED

# Cluster new-p16-151: huge ONNX segments overflow signed chunk arithmetic

Finding: `src/core/OnnxRuntimeStemSeparator.cpp:226` (historical line). `paddedLength` and the chunk count are `int`, so a large segment with a large frame count overflows `frames + padLeft + segment` (`padLeft = segment / 2`) and the other int indices in the loop. The finding's example is `segmentFrames = 1,500,000,000`, but that value is outside the control schema (see Reachability), so the reachable overflow is the schema-valid path with near-INT_MAX frame counts.

## Reachability (corrected in round 2)

The round-1 report called `segment_frames` unbounded. That was wrong. The reachable limits are:

- **Segment:** `src/core/ControlCommandsStems.cpp:352` declares `segment_frames` as `integerProperty(1, 44100 * 600)`, so 1..26,460,000. `src/core/ControlRegistry.cpp:251` runs `validateArgs` before the handler, so a value above that is refused as InvalidArgs. An absent `segment_frames` takes the default: `src/core/ControlStemSupport.cpp:245` `segmentFrames > 0 ? segmentFrames : HTDemucsSegmentFrames` (343980). The GUI calls `stem.job_start` with no `segment_frames` (`src/gui/StemSplitController.cpp:252`), so it always uses the default.
- **Frames:** `src/core/ControlStemSupport.cpp:66-70` `fitsAtModelRate()` accepts a mix whose frame count at the model rate is up to `INT_MAX` (its check is at :69). The mix is decoded whole (`:225`) before that check (`:235`). A file of about 13.5 hours at 44.1 kHz passes it.
- **Overflow:** with frames near `INT_MAX` and a schema-valid segment, the padding arithmetic exceeds `INT_MAX`. For example `frames = INT_MAX` with `HTDemucsSegmentFrames` gives `INT_MAX + 171990 + 343980`. The round-1 regression did not exercise this case: it used `frames = 44100` and a 1.5e9 segment that the schema rejects.

## Evidence (branch bugs/qwen-new-p16-151)

- Guard: `src/core/OnnxRuntimeStemSeparator.cpp:161-174` `segmentFits(frames, segment, error)` returns false when `frames + segment/2 + 2*segment > INT_MAX`, computed in `std::int64_t`. Every index the loop forms is below that sum: `paddedLength` (`:256`) is `frames + padLeft + segment`; the chunk count `(paddedLength + hop - 1) / hop` (`:257`) is below `frames + 2*padLeft + segment`, which is at most the sum; `start + i` is below `paddedLength + segment`; `segment + i` (`:288`, and the stereo reads) is below `2*segment`. The largest index is therefore at most the sum minus 2, so the sum being exactly `INT_MAX` is safe.
- `:176-180` `segmentFor()` maps a requested value of 1 or less to the model default, then calls `segmentFits`, and returns 0 when refused.
- `:227-231` `separate()` calls `segmentFor()` after the `frames` check and before `initializeSession` and the window allocation, and returns `Status::Failed` with the error.
- Header: `include/StemSeparation/OnnxRuntimeStemSeparator.h:59-64` (private statics; the test is a friend, as with `m_postRunTestHook`).
- Commits: `527706064` (seam scaffold: `segmentFits` returns true, so behaviour is unchanged; it is its own commit so the revert proof touches only the fix), `b47c5e7a7` (fix and round-1 tests), `eda23f5cd` (round-1 report wording), and this round's commit (round-2 tests and report).
- Tests, `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp`:
  - `testNearIntMaxFramesWithSchemaSegmentIsRefused` (`:199`): reachable case. `segmentFits(INT_MAX, HTDemucsSegmentFrames)` and `segmentFits(INT_MAX, 26460000)` are refused, and the error contains "too long".
  - `testSegmentIndexArithmeticIsBounded` (`:210`): boundary coverage, with each boundary tested at both values (see below).
  - `testOverlongSegmentIsRefusedBeforeAllocation` (`:233`): direct API only. A 1.5e9 segment is outside the schema. A window that size is 6 GB on the unfixed code, so the test checks the refusal comes before any allocation and that `separate()` wires `segmentFor()` in.

### Boundary arithmetic (checked with Python `int`, not by hand)

The round-1 claim was wrong. `1 + 429496729 + 2*858993458` is `2147483646`, which is `INT_MAX - 1`, not `INT_MAX`. The corrected values:

| Case | Frames | Segment | Sum `frames + seg/2 + 2*seg` | Result |
|---|---|---|---|---|
| HTDemucs, largest accepted | 2,146,623,697 | 343,980 | 2,147,483,647 = INT_MAX | accepted |
| HTDemucs, one more frame | 2,146,623,698 | 343,980 | 2,147,483,648 = INT_MAX + 1 | refused |
| Schema max, largest accepted | 2,081,333,647 | 26,460,000 | 2,147,483,647 = INT_MAX | accepted |
| Schema max, one more frame | 2,081,333,648 | 26,460,000 | 2,147,483,648 = INT_MAX + 1 | refused |
| One frame, pass | 1 | 858,993,458 | 2,147,483,646 = INT_MAX - 1 | accepted |
| One frame, fail | 1 | 858,993,459 | 2,147,483,648 = INT_MAX + 1 | refused |

Under the default segment, inputs above 2,146,623,697 frames (about 13.5 hours at 44.1 kHz) are refused. Under the schema maximum the limit is 2,081,333,647 frames (about 13.1 hours). The round-1 report said the limit was about 2.1466e9 for the default segment, which is correct.

## Commands and exit codes (round 2)

Logs are in `runs/new-p16-151.r2/logs`. Round-1 logs are in `runs/new-p16-151.r1/logs`. Build and tests ran from the worktree or from `build/tests`, as noted. The build used `-j4`.

| Step | Command | EXIT | Result |
|---|---|---|---|
| build (GREEN, final tree) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16 cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-1.log 2>&1` | 0 | |
| ONNX family, binary | `cd build/tests && QT_QPA_PLATFORM=offscreen ./OnnxRuntimeStemSeparatorTest > test-1-binary.log 2>&1` | 0 | `Totals: 12 passed, 0 failed` |
| registered CTest | `cd build/tests && ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-2-ctest.log 2>&1` | 0 | `100% tests passed out of 1` (Test #460) |
| revert product hunks | `git diff 527706064 -- include/StemSeparation/OnnxRuntimeStemSeparator.h src/core/OnnxRuntimeStemSeparator.cpp > fix-hunks-round1.patch && git apply -R fix-hunks-round1.patch` | 0 | Only the fix hunks are reverted; the test file stays. |
| rebuild (reverted) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16 cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-2-revert.log 2>&1` | 0 | |
| **RED, reachable case** | `cd build/tests && ./OnnxRuntimeStemSeparatorTest testNearIntMaxFramesWithSchemaSegmentIsRefused > red-1-reachable.log 2>&1` | **1** | `FAIL! ... '!OnnxRuntimeStemSeparator::segmentFits(intMax, HTDemucsSegmentFrames, &error)' returned FALSE`, `Loc: [...OnnxRuntimeStemSeparatorTest.cpp(205)]` |
| **RED, boundary** | `cd build/tests && ./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > red-2-boundary.log 2>&1` | **1** | `FAIL! ... '!OnnxRuntimeStemSeparator::segmentFits(intMax - htdemucsTail + 1, HTDemucsSegmentFrames, &error)' returned FALSE`, `Loc: [...(220)]` |
| restore | `git apply fix-hunks-round1.patch` | 0 | `git status` shows only the test file modified, before the test commit. |
| rebuild (restored) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16 cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-3-restore.log 2>&1` | 0 | |
| **GREEN, reachable case** | `cd build/tests && ./OnnxRuntimeStemSeparatorTest testNearIntMaxFramesWithSchemaSegmentIsRefused > green-1-reachable.log 2>&1` | **0** | |
| **GREEN, boundary** | `cd build/tests && ./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > green-2-boundary.log 2>&1` | **0** | |
| ONNX family, binary (after restore) | `cd build/tests && ./OnnxRuntimeStemSeparatorTest > test-3-binary.log 2>&1` | 0 | `Totals: 12 passed, 0 failed` |
| registered CTest (after restore) | `cd build/tests && ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-4-ctest.log 2>&1` | 0 | `100% tests passed out of 1` |

The family ran 12 tests: 10 test functions and the init and cleanup hooks. The ctest family is one test, #460, which runs all 12.

Round-1 history (its logs are in `runs/new-p16-151.r1/logs`): the first RED was `red-1.log` (exit 1, `segmentFits(1, 858993459)`), the first GREEN was `green-1.log` (exit 0), and the proof with the final round-1 diff was `red-3-proof.log` (exit 1) and `green-3-proof.log` (exit 0). The first round-1 fix put the bound as an `if` in `separate()`, which moved its complexity from 26 to 27 (`gate-complexity-1.log`). It was moved into `segmentFor()` so the baseline stays 26 (`tests/complexity-baseline.tsv:12`).

### Gates (final tree, round 2)

Logs in `logs/gate-*-1.log` (round 2).

| Gate | Command | EXIT | Note |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | Red on the train on purpose. 6 REGRESSION lines, all "new function over target" in files this branch does not change. `separate` is at 26, equal to its baseline (`gate-complexity-1.log:12`), so it is not a regression. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | Red on the train on purpose. 15 REGRESSION lines, none mentioning the ONNX files. Both changed source files are under 500 lines. |

Gate caveat: `feature_check.py` cannot run in this sandbox. It writes its logs to `<worktree>/../<name>-feature-check/` (`projects/zene-studio-program/zene-qwen-new-p16-151-feature-check`). `mkdir` fails with `PermissionError`, and that directory is outside my write-jail. Its ratchet rule is "no new item in a changed file". I ran the same commands from the worktree and checked that rule by hand. I did not get the script's base-comparison table, so the complexity and file-length PASS readings are mine, not the script's output.

`submit_check.py` is run after this commit (`logs/gate-submit-3.log`). Expected: PASS for commits, clean tree, report, and tests changed; FAIL only for `gates (feature_check.py)`, for the same sandbox reason.

## Self-check

1. **The regression fails with the change reverted and passes with it (exit codes).** Yes. The new reachable test exits 1 when only the product hunks are reverted (`red-1-reachable.log`, line 205), and the boundary test exits 1 (`red-2-boundary.log`, line 220). Both exit 0 after restore (`green-1-reachable.log`, `green-2-boundary.log`). The separate-level test `testOverlongSegmentIsRefusedBeforeAllocation` is not run on the reverted code, because the unfixed path allocates about 18 GB first. It is not RED-proven. The reachable seam test is the RED proof for the reachable path.
2. **Deterministic, no timing.** Yes. `segmentFits` is a pure function of two ints. The new tests use only integer constants and `HTDemucsSegmentFrames`. The separate-level test uses the bundled stub model and `makeSineMix(1)`. No clock, no sleep, no thread race.
3. **At the cause, complete; callers and siblings checked.** The cause is the padding and index arithmetic in `separate()` with no bound. The bound covers every int index formed in the loop (`:256`, `:257`, `:288`, the stereo reads, and `start + i - padLeft`). Callers: `separate()` is reached only through `StemJobManager` (`src/core/StemJobManager.cpp:519`). The GUI submits through the same `stem.job_start` command as the control path (`src/gui/StemSplitController.cpp:252`), so there is one entry point with the schema and frame limits above. The schema (`ControlCommandsStems.cpp:352`) and `fitsAtModelRate` (`ControlStemSupport.cpp:66-70`) are the upstream limits and are unchanged. `ExternalProcessStemSeparator` passes the segment to a Python CLI, not C++ int arithmetic, so it is unchanged. When the bound refuses a job, the job fails in the worker with "too long" and the error is reported through `jobFailed`.
4. **No collateral damage.** Schema-valid inputs whose sum is at most INT_MAX are unchanged. That includes every input the GUI sends (default segment) below 2,146,623,697 frames, and every control input with a segment and frame count inside the limits in the Reachability section. Refused inputs are exactly those whose int index arithmetic would overflow, which the original code already got wrong. No saved data or project path touches this (it is the stem job's in-memory backend), and no command or undo changes. The bound is conservative by at most 2 frames at its edge.
5. **Verdict FIXED** (not ALREADY-FIXED or FALSE-POSITIVE). The round-1 overflow is reachable through the control path at near-INT_MAX frame counts with schema-valid segments, and the guard refuses those inputs before any allocation.

Open follow-up, not in this fix: `ControlStemSupport.cpp:225` decodes the whole file into memory before the frame check at `:235`. A file near the frame limit can use a lot of memory before the job fails. This is not an overflow, so I did not move the check.

Realtime and registration: `separate()` runs on the stem worker thread, not the audio thread. The new code allocates only on the refusal path (`QStringLiteral`). No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged; `fork-sources-gate` EXIT 0.
