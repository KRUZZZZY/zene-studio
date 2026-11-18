FIXED

# Cluster ci-748342d46d7e9dd9 — MinGW64 CI configure failure: `Threads::Threads` not found

## Decision

**REAL.** The hosted MinGW64 configure failed at generate time on pinned train
`050/m0-train-27` at `a1a98b67b2f9c961fd6e79c078815b4aedaa8695`:

```
mingw64  Configure  CMake Error at tests/CMakeLists.txt:2722 (target_link_libraries):
mingw64  Configure    Target "SfzFilePoolTest" links to:
mingw64  Configure      Threads::Threads
mingw64  Configure    but the target was not found.
```

Evidence: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/a1a98b67b2f9c961fd6e79c078815b4aedaa8695/run-42805458633-job-129793314382.log`
(lines 1665-1670; the same log line 1570 shows `-- Found Threads: TRUE`, so threads
*were* found — just not in a scope `tests/` can see).

## Root cause (file:line)

- `CMakeLists.txt:768-770` — the root's thread search is gated to the pthread platforms:

  ```cmake
  IF(LMMS_BUILD_LINUX OR LMMS_BUILD_APPLE OR LMMS_BUILD_OPENBSD OR LMMS_BUILD_FREEBSD OR LMMS_BUILD_HAIKU)
      FIND_PACKAGE(Threads)
  ENDIF(...)
  ```

  On Windows (`LMMS_BUILD_WIN`) none of those flags is set, so the root never creates
  `Threads::Threads`.

- `tests/CMakeLists.txt:2717-2727` — the `SfzFilePoolTest` block linked the target directly:

  ```cmake
  target_link_libraries(${LMMS_TEST_NAME} PRIVATE
      sfzplayer_sfizz
      absl::strings absl::span absl::flat_hash_map absl::optional absl::memory
      Threads::Threads SndFile::sndfile)
  ```

- `plugins/SfzPlayer/CMakeLists.txt:8` calls `FIND_PACKAGE(Threads REQUIRED)`, and
  `plugins/SfzPlayer/sfizz/external/abseil-cpp/CMakeLists.txt:112` does too — but CMake
  imported targets are scoped to the directory that created them and below. Both are in
  sibling subtrees, so their `Threads::Threads` is invisible in `tests/`. On Linux the
  root call provides the target, which is why only the MinGW64 job failed.

The defect is in `tests/CMakeLists.txt`: it consumed a target that no ancestor scope is
obliged to provide on every platform. The other consumers of `Threads::Threads` in this
repo (`plugins/SfzPlayer/CMakeLists.txt:188`, `plugins/ZynAddSubFx/CMakeLists.txt:136`,
`plugins/LadspaEffect/calf/veal/...`) each call `find_package(Threads)` in their own
directory first; `tests/CMakeLists.txt` was the only one that assumed it.

## Fix (at the cause)

`tests/CMakeLists.txt:2717-2726` — the `SfzFilePoolTest` block now finds the target in its
own scope before linking it:

```cmake
if(LMMS_TEST_NAME STREQUAL "SfzFilePoolTest")
    # ... comment ...
    if(NOT TARGET Threads::Threads)
        find_package(Threads REQUIRED)
    endif()
    target_include_directories(...)
