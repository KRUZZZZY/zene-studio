ALREADY-FIXED

# Cluster ci-v041-2 - linux-arm64: vendored Eigen `-Wdeprecated-enum-enum-conversion` in NamModelLoaderTest

## Finding

The failing build is the arm64 job's `NamModelLoaderTest` translation unit. It compiles the vendored Eigen
headers through `NamModelLoader.cpp`, and GCC on aarch64 takes Eigen's NEON `gebp_traits` path, where
`GenericTraits::mr * NeonTraits::nr` is arithmetic between two different unnamed enumerations. That is a
`-Wdeprecated-enum-enum-conversion` diagnostic in code this repo does not modify, and `-DUSE_WERROR` turned
it into an error.

This was already fixed before this packet ran, by commit `ac17b5dddd3281f9cd01f12a4ec1cfcdf07b744a`
(2026-11-10, "fix(ci): CI regression: build / linux-arm64"). That commit is an ancestor of HEAD
`8571717ae8`. No code change is made here.

## Evidence

- `tests/CMakeLists.txt:2009-2012` defines the shared suppression `ZENE_NAM_EIGEN_OPTIONS`:
  `-Wno-error=deprecated-enum-enum-conversion` for GNU/Clang/AppleClang C++ only. It keeps the warning and
  drops only its error status, and only for targets that compile the NAM sources.
- `tests/CMakeLists.txt:6270` adds `NamModelEigenWarningProbe.cpp` to the `NamModelLoaderTest` executable,
  beside `NamModelLoader.cpp`; `:6286` applies `${ZENE_NAM_EIGEN_OPTIONS}` to that target with
  `target_compile_options`.
- Siblings that compile the same sources carry the same option: `tests/CMakeLists.txt:3069`
  (`NeuralAmpWavInputTest`), `:3373` (`NeuralAmpWavInputTest`'s own slot), `:3632`
  (`NamModelMonoOutputTest`) and `:6259` (`NamModelPrewarmTest`).
- `plugins/NeuralAmp/CMakeLists.txt:45-46` applies the same suppression to the `neuralamp` plugin target, and
  `plugins/NeuralAmp/tests/CMakeLists.txt:54` (inside `NEURALAMP_ENGINE_OPTIONS`) to the harness binaries.
- `tests/src/core/NamModelEigenWarningProbe.cpp:49-62` reproduces the diagnostic class on the host: it
  multiplies `gebp_traits<..., Generic>::mr` by `gebp_traits<..., NEON>::nr` for GCC
  (`:58` is the multiplication). `tests/src/core/NamModelLoaderTest.cpp:60` declares the probe and `:401`
  calls it in `main`, so the build itself fails if the suppression is lost.
- The vendored Eigen code is unmodified: `plugins/NeuralAmp/rtneural/modules/Eigen/Eigen/src/Core/products/GeneralBlockPanelKernel.h:157`
  and `:216` still contain the `Traits::mr * Traits::nr` enumerators, and `git status --porcelain
  plugins/NeuralAmp/rtneural` is empty.

## Provenance

`ac17b5dddd3281f9cd01f12a4ec1cfcdf07b744a` (2026-11-10, cluster ci-c487f7d0c9f0a1ac) is an ancestor of HEAD
`8571717ae8`:

```
git merge-base --is-ancestor ac17b5ddd HEAD   -> EXIT=0
```

It is not an ancestor of tag `v0.4.1-alpha` -> `4a406d8bb5`:

```
git merge-base --is-ancestor ac17b5ddd refs/tags/v0.4.1-alpha   -> EXIT=1
```

That is why hosted run 40237490965, built from the tag, still failed: the tag predates the fix. A guard
refused `git tag -l` (exit 97) in this lane, so the tag was resolved with
`git rev-parse -q --verify refs/tags/v0.4.1-alpha` (annotated tag object `da0b7fbbbf`, peeled commit
`4a406d8bb5`).

## Commands

Logs are in `~/.config/kads/zene/state/runs/ci-v041-2.r1/logs/`.

| Log | What it shows | Result |
| --- | --- | --- |
| 01-04 | fix commit, tag provenance, target grep, flags.make | `ac17b5ddd` present; `TAG_CONTAINS_FIX_EXIT=1`; `NamModelLoaderTest` includes the probe; the four NAM test `flags.make` files carry `-Wno-error=deprecated-enum-enum-conversion` |
| 05-build-green.log | `cmake --build build --target NamModelLoaderTest ...` | EXIT=0 (this log was later overwritten by a superset build; the first build's EXIT was seen only by the worker) |
| 06-ctest-nam.attempt1.log | first `ctest -R` run | EXIT=8, 46%: 7 of 13 `Not Run` because those executables were not built |
| 06-ctest-nam.log | after building the 13 tests | EXIT=0, 100% of 13 |
| 07-build-red.log, 10-redgreen-exits.log | reverse `fix.patch` applied (`APPLY_EXIT=0`) and rebuilt | `RED_BUILD_EXIT=2`, `07-build-red.log:253: NamModelEigenWarningProbe.cpp:58:34: error: ... [-Werror=deprecated-enum-enum-conversion]` |
| 08/09 + 10-redgreen-exits.log | patch reversed (`REVERSE_EXIT=0`), rebuilt, retested | `GREEN_BUILD_EXIT=0`, `GREEN_TEST_EXIT=0` |
| 10-feature-check.log | `feature_check.py` before this report existed | `RESULT: PASS`, EXIT=0, 8/8 PASS |

The RED build shows the probe really reproduces the CI diagnostic class on this box once the suppression is
removed, and the GREEN build/test shows it goes away.

## Limits

- linux-arm64 was not reproduced on this x86_64 host (`docs/LOCAL-CI.md:124` documents that the arm64 job
  cannot run here). The probe reproduces the same diagnostic class with host GCC, which is why the RED build
  carries the same `-Werror=deprecated-enum-enum-conversion` text; hosted CI on an `ci/*` branch is the final
  confirmation.
- No product file changes in this packet, so there is no behaviour change to test.

## Other findings

none

## Self-check

- **Already fixed, not newly fixed.** `ac17b5ddd` is in HEAD and not in the release tag; the report and
  `tests/fork-sources.txt` are the only changed paths.
- **Registration.** `tests/fork-sources.txt` lists `docs/qwen-fixes-2026-10/cluster-ci-v041-2.md`, the same
  way the sibling reports (e.g. `cluster-ci-v041-5.md` at `:3669`) are listed.
- **At the cause, siblings consistent.** The suppression is defined once
  (`tests/CMakeLists.txt:2012`) and applied to every test target that compiles
  `plugins/NeuralAmp/nam/*.cpp`: `:3069`, `:3373`, `:3632`, `:6259`, `:6286`. The plugin and harness targets
  use the same flag (`plugins/NeuralAmp/CMakeLists.txt:45-46`,
  `plugins/NeuralAmp/tests/CMakeLists.txt:54`).
- **Probe is load-bearing.** Without the option the probe's own line 58 is a hard error under `-Werror`
  (07-build-red.log:253); with it, the build and all 13 NAM tests pass (08/09).
- **Vendored code untouched.** `GeneralBlockPanelKernel.h:157/216` unchanged; `git status` shows no change
  under `plugins/NeuralAmp/rtneural`.
- **Gates after commit.** See below (filled from the post-commit gate and submit-check logs).
