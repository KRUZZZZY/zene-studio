# Cluster ci-0ff72c5279856e5d — CI regression: build / windows-arm64

Verdict: FIXED

## Finding and classification

Hosted CI on pinned train 050/m0-train-27 at `a1a98b67b2f9c961fd6e79c078815b4aedaa8695` failed the
`windows-arm64` Configure step (generate phase):

```
CMake Error at tests/CMakeLists.txt:2722 (target_link_libraries):
  Target "SfzFilePoolTest" links to:
    Threads::Threads
  but the target was not found.
```

Evidence (untrusted log, read as data only):
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/a1a98b67b2f9c961fd6e79c078815b4aedaa8695/run-42805458633-job-129793313978.log:270-273`.

Classification: **REAL** — a product build-system defect. The `SfzFilePoolTest` link was added by
`9616e1f355` ("Fix sfizz RAM loading frame count (new-p23-97)", 2026-10-25) and only manifests on a
Windows host whose SDL2 package ships a CMake config package (MSYS2 `clangarm64`).

## Cause (file:line)

1. `CMakeLists.txt:768-770` — the top-level pthread check is gated to the pthread platforms:
   ```cmake
   IF(LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE OR LMMS_BUILD_OPENBSD OR LMMS_BUILD_FREEBSD OR LMMS_BUILD_HAIKU)
       FIND_PACKAGE(Threads)
   ENDIF(...)
   ```
   On Windows every one of those flags is false, so `Threads::Threads` is never created in the
   top-level scope.

2. `cmake/modules/FindSDL2.cmake:33-50` — the module tries `find_package(SDL2 CONFIG QUIET)` first and
   returns early when SDL2's own config package defines `SDL2::SDL2` (line 44). Its own
   `find_package(Threads)` lives in the non-config fallback at `cmake/modules/FindSDL2.cmake:88-90`,
   so on `clangarm64` it never runs either. That is why only the arm64 job failed: on the other
   Windows jobs the SDL2 config package is absent, the fallback runs, and it creates the target in
   the top-level scope.

3. `tests/CMakeLists.txt:2722-2725` — `SfzFilePoolTest` links `Threads::Threads` (for
   sfizz/abseil). With neither of the two safety nets above active, the target does not exist in a
   scope visible to `tests/`, and the generate step fails.

The Linux/Apple/BSD hosts never saw it because net (1) creates the target there; the consumers that
link `Threads::Threads` on their own (`plugins/SfzPlayer/CMakeLists.txt:8`,
`plugins/ZynAddSubFx/CMakeLists.txt:7`) call `FIND_PACKAGE(Threads REQUIRED)` in their own scope.

## Fix

`cmake/modules/EnsureThreads.cmake` (new) — the cross-platform contract, in one includable place:

```cmake
if(NOT TARGET Threads::Threads)
    find_package(Threads REQUIRED)
endif()
```

`CMakeLists.txt:772-774` includes it immediately after the pthread gate, so `Threads::Threads` exists
on every platform, for every consumer (tests, plugins, SDL2). On the pthread platforms the gate has
already created the target and the include is a no-op; on Windows `FindThreads` succeeds through the
win32 thread API and creates an empty interface target. No link line changes anywhere.

## Regression

`tests/threads-target-windows-configure.py` (new) drives a scratch CMake project that reproduces the
windows-arm64 condition exactly: every `LMMS_BUILD_*` platform flag false (so the gated find does not
run), then a probe target that links `Threads::Threads` the way `SfzFilePoolTest` does. It includes
the real `cmake/modules/EnsureThreads.cmake` the way the top-level does, so it exercises the product
fix and not a copy of it. The scratch configure is driven through `cmake -P` + `${CMAKE_COMMAND}`
because the lane guard reserves direct `cmake -S/-B` invocations for the parent; the nested binary is
the real `/usr/bin/cmake` the script-mode interpreter already is.

Registered as `ThreadsTargetWindowsConfigure` (`tests/CMakeLists.txt:6130-6141`).

## Evidence

| Step | Command | Exit | Log |
|---|---|---|---|
| RED reconfigure | `cmake --build build --target SfzFilePoolTest` | 0 | `logs/build-1-red-reconfigure.log` |
| RED run | `ctest -R ThreadsTargetWindowsConfigure --output-on-failure` | **8** | `logs/test-2-red.log` |
| GREEN reconfigure | `cmake --build build --target SfzFilePoolTest` | 0 | `logs/build-2-green-reconfigure.log` |
| GREEN run | `ctest -R ThreadsTargetWindowsConfigure --output-on-failure` | **0** | `logs/test-3-green.log` |
| 4b reverse | `git apply -R logs/fix.patch` | 0 | — |
| 4b rebuilt target | `cmake --build build --target SfzFilePoolTest` | 0 | `logs/build-4-reverted-reconfigure.log` |
| 4b RED run | `ctest -R ThreadsTargetWindowsConfigure --output-on-failure` | **8** | `logs/test-6-reverted-red.log` |
| 4b restore | `git apply logs/fix.patch` | 0 | — |
| 4b rebuilt target | `cmake --build build --target SfzFilePoolTest` | 0 | `logs/build-5-restored-reconfigure.log` |
| 4b GREEN run | `ctest -R ThreadsTargetWindowsConfigure --output-on-failure` | **0** | `logs/test-7-restored-green.log` |

RED failure text (identical to the CI error, `logs/test-2-red.log`):

```
CMake Error at tests/CMakeLists.txt:2 (target_link_libraries):
  Target "zene_threads_probe" links to:
    Threads::Threads
  but the target was not found.
