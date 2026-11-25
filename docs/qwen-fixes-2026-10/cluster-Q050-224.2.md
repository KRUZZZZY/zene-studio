IMPLEMENTED

# Q050-224.2 - bounded raw crash frames (stage 2/4 of Q050-224), round 5

Branch `feat/050-Q050-224.2-oc`, worktree `kads-Q050-224.2`. Every log named below is in `~/.config/kads/zene/state/runs/Q050-224.2.r5/logs/` and was
written in this session. That directory also holds `proof.json`. Builds ran from the worktree root and
tests from `build/tests`, with `QT_QPA_PLATFORM=offscreen` and output redirected to a log, never piped.
The product code tested is commit `26fa58901b`.

## Round 5: the review point (complexity ratchet)

The round-4 report said the complexity gate emitted no `REGRESSION:` line. That was wrong: the gate exited 2
because `lizard` was not installed, so it measured nothing. This round installed lizard in a venv outside
the worktree (`python3 -m venv /tmp/liz-224 && /tmp/liz-224/bin/pip install lizard`, lizard 1.26.1,
`lizard-install.log` exit 0), put it on `PATH` and measured the gate.

| When | Command | Log | Exit | Result |
|---|---|---|---|---|
| Before, HEAD `524955038e` | `bash tests/complexity-gate.sh --check` | `gate-complexity-before.log` | **1** | `REGRESSION: new function over target: lmms::crashreporter::captureFrames@335-403@src/core/CrashReporterFormat.cpp (CCN 25)` (its only REGRESSION line) |
| After, `26fa58901b` | `bash tests/complexity-gate.sh --check` | `gate-complexity-after.log` | **0** | `PASS (check mode: no regressions; baseline not written)` |
| After | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 0 | `PASS (check mode: no regressions; baseline not written)` |
| After | `feature_check.py` (all eight gates) | `feature-check-1.log` | **0** | fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length: all PASS |

**The fix (`26fa58901b`, only `src/core/CrashReporterFormat.cpp`).** I split `captureFrames()` into
helpers in the same anonymous namespace. No baseline, ratchet or test was touched. Each helper uses only
raw loads or arithmetic: no allocation, no lock, no I/O. The checks keep their order, so every chain stops
at the same frame as before. Per-function lizard results (`lizard-before.log` and `lizard-after.log`):

| Function | NLOC | CCN |
|---|---|---|
| `captureFrames` (before) | 51 | 25 |
| `captureFrames` (after: out/capacity guard, PC first, early return at capacity, delegation when the context is null, `contextStackPointers`, `stackCeilingForHandler`, `walkBoundsAdmit`, walk) | 20 | 9 |
| `walkBoundsAdmit` (no ceiling / too narrow / wider than `kMaxStackSpan` / SP below the recorded floor / first FP outside `[low, high-2w]`, in the original order) | 8 | 8 |
| `frameReadable` (16-byte aligned and inside `[low, high-2w]`) | 5 | 3 |
| `savedFrameAdvances` (strictly increasing, `<= high-2w`, aligned) | 5 | 3 |
| `walkFramePointerChain` (the budgeted loop) | 14 | 5 |
| `appendCallerOutsideSignal` (the branch for a null context) | 5 | 3 |

One deliberate difference from the suggested split: `__builtin_return_address(0)` is still read inside
`captureFrames()` and passed to `appendCallerOutsideSignal()` as a value. Moving it into the helper would
make it return the helper's caller, which is `captureFrames()` itself, and that would change the output.

## Acceptance (all run in this session against `26fa58901b`)

| # | Criterion | Test | Log | Exit |
|---|---|---|---|---|
| 1 | A real fault report contains thread id, fault address when meaningful, PC and a bounded frame sequence | `CrashReporterFramesTest::report_contains_bounded_raw_frames`: a forked child with a real SIGSEGV at address 1. Asserts a nonzero `thread=`, `fault_addr=0x1`, a nonzero `pc=`, `frame[0]==pc`, 1-24 frames and size <= `kMaxReportBytes` | `run-3.log` | 0 |
| 2a | Signal handling is bounded | `frames_are_capped_on_a_deep_crash`: a 200-deep real crash gives exactly 24 frames, and a budget of 3 is respected | `run-3.log` | 0 |
| 2b | The handler is async-signal-safe | `fatal_handler_enters_neither_allocator_nor_loader`: `malloc`/`free`/`calloc`/`realloc`, `dl_iterate_phdr` and `_dl_find_object` are interposed in a child that hits a real fault. Any entry exits the child with 42 or 43; otherwise the test requires the report and death by SIGSEGV. `crash_while_the_loader_is_busy_still_reports`: a fault inside the `dl_iterate_phdr` callback does not hang, writes the report and dies by SIGSEGV | `run-3.log` | 0 |
| 2c | Normal shutdown leaves no report and no stale marker | `CrashReporterTest::clean_run_leaves_no_state`, in the 6-test ctest set | `test-2.log` | 0 |
| R | The test fails without the product change | Product files `src/core/CrashReporter.cpp`, `src/core/CrashReporterFormat.cpp`, `include/CrashReporter.h` and `include/CrashReporterFormat.h` reverted to `2ff68a5036~1` (the `_Unwind_Backtrace` handler), test kept: `build-2.log` exit 0, `run-2.log` **exit 1**, `Totals: 5 passed, 1 failed`, `FAIL! fatal_handler_enters_neither_allocator_nor_loader() the fatal handler entered the loader's object walk (child exit 42)`. Restored with `git checkout HEAD --`: `build-3.log` exit 0, `run-3.log` **exit 0** (`Totals: 6 passed, 0 failed, 0 skipped`), `test-2.log` exit 0 (6/6) | as listed | 1 -> 0 |

