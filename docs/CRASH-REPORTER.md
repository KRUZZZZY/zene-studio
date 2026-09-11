# Crash reporter (offline, local-only)

**Status:** implemented and proven on `post-alpha/crash-report` (first commit
`98a706dbe`). Scope: the smallest honest version — detect a crash, write one
bounded local report, offer that file on the next launch.

**There is no upload, no socket, no DNS, no telemetry and no network code of any
kind in this feature.** The only output is one text file inside the user's own
working directory, which the user attaches to a bug report by hand. Anything
that phones home unasked would be a failure of this task, not a feature of it.
The separate, boarded opt-in telemetry work (task #617, default-off) is a
different thing and is not touched here.

Dependencies added: **none**. No third-party library, no vendored code, nothing
to licence-check. GPLv2-clean by construction.

---

## 1. Today's behaviour, established before changing anything

All greps below are run against the base commit's tree (`git grep … 0c23587d2`),
so the new files cannot contaminate the result. Commands are reproduced verbatim.

### 1.1 What crash handling already existed: effectively none

```sh
git grep -n -iE "set_terminate|std::terminate|std::set_unexpected|Breakpad|crashpad|core_pattern|__asan" 0c23587d2 -- src include plugins
# (no matches)
```

So: no `std::set_terminate`, no `std::terminate` hook, no Breakpad/Crashpad,
no core-pattern handling, no sanitiser hooks. **The base commit has no crash
reporter — confirmed.**

Everything that does exist is in `src/core/main.cpp` (note: the app starts in
**`src/core/main.cpp`**, not `src/main.cpp`):

```sh
git grep -nE "\bsignal\s*\(\s*SIG|sigaction" 0c23587d2 -- src include
# 0c23587d2:src/core/main.cpp:318:  signal(SIGFPE, sigfpeHandler);
# 0c23587d2:src/core/main.cpp:320:  signal(SIGINT, gui::GuiApplication::sigintHandler);
# 0c23587d2:src/core/main.cpp:715:  struct sigaction sa;
# 0c23587d2:src/core/main.cpp:722:  if ( sigaction( SIGPIPE, &sa, nullptr ) )
```

- `main.cpp:82-100` — a SIGFPE handler that calls `backtrace()` +
  `backtrace_symbols_fd()` to stderr and `exit(signum)`. It is inside
  `#ifdef LMMS_DEBUG_FPE`, which is a **debug-only, off-by-default** build flag,
  and it only installs on the `LMMS_DEBUG_FPE` path (`main.cpp:308-319`).
- `main.cpp:320` — `SIGINT` → `GuiApplication::sigintHandler`, which writes one
  byte to a socketpair (`src/gui/GuiApplication.cpp:266-275`, socket created in
  `createSocketNotifier()`, `GuiApplication.cpp:278-291`) so Qt can quit cleanly.
  This is a shutdown request path, not a crash path.
- `main.cpp:714-726` — `SIGPIPE` set to `SIG_IGN`.
- **No handler for SIGSEGV, SIGBUS, SIGILL or SIGABRT anywhere. No SIGTERM either.**

The `backtrace`/`execinfo.h` search therefore finds only that one debug block
(`main.cpp:77`, `main.cpp:91`, `main.cpp:93`).

### 1.2 Where the app starts and how the event loop is set up

`src/core/main.cpp`:

- `main.cpp:379-381` — `QCoreApplication` (headless modes) or
  `gui::MainApplication` (GUI) is constructed.
- `main.cpp:688/694` — command line is checked, then
  `ConfigManager::inst()->loadConfigFile(configFile)`. From here the user's
  working directory is known.
- `main.cpp:808-961` — the GUI branch: `new GuiApplication()` (`:812`), the
  optional project-recovery dialog for `recover.mmp` (`:819-890`), the project
  load (`:908-952`), autosave setup (`:954-960`).
- `main.cpp:963` — `const int ret = app->exec();` — the Qt event loop; the app
  lives inside this call and returns on quit.

### 1.3 How the app reports an unhandled exception or a failed project load today

- **Unhandled exception:** it does not. `std::terminate` → `abort()` →
  `SIGABRT` with no handler installed, so the process dies and nothing records
  anything. (This is why the new reporter handles SIGABRT — see §2.1.)
- **Failed project load:** two paths, both in `src/core/Song.cpp`.
  `loadProject()` (`Song.cpp:1008`) sets the file name at `:1019`, and on
  failure (`:1053-1059`) calls `createNewProject()` and reverts the name to the
  previous value; the "malicious local plugins" case shows a
  `QMessageBox::critical` (`:1040`). There is a diagnostic
  `QTextStream(stderr)` line for the headless case (`:1046`).
- **Unclean exit:** the closest thing that exists is autosave recovery —
  `ConfigManager::recoveryFile()` is `<working dir>/recover.mmp`
  (`include/ConfigManager.h:205-208`), written by the autosave timer
  (`src/gui/MainWindow.cpp:1466`) and deleted by
  `MainWindow::sessionCleanup()` (`MainWindow.cpp:1318-1323`). The next launch
  sees it and offers to recover (`main.cpp:819-890`). That is a *project*
  recovery mechanism, not a crash reporter: it says nothing about the crash.

### 1.4 Where a "last session" marker could live (config / data directory helpers)

- `include/ConfigManager.h:62-69` — `ConfigManager::inst()` singleton.
- `include/ConfigManager.h:72-75` — `workingDir()`; the per-user data root.
- `include/ConfigManager.h:109-127, 165-183` — `userProjectsDir()`,
  `userTemplateDir()`, `userPresetsDir()`, `userVstDir()`, … all built as
  `workingDir() + <PATH>` from the constants at `ConfigManager.h:43-53`.
- `include/ConfigManager.h:205-208` — `recoveryFile()` follows exactly that
  pattern, which is the precedent the crash reporter reuses.
- `src/core/ConfigManager.cpp:303-315` — `createWorkingDir()` makes those
  subdirectories.
- Where the working directory comes from:
  `ConfigManager.cpp:704-706` (portable mode: next to the binary),
  `:708-714` (installed: `QStandardPaths::writableLocation(DocumentsLocation) +
  "/lmms/"`, with `$HOME/lmms/` as a courtesy fallback),
  `:716-752` (development: only the `.lmmsrc.xml` location is taken from
  `CMakeCache.txt`). An explicit `workingdir` value in `.lmmsrc.xml` wins
  (`ConfigManager.cpp:525` → `setWorkingDir`).

**Conclusion for the design:** the report belongs in the working directory,
because that is the only per-user location the app already owns and already
knows at the moment handlers can be installed.

---

## 2. Design

Files: `include/CrashReporter.h`, `src/core/CrashReporter.cpp`, wired into
`src/core/main.cpp`. Unit test: `tests/src/core/CrashReporterTest.cpp`.

### 2.1 Which signals, and why

| Signal | Why it is handled |
|---|---|
| `SIGSEGV` | Bad pointer / bad buffer — the classic DAW crash (plugin, buffer). `si_addr` carries the faulting address. |
| `SIGBUS` | Misaligned / mapped-memory fault; same family, same `si_addr`. |
| `SIGILL` | Bad instruction — usually a mis-built or corrupted plugin. |
| `SIGFPE` | Integer division by zero, etc. *Not* installed when `LMMS_DEBUG_FPE` is defined, because that debug flag has already claimed the signal with its own backtrace handler (`main.cpp:308-319`); the reporter does not steal it. |
| `SIGABRT` | `abort()`. This is how the C++ runtime delivers an **unhandled exception** (`std::terminate` → `abort`) and how Qt's `qFatal()` dies, so it is the "unhandled exception" case from §1.3. |

Deliberately **not** handled, with reasons:

- `SIGINT`, `SIGTERM` — a shutdown *request*, not a crash. `SIGINT` already has
  a clean-quit path (§1.1); treating it as a crash would turn every Ctrl-C in a
  terminal into a crash report.
- `SIGKILL`, `SIGSTOP` — uncatchable by definition.
- `SIGPIPE` — already `SIG_IGN` at startup (`main.cpp:722`).

Windows: the whole feature is a documented no-op (`CrashReporter.cpp`,
`#else // LMMS_BUILD_WIN32`). A Windows crash reporter is a different
implementation (minidumps, `SetUnhandledExceptionFilter`); shipping a `SIGSEGV`
handler the Windows CRT does not deliver would look like coverage while
providing none. The test asserts the no-op instead of pretending.

### 2.2 Async-signal-safety: the explicit argument

The handler runs **on the failing thread**, which may be the audio thread, with
arbitrary state already corrupt. It therefore calls only entry points on POSIX's
async-signal-safe list:

```
openat(), mkdirat(), write(), close(), sigaction(), sigemptyset(),
sigprocmask(), raise(), getpid(), time()
```

plus `syscall(SYS_gettid)`, a raw kernel entry that allocates nothing, takes no
lock and only writes thread-local `errno`. (This is the same call Breakpad and
Crashpad make from their handlers; it is *not* on the POSIX list, and that is
stated rather than glossed over — the alternative was to drop the thread id.)

There is **no `malloc`, no C++ object construction, no `std::string`, no
`stdio`, no `mutex`, no Qt call and no unbounded loop** on the handler path. The
report is assembled in a fixed 4096-byte stack buffer with hand-written bounded
decimal/hex formatters, then written with at most 8 `write()` calls.

**Why not the "pre-opened fd" ideal?** A pre-opened *report* fd is the textbook
answer, but it would create the report file on every clean run, which this task
forbids (behaviour-preserving: a clean run must leave no crash state behind).
The compromise, which keeps the same syscall-only property, is that **the
directory fd is opened once on the main thread at install time** and the handler
reuses it with `openat()`; the report *file* is created only when a crash
actually happens. For the same reason the `crash-reports/` directory itself is
created **inside the handler** with `mkdirat()`, so a clean run creates no
directory at all.

For the audio-thread rule: the reporter adds nothing to any audio-thread path.
The handler is not a processing callback — it is the last thing that runs on a
thread that is already dying, and it does at most four syscalls before
re-raising. That is the honest reading of the rule, and it is why the design is
write-only-to-one-fd rather than "log a line" via a Qt or stdio path that would
allocate or lock.

### 2.3 Bounds

- `kMaxReportBytes = 4096` is a hard cap enforced by the writer itself: the
  append helpers stop at the cap, so the file cannot exceed it.
- The project-path hint is truncated to `kMaxProjectPathBytes = 512` when
  copied on the main thread, so the cap is only reachable in principle.
- The write loop is capped at 8 `write()` calls; a short write cannot spin.
- Real report size in the end-to-end run: **215–311 bytes**.

### 2.4 Re-entrancy (a crash while handling a crash)

One `volatile sig_atomic_t` guard. A second entry finds the guard set, refuses
to write, and goes straight to re-raise — no recursion, no hang, no extra
syscalls. The signal being handled is also masked for the handler's duration, so
the *same* signal cannot re-enter; a *different* signal can, and the guard
catches it. The handler then always restores the default disposition, unblocks
the signal and re-raises it, so the process **still dies by the signal** exactly
as it would have without the reporter (verified: exit status 139 for SIGSEGV).

### 2.5 Files, lifecycle, and the offer

Everything lives in the user's working directory:

| Path | Written by | Purpose |
|---|---|---|
| `<working dir>/zene-session-open.marker` | `beginSession()`, removed by `endSession()` | Tells the next launch the previous session did not close cleanly. |
| `<working dir>/crash-reports/zene-crash-report.txt` | the signal handler (`mkdirat`+`openat`) | The report. |
| `<working dir>/crash-reports/zene-crash-report.offered` | `acknowledgePendingReport()` | Makes the offer happen once, without deleting the file. |

- `install()` is called in `main.cpp` right after the config is loaded
  (`main.cpp:704`; the base-commit equivalent is `:694`), so the modes that
  continue past startup — GUI, `render`, `--run-script` — are covered. The
  one-shot conversion actions (`upgrade`, `dump`, `compress`, `makebundle`)
  deliberately `return` before that point (base `main.cpp:391-465`) and are not
  covered: they open no session and load no project.
- It is called a second time right after `new GuiApplication()`
  (`main.cpp:844`), because that constructor may have just created the working
  directory after the first-run prompt. `install()` never creates the working
  directory itself, so it cannot answer that prompt on the user's behalf.
- `beginSession()` / `endSession()` bracket the session: the marker is created
  after install and removed after `app->exec()` returns (`main.cpp:1058`), plus
  explicitly on the three early-return/exit paths that happen after it
  (`main.cpp:765`, `main.cpp:962`, `main.cpp:1005`). **A clean exit therefore
  removes the marker and leaves no report and no `crash-reports/` directory** —
  proven in §3.4.
- Next launch: if a report is pending, the GUI shows one modal that gives the
  path and offers *Keep report* (default), *Open folder* and *Discard*; headless
  runs print one line to stderr instead and treat that as the offer. Either way
  nothing is transmitted anywhere.

### 2.6 What the report contains

```
Zene Studio crash report v1
signal=SIGSEGV(11)
fault_addr=0x1
pc=0x55f0…            # instruction pointer from the signal ucontext (x86_64/aarch64)
thread=4242
pid=77
time_unix=1700000000  # seconds since the epoch; no localtime() (unsafe in a handler)
version=0.1.0-alpha
platform=Linux x86_64
compiler=GCC 13.3.0
project=/path/to/the/project.mmp
```

- `fault_addr` comes from `si_addr` and is only meaningful for the synchronous
  signals (SIGSEGV/SIGBUS/SIGILL/SIGFPE); for SIGABRT it is written as `0x0`.
  A `kill`-delivered signal carries the sender's pid/uid in that union, which
  is why a report from an externally-sent SIGSEGV can show a number that is not
  a real faulting address — that is `si_addr` semantics, not a bug, and it is
  why the test asserts on a *real* bad write instead.
- `pc` is only filled on `aarch64` and on `x86_64` when the toolchain exposes
  `REG_RIP`; otherwise `0x0`, never a guess.
- `project` is the "currently open project" hint: set at launch after the load
  (`main.cpp:1039`) and followed on `Song::projectFileNameChanged`
  (`main.cpp:1042`), and also set immediately after `Song::loadProject()` in
  each load path — the argv load (`main.cpp:996`), the render load
  (`main.cpp:760`) and the script load (`main.cpp:814`) — so a crash inside a
  modal dialog raised during the load still names the file.

---

## 3. Proofs

All commands run from the worktree root
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-pa-crash`.
Exit codes are measured unpiped (`cmd > log 2>&1; echo EXIT=$?`).

### 3.1 Build and full test suite

```sh
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4 > build-final2.log 2>&1; echo EXIT=$?
```

```
configure EXIT=0   (log: build/configure.log)
build EXIT=0   (log: build/build.log)
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 26
  deviation: Qt5 development files not found: added -DWANT_QT6=ON (CI's runner installs qtbase5-dev; this box has Qt6 only)
local-ci: overall exit=0 (0 = every executed step passed)
```

The printed deviation is exactly the one `tools/local-ci.sh` documents for this
box. `-DUSE_WERROR=ON` is in effect, so the new code is warning-clean.

Baseline for comparison, on the pristine base commit with the new files not yet
referenced by the build (also `EXIT=0`): `100% tests passed, 0 tests failed out
of 25`. The delta is the one new test executable.

From `build/ctest.log`:

```
17/26 Test  #8: CrashReporterTest ................   Passed    0.64 sec
```

### 3.2 The reporter's own suite — it really is crashed

```sh
cd build/tests && QT_QPA_PLATFORM=offscreen ./CrashReporterTest > log 2>&1; echo EXIT=$?
# EXIT=0
```

```
PASS   : CrashReporterTest::writeReport_writes_expected_fields()
PASS   : CrashReporterTest::bounds_report_cannot_exceed_cap()
PASS   : CrashReporterTest::reentrancy_second_crash_is_refused()
PASS   : CrashReporterTest::child_real_sigsegv_is_reported()
PASS   : CrashReporterTest::child_abort_is_reported()
PASS   : CrashReporterTest::repeated_signal_does_not_hang()
PASS   : CrashReporterTest::clean_run_leaves_no_state()
PASS   : CrashReporterTest::next_launch_detects_and_offers_once()
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 612ms
```

What each one really exercises:

- `child_real_sigsegv_is_reported` forks a child that does
  `*(volatile int*)0x1 = 1` — a **genuine** fault — and asserts the child died
  **by SIGSEGV** (`WIFSIGNALED`) and that the report contains
  `signal=SIGSEGV(11)`, `fault_addr=0x1`, `time_unix=`, `version=<build>`,
  `project=<the path that was set>`, `thread=`, `pid=`.
- `child_abort_is_reported` forks a child that calls `abort()` and asserts the
  same for `signal=SIGABRT(6)` (the unhandled-exception route).
- `bounds_report_cannot_exceed_cap` hands the writer a 4096-character project
  path and maximum-width numeric fields and asserts the file is `<=
  kMaxReportBytes` **and** `> 400` bytes, i.e. that the cap is a real cap rather
  than a coincidence of short input.
- `repeated_signal_does_not_hang` forks a child that raises SIGSEGV twice in a
  row after `beginSession()`, and fails if the child does not exit inside a
  bounded 15-second wait.
- `next_launch_detects_and_offers_once` produces the on-disk state with a real
  crashing child (not by poking at files), then asserts the marker is present,
  `hasPendingReport()` is true, acknowledging stops the offer **without**
  deleting the report, and discarding removes it.

### 3.3 The same thing through the **real product binary** (not the library)

The unit test calls the library directly. These runs drive the built `lmms`
executable through `main.cpp`, with a throwaway `--config` so the user's real
working directory is not touched:

```sh
mkfifo "$T/out.wav"                       # no reader -> the offline render blocks in write()
lmms -c "$T/lmmsrc.xml" render tests/emptyproject.mmp -o "$T/out.wav" &
# wait for "Loading project..." (which is after crashreporter::install()), then:
kill -SEGV $PID ; wait $PID ; echo exit=$?      # exit=139  (128+SIGSEGV)
```

Report written by the real process:

```
Zene Studio crash report v1
signal=SIGSEGV(11)
fault_addr=0x3e800100126
pc=0x76e2cf91b315
thread=1048880
pid=1048880
time_unix=1789159402
version=0.1.0-alpha
platform=Linux x86_64
compiler=GCC 13.3.0
project=/home/…/zene-pa-crash/tests/emptyproject.mmp
```

Next launch, headless, offers the file and does not send it anywhere:

```
1:A crash report from an earlier session is pending: /tmp/…/crash-reports/zene-crash-report.txt
```

and after that clean run:

```
marker after a clean run: ABSENT
-rw------- zene-crash-report.offered
-rw-r--r-- zene-crash-report.txt
```

A GUI run (`QT_QPA_PLATFORM=offscreen lmms -c … tests/emptyproject.mmp`) was
also SIGSEGV'd and died 139 with a report written; see the caveat in §4.

### 3.4 Negative control, and the proof that it is load-bearing

`clean_run_leaves_no_state` opens a session, closes it the way `main.cpp` does,
and asserts: **no report file, no session marker, not pending, and the
`crash-reports/` directory was never even created.** Before the close it also
asserts the marker *does* exist, so the test cannot pass by being vacuous.

The check is load-bearing — inverting both assertions makes the test fail:

```sh
# invert the two negative-control assertions, rebuild only that test, run it
test EXIT=1
FAIL!  : CrashReporterTest::clean_run_leaves_no_state() 'QFileInfo::exists(dir.filePath(…kSessionMarkerName…))' returned FALSE. (INVERTED CHECK (must fail): a clean exit must clear the session marker)
   Loc: [tests/src/core/CrashReporterTest.cpp(348)]
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted, 761ms

# restored
test EXIT=0
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 779ms
```

### 3.5 Gate registration

- `src/core/CrashReporter.cpp` → `src/core/CMakeLists.txt`.
- `src/core/CrashReporterTest.cpp` → `tests/CMakeLists.txt` (`LMMS_TESTS`), so
  ctest, the no-tautology gate and the coverage gate all see it.
- `include/CrashReporter.h` + `src/core/CrashReporter.cpp` →
  `tests/fork-sources.txt` (fork-new scope).
- All three → `tests/all-sources.txt` (whole-tree scope). Regenerating with the
  documented command produces exactly those three additions; the file's header
  comment block is preserved.

No third-party dependency, so there is nothing to pin and nothing to licence
check. No new CMake option: the feature is always on where it is meaningful
(POSIX) and a documented no-op where it is not (Windows).

---

## 4. What is NOT proven / limitations

Stated plainly, because a crash reporter that oversells itself is worse than
none:

1. **Windows is not implemented.** The test asserts the documented no-op.
   Nothing about this feature is exercised on Windows, macOS or Haiku builds.
2. **The true nesting of one handler inside another is simulated, not
   provoked.** Provoking it deterministically would need a fault-injection hook
   in the product, which is not worth shipping. The guard itself is proven
   through the documented test seam (`setReentrancyGuardForTest`), and the
   "second signal must not hang" case is proven end to end with a real child.
   What is *not* covered is a genuine fault occurring inside the handler body.
3. **A stack-overflow SIGSEGV is only covered on the thread that installed the
   handlers.** `sigaltstack()` is a per-thread attribute (POSIX), and the
   reporter installs a 32 KiB alternate stack from the main thread, so only the
   main thread benefits. A stack overflow on another thread would still hang
   the process in the fault loop; this is not tested and not claimed.
4. **`pc` is architecture-dependent.** It is read from the signal `ucontext` on
   `aarch64` and on `x86_64` when `REG_RIP` is visible to the toolchain;
   elsewhere it is `0x0`. Only `x86_64` was exercised here.
5. **`fault_addr` is meaningless for a `kill`-delivered signal** (it is the
   sender's pid/uid in the `si_addr` union) and for SIGABRT. The report prints
   what the kernel gives; it does not invent an address.
6. **The report is a single file, overwritten by the next crash.** There is no
   history and no rotation. A second crash before the user reads the first
   overwrites it. This is a deliberate scope decision, not an oversight.
7. **No core dump.** The reporter does not change the core-pattern behaviour of
   the system; a core dump is produced (or not) exactly as before.
8. **In one GUI run on this box, `project=` read `(none)`.** The GUI process was
   alive, had definitely reached its event loop (SIGINT quit it cleanly and
   `endSession()` cleared the marker), but the report's project field was empty.
   The same field is populated in the headless path (§3.3) and in the unit test,
   and the hint is set immediately after `Song::loadProject()` in every load
   path, so the likely cause is that this audio-less machine failed the GUI
   project load (`ALSA: Couldn't open audio device: Host is down` in the run
   log) and `Song::loadProject` reverted the name on the failure path
   (`Song.cpp:1053-1059`). **This is an unexplained observation, not a proven
   cause**, and it is recorded here rather than smoothed over.
9. **`systemd-coredump`/AppImage sandboxing, `prctl(PR_SET_DUMPABLE)`, seccomp
   filters and hardened kernels** can prevent a handler from writing. Not
   tested; there is no fallback, and the reporter fails silently by design
   (it must never be a source of crashes).

## 5. Where the change lives

| File | What |
|---|---|
| `include/CrashReporter.h` | The API, and the design argument in comments (signals, safety, bounds, re-entrancy). |
| `src/core/CrashReporter.cpp` | The handler + the bounded writer + the main-thread bookkeeping. |
| `src/core/main.cpp` | Install/begin/end session hooks, the headless offer, the GUI offer dialog, the project-path hint. |
| `tests/src/core/CrashReporterTest.cpp` | The proofs in §3.2, §3.4. |
| `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` | Registration. |
