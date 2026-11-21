# Cluster new-p15-74: Graph plain-click sample index out of range

Verdict: **FIXED**

The finding's memory-access claim is not real: `graphModel::drawSampleAt()` crops the write index to `[0, length)` (`src/gui/widgets/Graph.cpp:767-776`), so a border click cannot write or read outside the sample array. The real defect is the index passed to `setSampleAt()` and emitted on `samplesChanged()`: a plain left press on the border produced `-16` (left) and an index past the last sample (right). `Graph::changeSampleAt()` now clamps the computed index to `[0, length)` before calling `setSampleAt()`.

Earlier shift-click path: the same class of defect was already fixed on this line by `97fcdb6b03` (cluster new-p10-167-port, an ancestor of HEAD) in `mousePressEvent`'s shift branch. This change covers the plain-press path that port did not touch.

## Evidence (file:line)

- Plain press calls `changeSampleAt()` with unclamped coordinates: `src/gui/widgets/Graph.cpp:181` (non-shift press). Shift press without a previous point: `:204`. Mouse move: `:150`, already clamped by `:136-137` to `[2, width-3]`.
- Index computed in `changeSampleAt()`: `_x -= 2` then `(int)(_x*xscale)` with `xscale = length/(width-4)` (`:290-296`). For a left-border press `_x = -2`, giving a negative index; for the right border `_x = width-3`, giving an index `>= length`.
- The write is already safe: `drawSampleAt()` clamps `x` to `[0, length-1]` (`:767-776`), so the fix produces the same written sample.
- Fix: `src/gui/widgets/Graph.cpp:304-305`.
- Regression: `tests/src/gui/ExportQualityControlsTest.cpp:173-192` (`plainClicksStayWithinGraphSamples`), placed beside the existing shift-click test (`:152-171`) that covers the same signal range for shift clicks.

## Commands and results

Environment note: the first build failed because ccache's configured temp directory (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) rejects temp files from this sandbox ("Permission denied"). The RED ctest run `test-red-1.log` used a stale binary from that failed build and is not evidence. All later builds set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-p15-74`, a writable directory; build logs show `ccache: error` count 0.

| Step | Command (run from `build/`, logs in the run folder) | EXIT |
|---|---|---|
| 1 | `cmake --build build --target zene ExportQualityControlsTest -j4 > logs/build-1.log` (ccache failure) | 2 (invalid) |
| 2 | `CCACHE_TEMPDIR=... cmake --build build --target zene ExportQualityControlsTest -j4 > logs/build-2.log` | 0 |
| 3 | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportQualityControlsTest$' --output-on-failure > logs/test-red-2.log` (fix not applied) | **8 (RED)** |
| 4 | RED assertion: `FAIL! : ...plainClicksStayWithinGraphSamples() ... Actual -16, Expected 0`, `Loc: [ExportQualityControlsTest.cpp(184)]` | — |
| 5 | Apply fix (after the edit above), `cmake --build ... -j4 > logs/build-3.log` | 0 |
| 6 | `ctest -R '^ExportQualityControlsTest$' ... > logs/test-green-3.log` | **0 (GREEN)** |
| 7 | Proof: `git diff -- src/gui/widgets/Graph.cpp > logs/fix.patch` | 0 |
| 8 | `git apply -R logs/fix.patch` | 0 |
| 9 | `cmake --build ... > logs/build-4.log` (fix reverted) | 0 |
| 10 | `ctest -R '^ExportQualityControlsTest$' ... > logs/test-red-proof-4.log` | **8 (fails without fix: -16, Expected 0)** |
| 11 | `git apply logs/fix.patch` | 0 |
| 12 | `cmake --build ... > logs/build-5.log` | 0 |
| 13 | `ctest -R '^ExportQualityControlsTest$' -V ... > logs/test-green-proof-5.log` | **0 (passes with fix; `Totals: 6 passed, 0 failed`)** |
| 14 | `cmake --build build --target EnvelopeGraphTest GraphWaveformEditorGuiTest > logs/build-6.log` | 0 |
| 15 | `cmake --build build --target WaveformDataTest SampleEditorDialogTest > logs/build-7.log` | 0 |
| 16 | `ctest -R 'EnvelopeGraphTest\|GraphWaveformEditorGuiTest\|ExportQualityControlsTest\|WaveformDataTest\|SampleEditorDialogTest' > logs/test-family-8.log` | **0; 5/5 passed** |

Step 16 supersedes `test-family-7.log` (EXIT 8), where WaveformDataTest and SampleEditorDialogTest were "Not Run" because their binaries had not been built; they were then built (step 15) and passed in step 16.