The first green pass came before the cycle: `build-1.log` exit 0, `test-1.log` exit 0 (6/6) and `run-1.log`
exit 0 (`6 passed, 0 failed, 0 skipped`). The ctest set is
`ctest -R 'CrashReporterTest|CrashReporterArmTest|CrashReporterFramesTest|WorkerAltStackTest|CrashReporterAppleSignalCheck|ControlCrashReporter' --output-on-failure`.

## What the unit contains (all rounds)

- **Raw frames.** The handler records the faulting PC as `frame[0]`, then walks the saved frame-pointer
  chain with raw loads. The walk stays inside `[interrupted SP, stack ceiling]`, needs a strictly
  increasing 16-byte-aligned chain and stops at `kMaxCrashFrames` (24). It uses no unwinder, no loader
  lookup and no allocation. `_Unwind_Backtrace()` was removed in `2ff68a5036`.
- **Thread id.** `gettid` on Linux. On Darwin, `mach_thread_self()` followed by `mach_port_deallocate()`:
  nonzero, no pthread lock (the JDK-8235962 fix).
- **Darwin stack top (`a85a046063`).** On Darwin the handler reads `pthread_get_stackaddr_np(pthread_self())`
  instead of a `thread_local`, because Darwin allocates TLV storage lazily through `malloc`. libpthread
  answers this call with a plain field read for the calling thread
  (https://github.com/apple-oss-distributions/libpthread/blob/main/src/pthread.c). This path is
  compile-only here; hosted CI builds it.
- **Commands, GUI, undo, format.** No new or changed command and no GUI change. The existing `crash.*`
  commands and the `CrashReportMenu` offer dialog are untouched, so the snapshot and A16 line need no
  regeneration. The change is `not_mutating`. The report stays format `v1` with the additive
  `frames=`/`frame[i]=` lines. `data/manual/index.html` describes the frame list, and
  `docs/050-features/Q050-224.2.md` and `docs/CRASH-REPORTER.md` describe the capture and its limits.

Delegation: Sonnet ran the green build, the 6-test ctest set, the direct test run and the full red/green
revert cycle (`build-1..3`, `test-1..2`, `run-1..3`), committing nothing. I checked the log tails myself:
`run-2.log` 5 passed/1 failed, `run-3.log` 6 passed, `test-2.log` 6/6, `build-3.log` built. I did the
complexity extraction, the lizard measurements, the gates, the manifest and this report myself: the
function was already in my context and the extraction was smaller than a request. Haiku and DeepSeek were
not needed.

## Self-check

**? Each new test FAILS with your product change reverted, then PASSES restored.**
Yes, this session. With the four product files reverted to `2ff68a5036~1`, `run-2.log` exited **1** (child
exit 42, loader walk). Restored, `run-3.log` exited **0** and `test-2.log` exited **0**. Both rebuilds
(`build-2.log`, `build-3.log`) exited 0.

**? Every acceptance criterion is proven by a test you ran yourself.**
Criterion 1: `report_contains_bounded_raw_frames` (`run-3.log`, exit 0). Criterion 2:
`frames_are_capped_on_a_deep_crash`, `fatal_handler_enters_neither_allocator_nor_loader` and
`crash_while_the_loader_is_busy_still_reports` (`run-3.log`, exit 0), plus
`CrashReporterTest::clean_run_leaves_no_state` (`test-2.log`, exit 0).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
There is no command, no model edit and no project data, so no undo step is created. The behaviour is
`not_mutating`.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**
This round's helpers run only inside the fatal signal handler, or inside a direct `captureFrames()` call
from a test. They are raw loads and arithmetic. The only code this unit places on the render/JACK paths is
`noteThreadStackMarker()`: one local's address and a compare/store on a `thread_local` on Linux, and
nothing on Darwin. `lmmsobjs` is linked into executables, so that TLS access is a direct offset. TSan does
not run on this box. By design, the new state is either per-thread or a fixed array in the crashing
thread's own snapshot.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project, serialization, song, transport or engine file is touched, and the crash report is not project
data.

**? Completeness: real user path and agent path; the edge case tried.**
User path: a real SIGSEGV writes `crash-reports/zene-crash-report.txt`, and the existing offer dialog shows
it at the next start. Agent path: the existing `crash.*` commands, exercised by `ControlCrashReporter`
(`test-2.log` exit 0). Edge cases tried: a fault three frames deep, a 200-deep stack (capped at 24), a fault
while the loader lock is held, allocator/loader interposition, and a clean start and close. Every fault case
also asserts that the process dies by the signal.

**? The GUI test drives the visible control and checks the SAME registered command ran.**
This unit adds no GUI action and no command. The existing dialog and the `crash.*` commands are unchanged.

**? Qt 5.15 / MSVC / macOS < 10.15 portability.**
The new helpers are plain C++ on `uintptr_t` and use no Qt API. They sit inside the existing
`#ifndef LMMS_BUILD_WIN32` region, so Windows keeps its no-op stubs. `__builtin_return_address` stays under
`#if defined(__GNUC__) && !defined(_WIN32)`, with a `0` fallback. There is no `std::filesystem` and no
`small`/`near`/`far`/`interface`/`min`/`max` identifier. The Darwin calls (`pthread_get_stackaddr_np`,
`mach_thread_self`) predate 10.15.

**Completion check.** `PATH=/tmp/liz-224/bin:$PATH python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-224.2`, logged in `submit-check-1.log`, ran against `b6b0efb85d` and exited **0** with `RESULT: PASS - submit`. Every row passed, including `gates (feature_check.py)`, and no row read FAIL or WARN.
