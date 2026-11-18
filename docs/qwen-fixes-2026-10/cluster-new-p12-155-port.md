# Cluster new-p12-155-port

Verdict: **FIXED** (port of `f506e714d`, branch `bugs/qwen-new-p12-155`)

The accepted fix did not merge into `050/m0-train-5` because of a conflict in
`tests/src/plugins/PluginPortsMigrationTest.cpp`. The defect is re-applied here by hand, and the
regression lives in the NeuralAmp area test that already compiles the effect, its controls and the
real `.nam` loader (`tests/src/plugins/NeuralAmpModelLoadTest.cpp`), so the conflicting file is not
touched at all.

## What was wrong

`NeuralAmpControls::loadSettings()` stored the loaded `model` attribute (empty when the element has
none) and only called the effect when that path was non-empty. A project loaded without a model
attribute therefore left whatever model a previous request or `LMMS_NAM_MODEL` had published active,
and the audio kept running through it.

The newest line has refactored the loader that the original fix patched: `setModelPath()` no longer
joins a thread per request, it records the request and a worker (`workerLoop()` -> `loadOne()`)
parses it. The original hunk ("publish a null model inside `setModelPath`") does not exist here, and
an empty path reaching `loadOne()` today fails in `nam::NamModel::loadFromFile("")`, which only sets
`load failed: ...` and leaves the old model published - the same user-visible defect.

## The fix

- `plugins/NeuralAmp/NeuralAmpControls.cpp:46` - forward the path to the effect even when empty.
- `plugins/NeuralAmp/NeuralAmpEffect.cpp:180` - `clearModel()` (new): skips the request when a newer
  one or the destructor made it stale, otherwise `publishModel(nullptr)` and status
  `no model loaded`. `loadOne()` calls it for an empty path before any file work.
  The clearing runs on the worker thread, and `publishModel()` already waits for an in-flight audio
  call before reclaiming the old model, so the audio thread stays allocation-, lock- and log-free.
- `plugins/NeuralAmp/NeuralAmpEffect.h:81` - documents that an empty path clears the model, and
  declares `clearModel()`.

The dialog's own "load" button still refuses an empty text field (`NeuralAmpControlDialog.cpp:103`),
so the only new clear trigger is loading a setting that has no model - the reported case.

## Regression

`NeuralAmpModelLoadTest::loadingASettingWithoutAModelClearsTheLoadedModel()` loads an effect from an
effect element that names model B, proves the model changes rendered audio, then loads the same
effect from an element with **no** `model` attribute and requires status `no model loaded`, an empty
`modelPath()` and dry output.

## Verification evidence

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Valid RED on the original behaviour | `ctest -R NeuralAmpModelLoadTest --output-on-failure` (from `build/tests`) | 8 | `test-red-1.log`: `FAIL! loadingASettingWithoutAModelClearsTheLoadedModel()` at NeuralAmpModelLoadTest.cpp:288, `Totals: 6 passed, 1 failed`. |
| Reverse-patch RED (fix reverted, test kept) | `ctest -R NeuralAmpModelLoadTest --output-on-failure` (from `build/tests`) | 8 | `test-red-2.log`: same failure at line 288, `Totals: 6 passed, 1 failed`. |
| GREEN after the fix | `ctest -R NeuralAmpModelLoadTest --output-on-failure` (from `build/tests`) | 0 | `test-green-1.log`: `100% tests passed out of 1`. |
| GREEN, test names visible | `ctest -R NeuralAmpModelLoadTest -V` | 0 | `test-green-2.log`: `Totals: 7 passed, 0 failed, 0 skipped`. |
| GREEN after reapplying the patch | `ctest -R NeuralAmpModelLoadTest --output-on-failure` | 0 | `test-green-3.log`. |
| Affected family | `ctest -R "NeuralAmp\|PluginPortsMigration" --output-on-failure` (from `build/tests`) | 0 | `test-family-4.log`: 6/6 CTest targets passed (NeuralAmpModelLoadTest, NeuralAmpWavInputTest, PluginPortsMigrationTest, NeuralAmpProfileArgumentsTest, NeuralAmpCompareRateTest, NeuralAmpWavRunSampleRateWarning). |
| Family before the missing fixtures were built | same ctest | 8 | `test-family-2.log`/`test-family-3.log`: `nam_harness` and the `decimator_1202` LADSPA fixture were simply not built in this tree; building them (`build-6.log`, `build-7.log`) turns the family green. Unrelated to this change. |
| Build of the test target (RED) | `cmake --build build --target NeuralAmpModelLoadTest --parallel 4` | 0 | `build-1.log`. |
| Build with the fix | `cmake --build build --target zene neuralamp NeuralAmpModelLoadTest --parallel 4` | 0 | `build-2.log`. |
| Build with the product patch reverted | same command | 0 | `build-3.log`. |
| Build after reapplying the patch | same command | 0 | `build-4.log`. |
| Build of the family and fixture targets | `cmake --build build --target zene neuralamp NeuralAmpModelLoadTest NeuralAmpWavInputTest NeuralAmpProfileArgumentsTest PluginPortsMigrationTest --parallel 4`, then `--target nam_harness ladspaeffect` and `--target decimator_1202` | 0 | `build-5.log`, `build-6.log`, `build-7.log`. |
| Reverse-patch setup | `git diff -- plugins/NeuralAmp/NeuralAmpControls.cpp plugins/NeuralAmp/NeuralAmpEffect.cpp plugins/NeuralAmp/NeuralAmpEffect.h > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | Only product files reverted; the regression test stayed. |
| Reverse-patch restore | `git apply .../logs/fix.patch` | 0 | Product changes restored before the final build and GREEN run. |
| Complexity gate (after the final edit) | `bash tests/complexity-gate.sh --check` | 1 | `complexity-gate-3.log`: 6 regressions, none in a file this change touches. The same 6 fail on pristine `ded355440` (`complexity-gate-baseline-head.log`); an earlier revision of this fix had pushed `loadOne` to CCN 12 and that entry is gone after the `clearModel()` extraction (`complexity-gate-2.log`). |
| File-length gate (after the final edit) | `bash tests/file-length-gate.sh --check` | 1 | `file-length-gate-2.log`: 14 regressions, identical to pristine HEAD (`file-length-gate-baseline-head.log`); `NeuralAmpModelLoadTest.cpp` is 335 lines, under the 500 limit. |
| Whitespace | `git diff --check` | 0 | Clean. |

Both ratchets already fail on the newest line for files this bug does not touch
(`tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/TakeLane.cpp`,
`tests/src/core/VideoTrackTest.cpp`, ...). That was measured by reverting every change of this port
(`git apply -R logs/my-changes.patch`) and re-running both gates on the untouched tree; the counts
are identical with and without the fix (6 and 14), so this port adds no gate regression and no gate
limit or baseline was touched.

All build and test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-155-port.r1/logs`.

## Files committed

- `plugins/NeuralAmp/NeuralAmpControls.cpp`
- `plugins/NeuralAmp/NeuralAmpEffect.cpp`
- `plugins/NeuralAmp/NeuralAmpEffect.h`
- `tests/src/plugins/NeuralAmpModelLoadTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-155-port.md`

No registration change is needed: all four source files are already registered product-new files in
`tests/fork-sources.txt` (lines 1184, 1186-1187 and 2552).