ALREADY-FIXED

# Cluster ci-v041-2 - linux-arm64: vendored Eigen `-Wdeprecated-enum-enum-conversion` in NamModelLoaderTest

## Finding

The CI object that failed was `NamModelLoaderTest.dir/__/plugins/NeuralAmp/nam/NamModel.cpp.o`, so the
vendored Eigen headers come in through `NamModel.cpp` (and `NamModelLoader.cpp`). GCC on aarch64 takes Eigen's NEON `gebp_traits` path, where
`GenericTraits::mr * NeonTraits::nr` is arithmetic between two different unnamed enumerations. That is a
`-Wdeprecated-enum-enum-conversion` diagnostic in code this repo does not modify, and `-DUSE_WERROR` turned
it into an error.

This was already fixed before this job, by commit `ac17b5dddd3281f9cd01f12a4ec1cfcdf07b744a`
(2026-11-10, "fix(ci): CI regression: build / linux-arm64"). That commit is an ancestor of HEAD
`8571717ae8`. No code change is made here.

## Evidence

- `tests/CMakeLists.txt:2009-2012` defines the shared suppression `ZENE_NAM_EIGEN_OPTIONS`:
  `-Wno-error=deprecated-enum-enum-conversion` for GNU/Clang/AppleClang C++ only. It keeps the warning and
  drops only its error status, and only for targets that compile the NAM sources.
- `tests/CMakeLists.txt:6270` adds `NamModelEigenWarningProbe.cpp` to the `NamModelLoaderTest` executable,
  beside `NamModelLoader.cpp`; `:6286` applies `${ZENE_NAM_EIGEN_OPTIONS}` to that target with
  `target_compile_options`.
- Siblings that compile the same sources carry the same option: `tests/CMakeLists.txt:3069` is the generic
  `LMMS_TESTS` loop that builds `NeuralAmpModelLoadTest`; `:3373` is `NeuralAmpWavInputTest`; `:3632`
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
| 07-build-red.log, 10-redgreen-exits.log | `reverse-fix.patch` applied (removes `tests/CMakeLists.txt:6286`) (`APPLY_EXIT=0`) and rebuilt | `RED_BUILD_EXIT=2`, `07-build-red.log:253: NamModelEigenWarningProbe.cpp:58:34: error: ... [-Werror=deprecated-enum-enum-conversion]` |
| 08/09 + 10-redgreen-exits.log | `reverse-fix.patch` re-applied (`REVERSE_EXIT=0`), rebuilt, retested | `GREEN_BUILD_EXIT=0`, `GREEN_TEST_EXIT=0` |
| 10-feature-check.log | `feature_check.py` before this report existed | `RESULT: PASS`, EXIT=0, 8/8 PASS |
| 11-feature-check-post-commit.log | `feature_check.py` on the committed tree | `RESULT: PASS`, EXIT=0 |
| 12-submit-check.log | `submit_check.py` before the rerun | `RESULT: PASS - submit`, EXIT=0 |
| 13-submit-check.log | `submit_check.py` rerun | `RESULT: PASS - submit`, EXIT=0 |
| 14-feature-check.log | `feature_check.py` rerun after this correction commit | `RESULT: PASS`, EXIT=0 |
| 15-submit-check.log | `submit_check.py` rerun after this correction commit | `RESULT: PASS - submit`, EXIT=0 |

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
- **At the cause, complete.** The single suppression `ZENE_NAM_EIGEN_OPTIONS`
  (`tests/CMakeLists.txt:2012`) covers every test target that compiles `plugins/NeuralAmp/nam/*.cpp`:
  `:3069` is the generic `LMMS_TESTS` loop that builds `NeuralAmpModelLoadTest`, `:3373` is
  `NeuralAmpWavInputTest`, `:3632` is `NamModelMonoOutputTest` and `:6259` is `NamModelPrewarmTest`;
  `:6286` applies it to `NamModelLoaderTest`. The plugin and harness targets use the same flag
  (`plugins/NeuralAmp/CMakeLists.txt:45-46`, `plugins/NeuralAmp/tests/CMakeLists.txt:54`).
- **Probe is load-bearing.** Without the option the probe's own line 58 is a hard error under `-Werror`
  (07-build-red.log:253); with it, 08 is the green build and 09 is `NamModelLoaderTest` 1/1 passed; the
  13/13 result is 06-ctest-nam.log.
- **Vendored code untouched.** `GeneralBlockPanelKernel.h:157/216` unchanged; `git status` shows no change
  under `plugins/NeuralAmp/rtneural`.
- **Gates after commit.** `feature_check.py` on the committed tree: `RESULT: PASS`, EXIT=0, 8/8 PASS
  (11-feature-check-post-commit.log). `submit_check.py`: all 6 checks PASS, RESULT: PASS, EXIT=0
  (12-submit-check.log; rerun in 13-submit-check.log, `RESULT: PASS`, EXIT=0). Both gates were rerun after
  this correction commit: 14-feature-check.log `RESULT: PASS`, EXIT=0, and 15-submit-check.log
  `RESULT: PASS - submit`, EXIT=0.
- **Regression test fails reverted, passes fixed.** `git apply reverse-fix.patch` removes the option line
  (`APPLY_EXIT=0`) and makes the build of `NamModelLoaderTest` fail with `RED_BUILD_EXIT=2` at
  `07-build-red.log:253`
  (`NamModelEigenWarningProbe.cpp:58:34: error: ... [-Werror=deprecated-enum-enum-conversion]`). Then
  `git apply -R reverse-fix.patch` restores the line (`REVERSE_EXIT=0`) and gives `GREEN_BUILD_EXIT=0` and
  `GREEN_TEST_EXIT=0` (10-redgreen-exits.log).
- **Deterministic.** The check is a compile-time probe: `NamModelEigenWarningProbe.cpp:49-62` performs the
  same enum-times-enum arithmetic Eigen's NEON kernel contains, and the build's `-Werror` plus the presence
  or absence of `ZENE_NAM_EIGEN_OPTIONS` decides the outcome. There is no clock, sleep, thread or race;
  `ctest` runs 13/13 with no timing assertion.
- **No collateral damage.** No product code, data format, limit, command or audio path is touched in this
  packet; only the warning's error status for vendored headers is suppressed, and only for those targets.
  Old projects and saved data are unaffected.
- **ALREADY-FIXED evidence.** `ac17b5ddd` is an ancestor of HEAD (`git merge-base --is-ancestor` EXIT=0) and
  not of tag `v0.4.1-alpha` (`EXIT=1`); the code showing the intended contract is the existing suppression at
  `tests/CMakeLists.txt:2009-2012` plus its application at `:6286`.
- **Sibling commands.** No control command, handler or schema is touched (git diff of this job is limited to
  the report and `tests/fork-sources.txt`), so the `ControlCommandsSnapshot` is unaffected.
