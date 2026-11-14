FIXED — cluster ci-9d1768ed0beb1636 (CI regression: build / macos-x86_64)

## Verdict and cause

REAL on this branch. The defect is in the test sources, not the product and not the CI
infrastructure. There were two layers, and the second one is the layer this round fixes.

**1. The macOS failure the patrol reported.** At `40b4322860` the hosted macos-x86_64 job
builds for a deployment target below macOS 10.15 (`CMakeLists.txt:263-277` sets this project's
floor to 10.13, and CI derives a lower-than-10.15 target from a dependency binary). libc++
marks `std::filesystem` unavailable there, so the build stopped on the newly added NAM loader
regression:

    tests/src/core/NamModelLoaderTest.cpp:137:46: error: 'path' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:141:17: error: 'basic_ofstream' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:250:23: error: 'file_size' is unavailable: introduced in macOS 10.15
    tests/src/core/NamModelLoaderTest.cpp:329:57: error: 'temp_directory_path' is unavailable ...

Verified against the exact commit with `git show 40b4322860:tests/src/core/NamModelLoaderTest.cpp`:
`#include <filesystem>` at :38, `rejectsModelJson(const std::filesystem::path&` at :137,
`std::filesystem::file_size` at :250 and the temporary-directory helpers at :329-:390. The
fix for this layer is the committed `15ef744f0e` (plain `std::string` paths plus
`mkdtemp`/`mkdir` and `stat`/`_stat64`; no `std::filesystem` left in the file). It is
preserved unchanged in this round.

**2. The registration blocker QA round 1 sent back.** The portability CTest added with that
fix was guarded at `tests/CMakeLists.txt:6157` as
`if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PYTHON3_EXECUTABLE)`, but the file's only
`find_program(PYTHON3_EXECUTABLE ...)` stood far below it, at :6425. CMake evaluates the
condition with whatever the variable holds at that point: a warm tree with
`PYTHON3_EXECUTABLE` in its cache registered the CTest, so the previous round's green run
proved nothing about a clean configure; a fresh configure - every hosted job - evaluated the
guard while the variable was still undefined and silently dropped the test. Measured here
against the unfixed file: a fresh configure then
`ctest -N -R '^NamModelLoader|^NamPortability'` listed only `NamModelLoaderTest`
(`build/qwen-logs/ctest-N-red-final.log`), while the same clean configure of the fixed file
lists both `NamModelLoaderPortabilityCheck` and `NamPortabilityRegistrationCheck`
(`build/qwen-logs/test-16-fresh-green-final.log`). A regression CTest that a clean configure
never registers protects nothing.

The contract was already established before the failing commit, which is why this is a
missing guard and not a design question:

- `CMakeLists.txt:263-277`: the supported macOS floor is 10.13, and the product's own POSIX
  paths therefore avoid `std::filesystem` (`src/core/BounceInPlace.cpp:310-321`,
  `src/core/DataFile.cpp:160-170`, `src/core/ProjectRevisions.cpp:72-80`).
- The loader's sibling regressions already avoid it for the same reason
  (`plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp:73`,
  `plugins/NeuralAmp/tests/nam_harness.cpp:397-399`,
  `tests/src/core/TwoTrackRecordingHarness.cpp:106-124` and `:274-285`).
- The other `std::filesystem` uses in `tests/` are `_WIN32`-guarded (`BounceInPlaceTest.cpp:35`,
  `VstSubPluginFeaturesTest.cpp:22`) or in a probe no target builds
  (`TwoTrackAlsaCaptureProbe.cpp`).

## Fix (this round, on top of `15ef744f0e`)

- `tests/CMakeLists.txt:18-24`: one `find_program(PYTHON3_EXECUTABLE ...)` near the top of the
  file, with its reason stated, so every `if(... AND PYTHON3_EXECUTABLE)` below it - including
  the portability registration at :6157 - is evaluated after discovery. The late duplicate at
  the old :6408 is removed (a comment at :6432 records where it went). This is the correction
  QA asked for and committed as `194d2fae98`, now on this branch together with the rest.
