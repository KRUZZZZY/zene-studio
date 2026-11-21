# Cluster ci-d14f7617bae6160d: macos-x86_64 build fails on the VST2 fixture's snprintf

**Verdict: FIXED**

## Root cause

`tests/data/vst2-test-instrument/vst2-test-instrument.c:47` (before the fix) defined
`_POSIX_C_SOURCE 199309L` before any system header. Darwin's `<sys/cdefs.h>` maps that to
`__DARWIN_C_LEVEL = 199309L` (unless `_DARWIN_C_SOURCE` is also defined), and Darwin's
`<_stdio.h>` declares `snprintf` only when

```c
__DARWIN_C_LEVEL >= 200112L || defined(_C99_SOURCE) || defined(__cplusplus)
```

so the C99 prototype was hidden. Apple clang makes `-Wimplicit-function-declaration` a hard
error in C99 and later, so the first call at `:173:2` failed and `make all` exited 2
(hosted run 42805458633, job 129793314406, log lines 1340-1407). `nanosleep` is POSIX.1b
(199309L) and stayed visible, which is why the error surfaced at the first `snprintf`, not
at `stall()`.

Both rules were read from Apple's own headers, not assumed:
`apple-oss-distributions/xnu` `bsd/sys/cdefs.h` (the `__DARWIN_C_LEVEL` decision) and
`apple-oss-distributions/Libc` `include/_stdio.h` (the `snprintf` guard). A local probe
(`01-linux-snprintf-probe.log`) confirms glibc declares `snprintf` under the same
`_POSIX_C_SOURCE` with gcc and clang in `c99` and `gnu17`, which is why the defect is
Darwin-only.

## Fix

`vst2-test-instrument.c:50` now defines the macro only where it is needed:

```c
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 199309L  /* nanosleep */
#endif
```

On Darwin the default `__DARWIN_C_LEVEL` is `__DARWIN_C_FULL`, which exposes both
`nanosleep` and `snprintf`; no `_DARWIN_C_SOURCE` is needed. The `_WIN32` branch
(`:57-62`) and the winegcc PE build are untouched. No product (`src/`) code is involved.

## Regression

`Vst2FixtureDarwinFeatureLevel` (new CTest, registered in
`tests/data/vst2-test-instrument/CMakeLists.txt`) preprocesses the fixture's real
feature-test preamble (before its first system header) twice with an appended assertion.
The Darwin simulation clears `_WIN32`, defines `__APPLE__`, and checks the relevant
`__DARWIN_C_LEVEL` rule from Apple's header; the POSIX probe checks the host's non-Apple
path. GNU-style compilers read stdin, while an MSVC-style frontend uses `/EP` on a temporary
source file. The native module build on macOS still compiles the fixture against the SDK's
actual headers.

`Vst2ChunkContractCheck` (new CTest) exercises the ZNV2 chunk contract directly: the written
text, the parse, the 128-byte read guard (`:190`) and the rejection of malformed input
(`:193`). Carla only ever hands a host well-formed chunks, so those branches are unreachable
through `CarlaVst2HostingTest`.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `ctest -R Vst2FixtureDarwinFeatureLevel --output-on-failure > …/06-regression-red.log 2>&1; echo EXIT=$?` from `build/tests`; **EXIT=8**. Probe reported `#error "vst2-test-instrument: the Darwin feature level hides the C99 snprintf declaration (ci-d14f7617bae6160d)"`; the POSIX probe passed. |
| GREEN after fix | Same ctest command, output to `08-regression-green.log`; **EXIT=0**, 1/1 CTest target passed. |
| Reversed-fix proof | `git apply -R …/fix.patch` **EXIT=0**; `cmake --build build --target vst2-test-instrument` **EXIT=0**; ctest output to `10-regression-reverted.log` **EXIT=8** (same `#error`). |
| Restored-fix proof | `git apply …/fix.patch` **EXIT=0**; rebuild **EXIT=0**; ctest output to `12-regression-reapplied.log` **EXIT=0**, 1/1 passed. |
| Affected family | `ctest -R "Vst2FixtureDarwinFeatureLevel\|Vst2ChunkContractCheck\|CarlaVst2HostingTest" --output-on-failure --timeout 300` from `build/tests`; **EXIT=0**, 3/3 CTest targets passed (`35-vst2-family-rerun.log`). |
| Chunk contract | `ctest -R Vst2ChunkContractCheck --output-on-failure` **EXIT=0**; direct run prints `chunk contract OK`, **EXIT=0**. |
| Hosting with runtime | `ctest -R CarlaVst2HostingTest -V` after `tools/build-carla-runtime.sh build/carla-runtime` (**EXIT=0**); **10 passed, 0 failed, 3 skipped** (the Wine-bridged cases). |
| Feature gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`; **EXIT=0**, all 8 rows PASS (`34-feature-check-final3.log`). |

## Build and test commands

All builds ran from the worktree root; all ctest runs ran from `build/tests`. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-d14f7617bae6160d.r1/logs`.

