# cluster new-p10-273-port — NAM model dimension validation

Verdict: **FIXED** (port of the accepted fix `f9dc91f84` from `bugs/qwen-new-p10-273`;
that branch could not merge because of a conflict in
`plugins/NeuralAmp/nam/NamModelLoader.cpp`, so the change was written by hand here).

## What the newest line already had, and what was still missing

The newest line carries two other clusters' equivalents of this fix
(`95dff2a47` "Fix NAM loader dimensions (cluster new-p9-311)" and
`8bba12a81` "Fix NeuralAmp mono head output (cluster new-p10-274)"), which is
why the finding's own `head_size: 0` example is already refused here:

- `plugins/NeuralAmp/nam/NamModelLoader.cpp:296` — `input_size`, `head_size`,
  `channels` and `bottleneck` must be positive, so a zero-row `headOut` can no
  longer be constructed;
- `plugins/NeuralAmp/nam/NamModelLoader.cpp:266` — kernel size >= 1;
  `:270` — dilation > 0;
- `plugins/NeuralAmp/nam/NamModelLoader.cpp:275-287` — the per-layer and total
  receptive field are computed in 64-bit and refused if they would overflow
  `int`, so `(kernelSize - 1) * dilation` at `:367` cannot wrap.

Two halves of the accepted fix were **not** on this line and are what this port
adds:

1. **Layer-count bound.** Nothing capped how many WaveNet layers a `.nam` file
   could declare, so each declared layer allocated a full set of
   `kMaxBlock`-sized Eigen buffers.
2. **Allocation preflight.** The declared dimensions were turned into Eigen
   matrix sizes (`rechannelW.resize(channels, inputSize)` at `:362`,
   `history.resize(inChannels, receptiveField + kMaxBlock)` at `:408`, …)
   *before* anything compared them against the weights the file actually
   carries, so `channels: 2000000000` with an empty `weights` array reached a
   multi-gigabyte `resize()` on the loader thread.

## The change

- `plugins/NeuralAmp/NamModelValidation.h` (new): `kMaxModelLayers` (256),
  checked size arithmetic (`checkedProduct`/`addCount`, Eigen-index bounded
  `matrixElements`) and `validateModelResources()`, which refuses a model whose
  declared dimensions do not imply exactly the weight count the file carries,
  and one whose engine would exceed a 512 MiB load budget.
- `plugins/NeuralAmp/nam/NamModelLoader.cpp:218-225` counts layers across every
  layer array and refuses `> kMaxModelLayers`;
  `:340-341` runs `detail::validateModelResources(spec, weightsJson.size(),
  totalLayerCount)` before the first `resize()`.
- `tests/src/core/NamModelLoaderTest.cpp` (new) + the `NamModelLoaderTest`
  target in `tests/CMakeLists.txt:3690-3710`: six invalid models, one per bound
  (receptive-field overflow, zero head size, zero channels, zero bottleneck,
  zero dilation, 257 layers), each required to be refused with a non-empty
  error. Qt-free, like the loader it exercises.
- `tests/CMakeLists.txt:1914,3672`: the NeuralAmp plugin root joins the include
  path of `NamModelMonoOutputTest` and `NamModelPrewarmTest`, which compile
  `NamModelLoader.cpp` and therefore its `NamModelValidation.h` include.

The positivity and receptive-field checks of the newest line are untouched and
still run first, so the messages a caller sees for those malformed models do not
change.

## Evidence

Every command was run from
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-273-port`
(tests from `build/tests`), with logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-273-port.r1/logs`.

| Step | Exact command | EXIT | Log |
| --- | --- | ---: | --- |
| RED (fix absent, original code) | `ctest -R NamModelLoaderTest --output-on-failure` | 8 | `test-1.log` — `loader accepted invalid model: too many layers` |
| GREEN (fix present) | `ctest -R "NamModelLoaderTest\|NamModelMonoOutputTest\|NamModelPrewarmTest\|NeuralAmpModelLoadTest\|NeuralAmpWavInputTest" --output-on-failure` | 0 | `test-3.log` — 5/5 passed |
| Reverse the product patch | `git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp plugins/NeuralAmp/NamModelValidation.h > …/logs/fix.patch && git apply -R …/logs/fix.patch` | 0 | — |
| Rebuild with the fix reversed | `cmake --build build --target NamModelLoaderTest -j2` | 0 | `build-6.log` |
| Sensitivity RED | `ctest -R NamModelLoaderTest --output-on-failure` | 8 | `test-4.log` — `loader accepted invalid model: too many layers` |
| Restore the product patch | `git apply …/logs/fix.patch` | 0 | — |
| Rebuild with the fix restored | `cmake --build build --target NamModelLoaderTest -j2` | 0 | `build-7.log` |
| Sensitivity GREEN | `ctest -R NamModelLoaderTest --output-on-failure` | 0 | `test-5.log` — 1/1 passed |
| `zene` rebuilt on the final tree | `cmake --build build --target zene -j2` | 0 | `build-8.log` |
| Affected-family run on the final tree | `ctest -R "NamModelLoaderTest\|NamModelMonoOutputTest\|NamModelPrewarmTest\|NeuralAmpModelLoadTest\|NeuralAmpWavInputTest" --output-on-failure` | 0 | `test-6.log` — 5/5 passed |
| Ratchet gate | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` |
| Ratchet gate | `bash tests/file-length-gate.sh --check` | 1 | `gate-filelength-2.log` |
| Registration gate (extra) | `bash tests/fork-sources-gate.sh` | 0 | `gate-forksources.log` |
| Manifest reproduces (extra) | `tests/all-sources.txt` vs `git ls-files` recipe | 0 | `all-sources-diff-2.log` (empty) |

Affected-family test count: **5 executed, 5 passed** (`NeuralAmpModelLoadTest`,
`NeuralAmpWavInputTest`, `NamModelMonoOutputTest`, `NamModelPrewarmTest`,
`NamModelLoaderTest`) — 0 tests run would have been an error.

### The two gate exits are pre-existing drift, not this change

Both gates already failed on this branch before any file of this fix was in
scope, and they fail with byte-identical regression lists afterwards:

- `diff <(rg "^(REGRESSION|improved)" gate-complexity.log | sort) <(…gate-complexity-2.log…)` -> EXIT 0 (`gate-complexity-ab.log`)
- `diff <(rg "^REGRESSION" gate-filelength.log | sort) <(…gate-filelength-2.log…)` -> EXIT 0 (`gate-filelength-ab.log`)

Neither log names any file of this change (`NamModelValidation.h`,
`NamModelLoader.cpp`, `NamModelLoaderTest.cpp`). The named regressions are
inherited-line drift in files this fix does not touch (for example
`src/core/TakeLane.cpp`, `tests/src/core/DeviceDspTest.cpp`,
`tests/src/core/VideoTrackTest.cpp`, `tests/src/gui/XyPadPairTest.cpp`). No
baseline, threshold or gate file was re-anchored.

## Files in this commit

- `plugins/NeuralAmp/NamModelValidation.h` (new)
- `plugins/NeuralAmp/nam/NamModelLoader.cpp`
- `tests/src/core/NamModelLoaderTest.cpp` (new)
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-273-port.md`