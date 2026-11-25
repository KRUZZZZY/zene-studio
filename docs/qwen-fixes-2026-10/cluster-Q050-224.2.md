IMPLEMENTED

# Q050-224.2 - bounded raw crash frames (stage 2/4 of Q050-224), round 4

Branch `feat/050-Q050-224.2-oc`. Round-4 logs and the proof manifest are in `~/.config/kads/zene/state/runs/Q050-224.2.r4/logs/`. Builds ran from the
worktree root and tests from `build/tests`, with `QT_QPA_PLATFORM=offscreen` and output redirected to a log,
never piped.

## Round 4: the review points

The round-3 review's two `[not-fixed]` points describe the round-1 code (`_Unwind_Backtrace()` at the old
`CrashReporterFormat.cpp:242`, and a zero Darwin thread id). Commit `2ff68a5036` already fixed both, and the
two later reviews gave no parsable verdict. This round re-checked both fixes and closed one more macOS gap.

1. **Unwinder in the fatal handler.** It is gone. `captureFrames()` (`src/core/CrashReporterFormat.cpp`)
   records the PC and then walks the saved frame-pointer chain using raw loads only, bounded by the
   interrupted SP, the stack ceiling, a strictly increasing 16-byte-aligned chain and `kMaxCrashFrames` (24).
   `fatal_handler_enters_neither_allocator_nor_loader` interposes `malloc`/`free`/`calloc`/`realloc`,
   `dl_iterate_phdr` and `_dl_find_object` in a child that hits a real fault. In this session it was RED with
   the round-1 product (exit 1, child exit 42) and GREEN now (exit 0); see the table.
2. **macOS thread id.** I checked `currentThreadId()` by reading the source. It returns
   `mach_thread_self()` and then calls `mach_port_deallocate()`: two Mach traps, no pthread lock, no
   allocation, and nonzero for a live thread. The nonzero `thread=` assertion in the real-fault test is kept,
   with no platform gate.
