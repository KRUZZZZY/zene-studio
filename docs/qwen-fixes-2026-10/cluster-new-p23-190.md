# Cluster new-p23-190: OutputSettings compression level is not range-checked

**Verdict: FIXED**

The finding is real on `bugs/qwen-new-p23-190`. The setter documents the legal range and does not enforce it. The fix clamps the value to [0.0, 1.0] and ignores NaN.

## Evidence

- `include/OutputSettings.h:111-117` (before the fix, `setCompressionLevel` was `m_compressionLevel = level;` with only the comment `// legal range is 0.0 to 1.0.`). The fix clamps with `std::clamp(level, 0.0, 1.0)` and returns early on `std::isnan(level)`. Includes added at `include/OutputSettings.h:29-30`.
- The value flows into the encoder unchecked: `src/core/audio/AudioFileFlac.cpp:68-71` passes `getCompressionLevel()` to `SFC_SET_COMPRESSION_LEVEL` under `LMMS_HAVE_SF_COMPLEVEL`.
- The only production caller is `src/gui/modals/ExportProjectDialog.cpp:361`. Its values come from the combo built at `src/gui/modals/ExportProjectDialog.cpp:162-174` as `i / maxCompressionLevel` for `i` in `0..8`, so every legitimate value is already in [0, 1]. The clamp does not change any value the dialog can produce.
- No other path sets the level. A grep of `setCompressionLevel|getCompressionLevel` across `src`, `include` and `tests` finds only the dialog, the FLAC writer and the header. `include/ControlExportPresetSupport.h:10` says presets do not carry it.
- The regression is `tests/src/gui/ExportQualityControlsTest.cpp:111-131`, slot `compressionLevelStaysInItsLegalRange()`. It checks that the default 5/8 and the bounds 0.0 and 1.0 are kept exactly, that 7.0 and -0.5 are clamped to 1.0 and 0.0, and that NaN leaves 0.25 in place.
- RED without the fix: `FAIL! ... compressionLevelStaysInItsLegalRange() ... Actual 7, Expected 1.0 ... Loc: [tests/src/gui/ExportQualityControlsTest.cpp(123)]`.

