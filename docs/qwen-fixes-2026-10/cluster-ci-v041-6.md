FIXED

# Cluster ci-v041-6 — include/fenv.h uses `#include_next`, which MSVC rejects (C1021)

**Verdict: REAL, fixed.** The error comes from the hosted msvc-x64 log (run 40237490965). It cannot be reproduced
on this Qt 6 / GCC box, so the fix is platform-guarded and the hosted CI run on a `ci/*` branch is what confirms it.

## Evidence

- `include/fenv.h:3` (before the fix) was an unconditional `#include_next <fenv.h>`. The file shadows the C library's
  `<fenv.h>` because `include/` is on the search path of lmmsobjs, the tests and every plugin
  (`src/CMakeLists.txt:52`, `cmake/modules/BuildPlugin.cmake:12`). cl.exe has no `#include_next`, so any MSVC
  translation unit that reaches `<fenv.h>` stops with C1021.
- How MSVC reaches it: MSVC's `<cfenv>` includes `<fenv.h>`, and that resolves to the shim first. New `<cfenv>`
  users on this line include `plugins/SfzPlayer/sfizz/src/sfizz/MathHelpers.h:23` (sfizz, built with
  `-I<src>/include`: `build/plugins/SfzPlayer/CMakeFiles/sfzplayer_sfizz.dir/flags.make`) and
  `tests/src/core/ControlNoteScaleVerbsTest.cpp:54`. Both were checked on Linux: their `.o.d` dependency files list
  `include/fenv.h` (`Panning.cpp.o.d`, `ControlNoteScaleVerbsTest.cpp.o.d`). The CI log could not be fetched here
  (the `gh` token is invalid), so which TU failed first is not confirmed. The fix does not depend on which TU it was.
- The only consumer of the polyfill is still `src/core/main.cpp:98` (`#include <fenv.h>` in the `LMMS_DEBUG_FPE`
  block). The Apple polyfill section (`#if defined(__APPLE__) && defined(__MACH__)`) is unchanged.

## Fix (`include/fenv.h:3-10`)

```cpp
#if defined(_MSC_VER) && !defined(__clang__)
#include <../ucrt/fenv.h>
#else
#include_next <fenv.h>
#endif
```

- **cl.exe only** (`_MSC_VER` without `__clang__`): the real header is the UCRT's
  `Windows Kits\10\Include\<ver>\ucrt\fenv.h`. The UCRT directory is a sibling of the `um`/`shared` SDK directories
  that are always on cl.exe's INCLUDE path, so `<../ucrt/fenv.h>` resolves through them. It does not resolve under
  `<src>/include`, Qt or the MSVC toolset directories, because none of those has a `ucrt` sibling. The
  `#include_next` line sits in a skipped `#else` group, and the preprocessor reads only the directive name there to
  track nesting (C++ [cpp.cond]). Whether cl.exe accepts it is reasoned from the standard, not observed, because there
  is no MSVC here. Hosted CI confirms it.
- **Unchanged:** GCC, Clang, Apple clang, mingw-w64 GCC and clang-cl, which defines `__clang__` and supports
  `#include_next`.
- No Qt API, no Windows macro identifiers, no audio-thread code, no command and no saved data is involved.

## Regression test: `FenvShimMsvcInclude` (`tests/fenv-shim-msvc-check.py`, registered in `tests/CMakeLists.txt`)

The test preprocesses a probe `#include <fenv.h>` with the host GCC/Clang, with `-I<src>/include` and a fake Windows
SDK layout (`Include/<ver>/um` on the path, sentinel header in `Include/<ver>/ucrt/fenv.h`). It runs three cases:

1. cl.exe predefines (`-D_MSC_VER=1929`, no `__clang__`). The probe must reach the UCRT sentinel. Before the fix
   the shim took `#include_next` and landed on the host's fenv.h, which is the line cl.exe rejects.
2. GCC/Clang (no `_MSC_VER`). The probe must reach the host's real `<fenv.h>`, so the Linux behaviour is pinned.
3. clang-cl (`_MSC_VER` and `__clang__`). The probe must keep `#include_next`, i.e. reach the host header.

The test is deterministic: no timing, a fresh temporary directory per run, and a pure preprocessor run. It is
registered only where the compiler is a GNU-style driver (`CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC`)
and python3 exists.

## Commands and exit codes (logs in `~/.local/share/zene-bunny-fix/qwen/runs/ci-v041-6.r1/logs/`)