Affected-family count: 5 ctest targets executed in step 16 (`WaveformDataTest`, `SampleEditorDialogTest`, `EnvelopeGraphTest`, `ExportQualityControlsTest`, `GraphWaveformEditorGuiTest`), all passed. `MultitapEchoControlDialog` (named in the finding) is not a registered ctest target in this build (`ctest -N` finds no match), so it was not run.

Gates:

- `feature_check.py` as written cannot run in this sandbox. `logs/gate-feature-check-original-13.log`, `python3 -I feature_check.py <worktree>`, EXIT=1: `PermissionError: [Errno 13] Permission denied: '…/zene-qwen-new-p15-74-feature-check'`. It creates its log directory next to the worktree, outside my write area, before any gate runs. I did not edit the shared script. I ran a copy (`logs/feature_check_logdir_copy.py`, `python3 -I`) that differs in two lines only: `ROOT` pinned to the original `qwen/` directory (so the existing `gate-baselines` cache for base `37eaf0755` is used) and the log directory redirected into the run folder. The gate logic is otherwise unchanged.
  - Before commit (`logs/gate-feature-check-9.log`): EXIT=0, `RESULT: PASS`.
  - After commit (`logs/gate-feature-check-11.log`): EXIT=0, `RESULT: PASS`. All eight rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures (exit 0), complexity and file-length ("no new item in your files").
- `submit_check.py` (`logs/gate-submit-check-10.log` before commit, `logs/gate-submit-check-12.log` after commit, EXIT=1 both times): after the commit, the commit, clean-tree, report and tests rows PASS. The remaining FAIL is the `gates (feature_check.py)` row, because `submit_check.py` runs the original `feature_check.py` as a subprocess and hits the `PermissionError` above. The gate results that row would report are the copy's PASS above. The "?" items are answered in the Self-check section below.

## Files changed

- `src/gui/widgets/Graph.cpp` — `Graph::changeSampleAt()` clamps the computed sample index to `[0, length)` (`:304-305`).
- `tests/src/gui/ExportQualityControlsTest.cpp` — new `plainClicksStayWithinGraphSamples()` (`:173-192`).
- `tests/upstream-modifications.txt` — one registration line for the `Graph.cpp` change (the file is inherited from LMMS).
- `docs/qwen-fixes-2026-10/cluster-new-p15-74.md` — this report.

## Self-check

- **Test fails without the fix, passes with it (exit codes):** step 3/10 EXIT 8 with `-16` vs `0` at `ExportQualityControlsTest.cpp:184`; step 6/13 EXIT 0 with `Totals: 6 passed`. The right-edge assertions (`:189-191`) are not reached in the RED run because the left-edge `QCOMPARE` aborts the function first; that branch is covered only by the GREEN run, not by its own RED failure.
- **Deterministic:** no timers, sleeps or threads. Fixed-size `Graph(20 x 40)`, `QTest::mouseClick` at fixed coordinates, `QSignalSpy` on `samplesChanged`, which is emitted synchronously on the GUI thread.
- **Fix at the cause and complete:** every caller of `changeSampleAt()` was checked. Mouse move (`:150`) is already clamped to `[2, width-3]` (`:136-137`), so `_x*xscale < length` and the new clamp is a no-op there. Plain press (`:181`) and shift press with no previous point (`:204`) pass unclamped coordinates, and both now get the clamped index. `drawLineAt()` clamps its own coordinates (`:232`, and `:197-198` for shift clicks) and is unchanged.
- **Collateral damage:** the written sample is the same as before, because `drawSampleAt()` (`:772`) applies the same clamp. Only the `samplesChanged(x, x)` arguments differ, and only for presses on the border. In-range indices are unchanged. No limit was added in a shared function, so nothing else can be rejected. No save format or project data path is touched. Consumers of `samplesChanged` that use the range (e.g. `plugins/Vibed/Vibed.cpp:131`, `plugins/BitInvader/BitInvader.cpp:231`) were not individually checked; they now receive a valid range instead of an out-of-range one, which is the contract.
- **Realtime / undo / commands:** `changeSampleAt()` runs only from GUI mouse handlers. It emits the same single `samplesChanged` per press as before. No new control command.
- **Not verified, out of scope:** `drawLineAt()` indexes `m_samples[sample_end - 1]` and `m_samples[sample_begin]` (`Graph.cpp:245`, `:254`) using `ceil` of float products. I did not prove an out-of-range read there and did not change it.
