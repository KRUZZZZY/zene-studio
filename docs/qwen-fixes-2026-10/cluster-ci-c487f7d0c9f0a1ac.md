FIXED

# Cluster ci-c487f7d0c9f0a1ac - CI regression: build / linux-arm64

## Finding

The `linux-arm64` hosted-CI build on train 050/m0-train-27 (commit
`a1a98b67b2f9c961fd6e79c078815b4aedaa8695`) stopped while compiling
`tests/CMakeFiles/NamModelLoaderTest.dir/__/plugins/NeuralAmp/nam/NamModel.cpp.o`.
Vendored Eigen's aarch64 NEON blocking kernel does arithmetic between the unnamed
enums of two `gebp_traits` specialisations, GCC emits
`-Wdeprecated-enum-enum-conversion`, and `-DUSE_WERROR=ON` promotes it to an error:

- `run-42805458633-job-129793314368.log:1728` and `:1731` - both errors in
  `plugins/NeuralAmp/rtneural/modules/Eigen/Eigen/src/Core/products/GeneralBlockPanelKernel.h`
  (lines 157 and 216).
- `run-42805458633-job-129793314368.log:1735` - `gmake[2]: ***
  [tests/CMakeFiles/NamModelLoaderTest.dir/build.make:113:
  tests/CMakeFiles/NamModelLoaderTest.dir/__/plugins/NeuralAmp/nam/NamModel.cpp.o] Error 1`.

Classification: **infrastructure/build wiring (REAL)**. The engine sources and the
vendored Eigen tree are unmodified; the target that compiles them was missing the
warning-suppression option its siblings carry.

## Cause

The fork already treats this vendor diagnostic as non-actionable for NAM targets:

- `tests/CMakeLists.txt:1942` defines
  `ZENE_NAM_EIGEN_OPTIONS` (`-Wno-error=deprecated-enum-enum-conversion` for
  GNU/Clang/AppleClang).
- `plugins/NeuralAmp/CMakeLists.txt:41-46` does the same for the plugin target.
- `plugins/NeuralAmp/tests/CMakeLists.txt:33-36,51,85,97` does it for the harnesses.
- `tests/CMakeLists.txt:2879`, `:3104`, `:3361`, `:5855` apply it to
  `NeuralAmpModelLoadTest`, `NeuralAmpWavInputTest`, `NamModelMonoOutputTest` and
  `NamModelPrewarmTest`.

`NamModelLoaderTest` compiles the same `NamModel.cpp` (`tests/CMakeLists.txt:5864-5868`)
but, before this fix, its target block ended at `target_compile_features(...)`
(`tests/CMakeLists.txt:5881`) with no `target_compile_options` line. The target was
added by commit `2074b5db25` (2026-10-18), after commit `7471dd285f` (2026-10-15) had
given every then-existing NAM test target the option, so this one target was never
covered. The local configure proved the omission in the generated build flags:

```
build/tests/CMakeFiles/NamModelLoaderTest.dir/flags.make : 0 occurrences
build/tests/CMakeFiles/NamModelPrewarmTest.dir/flags.make : 1 occurrence
```

## Fix

`tests/CMakeLists.txt:5882` adds the missing line, exactly as the siblings do:

```cmake
target_compile_options(NamModelLoaderTest PRIVATE ${ZENE_NAM_EIGEN_OPTIONS})
```

Every target that compiles the NAM engine was re-enumerated (`rg` over all
`CMakeLists.txt` for `NamModel.cpp`/`NamModelLoader.cpp`/`NeuralAmpEffect.cpp`):
the plugin, `plugins/NeuralAmp/tests` (all three harnesses), `NeuralAmpModelLoadTest`,
`NeuralAmpWavInputTest`, `NamModelMonoOutputTest`, `NamModelPrewarmTest` and now
`NamModelLoaderTest` all carry the option. No product code, audio-thread path,
command surface or save format is touched.

## Regression test

The failure is architecture-specific (GCC only emits it for the aarch64 NEON
`gebp_traits` pairing; the same TU compiles clean on x86_64), so a runtime test of
the loader cannot see it. The regression is therefore a compile-time canary in the
target that failed:

- `tests/src/core/NamModelEigenWarningProbe.cpp:49-61` performs the same enum
  arithmetic through Eigen's own traits (`gebp_traits<..., Generic>::mr *
  gebp_traits<..., NEON>::nr`). Without the target option the compilation fails with
  the same `-Werror=deprecated-enum-enum-conversion` diagnostic the arm64 job
  produced; with it the translation unit compiles (warning retained).
- `tests/src/core/NamModelLoaderTest.cpp:53-56` declares the probe and `:323-327`
  calls it; the probe file is part of the `NamModelLoaderTest` target
  (`tests/CMakeLists.txt:5866`).
- The probe is GCC-only (`#if defined(__GNUC__) && !defined(__clang__)`); Clang
  calls the diagnostic `-Wdeprecated-anon-enum-enum-conversion` and does not emit it
  for the NAM engine on the macOS jobs, so those builds compile the probe as a
  trivial `true` instead of failing for a warning name this cluster never saw.
- `tests/fork-sources.txt` and `tests/all-sources.txt` register the new source;
  `bash tests/all-sources-reproduce.sh` prints REPRODUCES.

