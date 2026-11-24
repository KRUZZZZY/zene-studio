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

## Evidence on this branch (file:line)

- `include/RemotePluginBase.h:48-68`: in the `!(LMMS_HAVE_SYS_IPC_H && LMMS_HAVE_SEMAPHORE_H)` branch (the
  shm-FIFO branch that macOS selects), lines 56-63 include `<unistd.h>` under `LMMS_HAVE_UNISTD_H`. The
  comment there names cluster ci-4e020460347493c3.
- `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp:21-27`: the probe deliberately has no `<unistd.h>`,
  and the `#ifdef __unix__` workaround is gone. It includes only `RemotePluginBase.h`.
- `tests/CMakeLists.txt:3378-3391`: target `RemotePluginShmFifoNoSysIpcAppleMacroProbe` compiles that probe
  with `-U__unix__` (`/U__unix__` on MSVC), against the shadow `lmmsconfig.h` that has no sys/ipc. This
  copies Apple's macro state on every host. `RemotePluginShmFifoTest` depends on it
  (`add_dependencies`, :3391), so a regression breaks the build of that test on Linux too.
- The fix came in with commit `3cc4c0a075` (`git log -S'cluster ci-4e020460347493c3' --
  include/RemotePluginBase.h`), and that commit is an ancestor of HEAD.

## Commands and EXIT codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-4e020460347493c3-port.r2/logs/`.

| Log | Command | Result |
| --- | --- | --- |
| proof-1 | `git log --format='%h %s' -S'cluster ci-4e020460347493c3' -- include/RemotePluginBase.h` | EXIT=0, `3cc4c0a075` |
| - | `git merge-base --is-ancestor 3cc4c0a075 HEAD` | EXIT=0 |
| build-1 | `cmake --build build --target zene RemotePluginShmFifoTest -j4` (also builds the AppleMacroProbe dependency) | EXIT=0 |
| test-1 | `ctest -R 'RemotePlugin' --output-on-failure` (build/tests) | EXIT=8: 2/4 passed; `RemotePluginAudioPortsTest` and `RemotePluginClientE2ETest` were Not Run because their binaries had not been built yet |
| build-2 | `cmake --build build --target RemotePluginAudioPortsTest RemotePluginClientE2ETest RemotePluginShmFifoNoSysIpcAppleMacroProbe -j4` | EXIT=0 |
| test-2 | `ctest -R 'RemotePlugin' --output-on-failure` (build/tests) | EXIT=0, 4/4 passed (AudioPorts, ClientE2E, ShmFifoTest, ShmFifoQtUsageCheck) |
| probe-2 | the probe's compile_commands.json command (probe-cmd.txt) with `-U__unix__` added, current header | EXIT=0, no diagnostics |
| probe-3 | same command with `-I<logs>/oldhdr` searched first, holding `git show 3cc4c0a075^:include/RemotePluginBase.h` (negative control) | EXIT=1: `oldhdr/RemotePluginBase.h:145:44: error: 'getpid' was not declared in this scope`, `oldhdr/RemotePluginBase.h:451:25: error: 'usleep' was not declared in this scope` |
| gates-1 | `python3 .../qwen/feature_check.py <worktree>` | EXIT=0, 8/8 PASS |

The probe-2/probe-3 pair shows that the in-tree probe still catches the CI defect: the pre-fix header
reproduces the CI's two errors under Apple's `__unix__` state, and the header on this branch compiles
cleanly.

## Self-check

- **Verdict evidence.** The header fix is at `include/RemotePluginBase.h:56-63` and its commit is an
  ancestor of HEAD (EXIT=0). The guard target is at `tests/CMakeLists.txt:3378-3391`.
- **Regression test.** No new test was added because nothing changed. The existing regression is the
  compile of `RemotePluginShmFifoNoSysIpcAppleMacroProbe` / `RemotePluginShmFifoTest`. It is green on this
  branch (build-1/2 EXIT=0, test-2 EXIT=0), and against the pre-fix header it fails with the CI's own
  errors (probe-3 EXIT=1). It is deterministic: a compile-time contract with no timing.
- **No product change.** This commit adds only this report. No source, test, gate or baseline was
  edited, so persistence, realtime, undo and commands are untouched, and no registration is needed.
- **Fix at the cause, siblings, collateral damage.** These are covered by the original fix's report
  (`cluster-ci-4e020460347493c3.md`, Self-check). `RemotePluginBase.h` is the only file that defines
  `SYNC_WITH_SHM_FIFO`; the socket branch and the mingw `<process.h>` path are unchanged. This port
  changes nothing, so there is nothing new to check.
- **Sibling commands (submit_check ASK).** This job changes no command file. The listed `app.*` /
  `arrangement.*` commands come from earlier branch history that `submit_check` diffs against its base,
  not from this commit (`git show --stat HEAD` touches only this report). They are not affected.
- **Platform.** This host is Linux. macOS itself was not run here. Apple's predefine difference
  (`__unix__` absent) is reproduced with `-U__unix__`, which is how the in-tree guard target does it.
- **Unverified.** I did not see a green hosted macos-arm64 run of this branch. That happens when the
  train runs CI.

Delegation: Haiku (run 1) built `zene` and `RemotePluginShmFifoTest`, ran the RemotePlugin ctest family,
and ran the Darwin-predefine probe compiles (green, plus the pre-fix-header negative control). Haiku
(run 2) built the two Not Run test targets, reran the family (4/4) and ran `feature_check.py`. Sonnet and
DeepSeek were not used: there was no code or test to write, because the fix was already present. I read
every log line cited above myself.

## Other findings

- `feature_check.py`/the brief point at a proof schema at `schemas/proof.json` in the config directory.
  It does not exist under `~/.config/kads/zene/` on this box, so the manifest uses the field names
  documented in the brief.
