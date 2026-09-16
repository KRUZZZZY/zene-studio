# RESUME NOTES — PLATFORM-DELTA lane (LINUX) · 030/wplat-lin · 2026-09-16

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030/wplat-lin`
(branch `030/wplat-lin`, base `eba78f8ff`). **Nothing was pushed.** Build tree `build/` exists here
(fresh Qt6 RelWithDebInfo, 5 targets: `ClipLinkTest ClipLinkPersistenceTest ImportDetectionTest
ControlShutdownHookTest SafeStartTest`). All three items are **fix committed + proved locally**;
the only unfinished deliverable is `docs/reports/WPLAT-LIN-REPORT.md` (not written — wind-down).

## Commits on this branch

| SHA | item |
|---|---|
| `87475243a` | the ClipLink/ImportDetection trio registers the offscreen Qt platform |
| `f387a2a57` | SafeStartTest's crashing child faults with the kernel's disposition |
| `272c9c42c` | ControlShutdownHookTest observes a re-created registry by state, not by address |

## Item 1 — ClipLinkTest (#16) / ClipLinkPersistenceTest (#17) / ImportDetectionTest (#57) · FIX COMMITTED `87475243a`

- **Root cause.** The three test BINARIES build a `QApplication`: each ends in `QTEST_MAIN`, and
  `QTEST_MAIN` expands to `QTEST_MAIN_SETUP()` → `QApplication app(argc, argv)` whenever
  `QT_WIDGETS_LIB` is defined (QtTest `qtest.h`) — which it is for every target in
  `tests/CMakeLists.txt` because they all link `${QT_LIBRARIES}` (Qt::Widgets). A `QApplication`
  with no display is a `qFatal` in its constructor. These three were the only Engine/control-surface
  tests not registering the headless platform ~85 siblings register.
- **CI evidence.** run 35126160372, job 104895806780: #16/#17/#57 "Subprocess aborted" after ~0.2 s,
  before their first line of output, with the `qt.qpa.xcb`/`no Qt platform plugin` text — while the
  same job's own backtrace step ran the same binaries with `QT_QPA_PLATFORM=offscreen` and passed
  them (ClipLinkTest `Totals: 8 passed, 0 failed`).
- **Local red (before the fix, Qt6 box).** `env -u DISPLAY ./build/tests/ClipLinkTest` → `EXIT=134`
  with the identical text; `env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest"`
  (from `build/tests`) → `CTEST_EXIT=8`, "0% tests passed, 3 tests failed out of 3", all
  `(Subprocess aborted)`. With `DISPLAY=:0` (the dev box) it was green — that is why it was CI-only.
- **Local green (after).** `env -u DISPLAY ctest -R <those three>` → `CTEST_EXIT=0`, 100% passed 3/3;
  with `DISPLAY=:0` → `CTEST_EXIT=0` 3/3; direct headless with the env the registration sets:
  ClipLinkTest `EXIT=0` (8 PASS/0 FAIL), ClipLinkPersistenceTest `EXIT=0` (4/0), ImportDetectionTest
  `EXIT=0` (7/0).
- **Files.** `tests/CMakeLists.txt` (the `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen")`
  block + re-derived the stale ImportDetectionTest comment), `tests/upstream-modifications.txt`
  (the `tests/CMakeLists.txt` ledger reason, same commit). Logs: `/tmp/wplat-lin/ctest-trio-{before,after,withdisplay}.log`,
  `/tmp/wplat-lin/*.headless.log`.

## Item 2 — ControlShutdownHookTest::aHookSurvivesTheRegistryInstanceItWasRegisteredOn · FIX COMMITTED `272c9c42c`

- **Root cause.** The slot proved "instance() built a NEW registry" by comparing addresses
  (`recreated != registry`). A pointer is not an identity witness across delete/new: the allocator may
  hand the new object the freed bytes, and on the runner it did. Product is fine — the hook store is a
  function-local static (`shutdownHooks()` in `src/core/ControlRegistry.cpp`), so the hook does survive;
  `delete s_instance; s_instance = nullptr;` in `destroy()` is intact.
- **CI evidence.** run 35126160372: `FAIL! … 'recreated != registry' returned FALSE. (destroy() did not
  build a new instance for instance()) Loc: tests/src/core/ControlShutdownHookTest.cpp(86)`, while the
  other four slots of the same 0.03 s run passed on the same statics.
- **Measured here.** `sizeof(lmms::ControlRegistry)=128`; a temporary probe printed
  `registry=0x5b1f5a478c60 recreated=0x5b1f5a521ba0` (different → green here). An `LD_PRELOAD`
  malloc/free tracer of that size class (source `~/.cache/wplat-lin/mtrace.c`) shows the freed chunk is
  not handed back: next `malloc(128)` = `0x61478d42ba10`. Reuse is nevertheless the *normal* outcome of
  the plain pattern: `~/.cache/wplat-lin/alloc-reuse.cpp` prints `first=0x…82b0 second=0x…82b0
  same-address=YES`.
- **Fix.** The destroyed instance gets a marker in its own state (`QObject::objectName`, nothing in the
  product names the registry); the marker is asserted observable at all, and the re-created instance
  must not carry it. No product source changed.