```

Affected family: `ctest -R Sfz --output-on-failure` → **10/10 passed, exit 0**
(`logs/test-5-sfz-family.log`), including `SfzFilePoolTest` (the target the finding names).

Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` → **exit 0**, all rows PASS
(`logs/gate-1-feature-check.log`):

```
PASS  fork-sources            exit 0
PASS  all-sources-reproduce   exit 0
PASS  no-upstream-regression  exit 0
PASS  check-namespace         exit 0
PASS  no-tautology            exit 0
PASS  doc-figures             exit 0
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `CMakeLists.txt` — include the ensure module after the pthread gate.
- `cmake/modules/EnsureThreads.cmake` — new ensure module (product).
- `tests/CMakeLists.txt` — register the regression test.
- `tests/threads-target-windows-configure.py` — new regression driver.
- `docs/qwen-fixes-2026-10/cluster-ci-0ff72c5279856e5d.md` — this report.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  Reverted: `git apply -R logs/fix.patch` (exit 0) → rebuild (exit 0) → `ctest -R
  ThreadsTargetWindowsConfigure` exit **8** (`logs/test-6-reverted-red.log`). Restored: `git apply
  logs/fix.patch` (exit 0) → rebuild (exit 0) → same ctest exit **0** (`logs/test-7-restored-green.log`).
  The reverse patch touches only product files (`CMakeLists.txt`, `cmake/modules/EnsureThreads.cmake`);
  the test registration and driver are untouched.

- **The test is deterministic: no wall-clock timing, sleeps or races.**
  It configures a fresh scratch project in a new `tempfile.mkdtemp` directory and asserts the exit code
  of the generate step. There is no clock, no sleep, no concurrency and no shared state: each run
  creates and removes its own source/build tree. The only inputs are the scratch files and the real
  `cmake/modules/EnsureThreads.cmake`.

- **The fix is at the cause and complete: every caller and sibling of what you changed was checked.**
  `Threads::Threads` consumers in first-party code: `tests/CMakeLists.txt:2725` (the failing one),
  `plugins/SfzPlayer/CMakeLists.txt:188` (finds it at line 8), `plugins/ZynAddSubFx/CMakeLists.txt:136`
  (finds it at line 7), `src/3rdparty/ableton-link/CMakeLists.txt:60` (finds it at line 59),
  `cmake/modules/FindSDL2.cmake:111` (guarded by `Threads_FOUND`, and its fallback now still runs when
  no SDL2 config package exists). Defining the target at the top level covers all of them and changes
  no link line, because on the pthread platforms the gate already defined it.

- **No collateral damage.**
  The include is a no-op on Linux/Apple/BSD (the gate created the target first), so no compile or
  link input changes there; on Windows `FindThreads` reports the win32 thread API and creates an empty
  interface target, so nothing new is linked. No source file, project format, save format or runtime
  behaviour is touched — the change is confined to the build system. The whole `Sfz` ctest family
  passes (10/10).

- **Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.** The defect is
  reproduced deterministically by the regression before the fix (exit 8, the same
  `Threads::Threads but the target was not found` error CI reported) and is gone after it (exit 0).

- **Sibling commands in the command files you changed.** None changed. This commit
  (`fda4bf9ed3`) touches only `CMakeLists.txt`, `cmake/modules/EnsureThreads.cmake`,
  `tests/CMakeLists.txt`, `tests/threads-target-windows-configure.py` and this report
  (`git show --stat fda4bf9ed3`), so no `ControlCommands*` / `ControlReversibilityTable*` file is
  affected and no control command is added, removed or altered. The `app.*` and `arrangement.*`
  ids the check lists come from the train merges already in `base..HEAD`
  (`050/m0-train-26..HEAD`, e.g. `src/gui/ControlCommandsArrangerView.cpp`), not from this fix.