The affected ctest family is `^(ExportQualityControlsTest|ExportChannelsTest|AudioFileMP3InitTest)$`. All three ran, 3/3 passed (`ExportQualityControlsTest` #443, `ExportChannelsTest` #444, `AudioFileMP3InitTest` #552).

## Commands and results

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-190.r1/logs`. Build commands ran from the worktree root and ctest commands from `build/tests`.

| Stage | Command | EXIT | Result |
| --- | --- | ---: | --- |
| Build attempt 1 (RED) | `cmake --build build --target ExportQualityControlsTest -j4 > .../build-red-1.log 2>&1; echo EXIT=$?` | 2 | Failed before compiling anything: ccache could not create its temp file under the sandbox's ccache temp directory (`Permission denied`). Environment only. |
| Build RED | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ExportQualityControlsTest -j4 > .../build-red-2.log 2>&1; echo EXIT=$?` | 0 | Built the test against the unfixed header. |
| RED | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > .../test-red-1.log 2>&1; echo EXIT=$?` | 8 | `compressionLevelStaysInItsLegalRange` failed: stored 7, expected 1.0. 5 passed, 1 failed. |
| Fix | Header change at `include/OutputSettings.h` (see Evidence). Registration line appended to `tests/upstream-modifications.txt`. | — | — |
| Build GREEN | `CCACHE_TEMPDIR=... cmake --build build --target ExportQualityControlsTest -j4 > .../build-green-1.log 2>&1; echo EXIT=$? >> ...` | 0 | Built, including the rebuild of `lmmsobjs` that the header change forces. |
| GREEN | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > .../test-green-1.log 2>&1; echo EXIT=$?` | 0 | 100% passed, 1 of 1. |
| Proof: save fix | `git diff -- include/OutputSettings.h > .../fix.patch` | — | Saved the product diff. |
| Proof: revert | `git apply -R .../fix.patch; echo REVERT_EXIT=$?` | 0 | `git diff --stat` on the header then empty; `clamp`/`isnan` no longer present. |
| Proof build RED | `cmake --build build --target ExportQualityControlsTest -j4 > .../build-proof-red.log 2>&1` | not logged | The `echo EXIT` went to the task output, not the log; the log ends `[100%] Built target ExportQualityControlsTest`. The only `error` match in that log is a warning excerpt at `src/core/ControlCommandsRenderStems.cpp:186`, not a failure. |
| Proof RED | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > .../test-proof-red.log 2>&1; echo EXIT=$?` | 8 | Same assertion: stored 7, expected 1.0. |
| Proof: re-apply | `git apply .../fix.patch; echo APPLY_EXIT=$?` | 0 | `include/OutputSettings.h | 9 +++++++--`. |
| Proof build GREEN | `CCACHE_TEMPDIR=... cmake --build build --target ExportQualityControlsTest -j4 > .../build-proof-green.log 2>&1; echo EXIT=$? >> ...` | 0 | Built with the fix restored. |
| Proof GREEN | `ctest -R '^ExportQualityControlsTest$' --output-on-failure > .../test-proof-green.log 2>&1; echo EXIT=$?` | 0 | 100% passed. |
| Header rebuild | `CCACHE_TEMPDIR=... cmake --build build --target zene ExportChannelsTest AudioFileMP3InitTest AudioClipLoopTest AudioFileOggInitTest ClipFadesRenderTest ClipOffsetFadeTest ExportDitherTest ExportWavDitherTest ExportWavWriteErrorTest LoudnessReportTest MasteringTest SessionPlaybackRenderTest SessionSchedulerRenderTest SessionTransitionsTest StemExportTest SurroundExportTest -j4 > .../build-family.log 2>&1; echo EXIT=$? >> ...` | 0 | 0 `error:` lines. |
| Family ctest | `cd build/tests && ctest -R '^(ExportQualityControlsTest\|ExportChannelsTest\|AudioFileMP3InitTest)$' --output-on-failure > .../test-family-1.log 2>&1; echo EXIT=$?` | 0 | 3/3 passed. |
| Gate (complexity, direct) | `bash tests/complexity-gate.sh --check > .../gate-7-complexity.log 2>&1; echo EXIT=$?` | 1 | Six REGRESSION lines, all in files this branch did not change (`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsSessionRecordLand.cpp`). None name `OutputSettings`, `ExportQualityControlsTest` or `upstream-modifications`. |
| Gate (file length, direct) | `bash tests/file-length-gate.sh --check > .../gate-8-file-length.log 2>&1; echo EXIT=$?` | 1 | 22 REGRESSION lines, none in this branch's files. `include/OutputSettings.h` is 183 lines and `ExportQualityControlsTest.cpp` 199 lines, both far under 500. |
| Gate (simple, direct) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` (logs `gate-1` to `gate-6`) | 0, 0, 0, 0, 0, 0 | All pass. |
| feature_check | `python3 .../feature_check.py <worktree>` | 1 | Did not run its checks. It crashed with `PermissionError` creating its log folder `zene-studio-program/zene-qwen-new-p23-190-feature-check`, which is outside the write-jail. See Self-check. |

The gate rows above are the same commands `feature_check.py` runs, invoked directly from the worktree. The ratchets' pass/fail logic is `feature_check.py`'s; I checked its rule by hand: a ratchet item only fails when it sits in a changed file. None sit in one.

## Files committed

- `include/OutputSettings.h`
- `tests/src/gui/ExportQualityControlsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p23-190.md`

## Self-check

- **RED/GREEN with the change reverted and restored.** Yes. RED `test-red-1.log` EXIT=8, GREEN `test-green-1.log` EXIT=0, proof RED `test-proof-red.log` EXIT=8, proof GREEN `test-proof-green.log` EXIT=0. The reverted and restored states are shown by `REVERT_EXIT=0` and `APPLY_EXIT=0`.
- **Deterministic.** Yes. The test uses no clock, sleep or thread. It calls the setter with literal values, plus `std::numeric_limits<double>::quiet_NaN()` for the NaN case. No seam is needed.
- **Fix at the cause, callers and siblings checked.** The cause is the setter, and the fix is there. The sole caller `src/gui/modals/ExportProjectDialog.cpp:361` only passes combo values in [0, 1] (`:162-174`), so it is unchanged. The sibling default `m_compressionLevel(0.625)` (`include/OutputSettings.h:71`) is in range. The reader `src/core/audio/AudioFileFlac.cpp:69-70` now always sees a value in [0, 1], or the previous value after a NaN. No other path sets or reads the level.
- **No collateral damage.** In-range values are stored exactly; the test asserts 0.0, 1.0 and 0.625. The legal range is exactly [0, 1], so the clamp touches only values that were already illegal. No project save or load code reads or writes the level (see the grep under Evidence), so saved projects load as before. NaN is an invalid value that previously reached the encoder; it now keeps the previous level. That is a deliberate choice, and I did not find a caller that sends NaN.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** So no "already fixed" or "not real" proof is needed. The unfixed code is shown at `include/OutputSettings.h:111-114` in the pre-fix diff.

### Items not met

- **Gates via `feature_check.py`: not run as the script.** The script must create `zene-studio-program/zene-qwen-new-p23-190-feature-check` next to the worktree, which is outside the write-jail (`PermissionError`). I did not change the gate or its path. I ran its six simple gates and two ratchets directly (table above). All six simple gates exit 0. The two ratchets exit 1, but every REGRESSION line is in a file this branch did not touch. The `feature_check.py` row therefore needs a run from an account that can write that folder.
- **Header rebuild is partial.** `OutputSettings.h` is included directly by `src/core/ControlCommandsProject.cpp`, `src/core/main.cpp` (both in `zene`), by the test sources and support headers listed by `grep -rl OutputSettings.h tests src include`, and by several include headers. I rebuilt `zene` and 17 test targets (`ExportQualityControlsTest` plus the 16 in the family build, `build-family.log`), with 0 errors, and ran the 3 family tests. Test targets that reach the header only through other headers (`AudioFileDevice.h`, `ProjectRenderer.h`, `RenderManager.h`, `MasteringJob.h`, `SrcQuality.h`, `SurroundExport.h`, `ControlExportPresetSupport.h`) were not rebuilt individually. A header-only, additive change that is already compiled into `zene` makes a compile break unlikely, but I did not verify that for each of them.
- **ccache workaround.** Build commands set `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` because the sandbox's default ccache temp path is not writable. This is an environment change for the build commands only.
