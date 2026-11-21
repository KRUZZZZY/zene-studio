# cluster new-p10-84 — POSIX client deadlines use the adjustable wall clock

Verdict: **FIXED**

## Finding

`src/core/ControlServer.cpp` computed every POSIX client idle deadline from
`QDateTime::currentMSecsSinceEpoch()` (the adjustable wall clock). A clock step
(NTP or a user change) moved every stored deadline with it: a forward step
dropped an active peer early, a backward step let an idle peer outlive its
timeout. The finding's historical line 218 is current `src/core/ControlServer.cpp:230`
(`client.deadlineMs = ...`), with the same clock used at `:324`
(`retireIdleClients`), `:359` (read-activity refresh) and `:499`
(write-activity refresh).

The contract is clear and the repo already fixes this exact class of bug the
same way: `src/common/SystemSemaphore.cpp:119-124` measures its wait on
`std::chrono::steady_clock` precisely so "a step of the wall clock cannot
stretch the wait".

## Fix

`src/core/ControlServer.cpp`
- added `#include <chrono>`, dropped the now-unused `#include <QDateTime>`;
- added a POSIX-only `monotonicNowMs()` helper (anonymous namespace) built on
  `std::chrono::steady_clock`;
- replaced all four `QDateTime::currentMSecsSinceEpoch()` deadline sites with
  `monotonicNowMs()`.

No header change, so no dependents needed a rebuild beyond the affected
targets.

## Regression test

`tests/src/core/ControlShutdownHookTest.cpp`
- `aClientDeadlineIsAnchoredToTheMonotonicClock()` accepts a real AF_UNIX
  connection, reads the stored `deadlineMs`, and asserts it is within the idle
  timeout of a `steady_clock` reading (and more than ~11 days from the wall
  clock). The two clock references are ~1.7e12 ms apart, so the assertion is
  deterministic. It reads the private `Client::deadlineMs` through the same
  `#define private public` seam used by `tests/src/core/CollabPolicyTest.cpp:51`.

## Evidence

| step | command | exit |
|---|---|---|
| RED (original code) | `cmake --build build --target ControlShutdownHookTest` then `ctest -R '^ControlShutdownHookTest$' --output-on-failure` | build 0 / test **8** |
| GREEN (fix) | same build + ctest | build 0 / test **0** |
| proof: reverse product patch | `git diff -- src/core/ControlServer.cpp > …/logs/fix.patch` then `git apply -R …/logs/fix.patch` | apply **0** |
| proof: RED without fix | rebuild `zene ControlShutdownHookTest` + `ctest -R '^ControlShutdownHookTest$'` | build 0 / test **8** |
| proof: re-apply fix | `git apply …/logs/fix.patch` | apply **0** |
| proof: GREEN with fix | rebuild + `ctest -R '^ControlShutdownHookTest$'` | build 0 / test **0** |

RED failure text (both RED runs):
`aClientDeadlineIsAnchoredToTheMonotonicClock() 'fromMonotonic <= ControlServer::ClientIdleTimeoutMs + 1000' returned FALSE. (the stored deadline is not anchored to the monotonic clock)`

Family: `ctest -N -R 'ControlShutdown|ControlSocket|ControlServer'` = 4 tests.
- `ControlShutdownHookTest` (#64): PASS after the fix.
- `ControlShutdown` (#560): PASS.
- `ControlSocketIntegration` (#508) and `ControlSocketPathSafety` (#571): FAIL
  **without** this change too (rebuilt `zene` with the patch reversed, exit 8),
  for unrelated reasons (`plugin.list is not broken down by format`;
  stderr refusal text mismatch). Pre-existing, not this cluster.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-84.r1/logs/`
(`build-*.log`, `test-*.log`, `fix.patch`, `full.patch`, `gate-*.log`).

## Gates

Both ratchet gates are red on this branch **before and after** this change,
for files/functions this cluster never touches; they are not regressions of
this fix (the new function `monotonicNowMs` is CCN 1, well under 10, and
`ControlShutdownHookTest.cpp` is not over the 500-code-line limit).

| gate | with change | with change reverted |
|---|---|---|
| `bash tests/complexity-gate.sh --check` | exit **1** | exit **1** |
| `bash tests/file-length-gate.sh --check` | exit **1** | exit **1** |

The `--check` regressions listed (e.g.
`src/core/TakeLane.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`,
`lmms::interchange::readdetail::parseNotes`) are all pre-existing branch state;
no baseline was re-anchored.

## Files committed

- `src/core/ControlServer.cpp` (fix)
- `tests/src/core/ControlShutdownHookTest.cpp` (regression)
- `docs/qwen-fixes-2026-10/cluster-new-p10-84.md` (this report)

`ControlServer.cpp` is a fork-NEW source already listed in
`tests/fork-sources.txt`; no manifest edit was needed.
