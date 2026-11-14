FIXED

# Cluster ci-f0fa6219c5d4b443 - macOS-x86_64 build, no-SysIpc probe

## Verdict and classification

FIXED. The hosted macOS-x86_64 `build` job failed in the test-only object library
`RemotePluginShmFifoNoSysIpcProbe`: Apple clang's Darwin target does not predefine
`__unix__`, so the probe's `#ifdef __unix__` include of `<unistd.h>` was skipped and
`RemotePluginBase.h`'s no-`<sys/ipc.h>` branch left `getpid()`/`usleep()` undeclared.

Classification: the failing translation unit is test-only, but the missing declaration
is a product-header completeness defect. The branch that calls `getpid()` and `usleep()`
must carry their declarations itself, exactly as the `sys/ipc.h` branch already does.
This is deterministic and source-level, not infrastructure or flakiness.

The observed hosted lane exited 2 (CI excerpt in the task packet). The hosted lane is not
runnable from this Linux worktree; the lane's predefined-macro state is reproduced
deterministically below with Apple clang's Darwin target and with `-U__unix__` on the
rebuilt probe.

## Evidence and reproduction

Exact commit: `0c3b9e3c23434f269526f189c02e88698ce63ec5` (HEAD of this worktree).

The failing translation unit in the CI excerpt includes `RemotePluginBase.h` at its
line 24 and reported:

- `include/RemotePluginBase.h:145:30: error: use of undeclared identifier 'getpid'`
- `include/RemotePluginBase.h:146:33: error: use of undeclared identifier 'getpid'`
- `include/RemotePluginBase.h:451:4: error: use of undeclared identifier 'usleep'`

Current line numbers after the fix are `include/RemotePluginBase.h:151-152` (`getpid`)
and `:457` (`usleep`); only the inserted comment/include block moved them.

Apple clang macro state, measured here with the local clang driver's Darwin target:

```
$ clang++ --target=x86_64-apple-darwin -std=c++20 -dM -E -x c++ /dev/null | \
      grep -E '__APPLE__|__MACH__|__unix__'
#define __APPLE__ 1
#define __MACH__ 1
```

No `__unix__` is emitted. LLVM's `getDarwinDefines()` (clang/lib/Basic/Targets/
OSTargets.cpp) defines `__APPLE__`, `__MACH__` and the Darwin version macros; it does
not define `__unix__`. Log: `02-apple-macro-definition.log`.

Reproduction on the exact commit with the probe's real compile flags from
`build/compile_commands.json`, plus the lane's macro state (`-U__unix__`),
`-fsyntax-only` instead of writing the object:

```
$ cd build/tests
$ <probe compile command with -U__unix__ -fsyntax-only> \
      > LOGS/01-repro-current-apple-macro.log 2>&1; echo EXIT=$?
EXIT=1
```

The log contains the same two undeclared identifiers:

```
include/RemotePluginBase.h:145:44: error: 'getpid' was not declared in this scope
include/RemotePluginBase.h:451:25: error: 'usleep' was not declared in this scope
```

Before the fix, `include/RemotePluginBase.h:48-60` included `<process.h>` only when
`LMMS_HAVE_PROCESS_H` was set and never included `<unistd.h>` in the
no-`<sys/ipc.h>` branch. The probe supplied `<unistd.h>` only under `#ifdef __unix__`
(`tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:20-22` before the change), which is
false on Apple clang.

## Fix

1. `include/RemotePluginBase.h:53-62`: the no-`<sys/ipc.h>` branch now includes
   `<unistd.h>` when `LMMS_HAVE_UNISTD_H`, beside the existing `<process.h>` include.
   The `sys/ipc.h` branch already did this; the two branches are now consistent. The
   Windows path (`LMMS_HAVE_PROCESS_H`, no `LMMS_HAVE_UNISTD_H` on MSVC) is unchanged.
2. `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:21-26`: the probe no longer
   compensates with the non-portable `__unix__` guard. It deliberately includes nothing
   before `RemotePluginBase.h`, so the test asserts that the header is self-contained in
   the branch it forces. The runtime assertion (`SYNC_WITH_SHM_FIFO == 1`) is unchanged.
