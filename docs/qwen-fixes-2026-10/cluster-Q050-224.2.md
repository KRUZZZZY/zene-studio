IMPLEMENTED

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

## Self-check

**? Each new test FAILS with your product change reverted (reverse the non-test hunks, rebuild, run: exit
code?), then PASSES restored.**
Done twice in this session; the final exact cycle reverted the four product files to `HEAD` (the round-1
unwinder) while keeping the new test. `build-16-red-exact.log` exit 0; `test-21-red-exact.log`:
`FAIL! CrashReporterFramesTest::fatal_handler_enters_neither_allocator_nor_loader() the fatal handler
entered the loader's object walk (child exit 42)`, QtTest **exit 1**. Restored with the saved 441-line
`product-final2.patch`: `build-17-green-exact.log` exit 0, `test-25-final-probe-direct.log` `PASS`,
**exit 0**; `test-24-final-suite.log` **exit 0**. (The earlier round-1 behaviour that the test catches was
exactly `_Unwind_Backtrace()` in the handler.)

**? Every acceptance criterion is proven by a test you ran yourself.**
Criterion 1 + 1m: `report_contains_bounded_raw_frames` (nonzero thread, `fault_addr=0x1`, nonzero PC,
`frame[0]==pc`, 1..24 frames) in `test-24-final-suite.log` exit 0, plus the standalone `probe-run-2.log`
exit 139 with the quoted report. Criterion 2: `frames_are_capped_on_a_deep_crash` (24 exactly on Linux),
`fatal_handler_enters_neither_allocator_nor_loader` (`test-25-final-probe-direct.log` exit 0) and
`crash_while_the_loader_is_busy_still_reports`, all in `test-24-final-suite.log` exit 0. Normal shutdown:
`CrashReporterTest::clean_run_leaves_no_state`, same run exit 0. Red/green: `test-21-red-exact.log` exit 1 ->
`test-25-final-probe-direct.log` exit 0. Gates: `feature-check-3.log` exit 0.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
There is no command, no model edit and no serialized data in this unit's diff, so no undo step and no
reversibility class is introduced; the only honest classification for the touched behaviour is
`not_mutating`. `git diff` of the final product patch contains only `include/CrashReporter*.h` and
`src/core/CrashReporter*.cpp`; none of those files modifies a project, a model or an undo record.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**
The flagged lines in the completion check (`plugins/ConvolutionReverb`, `plugins/LadspaEffect`,
`src/core/AudioEngine.cpp::removeAudioBusHandle`) are not in this unit's diff; they come from other units in
the 583-file branch diff. This unit's only addition to a real-time-called function is
`noteThreadStackMarker()` inside `installAlternateStackForCurrentThread()` (called from
`renderNextPeriod()`/JACK/device callbacks): it takes the address of one local and performs one
thread-local `uintptr_t` compare/store (`src/core/CrashReporterFormat.cpp:307`). It does not allocate, free,
lock, wait, read a file, call Qt or emit a signal. The allocating `recordThreadStackBounds()`
(`pthread_getattr_np`) is called only from `install()` on the main/control thread, never from an audio
callback. `sigaltstack()` in that function was already there and is a syscall on a static buffer. TSan does
not run on this box; the new state is `thread_local` plus a fixed array in the existing same-thread
snapshot, and the re-entrancy guard still admits exactly one writer across threads.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project, serialization, song, transport, take, recording or audio-engine file is touched. The report file
is not project data; its format is the same v1 format with the same additive frame lines introduced in
stage 2. There is nothing new to save or load, so round-trip behaviour is unchanged.

**? Completeness: real user path and agent path; the edge case tried.**
User path: a real `SIGSEGV` writes `crash-reports/zene-crash-report.txt`; the next start's existing offer
dialog (`src/gui/CrashReportMenu.cpp`) still offers Keep / Open folder / Discard, and the manual describes
it. Agent path: the existing `crash.*` commands over the control socket, exercised by `ControlCrashReporter`
in `test-24-final-suite.log` (exit 0), read the same on-disk report. Edge cases tried: a real fault three
frames deep (walk reaches callers), a 200-deep stack (exactly 24), a fault while the loader lock is held
(no hang, report written, signal death), allocator/loader interposition (neither entered), and normal
start/close (no report, no stale marker). `report_contains_bounded_raw_frames` and the loader-busy case also
assert death by the signal, so the report cannot be faked by swallowing the crash.

**? The GUI test drives the visible control and checks the SAME registered command ran.**
This unit adds no GUI action and no command, so there is no new visible control to drive and no new command
identity to check. The existing crash-report offer dialog and its `crash.*` commands are unchanged; the
completion check's GUI-path item reports the existing `src/gui/CrashReportMenu.cpp` and its existing GUI
test, not a new one from this unit. Changing the report's frame block does not change what the dialog does.

**? Qt 5.15 / MSVC / macOS < 10.15 portability.**
No Qt API is used in the new capture code, so there is nothing Qt-6-only to guard; the code is C++ and
POSIX. All POSIX headers and the `pthread_getattr_np`/`sigaltstack`/`syscall`/`ucontext` code are under
`#ifndef LMMS_BUILD_WIN32` (Windows gets the unchanged no-op stubs). Darwin uses `mach_thread_self()` and
`pthread_get_stackaddr_np()`, both long predating 10.15, with `_DARWIN_C_SOURCE`; `pthread_mach_thread_np`
is deliberately avoided because it is not signal-safe. There is no `std::filesystem`, no `small`/`near`/
`far`/`interface`/`min`/`max` identifier, and the only compiler-specific extensions
(`__builtin_return_address`, `.symver`, interposition attributes) are guarded by `__GNUC__`/`__linux__`/
`__GLIBC__`, with the allocator probe skipped outside glibc-on-Linux.
