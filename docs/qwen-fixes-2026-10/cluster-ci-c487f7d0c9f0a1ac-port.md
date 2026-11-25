ALREADY-FIXED

# Cluster ci-c487f7d0c9f0a1ac-port - Port: CI regression: build / linux-arm64

ci-c487f7d0c9f0a1ac-port: ALREADY-FIXED

## What the finding is

The finding has a title only. The original cluster `ci-c487f7d0c9f0a1ac`
(`docs/qwen-fixes-2026-10/cluster-ci-c487f7d0c9f0a1ac.md:1-30`) records the failure. The hosted
`linux-arm64` job (run 42805458633) stopped compiling `NamModel.cpp` for the
`NamModelLoaderTest` target. Vendored Eigen's aarch64 NEON `gebp_traits` does
enum-enum arithmetic, GCC emits `-Wdeprecated-enum-enum-conversion`, and
`-DUSE_WERROR=ON` turned it into an error. The target lacked
`${ZENE_NAM_EIGEN_OPTIONS}`, which every sibling NAM target carries. That cluster
was fixed by commit `ac17b5dddd` ("fix(ci): CI regression: build / linux-arm64").
This job ports that fix to this branch.

## Evidence that the fix is already on this branch

- `git merge-base --is-ancestor ac17b5dddd HEAD` -> `EXIT=0` (HEAD `e4194e0bf5`).
- `tests/CMakeLists.txt:2054` defines `ZENE_NAM_EIGEN_OPTIONS`
  (`-Wno-error=deprecated-enum-enum-conversion` for GNU/Clang/AppleClang).
- `tests/CMakeLists.txt:6501` sets
  `target_compile_options(NamModelLoaderTest PRIVATE ${ZENE_NAM_EIGEN_OPTIONS})`.
- `tests/CMakeLists.txt:6485` adds the compile-time canary
  `tests/src/core/NamModelEigenWarningProbe.cpp` to the target. Its line `:58`
  does the same `gebp_traits<..., Generic>::mr * gebp_traits<..., NEON>::nr`
  arithmetic under GCC. `tests/src/core/NamModelLoaderTest.cpp:60` declares the
  canary and `:401` calls it.
- The generated `build/tests/CMakeFiles/NamModelLoaderTest.dir/flags.make`
  contains `deprecated-enum-enum-conversion` exactly once.
- Sibling sweep: every target that compiles the NAM engine carries the
  suppression. The sweep covered all tracked CMake files except the vendored
  trees.

| target | compiles NAM at | suppression at |
|---|---|---|
| neuralamp plugin | plugins/NeuralAmp/CMakeLists.txt:4-8 | plugins/NeuralAmp/CMakeLists.txt:45-46 |
| NeuralAmpModelLoadTest | tests/CMakeLists.txt:3149-3150 | tests/CMakeLists.txt:3155 |
| NeuralAmpWavInputTest | tests/CMakeLists.txt:3450-3451 | tests/CMakeLists.txt:3459 |
| NamModelMonoOutputTest | tests/CMakeLists.txt:3709-3710 | tests/CMakeLists.txt:3718 |
| NamModelPrewarmTest | tests/CMakeLists.txt:6455-6456 | tests/CMakeLists.txt:6474 |
| NamModelLoaderTest | tests/CMakeLists.txt:6486-6487 | tests/CMakeLists.txt:6501 |
| nam_harness / nam_profile / rt_alloc_probe | plugins/NeuralAmp/tests/CMakeLists.txt:21-22 (used :60, :101, :113) | plugins/NeuralAmp/tests/CMakeLists.txt:51-54 (applied :71, :105, :117) |