```text
cmake --build build --target vst2-test-instrument > …/04-build-fixture.log 2>&1; echo EXIT=$?          # EXIT=0 (unfixed, for RED)
ctest -R Vst2FixtureDarwinFeatureLevel --output-on-failure > …/06-regression-red.log 2>&1; echo EXIT=$? # EXIT=8, RED
cmake --build build --target vst2-test-instrument > …/07-build-fixture-fixed.log 2>&1; echo EXIT=$?     # EXIT=0 (fix applied)
ctest -R Vst2FixtureDarwinFeatureLevel --output-on-failure > …/08-regression-green.log 2>&1; echo EXIT=$? # EXIT=0, GREEN
git apply -R …/fix.patch; echo EXIT=$?                                                                   # EXIT=0
cmake --build build --target vst2-test-instrument > …/09-build-reverted.log 2>&1; echo EXIT=$?          # EXIT=0
ctest -R Vst2FixtureDarwinFeatureLevel --output-on-failure > …/10-regression-reverted.log 2>&1; echo EXIT=$? # EXIT=8
git apply …/fix.patch; echo EXIT=$?                                                                      # EXIT=0
cmake --build build --target vst2-test-instrument > …/11-build-reapplied.log 2>&1; echo EXIT=$?          # EXIT=0
ctest -R Vst2FixtureDarwinFeatureLevel --output-on-failure > …/12-regression-reapplied.log 2>&1; echo EXIT=$? # EXIT=0
rm build/tests/data/vst2-test-instrument/native/ZeneVst2TestInstrument.so … (clean outputs)
cmake --build build --target vst2-test-instrument CarlaVst2HostingTest > …/20-clean-rebuild.log 2>&1; echo EXIT=$? # EXIT=0
cmake --build build --target vst2-test-instrument-wine > …/14-build-wine-fixture.log 2>&1; echo EXIT=$? # EXIT=0 (ZeneVst2TestInstrument.dll)
tools/build-carla-runtime.sh build/carla-runtime > …/24-carla-runtime.log 2>&1; echo EXIT=$?            # EXIT=0
ctest -R CarlaVst2HostingTest -V > …/25-carla-with-runtime.log 2>&1; echo EXIT=$?                        # EXIT=0, 10 passed / 0 failed / 3 skipped
cmake --build build --target vst2-chunk-contract-check > …/30-build-chunk-check.log 2>&1; echo EXIT=$?   # EXIT=0
ctest -R Vst2ChunkContractCheck --output-on-failure > …/31-chunk-check-run.log 2>&1; echo EXIT=$?        # EXIT=0
ctest -R "Vst2FixtureDarwinFeatureLevel|Vst2ChunkContractCheck|CarlaVst2HostingTest" --output-on-failure --timeout 300 > …/35-vst2-family-rerun.log 2>&1; echo EXIT=$? # EXIT=0, 3/3
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > …/34-feature-check-final3.log 2>&1; echo EXIT=$? # EXIT=0, PASS
```

The lane guard blocks the `all` target (`bunny-guard: target 'all' not allowed`, **EXIT=97**),
so the full-tree build the CI job runs was not reproduced here; the affected targets
(`vst2-test-instrument`, `vst2-test-instrument-wine`, `vst2-chunk-contract-check`,
`CarlaVst2HostingTest`) were rebuilt from clean outputs instead.

## Registration

- `tests/data/vst2-test-instrument/chunk-contract-check.c` — added to `tests/fork-sources.txt`
  and `tests/all-sources.txt` (the whole-tree recipe derives every tracked `*.c`).
- `tests/data/vst2-test-instrument/darwin-feature-level-check.py` — added to
  `tests/fork-sources.txt`. `tests/all-sources.txt` does not apply: its recipe derives only
  C/C++ plus two named `.py` drivers, and `fork-sources-gate.sh`'s non-tools scan is
  C/C++ only.
- `tests/upstream-modifications.txt` does not apply: both new files are fork-authored, and
  the modified `vst2-test-instrument.c` was already registered.
- `tests/data/vst2-test-instrument/CMakeLists.txt` — modified (test registration only).

## Portability

