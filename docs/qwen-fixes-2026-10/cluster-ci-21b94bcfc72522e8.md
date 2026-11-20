FIXED

# Cluster ci-21b94bcfc72522e8 - mingw64: `countCriticals` defined but not used

## Verdict and classification

REAL, and it is a test-source build-portability defect, not a product defect. The hosted job failed while
compiling `tests/src/core/RemotePluginAudioPortsTest.cpp`:

```
/home/runner/work/zene-studio/zene-studio/tests/src/core/RemotePluginAudioPortsTest.cpp:75:6:
error: 'void {anonymous}::countCriticals(QtMsgType, const QMessageLogContext&, const QString&)'
defined but not used [-Werror=unused-function]
```

The finding is from `run-43786984969-job-133036727594.log`, pinned train 050/m0-train-35 at
`fa4a5f2e785c73e32216931e33cdbe140c06647d`.

## Cause (file:line)

- `include/RemotePluginBase.h:48-51`: when the host has neither `<sys/ipc.h>` nor `<semaphore.h>` (the mingw64
  job), the header defines `SYNC_WITH_SHM_FIFO`, selecting the shared-memory FIFO transport. On Linux both
  headers exist, so the macro is absent and the socket transport is selected.
- `tests/src/core/RemotePluginAudioPortsTest.cpp:73-82` (fixed): `g_criticalsSeen`/`countCriticals` were
  declared outside any guard. Their only users are `channelCountMessageMissingAnArgumentIsRefused()` and
  `refusesMessageWithoutArguments()` (`:1288`, `:1344`), and both live inside the socket-transport-only block
  that ends at `:1351` (`#ifndef SYNC_WITH_SHM_FIFO`). On mingw64 that whole block is compiled out, leaving the
  helper unused; `-Wall -Werror` turns that into the hosted failure. On Linux the helper is used, so this tree
  could not see it by building the default configuration.

The other anonymous-namespace helpers already followed the same rule: `TestRemotePluginBase` (`:112`) and
`writeTestPeer` (`:161-288`) are inside `#ifndef SYNC_WITH_SHM_FIFO`, and the unguarded `countWarnings` is used
by an unguarded test (`:770-800`). No other helper in this translation unit is conditionally used.

## Fix (smallest, behaviour-preserving)

`tests/src/core/RemotePluginAudioPortsTest.cpp:73-82`: put `g_criticalsSeen` and `countCriticals` inside the same
`#ifndef SYNC_WITH_SHM_FIFO` block as their users. The Linux/socket build compiles exactly the same code and
tests as before; the shared-memory build no longer declares a helper it cannot use. No assertion, test case,
product path, saved data or audio-thread code is touched.

## Regression (fails before the fix, passes after)

`tests/CMakeLists.txt:3215-3230` adds `RemotePluginAudioPortsShmCompileCheck`, an OBJECT library that compiles
the real `RemotePluginAudioPortsTest.cpp` with `LMMS_STATIC_DEFINE LMMS_TESTING SYNC_WITH_SHM_FIFO` on every
host, using the build's own `-Wall -Werror`. It is part of the default test build (nothing links it), so the
Linux CI build now compiles the exact configuration that the mingw64 job takes. Before the fix it fails with the
CI's own diagnostic; after the fix it builds. This is the same OBJECT-compile-check pattern already used by
`RemotePluginShmFifoCompileCheck` beside it.

Test matrix covered:

| Path | Check |
| --- | --- |
| Normal Linux/socket configuration | `RemotePluginAudioPortsTest` runtime test, 4/4 `RemotePlugin` family tests pass |
| Windows/SHM configuration | `RemotePluginAudioPortsShmCompileCheck` compiles the real TU with `SYNC_WITH_SHM_FIFO` |
| Failure path (regression proof) | Reverting only the guard makes that target fail at `:75` with `-Werror=unused-function` |
| Reconfiguration/other hosts | The macro is selected by the existing header contract; no new host-dependent code |
| Audio thread / undo / commands | No product file changed; none of these paths is touched |
| Qt 5.15 / MSVC portability | Guard is plain preprocessor, no Qt API; `LMMS_STATIC_DEFINE` is the established compile-check branch |

## Evidence (exact commands, exit codes)

