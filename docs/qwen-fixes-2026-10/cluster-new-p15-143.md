# Cluster new-p15-143: ONNX outputs accepted by element count alone

**Verdict: FIXED**

The ONNX backend checked only that each stem's output held enough elements before reading it as planar `float` stereo. A tensor of the right size but the wrong element type or channel layout passed, and its values were then read as `float` in the wrong order. The check now requires, for every output the stems are read from, a float32 tensor whose shape (size-1 axes dropped) is exactly `[2, segment]` per stem, or `[S, 2, segment]` with `S >= 4` in the stacked layout. Anything else fails the separation with an error before any `GetTensorData<float>()` call.

## Evidence

Reproduced on the unfixed code: a four-output model with the right element count but transposed `[1, T, 2]` outputs, and one with int64 `[1, 2, T]` outputs, both reported `Success` (status 0) instead of `Failed`. Both are now in the regression test and fail on the unfixed code (RED table below).

- `src/core/OnnxRuntimeStemSeparator.cpp:99` `withoutUnitAxes` drops size-1 axes so `[1, 2, T]` and `[2, T]` compare equal.
- `src/core/OnnxRuntimeStemSeparator.cpp:117` `isStemBlockLayout` refuses non-tensors (`:120`), non-float element types (`:124`), and any shape other than the planar `[2, segment]` block (`:129-131`) or the stacked `[S>=4, 2, segment]` block (`:130`).
- `src/core/OnnxRuntimeStemSeparator.cpp:139-152` `outputsHoldSegment` calls it for each stem's output at `:147` and replaces the old element-count test at the original line 109. The error text is now "is not a float32 stereo block of N frames".
- The only read of tensor data is `src/core/OnnxRuntimeStemSeparator.cpp:310` (`GetTensorData<float>`), which runs after this check. `rg` finds no other ONNX tensor reader in `src/`, `include/` or `tests/`.
- Contract: `tools/stem_split_cli.py:19-21` (per-stem `float32 [1, 2, T]`, or stacked `float32 [1, 4, 2, T]`) and `doc/STEM-SPLIT.md:266-268`.
- Regression fixtures: `tools/make_stub_model.py:161` `build_layout_variant_model` with `--transposed` and `--int64`, written to `tests/data/stub-4stem-transposed.onnx` (762 B) and `tests/data/stub-4stem-int64.onnx` (693 B). The generator still reproduces `stub-4stem-linear.onnx` and `stub-4stem-stacked.onnx` byte-for-byte (`cmp` below).
- Tests: `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:68` helper `expectOutputLayoutRefused`; `:199` `testTransposedOutputsAreRefused`; `:205` `testNonFloatOutputsAreRefused`. Both are deterministic (no timing, no sleeps) and the existing 9 tests are unchanged.

Behaviour kept: the good planar and stacked stubs still separate (`testSeparatesWithTheStubModel`, `StemSplitPipelineTest`). The real pinned HTDemucs export (`[1, 4, 2, 343980]`, per `doc/STEM-SPLIT.md`) satisfies the stacked check when the segment is 343980. A model whose output time axis differs from the segment was already misread (the right channel is read at offset `segment`), so it is now refused rather than read wrong.

## Evidence table

All commands run from the worktree unless noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-143.r1/logs/`.

| Check | Command | EXIT / result |
| --- | --- | --- |
| Build attempt 1 (RED) | `cmake --build build --target OnnxRuntimeStemSeparatorTest -j4 > build-1-red.log 2>&1` | **EXIT=2**. Not a code error: ccache could not create a temp file under `/run/user/1000/ccache-tmp` (permission denied). |
| Build (RED) | `CCACHE_TEMPDIR=/tmp/p15143/ccache-tmp cmake --build build --target OnnxRuntimeStemSeparatorTest -j4 > build-2-red.log 2>&1` | **EXIT=0** |
| RED | `ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-1-red.log 2>&1` (from `build/tests`) | **EXIT=8**. `testTransposedOutputsAreRefused` and `testNonFloatOutputsAreRefused` fail: Actual `0` (Success), Expected `2` (Failed). Totals: 9 passed, 2 failed. |
| GREEN (first version) | `cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-3-green.log 2>&1` then `ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-2-green.log 2>&1` | build **EXIT=0**; ctest **EXIT=0**, 1/1 passed |
| Stem family build | `cmake --build build --target StemExportTest StemJobManagerTest StemModelStoreTest StemSplitPipelineTest -j4 > build-4-family.log 2>&1` | **EXIT=0** (these four binaries did not exist in this worktree; they were "Not Run" until built) |
| Stem family | `ctest -R 'Stem' --output-on-failure > test-5-family-green.log 2>&1` | **EXIT=0**, 8/8 passed |
| Reverse-fix proof (final diff) | `git diff -- src/core/OnnxRuntimeStemSeparator.cpp > fix.patch; git apply -R fix.patch` | diff **EXIT=0**, reverse **EXIT=0** |
| Unfixed build | `cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-9-final-proof-unfixed.log 2>&1` | **EXIT=0** |
| Unfixed test | `ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > test-11-final-proof-unfixed.log 2>&1` | **EXIT=8**. Both new tests fail, Actual `0`, Expected `2`. Totals: 9 passed, 2 failed. |
| Restore fix | `git apply fix.patch` | **EXIT=0** |
| Restored build | `cmake --build build --target zene OnnxRuntimeStemSeparatorTest -j4 > build-10-final-proof-fixed.log 2>&1` | **EXIT=0** |
| Restored test | `ctest -R '^OnnxRuntimeStemSeparatorTest$' -V > test-12-final-proof-fixed.log 2>&1` | **EXIT=0**. Totals: **11 passed, 0 failed** |
| Final build | `cmake --build build --target zene OnnxRuntimeStemSeparatorTest StemExportTest StemJobManagerTest StemModelStoreTest StemSplitPipelineTest -j4 > build-8-final.log 2>&1` | **EXIT=0** |
| Final stem family | `ctest -R 'Stem' --output-on-failure > test-10-final-family.log 2>&1` | **EXIT=0**, 8/8 passed |
| Generator reproducibility | `python3 tools/make_stub_model.py <tmp>/lin.onnx`, `--stacked <tmp>/st.onnx`, then `cmp` against the committed fixtures | both generators **EXIT=0**; `cmp` silent for both (byte-identical) |

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` **could not run** here: it creates its log folder at `<worktree>/../<name>-feature-check/`, which is `projects/zene-studio-program/zene-qwen-new-p15-143-feature-check`, outside the write-jail (`PermissionError`, log `gate-1-feature-check.log`, EXIT=1). I ran the same gate commands the script lists, from the worktree, on the final tree:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (pre-existing items in other files; see below) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (pre-existing items in other files; see below) |