- `tests/CMakeLists.txt:6165-6178`: new CTest `NamPortabilityRegistrationCheck` running
  `tests/nam-portability-registration-check.py`. It fails on the ordering defect itself and,
  on Linux, when the configured tree did not register `NamModelLoaderPortabilityCheck`.
- `tests/nam-portability-registration-check.py` (new, 182 lines): reads the real
  `tests/CMakeLists.txt` with comments stripped, requires the first use of
  `PYTHON3_EXECUTABLE` to follow its discovery, requires the portability `add_test` to sit
  behind an `if()` naming both `PYTHON3_EXECUTABLE` and Linux, and requires it to name an
  existing script. Two probe texts run first - a use-before-discovery text must be refused and
  an ordered text must pass - so a check that cannot fail fails here.
- `tests/fork-sources.txt`, `tests/upstream-modifications.txt`: registration of the new script
  and the ledger reason for the inherited `tests/CMakeLists.txt` change. The adapted
  `NamModelLoaderTest.cpp` and its portability check from `15ef744f0e` are untouched.

Nothing in the product, the audio thread, undo, the command surface or the saved-data path is
touched: the diff is CMake, one test-side Python check and two ledgers.

## Regression test matrix (run from `build/tests` on rebuilt targets)

| Requirement | Test | Result |
|---|---|---|
| Ordinary load / valid model | `NamModelLoaderTest`, `NamModelMonoOutputTest`, `NamLoaderTest_valid` | PASS |
| Malformed, overflow, limits, invalid rates/weights, size boundary | `NamModelLoaderTest` (all refusal cases inside the binary) | PASS |
| Realtime processing / prewarm | `NeuralAmpModelLoadTest` (`audioThreadAllocatesNothingWhileModelsSwap`: 0 allocations, finite output), `NamModelPrewarmTest` (`multiBlockPrewarmUsesSilence`) | PASS |
| Portability, adapted source | `NamModelLoaderPortabilityCheck` | PASS |
| Portability, old filesystem source (negative control) | `NamModelLoaderPortabilityCheck` with `40b4322860`'s `NamModelLoaderTest.cpp` restored | FAIL (exit 8) |
| Clean-configure registration | fresh `cmake -S . -B <fresh>` + `ctest -N` / focused `ctest` | FAIL before, PASS after |
| Registration ordering | `NamPortabilityRegistrationCheck` | PASS (RED when the product ordering is reverted) |
| Hosted macos-x86_64 native build | needs the macOS runner; see "Blocked capability" | not runnable here |