## Commands and exit codes

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-c487f7d0c9f0a1ac.r1/logs`.

| Step | Command | Exit | Log |
|---|---|---|---|
| baseline target build | `cmake --build build --target NamModelLoaderTest -j4` | 0 | `build-1-baseline.log` |
| RED before fix (probe in target, no option) | `cmake --build build --target NamModelLoaderTest -j4` | 2 | `build-3-red.log` |
| GREEN with fix | `cmake --build build --target NamModelLoaderTest -j4` | 0 | `build-4-green.log` |
| GREEN test | `cd build/tests && ctest -R "^NamModelLoaderTest$" --output-on-failure` | 0 | `test-1-green.log` (1/1 passed) |
| write fix patch | `git diff -- tests/CMakeLists.txt > .../fix.patch` | 0 | `fix.patch` |
| reverse fix | `git apply -R .../fix.patch` | 0 | - |
| RED with fix reverted | `cmake --build build --target NamModelLoaderTest -j4` | 2 | `build-5-reverted-red.log` |
| restore fix | `git apply .../fix.patch` | 0 | - |
| GREEN restored | `cmake --build build --target NamModelLoaderTest -j4` | 0 | `build-6-restored-green.log` |
| GREEN test restored | `cd build/tests && ctest -R "^NamModelLoaderTest$" --output-on-failure` | 0 | `test-2-green-restored.log` (1/1 passed) |
| family build | `cmake --build build --target NamModelLoaderTest NamModelMonoOutputTest NamModelPrewarmTest NeuralAmpAllocProbeArgumentsTest NeuralAmpHarnessBlockCount NeuralAmpModelLoadTest NeuralAmpProfileArgumentsTest NeuralAmpWavInputTest nam_harness nam_profile neuralamp -j4` | 0 | `build-7-family.log` |
| family tests | `cd build/tests && ctest -R "NamModel\|NamLoader\|NeuralAmp" --output-on-failure` | 0 | `test-3-family.log` (19/19 passed) |
| product build | `cmake --build build --target zene -j4` | 0 | `build-8-zene.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gate-1-feature-check.log` |

RED assertion, copied from `build-3-red.log`:

```
tests/src/core/NamModelEigenWarningProbe.cpp:58:34: error: arithmetic between different
enumeration types 'Eigen::internal::gebp_traits<float, float, false, false, 0>::<unnamed enum>'
and 'Eigen::internal::gebp_traits<float, float, false, false, 4>::<unnamed enum>' is deprecated
[-Werror=deprecated-enum-enum-conversion]
```

## Test matrix

| Path | Covered by | Result |
|---|---|---|
| normal path: target configured with `USE_WERROR=ON` | build + `ctest -R ^NamModelLoaderTest$` | PASS, 1/1 |
| failure path: suppression absent | reverse `fix.patch`, rebuild | FAIL (exit 2) at the probe |
| reconfiguration: option reaches generated flags | rebuild after CMakeLists edit; `flags.make` now has 1 occurrence | PASS |
| sibling targets: every NAM/NeuralAmp test and harness | family build + `ctest -R "NamModel\|NamLoader\|NeuralAmp"` | PASS, 19/19 |
| audio thread | no runtime/product file changed; probe runs once in `main()` before any engine exists | unchanged |
| GCC (linux x86_64 and arm64 CI) | probe active; error suppressed by target option | PASS |
| Clang/AppleClang | probe compiled out; no new diagnostic | unaffected |
| MSVC | probe compiled out; option is GNU/Clang-only like the existing siblings | unaffected |

## Files committed

- `tests/CMakeLists.txt` - missing option added; probe source registered on the target.
- `tests/src/core/NamModelEigenWarningProbe.cpp` - new compile-time canary.
- `tests/src/core/NamModelLoaderTest.cpp` - declares and calls the canary.
- `tests/fork-sources.txt`, `tests/all-sources.txt` - new source registered in both scopes.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  `git apply -R fix.patch` EXIT 0, rebuild EXIT 2 (`build-5-reverted-red.log`);
  `git apply fix.patch` EXIT 0, rebuild EXIT 0 and ctest EXIT 0
  (`build-6-restored-green.log`, `test-2-green-restored.log`).
- **Deterministic: no wall-clock timing, sleeps or races.** The failure is a
  compile-time diagnostic from a fixed set of Eigen trait instantiations; there is no
  seam, injected timestamp or scheduled work involved. The probe's runtime return is
  computed from the same constants, so it cannot pass spuriously.
- **The fix is at the cause and complete.** The cause was the one NAM target lacking
  the suppression the other seven carry; all callers/siblings were re-enumerated and
  the family build/test above exercises them. The option is defined once
  (`tests/CMakeLists.txt:1942`), so target and siblings cannot drift in spelling.
- **No collateral damage.** No product or runtime file changed, so old projects and
  saved data load exactly as before. No limit or check was added. The suppression is
  scoped to the vendor warning category and to targets that already compiled this
  vendored code; the x86_64 build still prints the warning rather than hiding it.
- **Verdict evidence (not ALREADY-FIXED/FALSE-POSITIVE).** The branch tip was the
  failing commit; `tests/CMakeLists.txt:5881` ended the target block without
  `target_compile_options`, and the generated `NamModelLoaderTest.dir/flags.make`
  contained zero occurrences of the option while `NamModelPrewarmTest` had one. The
  hosted log at `run-42805458633-job-129793314368.log:1728,1731,1735` names exactly
  this target and diagnostic.
- **Sibling commands in command files: not affected.** This fix changes no control
  command file, command id, schema, handler or snapshot. The `app.*` and
  `arrangement.*` ids named by the pre-submit check appear in the branch-wide
  `base..HEAD` diff of the train this cluster was cut from, not in this commit
  (`git show --stat HEAD` lists only the two manifests, `tests/CMakeLists.txt`, the
  two NAM test sources and this report). There is no sibling command whose behaviour
  or reversibility class this build-option change could alter.