The two ratchets exit 1 on the train as the brief describes. feature_check fails a ratchet only on a REGRESSION line whose file this branch changed. In the final logs no REGRESSION line names `OnnxRuntimeStemSeparator` or `make_stub_model`. The complexity log lists `OnnxRuntimeStemSeparator::separate` at CCN 26, which matches its baseline in `tests/complexity-baseline.tsv`, so it is grandfathered and not a regression. An earlier version of `isStemBlockLayout` was CCN 11 and was reported as a new item; I split out `withoutUnitAxes` and it is now below the target of 10 (no longer reported).

## Commits

Files committed with this fix:

- `src/core/OnnxRuntimeStemSeparator.cpp`
- `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp`
- `tools/make_stub_model.py` (already registered in `tests/tools-sources.txt`)
- `tests/data/stub-4stem-transposed.onnx`
- `tests/data/stub-4stem-int64.onnx`
- `docs/qwen-fixes-2026-10/cluster-new-p15-143.md`

Test fixtures are not listed in `tests/fork-sources.txt` or `tests/upstream-modifications.txt` (the existing `stub-4stem-linear.onnx` is not either). No new control commands were added and no header changed.

## Self-check

`submit_check.py` (run on the committed tree, log `submit-check-1.log`) reports two FAILs: this section was missing, and the gate row. Both are answered here.

- **Gate row (`feature_check.py`) — FAIL, environment, not a code result.** The script exits 1 with `PermissionError` creating `zene-qwen-new-p15-143-feature-check` beside the worktree (`gate-1-feature-check.log`, `gate-2-feature-check.log`, EXIT=1 both times). That path is outside the allowed write area, so I did not work around it. The six simple gates and both ratchets were run individually (table above). All six exit 0; the two ratchets exit 1 on other files' items and name none of this branch's files. The loop should re-run `feature_check.py` with a writable log location before treating this row as green.
- **Regression fails with the change reverted, passes with it:** `test-11-final-proof-unfixed.log` EXIT=8 (both new tests: Actual 0, Expected 2), and `test-12-final-proof-fixed.log` EXIT=0 (11/11). The RED run before any fix (`test-1-red.log`, EXIT=8) gave the same two failures.
- **Deterministic:** the new tests call the public `separate()` synchronously on committed fixtures and a fixed sine mix (`makeSineMix(1)`, pure arithmetic on sample indexes). They use no clock, sleep, thread or progress race, and no hook. Assertions are on the returned `Status` and the error text.
- **Callers and siblings:** `outputsHoldSegment` has one caller, `runSessionSegment` (`src/core/OnnxRuntimeStemSeparator.cpp:181`), which has one caller, `separate()` (`:304`). The only tensor-data read is `:310`, after the check. The planar and stacked paths share one check, and `stacked` is derived from `stemStride`, which is non-zero only in the stacked layout (`resolveStemOutputs`). The Python CLI `tools/stem_split_cli.py` is a separate external-process backend; it is not changed here, and its per-stem reads (`stem_block`) also do no dtype or shape check. Out of scope for this cluster, not verified further.
- **No collateral damage:** the good planar stub (`testSeparatesWithTheStubModel`) and the stacked stub (`StemSplitPipelineTest`) still separate (stem family 8/8, `test-10-final-family.log`). The new check refuses only output shapes that the loop already misreads: a time axis other than `segment` makes the right channel read at offset `segment` land in the wrong place, and the stacked stride assumes that axis too. No limit is added on segment length or on the number of stems beyond the existing `S >= NumStems`. Saved projects are not touched; the separator writes only in-memory stem buffers. The real pinned HTDemucs export (`[1, 4, 2, 343980]`, documented in `doc/STEM-SPLIT.md`) was not downloaded or run here (165 MB model, not available in this worktree). Its shape matches the stacked check by the documented contract, which is what I can state without running it.
- **Verdict item:** not applicable (verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE).
