FIXED

# Cluster ci-d2bfae2f2ab57bfb — windows-arm64 CI cannot compile `SfzPlayerTest` (nested Abseil header not found)

## Decision and evidence

**REAL — test build configuration** (not product, not infrastructure). On the pinned train
`050/m0-train-33` at `cc29a63dde09cf28bb00498a2d2a5b124d4d614d`, the hosted `windows-arm64` job stops at
`tests/src/plugins/SfzPlayerTest.cpp`:

```
C:/a/zene-studio/zene-studio/tests/src/plugins/../../../plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h:23:10:
fatal error: 'absl/meta/type_traits.h' file not found
```

- `tests/src/plugins/SfzPlayerTest.cpp:44` (before this fix) included the vendored header by relative path:
  `#include "../../../plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h"`.
  That path finds the one header, but the header's own includes at
  `plugins/SfzPlayer/sfizz/external/abseil-cpp/absl/random/internal/iostream_state_saver.h:23-24`
  (`"absl/meta/type_traits.h"`, `"absl/numeric/int128.h"`) are resolved by the compiler's include search, not
  relative to the vendored tree.
- The failing compiler command in the CI log has no vendored Abseil include root, and neither does the local
  target's generated flags (`build/tests/CMakeFiles/SfzPlayerTest.dir/flags.make:7` before the fix). The target
  only received `add_dependencies(... sfzplayer)` at `tests/CMakeLists.txt:2809-2813`.
- The sibling sfizz white-box tests already configure the root explicitly
  (`tests/CMakeLists.txt:2253-2263`, including `${CMAKE_SOURCE_DIR}/plugins/SfzPlayer/sfizz/external/abseil-cpp`).
  `SfzPlayerTest`'s direct include was added later by cluster new-p23-128 (commit `78953ffbb1`) without that
  configuration.
- Why Linux never saw it: this host has a system Abseil and the nested includes silently resolved there.
  `build/tests/CMakeFiles/SfzPlayerTest.dir/src/plugins/SfzPlayerTest.cpp.o.d` lists the outer header from the
  vendored tree but `/usr/include/absl/numeric/int128.h` from the host. The two trees are different releases
  (vendored `ABSL_LTS_RELEASE_VERSION 20230125`, patch 3; host `20260817`). `windows-arm64` has no system
  Abseil, so the same command fails outright. The CI log's failing command is therefore the observed
  manifestation of a real include-configuration defect, not a runner problem.

The defect is confined to the test target's build configuration; no product source changes.

## Fix (at the cause)

- `tests/CMakeLists.txt:2264-2272` — `SfzPlayerTest` now gets the same vendored Abseil include root as the
  sfizz white-box tests, so every `absl/...` include in the TU resolves inside the vendored tree on every
  platform. The target is still built and configured everywhere; only the include search gains the root.
- `tests/src/plugins/SfzPlayerTest.cpp:43-54` — the header is now included by its canonical path
  (`absl/base/config.h`, `absl/random/internal/iostream_state_saver.h`), and a compile-time
  `static_assert(ABSL_LTS_RELEASE_VERSION == 20230125, ...)` fails the build if a host Abseil is selected
  instead of the vendored one. That is the deterministic regression: on Linux without the include root the
  compiler picks the host copy and the build fails with a clear message; on Windows without a host Abseil it
  fails at the missing nested header. With the root it compiles against the vendored 20230125 tree.

No existing target, test, environment, timeout, assertion or property was changed. The product (plugin,
sfizz, vendored Abseil) is untouched.

## Regression matrix

| Path | Coverage |
| --- | --- |
| Normal | `SfzPlayerTest` builds and all 1/1 registered CTest cases pass; the whole `Sfz` family 11/11 passes. |
| Failure / reconfiguration | With the include-root hunk reversed, the build fails at the provenance `static_assert` (host Abseil 20260817 selected); on a host with no Abseil, the same TU fails at `absl/meta/type_traits.h` as `windows-arm64` did. With the hunk restored it builds and passes. |
| Boundary | The provenance assertion distinguishes the vendored release (20230125) from the host release (20260817), so a mixed or host include resolution can never silently pass again. |
| Invalid input / exhaustion | Not applicable: this is a compile-time include configuration; no runtime input, capacity or resource is involved. |
| Audio thread | No production or render-path code changed. `SfzPlayerRealtimeTest` (the sfizz render-thread regression) passes in the same family run. |
| Platforms | The include root and canonical `absl/...` spelling are compiler/generator neutral and match the existing sfizz white-box tests. Verified on this Linux C++20 build (GCC, RelWithDebInfo); the evidenced failing lane is `windows-arm64` (clang-cl/MSYS). The previous relative spelling could not work on any host without a matching system Abseil, so the fix also covers the same configuration on the other Windows lanes even though only `windows-arm64` was supplied as evidence. |

## Build and test evidence

The prescribed LOG FOLDER (`…/runs/ci-d2bfae2f2ab57bfb.r1/logs`) is not writable by this session's file
sandbox (`cp … logs/build-2.log` → `Permission denied`, exit 1). All build/test logs were therefore written
to the on-disk scratch directory `$TMPDIR/logs` (`/home/kruzzzzy/.cache/zene-tmp/zene-qwen-ci-d2bfae2f2ab57bfb/logs`)
and are named below. Command output was captured unpiped; the exit codes are the shell's.