- Linux native `.so`: built (`build/tests/data/vst2-test-instrument/native/ZeneVst2TestInstrument.so`).
- macOS native module: the failing path; fixed. Verified here by the feature-level probe
  (Apple's rule, no macOS SDK on this host); the hosted job is the real proof.
- `_WIN32` branch (`:57-62`): unchanged; not compiled on this host.
- non-Apple winegcc PE (`:29`): `ZeneVst2TestInstrument.dll` built.
- Wine-bridged hosting: 3 cases skip because the runtime has no `carla-bridge-win64.exe`
  (needs a Windows cross toolchain); documented best-effort path, unchanged by this fix.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.**
  `Vst2FixtureDarwinFeatureLevel`: RED before the fix **EXIT=8** (`06-regression-red.log`),
  GREEN after **EXIT=0** (`08-regression-green.log`), reversed fix **EXIT=8**
  (`10-regression-reverted.log`), restored fix **EXIT=0** (`12-regression-reapplied.log`).
  `Vst2ChunkContractCheck` passes on the fixed tree (**EXIT=0**).

- **The test is deterministic.** The regression feeds the fixture's real source plus a
  preprocessor assertion to `cc -fsyntax-only -x c -` on stdin; the verdict is the compiler's
  exit status, with no wall-clock timing, sleeps, threads or races. The chunk check calls
  the fixture's own `writeChunk`/`readChunk` with fixed strings and fixed sizes (45, 0, 7,
  127, 128); no timers.

- **The fix is at the cause and complete.** The file has exactly one feature-test macro
  (`grep -n "_POSIX_C_SOURCE"` → the single define at `:51`); both `snprintf` calls (`:177`,
  `:213`) are in the same translation unit and are covered by the one probe. `nanosleep`
  (`:141`) is checked by the second probe (`_POSIX_C_SOURCE >= 199309L`). The only other
  first-party `_POSIX_C_SOURCE` is `src/core/main.cpp:1387`, a `#if` test that does not
  define it; the remaining hits are the Carla submodule's own sources, untouched.

- **No collateral damage.** The change is confined to a test-only fixture under
  `tests/data/`; no `src/` product code, no save/load or undo path, no control command.
  `CarlaVst2HostingTest` (which round-trips projects and presets through the fixture)
  passes 10/10 with the runtime; `Vst2ChunkContractCheck` confirms the ZNV2 text, the parse
  and the guards are unchanged. No limit or check was added that another caller could hit.

- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the CI error is real and the
  probe reproduces its exact cause on the unchanged source.

- **Sibling commands:** no command file was changed (`git diff --cached --name-only`
  contains no `ControlCommands*`, command JSON or `ControlRegistry` file), so `app.*`,
  `arrangement.*` and the rest of the listed commands are unaffected; the
  `ControlCommandsSnapshot` canary is untouched.

- **Realtime / undo / commands:** nothing added on the audio thread; no control commands.

- **Registration and gates:** all files registered as above; `feature_check.py` PASS
  (all 8 rows, **EXIT=0**).

- **Not proven here:** the actual Apple clang compile (no macOS host/SDK on this Linux
  box); the `all` target (the lane guard blocks it, **EXIT=97**). Both are stated, not
  claimed.

- **Unrelated flake:** `CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree`
  failed 1 of 3 runs (timing-based overlap assertion); it does not reference the VST2 fixture
  (`grep -c` = 0) and is untouched by this change.

Files committed:

- `tests/data/vst2-test-instrument/vst2-test-instrument.c`
- `tests/data/vst2-test-instrument/chunk-contract-check.c`
- `tests/data/vst2-test-instrument/darwin-feature-level-check.py`
- `tests/data/vst2-test-instrument/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-ci-d14f7617bae6160d.md`

## Independent QA review

The first review found that this new CTest passed GNU/Clang-only command-line flags to
`CMAKE_C_COMPILER`, which would fail under the `msvc-x64` job. The test driver now selects
the MSVC frontend's `/EP` mode and uses a temporary source file. Its Darwin simulation
also clears the host's `_WIN32` macro so the regression still catches the original
unconditional `#ifndef _WIN32` definition when run by an MSVC compiler.

QA verification on Linux: `cmake --build build --target zene vst2-test-instrument
vst2-chunk-contract-check CarlaVst2HostingTest -j12` EXIT=0; the focused VST2 family CTest
passed 3/3 (EXIT=0); after reversing only the feature-macro source hunk, the rebuild passed
(EXIT=0) and `Vst2FixtureDarwinFeatureLevel` failed with the expected `#error` (CTest
EXIT=8); after restoring the source with `git checkout`, rebuild and family CTest passed
(EXIT=0 each). The feature gate passed (EXIT=0), and the MSVC frontend branch was exercised
locally using `clang-cl` (EXIT=0). The QA build and CTest logs are `39-qa-build-fixed.log`
through `45-qa-vst2-family-restored.log`; the final feature-gate log is
`47-qa-feature-check-final.log` in the run log folder above.

The fixer re-verified both frontends directly on the fixed source: the GNU path
(`/usr/bin/cc GNU GNU`) and the MSVC path (`/usr/bin/clang-cl MSVC MSVC`) both exit 0
(`50-gnu-fixed.log`, `49-msvc-fixed.log`). With only the fixture hunk reversed, both exit 1
with the same expected `#error` (`42-gnu-reverted.log`, `41-msvc-reverted.log`), so the
MSVC branch catches the original defect too. The focused family was then rerun from clean
outputs (`43-rebuild.log`, `44-family.log` — 3/3 passed) and `feature_check.py` passed
again (`45-feature-check.log`).