```

The guard makes the call a no-op on every platform where an ancestor scope already
provides the target (Linux, macOS, the other POSIX platforms), so behaviour there is
unchanged; on Windows/MinGW it creates the target and the generate step succeeds. The
sibling convention (`plugins/SfzPlayer/CMakeLists.txt:8`) is `FIND_PACKAGE(Threads
REQUIRED)`, which is what the new call uses.

Sibling check: the other test targets that link `sfzplayer_sfizz` (`SfzPlayerTest`,
`SfzPlayerRealtimeTest`, `SfzEffectBusViewTest`, `SfzSimdHelperAlignmentTest`,
`SfzEQPoolExhaustionTest`, `SfzFlexEnvelopeZeroStageTest`, `SfzFilterEqDescriptionTest`,
`SfzSimdGainAVXTest`) do **not** name `Threads::Threads` themselves. `sfzplayer_sfizz` is
a `STATIC` library (`plugins/SfzPlayer/CMakeLists.txt:144`) that links it `PRIVATE`, so
the dependency reaches consumers as `$<LINK_ONLY:Threads::Threads>`. That transitive form
resolves at generate time from the global target graph and does not need the target in
`tests/`'s directory scope — verified empirically (a scratch project whose static library
privately links a sibling-scope imported target with real content links it into the
consumer, exit 0). The CI log reporting only `SfzFilePoolTest` matches this. No sibling
change is needed.

## Regression test

`tests/cmake-threads-scope.sh` (new), registered as the `CMakeThreadsScope` ctest at
`tests/CMakeLists.txt:3011`.

The script extracts the `SfzFilePoolTest` block **from the product file on every run**
(balanced over nested `if`/`endif` pairs), writes a throwaway fixture in `$TMPDIR` (on
disk, deleted on exit) that mirrors the product's scope shape — a root whose
`find_package(Threads)` is gated exactly like `CMakeLists.txt:768`, and a `tests/`
subdirectory that runs the extracted block — and configures it twice with the real
`cmake`:

1. **POSIX shape** (`LMMS_BUILD_LINUX=ON`, control): must configure. Passes before and
   after the fix; a failure here is reported as exit 2 (broken harness, not a regression).
2. **Windows shape** (every `LMMS_BUILD_*` flag `OFF`, the `LMMS_BUILD_WIN` condition):
   must configure. Before the fix it fails at generate time with the exact CI error;
   after the fix it configures.

Deterministic: no wall-clock timing, sleeps or races; the only seam is the extracted
CMake block, and the assertion is the configure exit status. The script calls
`/usr/bin/cmake` directly when present because this lane's `cmake` shim refuses nested
configures (configuration is the parent's job); the fixture is outside the worktree and
never touches the lane build tree.

## Evidence

| # | What | Exact command (cwd) | Exit | Result |
|---|------|---------------------|-----:|--------|
| 1 | RED before fix | `bash tests/cmake-threads-scope.sh > .../logs/test-1.log 2>&1` (worktree root) | 1 | Windows-shaped configure failed with `Target "SfzFilePoolTest" links to: Threads::Threads but the target was not found.` — the CI error verbatim |
| 2 | GREEN after fix | `bash tests/cmake-threads-scope.sh > .../logs/test-2.log 2>&1` (worktree root) | 0 | Both shapes configure |
| 3 | Product patch | `git diff -- tests/CMakeLists.txt > .../logs/fix.patch` (worktree root) | 0 | 35-line patch, product file only |
| 4 | Reverse patch | `git apply -R .../logs/fix.patch` (worktree root) | 0 | `find_package(Threads REQUIRED)` and the `CMakeThreadsScope` registration gone |
| 5 | RED via ctest, fix reversed | `ctest -R CMakeThreadsScope --output-on-failure > .../logs/test-4-red.log 2>&1` (`build/tests`) | 8 | `0% tests passed, 1 tests failed out of 1`; the same `Threads::Threads` generate error |
| 6 | Restore patch | `git apply .../logs/fix.patch` (worktree root) | 0 | Fix and registration back |
| 7 | Reconfigure with fix | `cmake --build build --target cmake_check_build_system > .../logs/build-2.log 2>&1` (worktree root) | 0 | `Configuring done (2.2s)`, `Generating done (47.8s)` |
| 8 | GREEN via ctest, fix restored | `ctest -R CMakeThreadsScope --output-on-failure > .../logs/test-5-green.log 2>&1` (`build/tests`) | 0 | `1/1` passed |
| 9 | Build the affected target | `cmake --build build --target SfzFilePoolTest > .../logs/build-3-sfzpool.log 2>&1` (worktree root) | 0 | `Built target SfzFilePoolTest`, 0 errors |
| 10 | Family build | `cmake --build build --target SfzPlayerTest SfzPlayerRealtimeTest SfzEffectBusViewTest SfzSimdHelperAlignmentTest SfzEQPoolExhaustionTest SfzFlexEnvelopeZeroStageTest SfzFilterEqDescriptionTest SfzSimdGainAVXTest SfizzLeakDetectorTest > .../logs/build-4-sfzfamily.log 2>&1` | 0 | All 9 targets built, 0 errors |
| 11 | Affected family | `ctest -R 'Sfz\|CMakeThreadsScope' --output-on-failure > .../logs/test-7-family.log 2>&1` (`build/tests`) | 0 | **11/11 passed** (SfzPlayerTest, SfzPlayerRealtimeTest, SfzFilePoolTest, SfzEffectBusViewTest, SfzSimdHelperAlignmentTest, SfzEQPoolExhaustionTest, SfzFlexEnvelopeZeroStageTest, SfzFilterEqDescriptionTest, CMakeThreadsScope, SfzSimdGainAVXTest, SfzRenderThreadGate) |
| 12 | Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-1.log 2>&1` (worktree root) | 0 | `PASS` on all 8 rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length |

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-748342d46d7e9dd9.r1/logs`.

Note on the working tree: an early scratch command in this session wrote probe files into
the worktree by mistake. The three affected tracked files (`CMakeLists.txt`,
`plugins/CMakeLists.txt`, `tests/CMakeLists.txt`) were restored from `HEAD` with
`git show HEAD:<path> > <path>` and verified byte-identical (`git diff` empty); the stray
untracked probe files were removed. The final tree contains only the intended changes.

## Files committed

- `tests/CMakeLists.txt` — the fix (`SfzFilePoolTest` block finds `Threads::Threads` in its
  own scope) and the `CMakeThreadsScope` ctest registration.
- `tests/cmake-threads-scope.sh` — new regression (new file; a `tests/*.sh` driver is not
  in the C/C++ scope the fork-sources gate scans, and no `tests/*.sh` is registered there).
- `tests/upstream-modifications.txt` — declares the `tests/CMakeLists.txt` divergence with
  its reason (repo convention; the gate also classifies `tests/*` as allowed).
- `docs/qwen-fixes-2026-10/cluster-ci-748342d46d7e9dd9.md` — this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it
  (exit codes).** Reverted: `git apply -R logs/fix.patch` (exit 0) then
  `ctest -R CMakeThreadsScope` → exit **8**, `0% tests passed, 1 tests failed out of 1`,
  with `Target "SfzFilePoolTest" links to: Threads::Threads but the target was not found.`
  Restored: `git apply logs/fix.patch` (exit 0), reconfigure (exit 0), then
  `ctest -R CMakeThreadsScope` → exit **0**, `1/1` passed. Direct script runs agree:
  exit 1 before the fix (`logs/test-1.log`), exit 0 after (`logs/test-2.log`).
- **The test is deterministic.** No wall-clock timing, sleeps or races. The seam is the
  `SfzFilePoolTest` CMake block extracted verbatim from `tests/CMakeLists.txt` on every
  run; the assertion is the nested `cmake` configure exit status for the Windows-shaped
  scope, with a POSIX-shaped configure as a harness control (exit 2 if the control fails).
- **The fix is at the cause and complete.** The cause is the unguarded `Threads::Threads`
  reference in `tests/CMakeLists.txt`. Every other reference to that target in the repo is
  preceded by a `find_package(Threads)` in the same directory (`plugins/SfzPlayer:8,188`,
  `plugins/ZynAddSubFx:7,136`, `plugins/LadspaEffect/calf/veal`, abseil); `tests/` was the
  only consumer relying on an ancestor. The sibling test targets that link
  `sfzplayer_sfizz` were checked: they do not name the target, and the transitive
  `$<LINK_ONLY:Threads::Threads>` from the static library resolves at generate time from
  the global target graph (verified with a scratch project, exit 0), so they need no
  change. The full build-tree reconfigure with the fix succeeded, so every test target's
  CMake code still processes.
- **No collateral damage.** The new `find_package` is inside `if(NOT TARGET
  Threads::Threads)`, so on Linux/macOS/POSIX — where the root already creates the target
  — nothing is executed and nothing changes. On Windows the target is created, which is
  what the failing job needed. No product/source file changed, so no project, saved data,
  undo step or command behaviour is affected; no new control command was added. The
  affected family passes 11/11 and the gates pass 8/8.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The failure is reproduced
  deterministically from the product file and disappears with the two-line guarded
  `find_package`.
- **Sibling commands.** Not applicable to this fix: it changes no command file.
  `git show HEAD` touches only `tests/CMakeLists.txt`, `tests/cmake-threads-scope.sh`,
  `tests/upstream-modifications.txt` and this report, and `git show HEAD | grep -c
  'ControlCommands\|ControlReversibilityTable'` is 0. The `app.*` / `arrangement.*`
  siblings the check lists come from the train's own commits in the branch diff against
  the fix base (`af010bc3c`), not from this cluster; this fix adds no control command, so
  the `ControlCommandsSnapshot` canary is untouched.
