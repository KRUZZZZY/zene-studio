ALREADY-FIXED

# Cluster ci-4e020460347493c3-port - Port: CI regression: build / macos-arm64

ci-4e020460347493c3-port: ALREADY-FIXED

The finding is only a title. Its parent cluster, ci-4e020460347493c3, is described in
`docs/qwen-fixes-2026-10/cluster-ci-4e020460347493c3.md`. The macOS jobs (macos-arm64 run 43076107122)
stopped on:

```
include/RemotePluginBase.h:145:30: error: use of undeclared identifier 'getpid'
include/RemotePluginBase.h:451:4:  error: use of undeclared identifier 'usleep'
```

The cause was this: macOS has no `<sys/ipc.h>`, so `RemotePluginBase.h` takes its shm-FIFO branch. That
branch calls `getpid()` and `usleep()` but did not include `<unistd.h>`. The probe TU hid the gap behind
`#ifdef __unix__`, and Apple clang never defines that macro.

This port job asks whether the fix is on this branch (train 51). It is, and nothing needs to change.

Round 3 (after review): the header fix is already here, and this commit hardens the guard so the macOS
contract is a named, red/green test. The probe now states the contract with `static_assert`s, and the
`-U__unix__` compile is registered as a ctest.

## Evidence on this branch (file:line)

- `include/RemotePluginBase.h:48-68`: in the `!(LMMS_HAVE_SYS_IPC_H && LMMS_HAVE_SEMAPHORE_H)` branch (the
  shm-FIFO branch that macOS selects), lines 56-63 include `<unistd.h>` under `LMMS_HAVE_UNISTD_H`. The
  comment there names cluster ci-4e020460347493c3. The fix came in with commit `3cc4c0a075`, and that
  commit is an ancestor of HEAD.
- `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:28-39` (new in this commit): after including only
  `RemotePluginBase.h`, and under `#ifdef LMMS_HAVE_UNISTD_H`, it adds
  `static_assert(std::is_pointer_v<decltype(&::getpid)>)` and the same for `::usleep`. The probe states its
  contract that the header alone declares both. It no longer relies on the header's inline calls.
- `tests/CMakeLists.txt:3378-3394`: target `RemotePluginShmFifoNoSysIpcAppleMacroProbe` compiles the probe
  with `-U__unix__` (`/U__unix__` on MSVC), which is Apple clang's macro state, against the shadow
  `lmmsconfig.h` that has no sys/ipc. New in this commit (:3392-3394):
  `add_test(NAME RemotePluginShmFifoAppleMacroProbeCompile COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target RemotePluginShmFifoNoSysIpcAppleMacroProbe --config $<CONFIG>)`.
  The `-U__unix__` guard is now a named ctest.

## Commands and EXIT codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-4e020460347493c3-port.r3/logs/`, where L is
that folder. `probe_compile.py` (in L) takes the probe's compile_commands.json command for the AppleMacroProbe
object, asserts that it contains `-U__unix__`, and redirects `-o`. It can put an `-I` first on the search path.
`L/oldhdr/RemotePluginBase.h` is `git show 3cc4c0a075^:include/RemotePluginBase.h`.

| Log | Command | Result |
| --- | --- | --- |
| 01-build.log | `cmake --build build --target RemotePluginShmFifoNoSysIpcAppleMacroProbe RemotePluginShmFifoNoSysIpcProbe RemotePluginShmFifoTest -j4` | EXIT=0 |
| 02-red-old-header.log | `python3 $L/probe_compile.py $PWD/build $L/oldhdr $L/red-probe.o` (RED: pre-fix header, `-U__unix__`) | EXIT=1: `oldhdr/RemotePluginBase.h:145:44: error: 'getpid' was not declared in this scope`, `:451:25: 'usleep' was not declared`, `RemotePluginShmFifoNoSysIpcProbe.cpp:37:45: error: '::getpid' has not been declared`, `:38:45: '::usleep' has not been declared` |
| 03-green-current-header.log | `python3 $L/probe_compile.py $PWD/build - $L/green-probe.o` (GREEN: current header) | EXIT=0 |
| 04-ctest-apple-probe.log | `cd build/tests && ctest -R RemotePluginShmFifoAppleMacroProbeCompile --output-on-failure` | EXIT=0, 100% tests passed out of 1 |
| 05-ctest-shmfifo-family.log | `cd build/tests && ctest -R RemotePluginShmFifo --output-on-failure` | EXIT=0, 100% tests passed out of 3 (ShmFifoTest, AppleMacroProbeCompile, ShmFifoQtUsageCheck) |
| 06-feature-check.log | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | EXIT=0, 8/8 PASS, RESULT: PASS |

The RED/GREEN pair shows that the probe catches the CI defect. The pre-fix header reproduces the CI's two
errors under Apple's `__unix__` state, and the new static_asserts fire too. The header on this branch
compiles cleanly.

## Self-check

- **Verdict evidence.** The header fix is at `include/RemotePluginBase.h:56-63` (commit `3cc4c0a075`, an
  ancestor of HEAD). The guard is the probe at `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:28-39`
  and the ctest at `tests/CMakeLists.txt:3392-3394`.
- **Regression test.** The test is ctest `RemotePluginShmFifoAppleMacroProbeCompile` (probe file
  `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp`). RED is EXIT=1 with the pre-fix header and GREEN
  is EXIT=0 with the current header (logs 02/03), and the ctest is EXIT=0 (log 04). It is deterministic: a
  compile-time contract with no timing. The product fix predates this branch, so `git apply -R` of a
  product patch does not apply. RED instead swaps in the pre-fix header, searched first.
- **No product change.** Only test files changed (`tests/CMakeLists.txt`, the probe) plus this report.
  Persistence, realtime, undo and commands are untouched. feature_check's registration gates pass (log 06).
- **Fix at the cause, siblings, collateral damage.** `RemotePluginBase.h` is the only file that defines
  `SYNC_WITH_SHM_FIFO`. The socket branch and the mingw `<process.h>` path are unchanged. The static_asserts
  are guarded by `LMMS_HAVE_UNISTD_H`, so MSVC (no unistd) is unaffected. On mingw, `<unistd.h>` declares
  both functions.
- **Sibling commands (submit_check ASK).** This job changes no command file. The listed commands come from
  earlier branch history that `submit_check` diffs against its base, not from this commit.
- **Platform.** This host is Linux. macOS itself was not run here. Apple's predefine difference
  (`__unix__` absent) is reproduced with `-U__unix__`.
- **Unverified.** I did not see a green hosted macos-arm64 run of this branch. That happens when the
  train runs CI.

Delegation: Haiku ran the build, the RED/GREEN probe compiles and both ctest runs (logs 01-05). I made the
two small test edits myself (15 lines, smaller than a request) and ran feature_check. Sonnet and DeepSeek
were not used: there was no multi-file fix to implement. I checked the cited log lines myself.

## Other findings

- The brief points at a proof schema at `schemas/proof.json` in the config directory. The manifest uses
  the field names documented in the brief.
