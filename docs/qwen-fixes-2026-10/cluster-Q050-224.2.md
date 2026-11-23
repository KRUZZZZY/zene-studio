IMPLEMENTED

# Q050-224.2 - bounded raw crash frames (stage 2/4 of Q050-224)

A crash report now preserves raw stack context: up to `kMaxCrashFrames` (24) instruction addresses, nearest
frame first, with the faulting instruction as `frame[0]`. The walk is the platform's own
`_Unwind_Backtrace` over the crashing thread's stack, captured in the signal handler from the signal
context, and nothing is symbolised or module-resolved there — the addresses are written raw and resolved by
whoever reads the report, in another process. Bounded by construction: the walk's callback returns
`_URC_END_OF_STACK` once the budget is full, so a corrupt or cyclic unwind costs at most 24 steps, and the
frame lines go through the existing capped appenders so `kMaxReportBytes` still holds.

Branch `feat/050-Q050-224.2-oc`, product commit `008a51f6`, report commit named by the proof manifest.
Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-224.2`. Logs:
`/home/kruzzzzy/.config/kads/zene/state/runs/Q050-224.2.r1/logs/`. All runs: `QT_QPA_PLATFORM=offscreen`,
`build/tests`, `cmake --build build --target … -- -j4`.

Files: `include/CrashReporter.h` (`kMaxCrashFrames`, `CrashInfo::frames`/`frameCount`, the
async-signal-safety statement), `include/CrashReporterFormat.h` (`captureFrames`, `appendFrames`),
`src/core/CrashReporterFormat.cpp` (the walk and the formatter, plus the Windows no-op half),
`src/core/CrashReporter.cpp:139`/`:524` (capture in the handler, format in the writer),
`tests/src/core/CrashReporterFramesTest.cpp` (new binary), `tests/CMakeLists.txt`,
`tests/fork-sources.txt`, `tests/all-sources.txt`, `docs/050-features/Q050-224.2.md`,
`docs/CRASH-REPORTER.md` (§2.6 sample, new §2.7), `data/manual/index.html` (one paragraph).

## Acceptance

| # | Criterion | Test | Command | Exit |
|---|---|---|---|---|
| 1 | A real fault report contains thread id, fault address when meaningful, PC and a bounded sequence of frame addresses | `CrashReporterFramesTest::report_contains_bounded_raw_frames` | `cd build/tests && ./CrashReporterFramesTest` (`test-11-green-guards.log`) | 0 |
| 2 | Signal handling remains bounded and async-signal-safe | `CrashReporterFramesTest::frames_are_capped_on_a_deep_crash` (200-deep stack → exactly 24 frames; `captureFrames(…, 3)` keeps ≤ 3) | same run (`test-11-green-guards.log`) | 0 |
| 2b | …including a fault while the loader's lock is held (plug-in loading), which must not hang | `CrashReporterFramesTest::crash_while_the_loader_is_busy_still_reports` | same run (`test-11-green-guards.log`) | 0 |
| 2c | Normal shutdown still leaves no report or stale marker | `CrashReporterTest::clean_run_leaves_no_state` (unchanged) | `ctest -R '^(CrashReporterTest\|CrashReporterFramesTest\|ControlCrashReporter)$' --output-on-failure` (`test-12-final.log`) | 0 |
| 3 | Every new case fails without the product change | the three cases on the reverted product | `./CrashReporterFramesTest` (`test-07-red-behaviour.log`) → `2 passed, 3 failed`; full revert → `build-12-red-full.log` | 3 / 2 (build) |
| 3b | …and passes restored | all four crash binaries | `ctest -R '^(CrashReporterArmTest\|CrashReporterTest\|CrashReporterFramesTest\|CrashReporterAppleSignalCheck)$'` (`test-14-arm-and-apple.log`) | 0 |
| 4 | Gates | `feature_check.py` (8/8 PASS), `tests/doc-figures-gate.py`, `ReversibilityContractTest` | `gates-02.log`, `doc-figures-gate.log`, `test-13-a16.log` | 0 / 0 / 0 |

Build for the runs above: `build-17-final-product.log` (exit 0), `build-14`/`build-15` for the
revert/restore cycle, `build-16-guards.log` after the POSIX guards.

## Self-check

**? Each new test FAILS with your product change reverted, then PASSES restored.**
Two reverts, both recorded. (a) Full product revert (`git apply -R` of the four non-test hunks, 212-line
patch): the new binary no longer compiles — `build-12-red-full.log`, `EXIT=2`,
`error: 'kMaxCrashFrames' was not declared in this scope`, `error: 'captureFrames' was not declared`.
(b) Behavioural revert — the API kept, the two call sites removed (capture in the handler, the frame block
in the writer), so the report is exactly what it was before this unit: `build-13-red-behaviour.log` build
exit 0, then `./CrashReporterFramesTest` → `test-07-red-behaviour.log`, **exit 3**, `2 passed, 3 failed`,
every failure on `'frames.declared >= 1' returned FALSE (the report carries no frame sequence)`. Restored:
`git apply` of the same patch, `build-15-green-restored.log` exit 0, `./CrashReporterFramesTest` →
`test-08-green-restored.log` **exit 0** (`5 passed`), `ctest` of both crash tests → `test-09-green-ctest.log`
exit 0, and after the POSIX guards `test-11-green-guards.log` exit 0.

**? Every acceptance criterion is proven by a test you ran yourself.**
See the table: criterion 1 → `report_contains_bounded_raw_frames` exit 0 (a forked child really faults:
`thread=` non-zero, `fault_addr=0x1` from `si_addr`, `pc=` non-zero, `frames=N` with `1≤N≤24`, exactly N
`frame[i]=0x…` lines, all non-zero, `frame[0] == pc`, `N≥3` on Linux where the walk crosses the signal
frame, file ≤ 4096 bytes). Criterion 2 → `frames_are_capped_on_a_deep_crash` exit 0 (200 real frames →
exactly 24 written, file ≤ 4096 bytes, a 3-address budget keeps ≤ 3) plus
`crash_while_the_loader_is_busy_still_reports` exit 0 (bounded wait, report written, dies by SIGSEGV) and
the unchanged `clean_run_leaves_no_state` (no report, no marker, no `crash-reports/`). A real report from
this branch is quoted in the feature record (499 bytes, 10 frames, from a `/tmp` probe linking the built
`CrashReporter*.o`).

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
No command is added or changed: the frame block is what a report file contains, and no member of the
`crash.*` group reads or writes report fields (`crash.list_reports` lists paths/state,
`crash.acknowledge_report`/`crash.discard_report` move two sentinel files, `crash.enable`/`crash.disable`
arm and disarm). So there is no new undo step to make one step, no `ControlRegistry` row, no A16 row and no
snapshot change — `ReversibilityContractTest` passes unchanged (`test-13-a16.log`, exit 0), including its
histogram assertion, so `docs/A16-REVERSIBILITY.md` needs no edit.

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a
signal.**
Nothing added is on any audio path: the two call sites are `zeneCrashSignalHandler` (a dying thread, never
a render callback) and `writeReportIfIdle` (the same handler path plus tests). The handler gains one call,
`_Unwind_Backtrace`, named in `include/CrashReporter.h` beside the pre-existing `syscall(SYS_gettid)`:
no allocation in our code, no I/O, no Qt, no stdio, no symbol or module resolution, and bounded to 24
steps by the callback, so it cannot spin. It re-enters the loader's lock, which glibc
(`__rtld_lock_define_recursive`) and musl define recursively; that is measured, not assumed
(`crash_while_the_loader_is_busy_still_reports`). The FLAGGED lines submit_check prints
(`plugins/ConvolutionReverb/ConvolutionReverb.cpp`, `plugins/LadspaEffect/LadspaEffect.cpp`,
`src/core/AudioEngine.cpp removeAudioBusHandle`) belong to other units in this branch's diff against the
base, not to this one: `git diff` of this unit touches `include/CrashReporter*.h`,
`src/core/CrashReporter*.cpp`, tests and docs only. The audio thread's own stack registration
(`installAlternateStackForCurrentThread`) was already a one-shot call at thread start and is untouched.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
No project data is touched: nothing in this unit reads or writes a project, a settings file, a preset or a
format. The only file written is the crash report, whose change is additive (a `frames=` line and
`frame[i]=` lines after `project=`), so a reader of the named fields is unaffected and a reader that wants
frames looks for `frames=`. Old projects therefore load and render exactly as before (no code on that path
changed) and there is no new data to round-trip. `ctest -R ControlCrashReporter` (the socket transcript that
plants a pre-existing report body with no `frames=` line and reads it back) passes: `test-12-final.log`
exit 0 — i.e. a report without the block is still read exactly as before.

**? Completeness: walk each criterion through the real user path and the agent path, and name the edge case
you tried.**
User path: a crash writes the report (`zeneCrashSignalHandler` → `writeReportIfIdle`); the next start offers
it once (`main.cpp:offerPendingCrashReport`: Keep report / Open folder / Discard, stderr in an unattended
run), and the manual's new paragraph describes exactly that dialog and the file's fields. Agent path: the
same file, plus `crash.list_reports` / `crash.acknowledge_report` / `crash.discard_report` /
`crash.enable` / `crash.disable` unchanged (the GUI's `CrashReportMenu` invokes those same registered
commands, so GUI and agent still share one route). Edge cases tried: SIGSEGV through a three-deep call chain
and through a 200-deep one; a fault inside `dl_iterate_phdr`'s callback (loader lock held); a repeated
signal; SIGABRT; and the negative control (a clean run creates no report, no marker, no `crash-reports/`
directory). Not reachable here: macOS, Windows and the ARM/Apple signal model beyond
`CrashReporterAppleSignalCheck`, which compiles the crash reporter's test translation units under a model of
Apple's `<signal.h>` and passes (`test-14-arm-and-apple.log`, exit 0).

**? The GUI test drives the visible control and checks the SAME registered command ran.**
No GUI file changed in this unit, so there is no new visible control to drive: the crash-report menu and
dialog already exist and already go through `ControlRegistry` (`src/gui/CrashReportMenu.cpp` sets
`crash.list_reports`/`crash.enable`/`crash.acknowledge_report`/`crash.discard_report` as each item's data and
invokes them). Adding a GUI test for an action that does not exist would be theatre; the surfaces this unit
touches are the report file and the handler, and the manual paragraph documents what the user sees.

**? Qt 5.15 / MSVC / macOS < 10.15 portability.**
`_Unwind_Backtrace` arrives through `<unwind.h>`, which GCC and Clang both ship (glibc/Linux, macOS, mingw)
and which MSVC does not — so the whole walk sits inside the existing `#ifndef LMMS_BUILD_WIN32` block, with
an `#else` half returning 0 and formatting nothing, and the Windows test asserts that no-op instead of
compiling the walk. No Qt API is used in the product code at all (no Qt version guard needed); no
`std::filesystem`; no POSIX-only call added outside the existing platform guard; `dl_iterate_phdr` and
`<link.h>` are used **only in the test**, under `#if defined(__linux__)`, and the case is `QSKIP`ped with
its reason elsewhere. No `small`, `near`, `far`, `interface`, `min` or `max` identifiers are introduced.
Qt 5.15: `captureFrames`/`appendFrames` use only C++20 (the tree's standard) and no Qt type; the test uses
`QByteArray`/`QList`/`QCOMPARE` with `static_cast<int>(…size())` so it compiles under both Qt 5 and Qt 6
without a deprecation or type mismatch.