3. `tests/CMakeLists.txt:3062-3080`: new OBJECT target
   `RemotePluginShmFifoNoSysIpcAppleMacroProbe` compiles the same probe with
   `-U__unix__` (the Darwin macro state) and is a dependency of
   `RemotePluginShmFifoTest`. This is the deterministic compile-time regression; it is
   the same build-time pattern as the existing `RemotePluginShmFifoCompileCheck`.

The `SYNC_WITH_SHM_FIFO` redefinition guard at `include/RemotePluginBase.h:49-51` and
the mingw `ci-v041-5` fix are untouched.

## RED/GREEN proof (step 4b)

The product diff for the fix is only `include/RemotePluginBase.h`; the probe and test
target remain in place while the header fix is removed and restored.

```
$ git diff -- include/RemotePluginBase.h > LOGS/fix.patch   # EXIT=0
$ git apply -R LOGS/fix.patch                               # EXIT=0
$ cmake --build build --target RemotePluginShmFifoNoSysIpcAppleMacroProbe \
      > LOGS/06-regression-red-without-header-fix.log 2>&1; echo EXIT=$?
EXIT=2
```

RED log (exact CI errors):

```
include/RemotePluginBase.h:145:44: error: 'getpid' was not declared in this scope
include/RemotePluginBase.h:451:25: error: 'usleep' was not declared in this scope
make[3]: *** [.../RemotePluginShmFifoNoSysIpcProbe.cpp.o] Error 1
```

```
$ git apply LOGS/fix.patch                                  # EXIT=0
$ cmake --build build --target RemotePluginShmFifoNoSysIpcAppleMacroProbe \
      > LOGS/07-regression-green-with-header-fix.log 2>&1; echo EXIT=$?
EXIT=0
```

The target is rebuilt (not reused) in both directions: the header timestamp changes, and
the logs show `Building CXX object .../RemotePluginShmFifoNoSysIpcProbe.cpp.o`.

## Test matrix

| Path | Command | Result |
|---|---|---|
| Normal SysIpc build (ordinary path) | `cmake --build build --target zene RemoteZynAddSubFx` (`14-...log`) | EXIT=0; both compiled/linked with the header's `sys/ipc.h` branch |
| No-SysIpc probe on Linux (boundary: shadow `lmmsconfig.h`) | built as a dependency of `RemotePluginShmFifoTest` (`04-...log`) | EXIT=0 |
| Apple macro state (failure path: `__unix__` undefined) | `cmake --build build --target RemotePluginShmFifoNoSysIpcAppleMacroProbe` (`07-...log`) | EXIT=0 after fix; EXIT=2 before fix (`06-...log`) |
| Runtime FIFO family | `ctest -R RemotePlugin` from `build/tests` (`13-...log`) | 3/3 passed: `RemotePluginAudioPortsTest`, `RemotePluginClientE2ETest`, `RemotePluginShmFifoTest` |
| Windows/mingw `process.h` branch | not runnable on this Linux host; no mingw toolchain installed | Not run. The branch is source-compatible: `LMMS_HAVE_UNISTD_H` is guarded, mingw's `<unistd.h>` includes `<process.h>` and both guard `getpid` with `_CRT_GETPID_DEFINED`; the `SYNC_WITH_SHM_FIFO` guard is unchanged. Independent QA should run the hosted windows lane. |
| Invalid input | not applicable: compile-time include/guard change; no new parsing or input surface | N/A |

`RemotePluginShmFifoCompileCheck` also rebuilt cleanly (`10-...log`, EXIT=0).

## Acceptance / lane status

- The three undeclared-identifier errors no longer occur in the rebuilt probe target,
  including the Apple macro state. The local compile regression is green.
- The hosted macOS-x86_64 lane itself was not run from this Linux worktree (nothing may
  be pushed). The observed hosted exit was 2; the local emulation of its predefined-macro
  state exits 0 after the fix. The owner/train must re-run the hosted lane for the final
  lane exit code.
- No GUI path is involved; no GUI claim is made.

## Contracts

The change is compile-time only: include selection plus a test-only compile target. No
runtime code path changed. There is no new save/load, undo, compatibility or realtime
behavior; nothing was added to an audio-thread path, and no allocation, lock, wait or log
was introduced. The semKey derivation (`getpid()<<10`), the `usleep(5)` retry and the
`SYNC_WITH_SHM_FIFO` selection are byte-for-byte unchanged.

## Quality gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
(`15-feature-check.log`) exited 0:

```
PASS fork-sources
PASS all-sources-reproduce
PASS no-upstream-regression
PASS check-namespace
PASS no-tautology
PASS doc-figures
PASS complexity (no new item in your files)
PASS file-length (no new item in your files)
RESULT: PASS
```

`bash tests/run-all-gates.sh` (`16-run-all-gates.log`) exited 1. Gate 5 mutation passed
(81.5% >= 80%) after `RoutingGraphTest` was built; gates 3, 4, 6, 8, 9, 10 and 12 passed.
The failures are lane/train conditions, not this change:

- Gate 1: the runner invokes an unqualified full `cmake --build build`; the Bunny lane
  guard refuses full builds (exit 97). The affected tests were built and run directly
  instead (3/3 passed).
- Gate 7: the file-length ratchet is red on the train on purpose; no regression item is
  in a file this change touches (feature_check confirms).
- Gate 11: self-test 9 (a tracked over-cap file) is red in this lane; the tree's own
  evidence scan passes.
- Gates 13, 14, 15: their self-tests need `git config`/`git submodule`/scratch worktrees,
  which the Bunny lane guard refuses.
- Gate 2 was skipped without `--with-coverage`.

`bash tests/run-all-gates.sh --with-coverage` (`18-run-all-gates-with-coverage.log`)
exited 1; gate 2 fails at `cmake -B build-coverage` because the lane guard reserves
configure for the parent. No gate was weakened, skipped by this change, or re-anchored.

## Registration

- `tests/upstream-modifications.txt:5824-5825`: the inherited `include/RemotePluginBase.h`
  change and the `tests/CMakeLists.txt` compile-regression registration, both naming
  cluster `ci-f0fa6219c5d4b443`.
- `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp` was already registered in
  `tests/fork-sources.txt:3362`; no new source file was added.
- The new CMake target is file-registration covered by the existing source entry.

## Files committed

- `include/RemotePluginBase.h`
- `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-ci-f0fa6219c5d4b443.md`

## Self-check

### The regression test FAILS with your product change reverted and PASSES with it (exit codes)

Yes. With only `include/RemotePluginBase.h` reverted, rebuilding
`RemotePluginShmFifoNoSysIpcAppleMacroProbe` exited 2 with the exact `getpid`/`usleep`
undeclared errors (`06-regression-red-without-header-fix.log`). With the header fix
re-applied, the same rebuilt target exited 0 (`07-regression-green-with-header-fix.log`).
The commands and both exit codes are in "RED/GREEN proof" above.

### The test is deterministic: no wall-clock timing, sleeps or races

Yes. It is a compile-time include check: no timers, sleeps, threads or wall-clock values.
The only variable is the compiler's predefined-macro state, and the target pins the
relevant state explicitly with `-U__unix__` (the Darwin state measured above). The
runtime FIFO tests remain separate and deterministic (existing suite).

### The fix is at the cause and complete: every caller and sibling checked

The header's two branches were compared: both now include `<unistd.h>` under
`LMMS_HAVE_UNISTD_H`. The Windows sibling (`<process.h>`) is unchanged. Every direct
includer was rebuilt: `zene`/`lmmsobjs`, `RemoteZynAddSubFx`, `RemotePluginBase.cpp`
(`RemotePluginShmFifoCompileCheck` and `RemotePluginShmFifoTest`), the probe, and the
runtime family. The probe's old non-portable guard was removed rather than left as a
second, inconsistent workaround.

### No collateral damage: old projects/saved data, limits and other callers

No runtime code, serialization, save/load, undo, command or limit changed. The include
addition is guarded by the existing configure-detected `LMMS_HAVE_UNISTD_H`; hosts that
lack it keep their previous include set. The `SYNC_WITH_SHM_FIFO` guard and mingw
`ci-v041-5` behavior are untouched.

### If your verdict is ALREADY-FIXED or FALSE-POSITIVE: file:line or contract

Not applicable: the verdict is FIXED. The pre-fix lines are
`include/RemotePluginBase.h:48-60` (no unistd include in the no-SysIpc branch) and
`tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:20-22` (the `__unix__` guard).

### Sibling commands in changed command files

No command file, command definition, schema or command snapshot was changed by this fix.
The listed `app.*` and `arrangement.*` commands, and all other control commands, are
unaffected; there is no new command and no changed argument or reply contract. The branch
diff's `commands_snapshot.json` change is the train-27 snapshot commit already on HEAD,
not part of this fix.