| Phase | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| Baseline build on exact commit | `cmake --build build --target SfzPlayerTest` | 0 | `build-1.log` |
| Baseline test on exact commit | `ctest -R '^SfzPlayerTest$' --output-on-failure` (from `build/tests`) | 0 | `test-0-baseline.log`; 1/1 passed |
| RED — regression added, no CMake fix | `cmake --build build --target SfzPlayerTest` | 2 | `build-2-red-no-cmake-fix.log`; `error: static assertion failed: SfzPlayerTest must compile against the vendored Abseil 20230125; add its include root to this target` |
| GREEN — CMake fix applied | `cmake --build build --target SfzPlayerTest` | 0 | `build-3-green-with-cmake-fix.log` |
| GREEN test | `ctest -R '^SfzPlayerTest$' --output-on-failure` | 0 | `test-1-green-sfzplayer.log`; 1/1 passed |
| 4b reverse fix | `git diff -- tests/CMakeLists.txt > $TMPDIR/logs/fix.patch`; `git apply -R $TMPDIR/logs/fix.patch` | 0; 0 | `fix.patch` |
| 4b RED build | `cmake --build build --target SfzPlayerTest` | 2 | `build-4-red-reverted.log`; same provenance `static_assert` failure (host Abseil selected) |
| 4b RED test | removed the stale binary (`rm -f build/tests/SfzPlayerTest`, after `file` showed an ELF executable), then `ctest -R '^SfzPlayerTest$' --output-on-failure` | 8 | `test-2-red-reverted.log`; `356 - SfzPlayerTest (Not Run)` — without the fix the target cannot be produced, so no stale green binary can mask it |
| 4b restore and GREEN build | `git apply $TMPDIR/logs/fix.patch`; `cmake --build build --target SfzPlayerTest` | 0; 0 | `build-5-green-restored.log` |
| 4b GREEN test | `ctest -R '^SfzPlayerTest$' --output-on-failure` | 0 | `test-3-green-restored.log`; 1/1 passed |
| Family build | `cmake --build build --target SfzPlayerTest SfzPlayerRealtimeTest SfzFilePoolTest SfzEffectBusViewTest SfzDistoBuffersTest SfzSimdHelperAlignmentTest SfzEQPoolExhaustionTest SfzFlexEnvelopeZeroStageTest SfzFilterEqDescriptionTest SfzSimdGainAVXTest` | 0 | `build-6-sfz-family.log` |
| Family tests | `ctest -R 'Sfz' --output-on-failure` (from `build/tests`) | 0 | `test-5-sfz-family-built.log`; 11/11 passed (the first family run, `test-4-sfz-family.log`, reported 9 unbuilt targets as "Not Run"; they were then built and all passed) |
| Product rebuild | `cmake --build build --target zene` | 0 | `build-7-zene.log` |

## Gates

| Command | Exit | Result |
| --- | ---: | --- |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-ci-d2bfae2f2ab57bfb` | 0 | `gate-1.log` (before commit) and `gate-2-after-commit.log` (after commit): all eight rows PASS, `RESULT: PASS`, including `fork-sources` with the report registration and `all-sources-reproduce`. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-ci-d2bfae2f2ab57bfb ci-d2bfae2f2ab57bfb` | 0 | `submit-1.log`: `PRE-SUBMIT CHECK … RESULT: PASS - submit`; commits, clean tree, report, changed test and gates all PASS, and every ASK item is answered above. |

## Self-check

- **Regression fails reverted and passes restored:** RED `build-4-red-reverted.log` (build exit 2) and
  `test-2-red-reverted.log` (CTest exit 8, "Not Run" with no stale binary); GREEN `build-5-green-restored.log`
  (0) and `test-3-green-restored.log` (0, 1/1). The pre-fix RED is `build-2-red-no-cmake-fix.log` (2).
- **Deterministic:** the regression is the source's compile-time `static_assert` on the vendored release
  constant plus the header include itself. No clock, sleep, thread or runtime race is involved, and no values
  are injected at run time.
- **Cause complete / siblings checked:** the only direct Abseil include in `tests/` is `SfzPlayerTest.cpp`;
  `SfzFlexEnvelopeZeroStageTest.cpp`'s `<absl/types/span.h>` is already in the explicit include-root block
  (`tests/CMakeLists.txt:2253-2263`), and `SfzSimdHelperAlignmentTest` links `sfzplayer_sfizz`, whose
  `SFIZZ_SRC` is a `PUBLIC` include directory. No other test target needs the root, and no caller of the
  changed target is affected.
- **No collateral damage:** the change only adds an include directory to one test target and replaces one
  test's relative include with the canonical spelling. Project save/load, undo, command surfaces, mixing and
  render behaviour are untouched; the sfizz family and the `zene` product binary rebuild and pass.
- **Verdict is FIXED:** the include root is absent at `tests/CMakeLists.txt:2264` before this change and the
  CI log shows the resulting missing nested header; the code and the failing lane agree, so this is not an
  already-fixed or false-positive finding.
- **Other Windows lanes:** not supplied as evidence. The defect is in a target that is configured on every
  platform, and the previous spelling could only work where a matching system Abseil happened to exist, so
  the fix is platform-neutral; `windows-arm64` is the only lane this report claims to have reproduced.

## Files in the fix commit

- `tests/CMakeLists.txt`
- `tests/src/plugins/SfzPlayerTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt` (report registration)
- `docs/qwen-fixes-2026-10/cluster-ci-d2bfae2f2ab57bfb.md`