## Commands and exit codes (this session)

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/ci-c487f7d0c9f0a1ac-port.r2/logs`.

| Step | Command | EXIT | Log |
|---|---|---|---|
| ancestry | `git merge-base --is-ancestor ac17b5dddd HEAD` | 0 | - |
| build | `cmake --build build --target zene NamModelLoaderTest NamModelPrewarmTest NamModelMonoOutputTest -j4` | 0 | build-1.log |
| family tests | `cd build/tests && ctest -R "NamModel" --output-on-failure` | 0 | test-1.log (`100% tests passed out of 4`: NamModelMonoOutputTest, NamModelPrewarmTest, NamModelLoaderTest, NamModelLoaderPortabilityCheck) |
| remove fix line | `sed -i 6501d tests/CMakeLists.txt; git diff -R -- tests/CMakeLists.txt > .../fix.patch` | 0 | fix.patch |
| RED, guard without the fix | `cmake --build build --target NamModelLoaderTest -j4` | 2 | build-2-red.log |
| restore | `git apply .../fix.patch` | 0 | - (`git status --porcelain` empty afterwards) |
| GREEN | `cmake --build build --target NamModelLoaderTest -j4` | 0 | build-3-green.log |
| GREEN test | `cd build/tests && ctest -R "^NamModelLoaderTest$" --output-on-failure` | 0 | test-2-green.log (`100% tests passed out of 1`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | gate-1.log (all 8 PASS, `RESULT: PASS`) |

RED diagnostic (`build-2-red.log:251`). The x86_64 host reproduces the arm64 error class:

```
../../tests/src/core/NamModelEigenWarningProbe.cpp:58:34: error: arithmetic between different
enumeration types 'Eigen::internal::gebp_traits<float, float, false, false, 0>::<unnamed enum>'
and 'Eigen::internal::gebp_traits<float, float, false, false, 4>::<unnamed enum>' is deprecated
[-Werror=deprecated-enum-enum-conversion]
```

No product or test file is changed by this job. The only file committed is this report.

## Round 3: target-coverage regression guard (NamEigenOptionsCoverageCheck)

The reviewer asked for a guard in this unit's diff. This round adds one.

- **The guard script:** `tests/nam-eigen-options-coverage-check.py` (new, 214 lines, stdlib only).
  - It parses only `tests/CMakeLists.txt`, `plugins/NeuralAmp/CMakeLists.txt` and
    `plugins/NeuralAmp/tests/CMakeLists.txt`.
  - It finds every target that gets `nam/NamModel.cpp` from `add_executable`, `add_library`,
    `build_plugin` or `target_sources`. It expands `set()` variables such as
    `NEURALAMP_SOURCES` and `NEURALAMP_ENGINE_SOURCES`, and resolves `${LMMS_TEST_NAME}` from the
    enclosing `if(LMMS_TEST_NAME STREQUAL ...)`.
  - It fails unless each such target's `target_compile_options` carries
    `-Wno-error=deprecated-enum-enum-conversion`, directly or through
    `ZENE_NAM_EIGEN_OPTIONS` or `NEURALAMP_ENGINE_OPTIONS`.
  - It also checks that each target's generated `flags.make` contains
    `deprecated-enum-enum-conversion` when that file exists. Every one existed in this build.
  - It has a discovery floor, so a parser that finds nothing fails instead of passing. The floor
    is neuralamp, nam_harness, NamModelLoaderTest, NamModelPrewarmTest,
    NamModelMonoOutputTest, NeuralAmpModelLoadTest and NeuralAmpWavInputTest.
- **Registration:** `tests/CMakeLists.txt:6534-6544` registers it as CTest
  `NamEigenOptionsCoverageCheck`, under
  `if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PYTHON3_EXECUTABLE)`, the same guard as
  NamModelLoaderPortabilityCheck. The block sits after the `NamPortabilityRegistrationCheck`
  block, because the comment that follows the portability block refers to "the registration
  above".
- **Source list:** the script is registered in `tests/fork-sources.txt`.
- **GREEN discovery:** 9 targets, all OK.

| target | option at |
|---|---|
| NamModelLoaderTest | tests/CMakeLists.txt:6501 |
| NamModelMonoOutputTest | tests/CMakeLists.txt:3718 |
| NamModelPrewarmTest | tests/CMakeLists.txt:6474 |
| NeuralAmpModelLoadTest | tests/CMakeLists.txt:3155 |
| NeuralAmpWavInputTest | tests/CMakeLists.txt:3459 |
| neuralamp | plugins/NeuralAmp/CMakeLists.txt:45 |
| nam_harness | plugins/NeuralAmp/tests/CMakeLists.txt:71 |
| nam_profile | plugins/NeuralAmp/tests/CMakeLists.txt:105 |
| rt_alloc_probe | plugins/NeuralAmp/tests/CMakeLists.txt:117 |

Round-3 logs: `/home/kruzzzzy/.config/kads/zene/state/runs/ci-c487f7d0c9f0a1ac-port.r3/logs`.

| Step | Command | EXIT | Log |
|---|---|---|---|
| configure+build | `cmake --build build --target NamModelLoaderTest -j4` | 0 | build-1.log |
| GREEN | `cd build/tests && ctest -R "^NamEigenOptionsCoverageCheck$" --output-on-failure` | 0 | test-1-green.log (`100% tests passed out of 1`) |
| RED: delete `tests/CMakeLists.txt:6501`, rebuild | `cmake --build build --target NamModelLoaderTest -j4` | 2 | build-2-red.log (probe error, as in round 2) |
| RED guard | `cd build/tests && ctest -R "^NamEigenOptionsCoverageCheck$" --output-on-failure` | 8 | test-2-red.log (`0% tests passed, 1 tests failed out of 1`) |
| RED guard, direct | `python3 tests/nam-eigen-options-coverage-check.py --source-root . --build-dir build` | 1 | test-3-red-direct.log |
| restore line 6501, rebuild | `cmake --build build --target NamModelLoaderTest -j4` | 0 | build-3-green.log |
| GREEN guard | `cd build/tests && ctest -R "^NamEigenOptionsCoverageCheck$" --output-on-failure` | 0 | test-4-green.log (`100% tests passed out of 1`) |
| family build | `cmake --build build --target zene NeuralAmpModelLoadTest NeuralAmpWavInputTest nam_harness nam_profile rt_alloc_probe NamModelLoaderTest NamModelPrewarmTest NamModelMonoOutputTest -j4` | 0 | build-4-family.log |
| family build (rest) | `cmake --build build --target NeuralAmpProfileArgumentsTest NeuralAmpAllocProbeArgumentsTest NeuralAmpHarnessBlockCount -j4` | 0 | build-6-family.log |
| family tests | `cd build/tests && ctest -R "NamModel\|NamEigen\|NeuralAmp\|NamPortability" --output-on-failure` | 0 | test-7-family.log (`100% tests passed out of 14`) |
| GREEN direct | `python3 tests/nam-eigen-options-coverage-check.py --source-root . --build-dir build` | 0 | test-8-direct-green.log (`PASS`) |
| all-sources | `bash tests/all-sources-reproduce.sh` | 0 | all-sources-1.log (`REPRODUCES`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | gate-1.log (8/8 PASS) |

RED output naming the target, copied from `test-3-red-direct.log`:

```
MISSING NamModelLoaderTest (tests/CMakeLists.txt:6486)
MISSING-GENERATED NamModelLoaderTest (tests/CMakeFiles/NamModelLoaderTest.dir/flags.make)
FAIL: NamModelLoaderTest compiles nam/NamModel.cpp without -Wno-error=deprecated-enum-enum-conversion
FAIL: NamModelLoaderTest: generated flags.make lacks deprecated-enum-enum-conversion
```

The intermediate run `test-6-family.log` (EXIT 8) failed only because 3 test executables had
not been built yet ("Not Run"). The rerun after building them, `test-7-family.log`, passed
14/14.

## Self-check
- **Round 3, the guard fails without the fix and passes with it.** With
  `tests/CMakeLists.txt:6501` deleted, `NamEigenOptionsCoverageCheck` gives ctest EXIT 8 and a
  direct run gives EXIT 1, naming NamModelLoaderTest. With the line restored, it gives EXIT 0.
  The check is a static parse plus a file read, with no timing, so it is deterministic.

- **Verdict evidence:** the fix commit `ac17b5dddd` is an ancestor of HEAD (EXIT 0).
  The option is at `tests/CMakeLists.txt:6501` and reaches the generated flags
  (1 occurrence in `flags.make`).
- **Regression test fails without the fix and passes with it.** I used the existing
  canary, `NamModelEigenWarningProbe.cpp` in `NamModelLoaderTest`. With
  `tests/CMakeLists.txt:6501` removed, the build gives EXIT 2 at the probe. With it
  restored, the build gives EXIT 0 and ctest EXIT 0. The check is deterministic: it
  is a compile-time diagnostic, with no timing or threads.
- **Fix is complete across siblings:** see the target table. All nine targets that
  compile the NAM engine carry the suppression.
- **No collateral damage:** this job changes no product code, save format, command,
  or audio-thread path.
- **Sibling commands (`app.*`, `arrangement.*` listed by the pre-submit check):**
  this job does not affect them. They come from the branch-wide base..HEAD diff of
  the train. This job's commits touch only this report, and no command file,
  schema or handler changed.
- **Not verified here:** an actual aarch64 build, because this host is x86_64. The
  GCC probe reproduces the same diagnostic class on x86_64, so the guard is
  exercised locally.

Delegation: Haiku ran the build plus the NamModel ctest family, swept every CMake
target that compiles the NAM engine for the suppression, and ran the RED/GREEN line
removal and restore. A fourth Haiku start was wasted on a no-op. DeepSeek attempted
the RED/GREEN run but could not write the log folder (read-only in its sandbox). It
restored the file, and I confirmed `git status` was clean. Sonnet was not used: no
fix or test needed writing.

Delegation (round 3): Sonnet wrote the guard script, registered it, and ran the
RED/GREEN proof. Haiku built the remaining NAM family executables and ran the
14-test family. I reviewed the script and diff and re-checked every log line
quoted above.

## Other findings

None.