## Evidence (exact commands and exit codes)

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-ci-9d1768ed0beb1636`.
`LOGS` below is that worktree's `build/qwen-logs`, because the designated
`~/.local/share/zene-bunny-fix/qwen/runs/ci-9d1768ed0beb1636.r3/logs` is not writable in this
session (see "Blocked capability").

| Step | Command | Exit |
|---|---|---|
| RED, ordering check on the unfixed file | `python3 tests/nam-portability-registration-check.py --source-root .` (`check-1-red-source.log`) | 1 |
| Fresh configure, unfixed file | `/usr/bin/cmake -S . -B "$TMPDIR/fresh-pre" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DLMMS_CLAP_PATH="$PWD/build/clap"` (`configure-2-pre-fix.log`) | 0 |
| Unfixed file: no portability CTest in a clean tree | `cd "$TMPDIR/fresh-pre/tests" && /usr/bin/ctest -N -R "^NamModelLoader"` (`ctest-N-pre-fix.log`) | 0 (1 test; portability missing) |
| Regression sees a configured tree that lacks it | `python3 tests/nam-portability-registration-check.py --source-root . --build-dir "$TMPDIR/fresh-pre"` (`check-6-red-stale-tree.log`) | 1 |
| GREEN, ordering check after the fix | `python3 tests/nam-portability-registration-check.py --source-root . --build-dir build` (`check-4-green-lane.log`) | 0 |
| Fresh configure, fixed file | `/usr/bin/cmake -S . -B "$TMPDIR/fresh-post2" ... -DWANT_QT6=ON -DLMMS_CLAP_PATH="$PWD/build/clap"` (`configure-4-post-fix-final.log`) | 0 |
| Fresh tree registers both CTests | `cd "$TMPDIR/fresh-post2/tests" && /usr/bin/ctest -R "^NamModelLoaderPortabilityCheck$\|^NamPortabilityRegistrationCheck$" --output-on-failure` (`test-9-fresh-final.log`) | 0 (2/2) |
| Build family targets | `cmake --build build --target NamModelLoaderTest NamModelMonoOutputTest NamModelPrewarmTest NeuralAmpModelLoadTest nam_harness` (`build-2-family.log`) | 0 |
| Family suite | `/usr/bin/ctest -R "NamModel\|NamLoader\|NamPortability\|NeuralAmpModelLoadTest" --output-on-failure` (`test-15-family-green-final.log`) | 0 (14/14) |
| Realtime pair, explicit | `/usr/bin/ctest -R "NeuralAmpModelLoadTest\|NamModelPrewarmTest" --output-on-failure` (`test-3-realtime.log`) | 0 (2/2) |
| Negative control, old filesystem source | `/usr/bin/ctest -R "^NamModelLoaderPortabilityCheck$"` with `40b4322860`'s test source restored (`test-17-red-old-filesystem-final.log`) | 8 |
| Restored source, rebuild + rerun | `cmake --build build --target NamModelLoaderTest` (`build-11-restore-nam-final.log`), `/usr/bin/ctest -R "^NamModelLoaderTest$\|^NamModelLoaderPortabilityCheck$"` (`test-18-green-portability-final.log`) | 0, 0 (2/2) |
| 4b: reverse the product change (CMake ordering only; the registration and the test script stay) | `diff -u tests/CMakeLists.txt build/qwen-logs/CMakeLists.product-reverted.txt > build/qwen-logs/fix.patch && git apply build/qwen-logs/fix.patch` | 0 |
| 4b: RED, ordering check | `python3 tests/nam-portability-registration-check.py --source-root .` (`check-14-red-final.log`) | 1 |
| 4b: RED, ctest | `cmake --build build --target rebuild_cache`, then `/usr/bin/ctest -R "^NamPortabilityRegistrationCheck$" --output-on-failure` (`test-14-red-ctest-final.log`) | 0, 8 |
| 4b: RED, clean configure | `/usr/bin/cmake -S . -B "$TMPDIR/fresh-red4" ...` (`configure-7-red-final.log`), `/usr/bin/ctest -N -R "^NamModelLoader\|^NamPortability"` (`ctest-N-red-final.log`) | 0 (1 test; portability missing) |
| 4b: restore the product change | `git apply -R build/qwen-logs/fix.patch` | 0 |
| 4b: GREEN, regenerate + family | `cmake --build build --target rebuild_cache` (`build-10-rebuild-cache-green.log`), `/usr/bin/ctest -R "NamModel\|NamLoader\|NamPortability\|NeuralAmpModelLoadTest"` (`test-15-family-green-final.log`) | 0, 0 (14/14) |
| 4b: GREEN, clean configure | `/usr/bin/cmake -S . -B "$TMPDIR/fresh-green-final" ...` (`configure-8-green-final.log`), focused `/usr/bin/ctest` (`test-16-fresh-green-final.log`) | 0, 0 (2/2) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`feature-check-3-final.log`) | 0 (all eight rows PASS) |

Reverted-state assertion text, copied from `build/qwen-logs/check-14-red-final.log`:

    FAIL: tests/CMakeLists.txt uses PYTHON3_EXECUTABLE at line 6150 but discovers it at line 6425;
    a fresh configure evaluates the use while the variable is undefined

Restored-state text, copied from `build/qwen-logs/check-13-green-split.log`:

    PASS: tests/CMakeLists.txt discovers PYTHON3_EXECUTABLE (line 24) before every use
    PASS: NamModelLoaderPortabilityCheck is a guarded Linux CTest naming an existing script
    PASS: the configured tree registered NamModelLoaderPortabilityCheck

## Blocked capability

- **Hosted macos-x86_64 native build.** This workspace has no Darwin/Xcode runner, so the
  below-10.15 deployment-target compile cannot be executed here. The shadow `<filesystem>`
  check covers the contract and fails on the pre-fix source (exit 8); native confirmation
  still needs the hosted job.
- **The designated log directory is not writable.** Every write under
  `~/.local/share/zene-bunny-fix/qwen/runs/ci-9d1768ed0beb1636.r3/logs` (and the run directory
  itself) is refused with `EACCES`, although `stat` shows it owned by this uid with `rwx`;
  `/home/kruzzzzy` itself is refused too, while the worktree and `$TMPDIR` are writable. All
  logs for this round are therefore in `<worktree>/build/qwen-logs/` (build-ignored) and are
  complete; they are listed in the table above.

## Self-check

- **The regression FAILS with the product change reverted and PASSES with it.** Reverted
  ordering: `NamPortabilityRegistrationCheck` exit 8 (0/1) and the direct script exit 1 with
  the use-before-discovery line quoted above; a clean configure of the reverted tree lists
  only `NamModelLoaderTest`. Restored: script exit 0, CTest exit 0, clean configure lists and
  passes 2/2. The older filesystem adaptation is separately proven by restoring
  `40b4322860`'s source: `NamModelLoaderPortabilityCheck` exit 8; restored source exit 0 (2/2).
- **Deterministic.** No wall-clock timing, sleeps, network or races: the check reads the
  committed CMake file with comments stripped, compares fixed line positions and runs two fixed
  probe texts; the registered-tree check reads the generated `tests/CTestTestfile.cmake`. The
  portability check recompiles from the recorded compile command against a static shadow
  header.
- **Cause and completeness.** Every use of `PYTHON3_EXECUTABLE` in `tests/CMakeLists.txt`
  (209 lines mention it; first use at :6157; one discovery at :24) now follows discovery. No other
  file in the tree references it (`grep` over `CMakeLists.txt` and `*.cmake`, output above);
  the duplicate late discovery is removed rather than left as a second definition.
- **No collateral damage.** No loader assertion, fixture or behaviour changed. The CMake edit
  is an ordering move with identical semantics for every other registration; the tree's
  registered-test list gains exactly the two intended entries in a clean configure
  (853 -> 855 tests: `NamModelLoaderPortabilityCheck`, `NamPortabilityRegistrationCheck`).
  Old projects and saved data are untouched.
- **No audio-thread, undo or command changes.** No product source is in the diff; no control
  command, snapshot canary or reversibility surface is touched.
- **Registration and gates.** New script registered in `tests/fork-sources.txt`; the inherited
  `tests/CMakeLists.txt` change recorded in `tests/upstream-modifications.txt`;
  `feature_check.py` exits 0 with every row PASS (the first run flagged
  `registration_problem` at CCN 11, which was split into helpers, not waived).

## Files committed

This round:

- `tests/CMakeLists.txt` (Python discovery moved before first use; `NamPortabilityRegistrationCheck` registered)
- `tests/nam-portability-registration-check.py` (regression; new)
- `tests/fork-sources.txt` (new-script registration)
- `tests/upstream-modifications.txt` (ledger reason for the inherited file)
- `docs/qwen-fixes-2026-10/cluster-ci-9d1768ed0beb1636.md` (this report)

Already on the branch from the previous round (`15ef744f0e`), unchanged here:

- `tests/src/core/NamModelLoaderTest.cpp` (filesystem adaptation)
- `tests/nam-model-loader-portability-check.py` (portability check; new)