The prescribed log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-21b94bcfc72522e8.r1/logs` rejected every write with
`Permission denied` under this session's file policy (the parent directories are owned and writable by
`kruzzzzy`, but the sandbox denies them; `/tmp` and the worktree are writable). The identical numbered logs are
therefore in `build/qwen-logs/ci-21b94bcfc72522e8.r1/` (build directory, not committed).

| Step | Command | Result |
| --- | --- | --- |
| Reproduce on this commit (manual compiler, real TU, SHM define) | `python3` wrapper over the `compile_commands.json` entry plus `-DSYNC_WITH_SHM_FIFO` (`build/qwen-logs/.../03-red-manual-compile-shm.log`) | EXIT=1; same `:75 ... defined but not used [-Werror=unused-function]` (plus stale-moc noise, because that one-off invocation used the socket-configuration moc) |
| RED, regression target before fix | `cmake --build build --target RemotePluginAudioPortsShmCompileCheck` (`04-red-compile-check-build.log`) | EXIT=2; `RemotePluginAudioPortsTest.cpp:75:6: error: ... defined but not used [-Werror=unused-function]` |
| GREEN, regression target after fix | `cmake --build build --target RemotePluginAudioPortsShmCompileCheck` (`05-green-compile-check-build.log`) | EXIT=0; `Built target RemotePluginAudioPortsShmCompileCheck` |
| GREEN, runtime target | `cmake --build build --target RemotePluginAudioPortsTest` (`06-green-runtime-test-build.log`) | EXIT=0 |
| GREEN, runtime test | `cd build/tests && ctest -R '^RemotePluginAudioPortsTest$' --output-on-failure` (`07-green-ctest.log`) | EXIT=0; 1/1 passed, 31.39 s |
| 4b RED: reverse only the guard | `git diff -- tests/src/core/RemotePluginAudioPortsTest.cpp > fix.patch; git apply -R fix.patch` (`08-red-4b-reverted-build.log`) | diff EXIT=0, revert EXIT=0; build EXIT=2 at `:75`, `-Werror=unused-function` |
| 4b GREEN: restore the guard | `git apply fix.patch; cmake --build build --target RemotePluginAudioPortsShmCompileCheck` (`09-green-4b-reapplied-build.log`) | apply EXIT=0; build EXIT=0 |
| Family binaries | `cmake --build build --target RemotePluginClientE2ETest RemotePluginShmFifoTest` (`12-family-build.log`) | EXIT=0 |
| Family tests | `cd build/tests && ctest -R 'RemotePlugin' --output-on-failure` (`13-family-ctest-after-build.log`) | EXIT=0; 4/4 passed |
| Product target still builds | `cmake --build build --target zene` (`15-zene-build.log`) | EXIT=0; `Built target zene` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`14-feature-check.log`, again after the report/registration in `16-feature-check-final.log`) | EXIT=0; 8/8 PASS both times |
| Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-21b94bcfc72522e8` (`18-submit-check.log`) | EXIT=0; `RESULT: PASS - submit` |

## Self-check

- **Regression fails with the fix reverted, passes with it.** 4b above: revert EXIT=0, build EXIT=2 with the
  CI's `:75 ... unused-function`; restore EXIT=0, build EXIT=0. The revert patch contains only the test-file
  guard (this bug's fix is itself test-only; there is no product file to revert).
- **Deterministic.** Both checks are compiler facts: the real translation unit is compiled with the Windows
  branch's `SYNC_WITH_SHM_FIFO` and the build's own `-Werror`. No wall clock, sleep, thread or network.
- **At the cause and complete.** The helper's two callers are both inside `#ifndef SYNC_WITH_SHM_FIFO`
  (`:1288`, `:1344`, block ends `:1351`); the header's selection rule is `include/RemotePluginBase.h:48-51`.
  The guarded helper now matches the already-guarded `TestRemotePluginBase`/`writeTestPeer` siblings, and the
  unguarded `countWarnings` remains correct because its test is unguarded. The new target compiles the real TU,
  not a copied snippet, so any future socket-only helper in it is caught too.
- **No collateral damage.** No product source changed; `zene` still builds. The Linux test configuration
  compiles the same pre-fix code and its runtime test passes. No saved format, limit, command, undo step or
  audio-thread path is touched.
- **Old projects/data.** Not applicable: no serializer, loader, project file or runtime behaviour changed.
- **Realtime/undo/commands.** No product path changed, so nothing was added to the audio thread, undo stack or
  command registry.
- **Registration.** `tests/upstream-modifications.txt` records the two inherited files changed
  (`tests/src/core/RemotePluginAudioPortsTest.cpp`, `tests/CMakeLists.txt`) with the reason. The new report is
  registered in `tests/fork-sources.txt`. `tests/fork-sources.txt` ends with a newline.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED. The unguarded helper is present at
  this commit (`:73-82` before the fix) and the compile check reproduces the CI diagnostic (EXIT=2); the header's
  `SYNC_WITH_SHM_FIFO` selection for a host without `<sys/ipc.h>` is the intended contract
  (`include/RemotePluginBase.h:48-51`), not a stray build flag.
