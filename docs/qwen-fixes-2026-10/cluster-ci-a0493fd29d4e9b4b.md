FIXED

# Cluster ci-a0493fd29d4e9b4b: msvc-x64 cannot compile RemotePluginShmFifoCompileCheck

## Verdict

REAL on the finding's commit `dec3f1d45c62b26bce955875521780db173a0228`. This is a
build-configuration defect in the test target, not infrastructure and not a flaky
runner. A later train commit (`5854c983c8`, "missing Qt compiler requirements on
the shared-memory compile-check target") carries the same one-line repair, but that
commit is not an ancestor of `dec3f1d45c`; the finding's build tree has the defect.

## Evidence

The CI log stops at the compile of `src/common/RemotePluginBase.cpp` in the
`RemotePluginShmFifoCompileCheck` target with:

```
qcompilerdetection.h(1258): fatal error C1189: #error:
  "Qt requires a C++17 compiler, and a suitable value for __cplusplus.
   On MSVC, you must pass the /Zc:__cplusplus option to the compiler."
```

`RemotePluginBase.cpp` includes Qt through `include/RemotePluginBase.h:86`
(`#include <QString>`). Qt 6 supplies `/Zc:__cplusplus` to consumers through
`Qt6::Platform` (`/usr/lib/cmake/Qt6/QtPlatformTargetHelpers.cmake:77` calls
`qt_set_msvc_cplusplus_options(Platform INTERFACE)`; that function adds
`-Zc:__cplusplus` at `/usr/lib/cmake/Qt6/QtFlagHandlingHelpers.cmake:384-390`).
The product's own targets get it from `lmmsobjs`
(`src/CMakeLists.txt:340-342`, `TARGET_COMPILE_OPTIONS(lmmsobjs PUBLIC ...)`).

Before the fix, `tests/CMakeLists.txt:3197-3199` compiled `RemotePluginBase.cpp`
while borrowing only `$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>`:

```
add_library(RemotePluginShmFifoCompileCheck OBJECT ${CMAKE_SOURCE_DIR}/src/common/RemotePluginBase.cpp)
target_include_directories(RemotePluginShmFifoCompileCheck PRIVATE
    $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
```

It linked no Qt target, so it consumed neither `QT_CORE_LIB` nor Qt's MSVC
`/Zc:__cplusplus`. The sibling targets in the same block already link
`${QT_LIBRARIES}` (`tests/CMakeLists.txt:3220` and `:3235`); only the compile
check was missing it.

## Fix

`tests/CMakeLists.txt:3200-3204`:

```cmake
# RemotePluginBase.cpp includes <QString>, so consume Qt's usage requirements too
# (QT_CORE_LIB and, on MSVC, /Zc:__cplusplus from Qt6::Platform); borrowing only
# lmmsobjs's include directories left cl.exe rejecting the Qt header (cluster
# ci-a0493fd29d4e9b4b).
target_link_libraries(RemotePluginShmFifoCompileCheck PRIVATE ${QT_LIBRARIES})
```

The sibling audits were checked: `RemotePluginShmFifoTest` and both
`RemotePluginShmFifoNoSysIpc*Probe` targets already link Qt; every other target
that compiles a Qt-including translation unit either links Qt or `lmmsobjs` after
`target_static_libraries` (`tests/CMakeLists.txt:2359-2402`). `PluginExportBranchTest`
receives Qt's `Platform` options (its compile flags contain the GNU equivalent
`-mno-direct-extern-access`), so it gets `/Zc:__cplusplus` on MSVC despite its own
`QT_CORE_LIB` define being absent from `flags.make`.

## Regression test

`tests/remote-plugin-shm-fifo-qt-usage-check.py` is registered as
`RemotePluginShmFifoQtUsageCheck` at `tests/CMakeLists.txt:3250-3264`. It:

- reads `build/compile_commands.json`, requires the configured
  `RemotePluginShmFifoCompileCheck` command to carry `QT_CORE_LIB`, and on MSVC
  requires `/Zc:__cplusplus`;
- compiles that command against a probe that errors without `QT_CORE_LIB`
  (positive control), then removes the define and requires failure (negative
  control);
- extracts the target's own CMake block and configures it in a synthetic CMake
  project whose `QtContract` interface provides `QT_CORE_LIB` and
  `/Zc:__cplusplus`, requiring the block with the link line to inherit the option
  and the block with the link line removed not to inherit it. The synthetic
  project asks CMake for the host's **default generator**; it does not require
  Ninja (qa review `ci-a0493fd29d4e9b4b.r1`).

It uses `--cmake ${CMAKE_COMMAND}` (the configured absolute CMake) so the suite is
not affected by wrapper shims in `PATH`. No wall-clock timing, sleeps or races.

## Test matrix

| Path | Coverage |
|---|---|
| normal | configured compile-check command compiles with Qt's defines (positive control) |
| failure | removing `QT_CORE_LIB` from the same command fails (negative control) |
| reconfiguration | the synthetic project models the current CMake block at test time using the host's default generator; a removed link line is the red case |
| audio thread | no product runtime code touched; the change is target usage requirements only |
| supported platforms | Linux host executes the model and the compile-database probe; the msvc-x64 CI build is the native MSVC gate |

## RED/GREEN proof (exact commands and exit codes)

Logs were produced in
`$TMPDIR/ci-a0493fd29d4e9b4b-logs` (`TMPDIR=/home/kruzzzzy/.cache/zene-tmp/zene-qwen-ci-a0493fd29d4e9b4b`).
The provided LOG FOLDER was not writable by this session: `touch` and the
filesystem tool both failed with `EACCES` even under the reported
`danger-full-access` policy (Landlock is enabled: `/sys/kernel/security/lsm` lists
`landlock`). The log files remain at the path above; no claim below relies on a
file I did not inspect.