3. **New this round, `a85a046063`: no Darwin `thread_local` in the handler.** On Darwin a `thread_local` is
   allocated lazily through `malloc` on a thread's first access (dyld TLV). The walk read the recorded
   ceiling from a `thread_local`, so a crash on a thread that never registered would have entered the
   allocator in the signal. On Darwin the handler now reads the stack top with
   `pthread_get_stackaddr_np(pthread_self())`, which libpthread serves as a plain `t->stackaddr` read for the
   calling thread (no list lock; source:
   https://github.com/apple-oss-distributions/libpthread/blob/main/src/pthread.c). The registration
   functions no longer touch a `thread_local` on Darwin. Linux behaviour is unchanged. This box cannot run
   the Darwin path; hosted CI compiles it. The now-unused `thread_local`s are `#if`-guarded so clang raises
   no unused-variable warning.

## Acceptance (round 4, all run in this session against `a85a046063`)

| # | Criterion | Test | Log | Exit |
|---|---|---|---|---|
| 1 | A real fault report contains thread id, fault address when meaningful, PC and a bounded frame sequence | `CrashReporterFramesTest::report_contains_bounded_raw_frames`: a forked child with a real SIGSEGV at address 1. Asserts a nonzero `thread=`, `fault_addr=0x1`, a nonzero `pc=`, `frame[0]==pc`, 1-24 frames and size <= `kMaxReportBytes` | `run-3.log` (6 passed) | 0 |
| 2a | Signal handling is bounded | `frames_are_capped_on_a_deep_crash`: a 200-deep real crash gives exactly 24 frames, and the budget is respected | `run-3.log` | 0 |
| 2b | The handler is async-signal-safe: no allocator, no loader | `fatal_handler_enters_neither_allocator_nor_loader` (described above); `crash_while_the_loader_is_busy_still_reports` (a fault inside the `dl_iterate_phdr` callback writes the report, does not hang and dies by SIGSEGV) | `run-3.log` | 0 |
| 2c | Normal shutdown leaves no report and no stale marker | `CrashReporterTest::clean_run_leaves_no_state`, in the 6/6 ctest run | `test-2.log` | 0 |
| R | The new test fails without the product change | The four product files reverted to `2ff68a5036~1` with the test kept: `build-2.log` exit 0, then `run-2.log` **exit 1**: `FAIL! fatal_handler_enters_neither_allocator_nor_loader() the fatal handler entered the loader's object walk (child exit 42)`. Restored with `git checkout HEAD --`: `build-3.log` exit 0, `run-3.log` **exit 0**, `test-2.log` 6/6 **exit 0** | as listed | 1 -> 0 |

The suite is `ctest -R 'CrashReporterTest|CrashReporterArmTest|CrashReporterFramesTest|WorkerAltStackTest|CrashReporterAppleSignalCheck|ControlCrashReporter' --output-on-failure`.
It passed 6/6 before the cycle (`test-1.log`) and after it (`test-2.log`). The first green build was
`build-1.log` exit 0.

## Gates

`submit_check.py` (`submit-check-1.log`) passes every item except `gates (feature_check.py)`. The session
guard refused that runner: it runs `git -C ~/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q
--detach 3436a808b6`, a checkout outside this worktree, and the guard answered
`bunny-guard: ... is not allowed in a Bunny lane` (`feature-check-1.log`, exit 1). I did not work around the
refusal. Instead I ran the eight gates it wraps directly in the worktree:

| Gate | Log | Exit |
|---|---|---|
| `bash tests/fork-sources-gate.sh` | `gate-1.log` | 0 |
| `bash tests/all-sources-reproduce.sh` | `gate-2.log` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | `gate-3.log` | 0 |
| `tests/scripted/check-namespace` | `gate-4.log` | 0 |
| `bash tests/no-tautology-gate.sh` | `gate-5.log` | 0 |
| `python3 tests/doc-figures-gate.py` | `gate-6.log` | 0 |
| `bash tests/complexity-gate.sh --check` | `gate-7.log` | 2: `lizard is not installed` (the tool is missing on this box, the same as in the round-3 review; no `REGRESSION:` line was emitted) |
| `bash tests/file-length-gate.sh --check` | `gate-8.log` | 0 |

Complexity, checked by reading the code: the only new function, `stackCeilingForHandler()`, has a single
path. The `captureFrames()` change replaces two variable reads with locals and adds no branch.

## Commands, GUI, undo, format

There are no new or changed commands and no GUI change. The unit changes only what a report file contains
and how the handler gathers it. The existing `crash.*` commands and the `CrashReportMenu` offer dialog are
untouched, so the snapshot and A16 line need no regeneration. Nothing edits a project, so the change is
`not_mutating`. The report format stays `v1` with the additive `frames=`/`frame[i]=` lines. The manual
(`data/manual/index.html`) already describes the frame list. `docs/050-features/Q050-224.2.md` and
`docs/CRASH-REPORTER.md` now describe the Darwin stack-top read.

Delegation: Sonnet ran the green build, the 6-test ctest suite, the direct test run and the full red/green
revert cycle (`build-1..3`, `test-1..2`, `run-1..3`), committing nothing. I checked its claims against the
log tails (`run-2.log` 5 passed/1 failed with the child-exit-42 FAIL, `run-3.log` 6 passed, `test-2.log`
6/6) and confirmed `git status` was clean. I designed and wrote the Darwin change, the docs, the manifest
and this report myself, because each was smaller than a request would have been. Haiku and DeepSeek were
not needed.

# Earlier rounds (history)

# Q050-224.2 - bounded raw crash frames (stage 2/4 of Q050-224)

Round 2 of the unit. The round-1 commits (`008a51f6`, `913d2e38`, `bc2480ef`, `9572f18b`) stay on the branch;
this round replaces the fatal handler's general-purpose unwinder with a bounded raw frame-pointer walk and
gives Darwin a signal-safe nonzero thread id. Branch `feat/050-Q050-224.2-oc`, worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-224.2`. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/Q050-224.2.r2/logs/`; every command below was run from
`build/tests` (tests) or the worktree root (builds) with `QT_QPA_PLATFORM=offscreen`, unpiped, as
`cmd > log 2>&1; echo EXIT=$?`.

## Review points, and what changed

1. **Unwinder in the fatal handler (not-fixed).** `captureFrames()` (`src/core/CrashReporterFormat.cpp:317`)
   no longer calls `_Unwind_Backtrace()`. It writes the faulting PC (`src/core/CrashReporter.cpp:141`) and
   then walks the crashing thread's saved frame-pointer chain with raw loads only: no loader object lookup,
   no unwind-table query, no allocation, no lock and no I/O. The walk is bounded by exact stack bounds
   recorded outside the signal (`recordThreadStackBounds()`, `CrashReporterFormat.cpp:278`), a strictly
   increasing 16-byte-aligned chain, and `kMaxCrashFrames` (24). The exact-bounds call runs only in
   `install()`; the render/JACK paths only take a local's address in `noteThreadStackMarker()`
   (`CrashReporterFormat.cpp:307`) through `installAlternateStackForCurrentThread()`.
2. **macOS thread id zero (not-fixed).** `currentThreadId()` (`CrashReporterFormat.cpp:192`) now returns the
   current thread's Mach port through `mach_thread_self()` and releases the extra send right with
   `mach_port_deallocate()` - two Mach traps, no pthread lock and no allocation, the fix OpenJDK shipped for
   JDK-8235962. The existing nonzero assertion is retained (`tests/src/core/CrashReporterFramesTest.cpp:360`).
3. **Deterministic real-fault regression.** `CrashReporterFramesTest::fatal_handler_enters_neither_allocator_nor_loader`
   (`tests/src/core/CrashReporterFramesTest.cpp:488`) forks a child, interposes
   `malloc`/`free`/`calloc`/`realloc`, `dl_iterate_phdr` and glibc 2.35+'s
   `_dl_find_object@GLIBC_2.35`, sets the watch flag and faults for real. An entry exits the child 42
   (loader) or 43 (allocator); the test requires the report to be written and the child to die by
   `SIGSEGV`. Reverted product: **exit 1**, `the fatal handler entered the loader's object walk (child exit 42)`.
   Restored: **exit 0**.

## Acceptance

| # | Criterion | Test / evidence | Exit |
|---|---|---|---|
| 1 | A real fault report contains thread id, fault address when meaningful, PC and a bounded sequence of frame addresses | `CrashReporterFramesTest::report_contains_bounded_raw_frames` (in `test-24-final-suite.log`): forked real SIGSEGV at address 1; asserts nonzero `thread=`, `fault_addr=0x1`, nonzero `pc=`, `frame[0] == pc`, `1 <= frames <= 24`, report <= `kMaxReportBytes`. Standalone product check: `probe-run-2.log` (re-linked `frame-probe`, exit 139 by SIGSEGV) wrote `thread=127719`, `fault_addr=0x1`, `pc=0x55f1f2ae963e`, `frames=6`, `frame[0]=0x55f1f2ae963e`. | 0 / 139 (expected signal) |
| 1m | The macOS thread id is nonzero and signal-safe | same test's `thread=` assertion (`tests/src/core/CrashReporterFramesTest.cpp:360`), with the Mach-port implementation above. This box is Linux; macOS is CI-compiled. The assertion is not weakened or platform-gated. | 0 on Linux; CI for macOS |
| 2 | Signal handling remains bounded and async-signal-safe | `CrashReporterFramesTest::frames_are_capped_on_a_deep_crash` (200-deep real crash -> exactly 24 frames; `captureFrames(nullptr, out, 3)` keeps <= 3) and `fatal_handler_enters_neither_allocator_nor_loader` (direct `test-25-final-probe-direct.log`: 3 passed, exit 0), both in `test-24-final-suite.log`. | 0 |
| 2b | ... a fault while the loader is busy neither hangs nor goes unreported | `CrashReporterFramesTest::crash_while_the_loader_is_busy_still_reports` (real fault inside `dl_iterate_phdr`'s callback, bounded wait, report written, SIGSEGV) in `test-24-final-suite.log`. | 0 |
| 2c | Normal shutdown still leaves no report or stale marker | `CrashReporterTest::clean_run_leaves_no_state` (unchanged) in `test-24-final-suite.log`. | 0 |
| 3 | The new test FAILS without the product change and PASSES with it | Reverted: `build-16-red-exact.log` exit 0, then `test-21-red-exact.log` **exit 1** (child exit 42, loader walk). Restored: `build-17-green-exact.log` exit 0, then `test-25-final-probe-direct.log` **exit 0** and `test-24-final-suite.log` 6/6 **exit 0**. | 1 -> 0 |
| 4 | Build and gates | `build-15-final.log` (zene + four test binaries) exit 0; `build-18-final-comment.log` exit 0; `feature-check-3.log` 8/8 PASS exit 0 (includes `doc-figures` and `check-namespace`); `tests/doc-figures-gate.py` is inside that gate. | 0 |

Supporting suite: `ctest -R 'CrashReporterTest|CrashReporterArmTest|CrashReporterFramesTest|WorkerAltStackTest|CrashReporterAppleSignalCheck|ControlCrashReporter'`
-> 6/6 passed (`test-24-final-suite.log`), including the agent/control-socket path `ControlCrashReporter`.

## Commands, GUI, undo, format

- **No new or changed command and no GUI change.** This unit changes only what a report file contains. The
  existing `crash.*` commands and the existing `CrashReportMenu` offer dialog are untouched; no
  `ControlRegistry`, snapshot, reversibility-table or A16 line changes, so no snapshot regeneration or
  reversibility class is required for this unit.
- **Undo:** nothing edits the project, so the change is `not_mutating` and creates no undo step.
- **Format:** still `Zene Studio crash report v1`; the frame block (`frames=N`, `frame[i]=0x...`) is already
  additive from stage 2 and unchanged by this round.
- **Projects:** no save/load or project-data code is touched, so old projects load and render as before and
  there is no new data to round-trip.
- **Manual:** `data/manual/index.html` already documents the report file, the offer dialog and the raw
  frame list (committed in `913d2e38`); this round changes how the list is captured, not what the user sees.

## Round 3 re-verification (no source change)

The round-2 logs named below (`test-21-red-exact.log` etc., run `Q050-224.2.r2`) are no longer on disk, so this
round re-ran the evidence against the unchanged product code (`src/core/CrashReporter*.cpp` and
`include/CrashReporter*.h` are byte-identical to `2ff68a5036`). Logs: `~/.config/kads/zene/state/runs/Q050-224.2.r3/logs/`.
The cycle below supersedes every `r2` log name in the Self-check section.

| Step | Command | Log | Exit |
|---|---|---|---|
| Existing binaries, suite | `ctest -R 'CrashReporterTest\|CrashReporterArmTest\|CrashReporterFramesTest\|WorkerAltStackTest\|CrashReporterAppleSignalCheck\|ControlCrashReporter'` (6/6) | `test-1-suite.log` | 0 |
| Existing binaries, frames test | `./CrashReporterFramesTest` (6 passed) | `test-2-frames-direct.log` | 0 |
| RED: the four product files reverted to `2ff68a5036~1` (the `_Unwind_Backtrace` handler), test kept; rebuilt | `cmake --build build --target CrashReporterFramesTest -- -j4` | `build-3-red.log` | 0 |
| RED run | `./CrashReporterFramesTest`: `FAIL! fatal_handler_enters_neither_allocator_nor_loader() the fatal handler entered the loader's object walk (child exit 42)` | `test-3-red.log` | **1** |
| GREEN: product restored with `git checkout HEAD -- <four files>` (tree clean), rebuilt | `cmake --build build --target zene CrashReporterFramesTest CrashReporterTest CrashReporterArmTest WorkerAltStackTest -- -j4` | `build-4-green.log` | 0 |
| GREEN run | `./CrashReporterFramesTest` (6 passed) | `test-4-green.log` | **0** |
| GREEN suite | the same six-test `ctest -R` as the first row (6/6) | `test-5-suite.log` | 0 |
| Completion check | `submit_check.py` | `submit-check-1.log` | 0 (PASS) |

macOS thread id, by source inspection: `currentThreadId()` (`src/core/CrashReporterFormat.cpp:192-201`) returns
`mach_thread_self()`'s port on Apple after releasing the extra send right with `mach_port_deallocate()`
(no pthread lock, no allocation, nonzero for a live thread), and `tests/src/core/CrashReporterFramesTest.cpp`
still asserts a nonzero `thread=` with no platform gate. Not executable on this Linux box; CI compiles it.


## Self-check

**? Each new test FAILS with your product change reverted, then PASSES restored.**
Yes, this session. With the four product files reverted to `2ff68a5036~1` and the test kept, `build-2.log`
exited 0 and `run-2.log` exited **1** (`fatal_handler_enters_neither_allocator_nor_loader ... the loader's
object walk (child exit 42)`). Restored with `git checkout HEAD --` (tree clean), `build-3.log` exited 0,
`run-3.log` exited **0** (6 passed) and `test-2.log` exited **0** (6/6).

**? Every acceptance criterion is proven by a test you ran yourself.**
Criterion 1: `report_contains_bounded_raw_frames`, `run-3.log` exit 0. Criterion 2:
`frames_are_capped_on_a_deep_crash`, `fatal_handler_enters_neither_allocator_nor_loader` and
`crash_while_the_loader_is_busy_still_reports` (`run-3.log` exit 0), plus
`CrashReporterTest::clean_run_leaves_no_state` for normal shutdown (`test-2.log` exit 0).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
There is no command, no model edit and no project data in this unit, so no undo step is created. The
behaviour is `not_mutating`.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**
The only code this unit puts on a real-time path is `noteThreadStackMarker()`, called through
`installAlternateStackForCurrentThread()`. On Linux it takes one local's address and does a compare/store on
a `thread_local`. On Darwin it is now empty, so it no longer triggers the lazy TLV allocation on a render
thread's first call. The allocating `recordThreadStackBounds()` runs only from `install()`, on the
main/control thread. TSan does not run on this box. By design, all new state is either per-thread or a
fixed array in the crashing thread's own snapshot, and the existing re-entrancy guard admits one writer.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project, serialization, song, transport or engine file is touched, and the crash report is not project
data. There is nothing new to save or load.

**? Completeness: real user path and agent path; the edge case tried.**
User path: a real SIGSEGV writes `crash-reports/zene-crash-report.txt`, and the existing offer dialog shows
it at the next start. Agent path: the existing `crash.*` commands, exercised by `ControlCrashReporter`
(`test-2.log` exit 0). Edge cases tried: a fault three frames deep, a 200-deep stack (capped at 24), a fault
while the loader lock is held (no hang), allocator/loader interposition (neither is entered), and a clean
start and close (no report, no marker). Every fault case also asserts that the process dies by the signal.

**? The GUI test drives the visible control and checks the SAME registered command ran.**
This unit adds no GUI action and no command, so there is no new control to drive. The existing dialog and
the `crash.*` commands are unchanged.

**? Qt 5.15 / MSVC / macOS < 10.15 portability.**
The capture code uses no Qt API. All POSIX code is under `#ifndef LMMS_BUILD_WIN32`, and Windows keeps the
no-op stubs. The new Darwin call `pthread_get_stackaddr_np()` was already used in this file. It is declared
in `<pthread.h>` (included) and has existed since macOS 10.4. The `thread_local`s are `#if !defined(__APPLE__)`
so nothing on Darwin is left unused. There is no `std::filesystem` and no `small`/`near`/`far`/`interface`/
`min`/`max` identifier.

**Completion check.** `submit-check-1.log`: every item passes except `gates`, whose runner the session guard
refused (see Gates). Seven of its eight gates pass when run directly. The complexity gate cannot run here
because `lizard` is not installed.
