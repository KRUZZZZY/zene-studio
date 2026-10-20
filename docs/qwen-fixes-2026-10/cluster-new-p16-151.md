FIXED

# Cluster new-p16-151: huge ONNX segments overflow signed chunk arithmetic

Finding: `src/core/OnnxRuntimeStemSeparator.cpp:226` (historical line). `paddedLength` and the chunk count are `int` with no bound, so a huge `segmentFrames` (the finding's example: 1,500,000,000) overflows `frames + padLeft + segment` (2.25e9 > INT_MAX = 2,147,483,647) before the window allocation.

## Evidence (current tree, branch bugs/qwen-new-p16-151)

- The request is unbounded upstream: `src/core/ControlCommandsStems.cpp:127-129` takes `segment_frames` with `toInt()` and no range check; `src/core/ControlStemSupport.cpp:245` stores it, and `src/core/StemJobManager.cpp:519` passes it to `separate()`.
- Overflowing int expressions in the loop (before the fix): `:256` `paddedLength = frames + padLeft + segment`; `:257` `chunkCount = (paddedLength + hop - 1) / hop`; `:259` `std::vector<float> window(segment)`; `:288` `inputTensor[segment + i]`; the stereo reads `data[segment + i]` in the accumulation loop; `start + i - padLeft` (start < paddedLength).
- Fix: `src/core/OnnxRuntimeStemSeparator.cpp:161-174` `segmentFits()` checks `frames + segment/2 + 2*segment <= INT_MAX`, computed in `std::int64_t`. That sum bounds every index above: `paddedLength < sum`, `paddedLength + hop - 1 < sum`, `start + i < paddedLength + segment < sum`, `segment + i < 2*segment < sum`.
- `:176-180` `segmentFor()` replaces the old ternary (`segmentFrames > 1 ? segmentFrames : HTDemucsSegmentFrames`) and returns 0 when refused.
- `:227-231` `separate()` calls `segmentFor()` after the `frames` check and before `initializeSession` and the window allocation, returning `Status::Failed` with the error.
- Header seam: `include/StemSeparation/OnnxRuntimeStemSeparator.h:61-64` (private statics; the test is a friend, as with `m_postRunTestHook`).
- Commits: `527706064` (seam scaffold, returns true, so behaviour is unchanged; committed on its own so the revert proof touches only the fix) and the fix commit that contains this report.
- Tests: `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:198-222` adds `testSegmentIndexArithmeticIsBounded` (seam: the default segment and the exact boundary `1 + 429496729 + 2*858993458 = INT_MAX` pass, `858993459` fails, `1.5e9` fails with "too long") and `testOverlongSegmentIsRefusedBeforeAllocation` (`separate()` with 1.5e9 returns Failed with "too long").

Why the seam: running `separate()` at a segment that overflows allocates a 6 GB window and a 12 GB input tensor on the unfixed code (and this machine is shared, 30 GB). The seam runs the same bound with no allocation, so the RED is an assertion, not a memory event.

## Commands and exit codes

| Step | Command (run in the worktree, or in build/tests where noted) | EXIT | Log |
|---|---|---|---|
| build (first try) | `cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-1.log 2>&1` | 2 | ccache could not create `/run/user/1000/ccache-tmp` (permission). Not a code error. |
| build (ccache temp in /tmp) | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16 cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-2.log 2>&1` | 0 | build-2.log |
| **RED** | `cd build/tests && QT_QPA_PLATFORM=offscreen ./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > red-1.log 2>&1` | **1** | red-1.log: `FAIL! ... '!OnnxRuntimeStemSeparator::segmentFits(1, 858993459, &error)' returned FALSE`, `Loc: [...OnnxRuntimeStemSeparatorTest.cpp(204)]` |
| **GREEN (seam, first fix)** | build-3 (same build command, EXIT 0); `QT_QPA_PLATFORM=offscreen ./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > green-1.log 2>&1` | **0** | green-1.log |
| full family (binary) | `./OnnxRuntimeStemSeparatorTest > test-3.log 2>&1` (final tree) | **0** | `Totals: 11 passed, 0 failed` |
| ctest family | `ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-4-ctest.log 2>&1` (from build/tests, final tree) | **0** | `100% tests passed out of 1` (Test #460) |
| complexity fix | the first fix put the bound as an `if` in `separate()`; gate went 26 -> 27 (`gate-complexity-1.log`). Moved the ternary and the bound into `segmentFor()`; `separate` is back at 26 (baseline 26, `tests/complexity-baseline.tsv:12`). | - | gate-complexity-2.log |

Revert-and-reapply proof on the final two-file diff (`include/StemSeparation/OnnxRuntimeStemSeparator.h` + `src/core/OnnxRuntimeStemSeparator.cpp`, vs. the scaffold commit), patch in `logs/fix.patch`:

| Step | Command | EXIT |
|---|---|---|
| revert | `git apply -R logs/fix.patch` | 0 |
| rebuild | `CCACHE_TEMPDIR=/tmp/zene-ccache-tmp-p16 cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-7-revert.log 2>&1` | 0 |
| **RED (proof)** | `./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > red-3-proof.log 2>&1` | **1** (same failing assertion, line 204) |
| reapply | `git apply logs/fix.patch` | 0 |
| rebuild | build-8-reapply.log | 0 |
| **GREEN (proof)** | `./OnnxRuntimeStemSeparatorTest testSegmentIndexArithmeticIsBounded > green-3-proof.log 2>&1` | **0** (`Totals: 3 passed, 0 failed`) |

An earlier proof with the first, cpp-only patch (`logs/fix-v1-cpp-only.patch`) gave the same result: `red-2-proof.log` EXIT 1 and `green-2-proof.log` EXIT 0.

Affected-family tests executed: 11 (`OnnxRuntimeStemSeparatorTest` binary: 9 test functions plus init/cleanup; `Totals: 11 passed`). The ctest family is one test, #460, which runs the 11.

Gates, run as the individual commands `feature_check.py` runs (see below):

| Gate | Command | EXIT | Note |
|---|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | Red on the train on purpose: 6 REGRESSION lines, none in a file this branch changed (XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions, ControlCommandsShortcuts). `separate` is no longer among them (was 26 -> 27 before the `segmentFor` move). |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | Red on the train on purpose: 15 REGRESSION lines, none mentioning the ONNX files (`grep -c Onnx gate-file-length-2.log` = 0). Both changed files are under 500 lines. |

Gate caveat: `feature_check.py` itself cannot run in this sandbox. It writes its logs to `<worktree>/../<name>-feature-check/` (`projects/zene-studio-program/zene-qwen-new-p16-151-feature-check`), and `mkdir` fails with `PermissionError`. That directory is outside my write-jail. Its ratchet rule is "no new item in a changed file". I ran the same commands from the worktree and checked that rule by hand: the ratchet items for my two files are none, and the other lines are in untouched files. I did not get the script's base-comparison table, so the PASS rows for complexity and file-length are my reading, not the script's output.

`submit_check.py` after the fix commit (`logs/gate-submit-2.log`, EXIT 1): PASS for commits on the branch, clean working tree, this report, and tests changed; FAIL only for `gates (feature_check.py)`, which cannot run in this sandbox (see the gate caveat above). The five ASK items are answered in the Self-check section.

## Self-check

1. **The regression fails with the change reverted and passes with it (exit codes).** Yes. RED: `red-1.log` EXIT 1 (first run) and `red-3-proof.log` EXIT 1 (final diff reverted), both at test line 204 (`segmentFits(1, 858993459)` returns true). GREEN: `green-1.log` EXIT 0 and `green-3-proof.log` EXIT 0. The separate-level test `testOverlongSegmentIsRefusedBeforeAllocation` is not run on the reverted code, because the unfixed path allocates about 18 GB before any check. It passes on the fixed code (ctest EXIT 0), but it is not RED-proven. The seam test is the RED proof.
2. **Deterministic, no timing.** Yes. `segmentFits` is a pure function of two ints. The separate-level test uses the bundled stub model and `makeSineMix(1)`. No clock, no sleep, no thread race.
3. **At the cause, complete; callers and siblings checked.** The cause is the unbounded `segment_frames` reaching int arithmetic. The bound covers every int index formed in the loop (`:256`, `:257`, `:288`, the `data[segment + i]` reads, and `start + i - padLeft`), as derived in the Evidence section. Callers: `separate()` is the only caller of the in-process path (`StemJobManager.cpp:519`). `ExternalProcessStemSeparator` passes the value to a Python CLI, not C++ int arithmetic, so it is unchanged. The control command still accepts any int (`ControlCommandsStems.cpp:127`). A huge value now fails the job in the worker with "too long" instead of overflowing. Refusing it at the command is a possible follow-up; I did not do it, because it changes the command's contract.
4. **No collateral damage.** The default segment (343980) and every segment with `frames + segment/2 + 2*segment <= INT_MAX` are unchanged. Refused inputs are exactly those whose loop indices already overflow int. For the default segment that is inputs above about 2.1466e9 frames (over 13 hours at 44.1 kHz); the bound is conservative by at most 2 frames at that edge. No saved data or project path touches this (it is the stem job's in-memory backend). No new command, no undo change.
5. **Verdict FIXED** (not ALREADY-FIXED or FALSE-POSITIVE), so no code-citation burden beyond the evidence above.

Realtime and registration: `separate()` runs on the stem worker thread, not the audio thread. The new code allocates only on the refusal path (`QStringLiteral`). No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged; `fork-sources-gate` EXIT 0.