1. Pre-fix RED (test present, product line absent):

```
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
EXIT=8
# RemotePluginShmFifoCompileCheck compile command lacks propagated QT_CORE_LIB
```

2. With the fix, GREEN:

```
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
EXIT=0   # 1/1 passed
```

3. Reverse the product hunk only, rebuild, RED again:

```
git apply -R "$TMPDIR/ci-a0493fd29d4e9b4b-logs/fix.patch"
APPLY_REVERSE_EXIT=0
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
BUILD_EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
CTEST_EXIT=8
# RemotePluginShmFifoCompileCheck compile command lacks propagated QT_CORE_LIB
```

4. Restore the fix, rebuild, GREEN again:

```
git apply "$TMPDIR/ci-a0493fd29d4e9b4b-logs/fix.patch"
APPLY_EXIT=0
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
BUILD_EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
CTEST_EXIT=0   # 1/1 passed
```

The reverse patch contains only the two-line product change plus its comment; the
test registration stays in place, so the same CTest is found in steps 3 and 4.

5. Affected family, after restore:

```
cd build/tests && ctest -R '^(RemotePluginShmFifo|Train43Portability)' --output-on-failure
EXIT=0   # 3/3 passed: RemotePluginShmFifoTest, RemotePluginShmFifoQtUsageCheck, Train43Portability
```

6. Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
EXIT=0   # RESULT: PASS (all eight rows PASS)
```

## Round 2 rework: default generator, not Ninja

QA review `ci-a0493fd29d4e9b4b.r1` found that the synthetic model hard-coded
`-G Ninja` (the old line 160) and therefore failed on a valid host without Ninja.
The command now passes no `-G`; CMake chooses the host's default generator.
Nothing else in the positive/negative controls changed.

`ninja` was removed from `PATH` by pointing PATH at a directory of `/usr/bin`
symlinks with `ninja` and `ninja-build` omitted
(`$TMPDIR/no-ninja-bin-a0493fd29d4e9b4b`), then running the same CTest:

```
cmake --build build --target zene RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
BUILD_EXIT=0
cd build/tests && env PATH="$TMPDIR/no-ninja-bin-a0493fd29d4e9b4b" CXX=/usr/bin/c++ \
    /usr/bin/ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
CTEST_EXIT=0   # 1/1 passed without Ninja

cd build/tests && ctest -R '^(RemotePluginShmFifo|Train43Portability)' --output-on-failure
EXIT=0   # 3/3 passed in the normal environment
```

With the Qt usage link reversed, the same test fails in **both** environments:

```
git apply -R "$TMPDIR/ci-a0493fd29d4e9b4b-logs/fix.patch"   # APPLY_REVERSE_EXIT=0
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
BUILD_EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
NORMAL_CTEST_EXIT=8   # lacks propagated QT_CORE_LIB
cd build/tests && env PATH="$TMPDIR/no-ninja-bin-a0493fd29d4e9b4b" CXX=/usr/bin/c++ \
    /usr/bin/ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
NONINJA_CTEST_EXIT=8  # same failure

git apply "$TMPDIR/ci-a0493fd29d4e9b4b-logs/fix.patch"       # APPLY_EXIT=0
cmake --build build --target RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck -j2
BUILD_EXIT=0
cd build/tests && ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
NORMAL_CTEST_EXIT=0   # 1/1 passed
cd build/tests && env PATH="$TMPDIR/no-ninja-bin-a0493fd29d4e9b4b" CXX=/usr/bin/c++ \
    /usr/bin/ctest -R '^RemotePluginShmFifoQtUsageCheck$' --output-on-failure
NONINJA_CTEST_EXIT=0  # 1/1 passed
```

## Files committed

- `tests/CMakeLists.txt` (fix + regression registration)
- `tests/remote-plugin-shm-fifo-qt-usage-check.py` (new regression)
- `tests/fork-sources.txt` (new-file registration)
- `tests/upstream-modifications.txt` (inherited-file change reason)
- `docs/qwen-fixes-2026-10/cluster-ci-a0493fd29d4e9b4b.md` (this report)

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it
  (exit codes).** Reverse: `git apply -R .../fix.patch` EXIT=0, rebuild EXIT=0,
  CTest EXIT=8 with `lacks propagated QT_CORE_LIB`. Restore: `git apply .../fix.patch`
  EXIT=0, rebuild EXIT=0, CTest EXIT=0 (1/1). See section above.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It reads
  the configured compile command, runs one syntax-only compile, and configures a
  synthetic CMake model with the target's own block. It uses the host's default
  CMake generator, so it does not require Ninja (proved above with Ninja absent
  from `PATH`). No timing or scheduling.
- **The fix is at the cause and complete: every caller and sibling checked.** The
  three sibling targets already link Qt; the other direct-compile target
  `zene_allocation_probe` includes no Qt. The compile database audit found no other
  target that includes Qt directly without consuming Qt usage requirements.
- **No collateral damage.** No runtime code, saved data, undo/command path or gate
  changed. The only target property added is Qt's usage requirements to an object
  library that is never linked into a product binary; existing family tests
  (`RemotePluginShmFifoTest`, `Train43Portability`) still pass.
- **Verdict evidence.** `src/common/RemotePluginBase.cpp` includes
  `include/RemotePluginBase.h:86` (`<QString>`), Qt adds `/Zc:__cplusplus` through
  `Qt6::Platform` (`QtPlatformTargetHelpers.cmake:77`,
  `QtFlagHandlingHelpers.cmake:384-390`), and the pre-fix target linked no Qt
  (`tests/CMakeLists.txt:3197-3199`). This is not an already-fixed tree: the fix
  commit `5854c983c8` is absent from `dec3f1d45c`'s history.