- **Proof it is not vacuous (red).** With a temporary `destroy()` that deletes nothing (reverted, never
  committed): `FAIL! … 'recreated->objectName() != marker' returned FALSE … Loc: […ControlShutdownHookTest.cpp(101)]
  Totals: 2 passed, 1 failed`.
- **Green (final binaries).** `env -u DISPLAY ./build/tests/ControlShutdownHookTest` → `EXIT=0`,
  `Totals: 6 passed, 0 failed`; `env -u DISPLAY ctest -R "ControlShutdownHookTest|SafeStartTest"` →
  `CTEST_EXIT=0`, 2/2.

## Item 3 — SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe (SIGSEGV) · FIX COMMITTED `f387a2a57`

- **Root cause.** The crashing child is a `fork()`, so it inherits the SIGSEGV handler QtTest installed
  in `qExec` — and **Qt 5.15.3's handler is not a re-raiser**: it dumps a stack through gdb, reports
  "Received a fatal error." through QTestResult and ends the process on its own path, so the waiter sees
  an abort/exit status and the `WTERMSIG(status) == SIGSEGV` assertion fails. Qt 6.4.2 re-raises
  (child comes back signaled 11) — which is why this box was green.
- **CI evidence.** run 35126160372: `QFATAL : … Received signal 11 … Function time: 3ms` plus that
  child's own QtTest dump (`=== Received signal at function time: 3ms … dumping stack ===` → gdb attach →
  `=== End of stack trace ===`) and then `FAIL! … 'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV'
  returned FALSE … Loc: […/tests/src/core/SafeStartTest.cpp(186)]`.
- **Local red, real binary.** `LD_PRELOAD=~/.cache/wplat-lin/forkhook.so QT_QPA_PLATFORM=offscreen
  ./SafeStartTest realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe` (the preload installs a
  Qt5-shaped handler in the child at the `fork()` return point) → `EXIT=1`, the same failure at
  `SafeStartTest.cpp(186)`. Unfixed binary kept at `~/.cache/wplat-lin/SafeStartTest.unfixed`
  (md5 `98138c99640c58626bc70008aa2c985c`).
- **Local red/green against the job's OWN QtTest** (ubuntu 22.04 `libqt5test5 5.15.3+dfsg-2ubuntu0.2`
  + `libqt5core5a` + `qtbase5-dev` + `qtbase5-dev-tools` + `libicu70` unpacked into
  `~/.cache/wplat-lin/qt5`, source `~/.cache/wplat-lin/ft5/forktest.cpp`):
  fork + child null-deref **without** the reset → `PARENT-VERDICT: signaled=1 termsig=6` → FAIL `EXIT=1`;
  **with** `signal(SIGSEGV, SIG_DFL)` in the child → `signaled=1 termsig=11` → 3 passed, 0 failed `EXIT=0`.
- **Fix.** In the child, `::signal(SIGSEGV, SIG_DFL);` before faulting (the assertion's own words: "as it
  would without any of this"). The SIGKILL slot needs nothing; `CrashReporterTest`'s children are
  unaffected because they install the product's handler, which restores SIG_DFL and re-raises.
- **Green (final binaries).** under the same fork-hook → `EXIT=0`, 3 passed; whole binary headless →
  `EXIT=0`, `Totals: 9 passed, 0 failed`; `env -u DISPLAY ctest -R "SafeStartTest|ControlShutdownHookTest"`
  → `CTEST_EXIT=0`.

## Gates (all run after the last commit, unpiped) — ALL GREEN

```
bash tests/complexity-gate.sh --check      -> COMPLEXITY_EXIT=0   (PASS, baseline not written)
bash tests/file-length-gate.sh --check     -> FILELENGTH_EXIT=0   (PASS)
python3 tests/scripted/check-namespace     -> NAMESPACE_EXIT=0    (0 errors)
bash tests/fork-sources-gate.sh            -> FORKSOURCES_EXIT=0  (660 fork / 1104 all-sources / 40 tools / 0 stale)
bash tests/no-upstream-regression-gate.sh  -> UPMOD_EXIT=0        (422 changed paths declared; ledger 460)
bash tests/all-sources-reproduce.sh        -> ALLSOURCES_EXIT=0   (REPRODUCES)
```
Logs in `/tmp/wplat-lin/gate-*.log`. Note: `include/ControlRegistry.h` is at **509** lines and
`tests/CMakeLists.txt` (~2900) — do not append to the capped header (LANE-BRIEF §5).

## NEXT ACTION (whoever picks this up)

1. Write `docs/reports/WPLAT-LIN-REPORT.md` from the three commit messages above (they already carry
   root cause + evidence + residual per item) and commit it.
2. Sanity re-run on a merge tip: `cd build/tests && env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest|ControlShutdownHookTest|SafeStartTest"` — expect 5/5, `CTEST_EXIT=0`.
3. Free the disk when done: delete this worktree's `build/` (~6 GB; 122 G free at handover).
4. Residuals to name in the report if it is written by someone else: (a) no full-suite `ctest -j4`
   run was made in this lane (only the five targets were built); (b) item 2's local green is
   address-luck — the assertion no longer depends on it, but this box does not reproduce the
   runner's reuse; (c) item 3 is proved on Qt6 + on the job's own Qt5.15.3 in isolation, not on a
   Qt5 build of the whole test binary (no Qt5 on this box: `WANT_QT6=ON` is the only local config).