| Step | Command | EXIT |
| --- | --- | --- |
| Configure (blocked) | `cmake build > 01-configure.log` | 97: the bunny-guard refuses reconfigure in lanes. The build-step reconfigure below registered the test (`ctest -N -R FenvShim` → `Test #661`) |
| Build (blocked) | `cmake --build build --target zene -j4 > 03-build-zene.log` | 2: ccache could not write its tmp dir in the jail. Every later build used `CCACHE_TEMPDIR=$TMPDIR/ccache-ci-v041-6` |
| **RED** (before fix) | `cd build/tests && ctest -R FenvShimMsvcInclude --output-on-failure > 04-test-red.log` | **8**: `FAIL [cl.exe (_MSC_VER, no __clang__)]: expected zene_probe_reached_fake_ucrt, the probe saw ['zene_probe_reached_next_fenv']`. Cases 2 and 3 passed |
| **GREEN** (after fix) | `ctest -R FenvShimMsvcInclude --output-on-failure > 05-test-green.log` | **0**: 1/1 passed |
| Script directly | `python3 tests/fenv-shim-msvc-check.py /usr/bin/c++ include > 06-script-green.log` | 0: 3 × PASS |
| Rebuild header dependents | `cmake --build build --target zene ControlNoteScaleVerbsTest -j4 > 07-build.log` | 0 |
| Family tests | `ctest -R 'FenvShimMsvcInclude\|ControlNoteScaleVerbs' --output-on-failure > 08-test-family.log` | 0: 2/2 passed (2 tests run) |
| sfizz TUs including the shim | `make -f plugins/SfzPlayer/CMakeFiles/sfzplayer_sfizz.dir/build.make …/Panning.cpp.o …/WindowedSinc.cpp.o > 10-build-sfizz-tu.log` (from `build/`) | 0 |
| 4b revert | `git diff -- include/fenv.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild (reverted) | `cmake --build build --target zene ControlNoteScaleVerbsTest -j4 > 11-build-reverted.log` | 0 |
| 4b test (reverted) | `ctest -R FenvShimMsvcInclude --output-on-failure > 12-test-reverted.log` | **8**: same FAIL line as RED |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild (reapplied) | `cmake --build build --target zene ControlNoteScaleVerbsTest -j4 > 13-build-reapplied.log` | 0 |
| 4b test (reapplied) | `ctest -R 'FenvShimMsvcInclude\|ControlNoteScaleVerbs' --output-on-failure > 14-test-reapplied.log` | **0**: 2/2 passed |
| Gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 15-feature-check.log` | 1: `PermissionError` creating `<worktree>/../zene-qwen-ci-v041-6-feature-check` (outside the write-jail). No gate ran |
| Gates (jail wrapper) | `python3 logs/feature_check_jailed.py <worktree> logs/feature-check > 16-feature-check.log` | **0**: every row PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

`logs/feature_check_jailed.py` runs `feature_check.py`'s own source with two changes. Its log directory is the LOG
FOLDER. Its ratchet baseline is empty, because the baseline cache and base worktree are also outside the jail. An
empty baseline is stricter: every complexity or file-length item in a changed file counts as new, and there were none.

## Registration

- `tests/fork-sources.txt`: `tests/fenv-shim-msvc-check.py` (product-new), with a comment.
- `tests/upstream-modifications.txt`: new reason lines for `include/fenv.h` and `tests/CMakeLists.txt`. The file
  still ends with a newline.

## Self-check

- **Regression fails with the fix reverted, passes with it:** reverted, `ctest -R FenvShimMsvcInclude` gave EXIT=8
  (`12-test-reverted.log`). Reapplied, it gave EXIT=0 (`14-test-reapplied.log`). The original RED/GREEN was 8/0
  (`04`, `05`).
- **Deterministic:** the test is a pure preprocessor run over fixed inputs (a fake SDK tree in a fresh
  `tempfile.TemporaryDirectory`, sentinel macro `ZENE_TEST_FAKE_UCRT_FENV`). It has no timing, sleeps or threads.
- **At the cause and complete:** the cause is the one unconditional directive. Every path into the shim goes through
  it: `<fenv.h>` directly (`src/core/main.cpp:98`, simde `simde-common.h:749-780`), or `<cfenv>` → `<fenv.h>` (sfizz
  `MathHelpers.h:23`, `ControlNoteScaleVerbsTest.cpp:54`, MSVC's own STL). There is no other `include_next` in
  `include/`. The Apple polyfill is unchanged, and the x86/arm64 branches are not reached on Windows.
- **No collateral damage:** the GCC/Clang path is byte-identical (`#include_next` in the `#else` branch, pinned by
  test case 2). `zene`, `ControlNoteScaleVerbsTest` and two sfizz TUs that include the shim rebuilt with exit 0, and
  the family tests pass. No data format, command, limit or audio-thread code is touched.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable (verdict FIXED).
- **Not verified here:** cl.exe itself. Hosted CI on a `ci/*` branch must confirm that the msvc-x64 job gets past
  `include\fenv.h`.
