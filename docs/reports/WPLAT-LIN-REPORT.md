# WPLAT-LIN — the three platform-dependent test failures, each fixed at its own cause

**Lane:** platform-delta (LINUX), branch **`030/wplat-lin`** in `zene-030/wplat-lin` (worktree of
`lmms/.git`). **Base:** `eba78f8ff`. **Tip:** `c9b980d36` (the fixes) + this report. **Date:**
2026-09-17. **Nothing was pushed** — `origin` is LMMS/lmms, `product` is the fork, and this lane
has no remote branch.

Three failures from the linux-x86_64 CI job (run **35126160372**, job **104895806780**). Two were
test defects against the harness (Qt platform, Qt signal handler); one was an assertion that could
only ever pass by allocator coincidence. Each is fixed and proved locally; **no product source
changed in any of the three commits.**

| # | item | CI symptom | after (local) | commit |
|---|---|---|---|---|
| 1 | `ClipLinkTest` (#16), `ClipLinkPersistenceTest` (#17), `ImportDetectionTest` (#57) | "Subprocess aborted" ~0.2 s in, before the first line of output | 3/3 green headless (`CTEST_EXIT=0`) | `87475243a` |
| 2 | `ControlShutdownHookTest::aHookSurvivesTheRegistryInstanceItWasRegisteredOn` | `FAIL! … 'recreated != registry' returned FALSE` at `ControlShutdownHookTest.cpp(86)` | `EXIT=0`, `Totals: 6 passed, 0 failed` | `272c9c42c` |
| 3 | `SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe` | `FAIL! … 'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV' returned FALSE` at `SafeStartTest.cpp(186)` | `EXIT=0`, `Totals: 9 passed, 0 failed` | `f387a2a57` |

Item numbering follows `RESUME-NOTES.md`; commit order on the branch is `87475243a`, `f387a2a57`,
`272c9c42c`.

---

## 1 · the trio — a guiless test is not a guiless binary

**Root cause.** `ClipLinkTest`, `ClipLinkPersistenceTest` and `ImportDetectionTest` each end in
`QTEST_MAIN`, and `QTEST_MAIN` expands to `QTEST_MAIN_SETUP()` → `QApplication app(argc, argv)`
whenever `QT_WIDGETS_LIB` is defined (QtTest `qtest.h`) — which it is for every test target in
`tests/CMakeLists.txt`, because they all link `${QT_LIBRARIES}` (Qt::Widgets, and Qt puts
`QT_WIDGETS_LIB` on the target's usage requirements). A `QApplication` constructor with no display
is a `qFatal`:

```
qt.qpa.xcb: could not connect to display
qt.qpa.plugin: Could not load the Qt platform plugin "xcb" in "" even though it was found.
This application failed to start because no Qt platform plugin could be initialized.
```

These three were the only tests that construct an Engine / a control surface and did **not**
register the headless platform their ~85 siblings register. The old comment above the slot claimed
`ImportDetectionTest` "needs no Qt platform plugin and deliberately gets no `set_tests_properties`
line here" — true of the TEST (no Engine, no window, no widget), false of the BINARY.

**Fix (`87475243a`, `tests/CMakeLists.txt` + `tests/upstream-modifications.txt`).** Both the test
registration and the comment are re-derived: the three tests get the
`ENVIRONMENT "QT_QPA_PLATFORM=offscreen"` property their siblings use, and the stale comment says
what is now true. `QT_QPA_PLATFORM` is also the variable `lmms::isUnattendedRun()` reads, so the
registration is what puts a runner into the headless state the product is told about. The ledger
reason for `tests/CMakeLists.txt` is carried in `tests/upstream-modifications.txt` in the same
commit. Registration and comments only — no test's behaviour changes.

**Evidence.**

- CI: #16/#17/#57 "Subprocess aborted" after ~0.2 s, before their first line of test output — and
  the SAME job's "Signal-death backtrace" step, which runs the same binaries with
  `QT_QPA_PLATFORM=offscreen`, passed them (`ClipLinkTest` `Totals: 8 passed, 0 failed`).
- Local red (Qt6 box, `DISPLAY` removed — the runner's condition, before the fix):

  ```
  env -u DISPLAY ./build/tests/ClipLinkTest        -> EXIT=134, the identical qt.qpa.xcb text
  env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest"
      (from build/tests)                           -> CTEST_EXIT=8
        "0% tests passed, 3 tests failed out of 3"  (#16/#17/#57 all "(Subprocess aborted)")
  ```

  With `DISPLAY=:0` (the dev box's own configuration) it was green — which is why this was
  CI-only.
- Local green, after the fix:

  ```
  env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest"
      -> CTEST_EXIT=0, "100% tests passed, 0 tests failed out of 3"
  ctest -R ... (with DISPLAY=:0)
      -> CTEST_EXIT=0, "100% tests passed, 0 tests failed out of 3"
  direct, headless, with the environment the registration now sets:
      ClipLinkTest              EXIT=0 (8 PASS / 0 FAIL)
      ClipLinkPersistenceTest   EXIT=0 (4 PASS / 0 FAIL)
      ImportDetectionTest       EXIT=0 (7 PASS / 0 FAIL)
  ```

**Residual.** The binaries themselves are untouched, so a caller who runs them directly (outside
ctest) still has to set `QT_QPA_PLATFORM=offscreen`; only the ctest registration sets it. CI
confirmation of this fix belongs to the next linux-x86_64 run — it is proved locally on the
runner's own condition, not yet on the runner.

## 2 · `ControlShutdownHookTest` — a pointer is not an identity witness

**Root cause.** The slot proved "`instance()` built a NEW registry" by comparing addresses —
`QVERIFY2(recreated != registry, ...)`. A pointer is not an identity witness across a `delete`/`new`
pair: the allocator may hand the new object the bytes the destroyed one just released, and on the
runner it did. The product is fine — the store the hooks live in is a function-local static
(`shutdownHooks()` in `src/core/ControlRegistry.cpp`), so the hook really does survive the
instance, and `delete s_instance; s_instance = nullptr;` in `destroy()` is intact. The old assertion
could only ever be right by coincidence.

**Fix (`272c9c42c`, `tests/src/core/ControlShutdownHookTest.cpp`, +18/−1).** The destroyed instance
gets a marker in its own state (`QObject::objectName`; nothing in the product names the registry),
the marker is asserted observable at all, and the re-created instance must NOT carry it — a rebuilt
object cannot carry the destroyed one's state whatever address it lands on; an `instance()` that did
not destroy-and-rebuild cannot lose it. No product source changed and no ledger entry is owed.

**Evidence.**

- CI: `FAIL! : … 'recreated != registry' returned FALSE. (destroy() did not build a new instance
  for instance())  Loc: tests/src/core/ControlShutdownHookTest.cpp(86)` — while the same 0.03 s run
  passed `theOwnerTakesItsHookWithItWhenItDies`, `removalIsTargetedAndTheStoreIsDrainedOnce` and
  `theShutdownPathUnlinksALiveServersSocket`, all of which use the same statics.
- Measured here (Qt6, same source): `sizeof(lmms::ControlRegistry) = 128` bytes (gdb, the built
  binary); a temporary probe in the slot printed `registry=0x5b1f5a478c60`,
  `recreated=0x5b1f5a521ba0` — different, so this box stayed green by luck. An `LD_PRELOAD`
  malloc/free tracer of that size class showed the same for its run: the registry chunk is freed
  once (`free(0x61478d368c60)`) and the next `malloc(128)` is `0x61478d42ba10`.
- Reuse is the *normal* outcome, not an exotic one: a minimal program with the same size class
  prints `first=0x6037e6b282b0 second=0x6037e6b282b0 same-address=YES`.
- Proof the new assertion is not vacuous (red): with a temporary `destroy()` that deletes nothing
  (reverted, never committed) → `FAIL! … 'recreated->objectName() != marker' returned FALSE … Loc:
  [tests/src/core/ControlShutdownHookTest.cpp(101)] Totals: 2 passed, 1 failed`.
- Green, final binaries: `env -u DISPLAY ./build/tests/ControlShutdownHookTest` → `EXIT=0`,
  `Totals: 6 passed, 0 failed`; `env -u DISPLAY ctest -R "ControlShutdownHookTest|SafeStartTest"`
  (from `build/tests`) → `CTEST_EXIT=0`, 2/2.

**Residual.** This box does not reproduce the runner's allocator reuse, so the local green is
address-luck too — but the assertion no longer depends on the address, which is the point of the
change. The reuse itself is proved normal on this box by the control program above, not by
reproducing the CI failure.

## 3 · `SafeStartTest` — the crashing child faults with the kernel's disposition

**Root cause.** The child that must die by SIGSEGV is a `fork()` of the test process, so it
**inherits** the SIGSEGV handler QtTest installed in `qExec` — and Qt **5.15.3's handler is not a
re-raiser**: it dumps a stack through gdb, reports "Received a fatal error." through QTestResult,
prints the run's totals and ends the process on its own path. The waiter therefore sees an
abort/exit status, not death-by-signal, and the assertion that names SIGSEGV fails. The runner's Qt
is 5.15.3; the dev box (Qt 6.4.2, whose handler re-raises — measured: the child comes back signaled
11) never saw it.

**Fix (`f387a2a57`, `tests/src/core/SafeStartTest.cpp`, +18).** In the child,
`::signal(SIGSEGV, SIG_DFL);` before faulting, so what dies is the product's crash and not the
harness's handler — the assertion's own words are "as it would without any of this". The SIGKILL
slot needs nothing (SIGKILL is uncatchable by construction); `CrashReporterTest`'s forked children
are unaffected because they install the product's own handler, which restores `SIG_DFL` and
re-raises.

**Evidence.**

- CI: `QFATAL : SafeStartTest::realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe() Received signal
  11 … Function time: 3ms` plus that child's own QtTest stack dump (`=== Received signal at function
  time: 3ms … dumping stack ===` → gdb attach → `=== End of stack trace ===`) in the same output,
  then `FAIL! : … 'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV' returned FALSE … Loc:
  [.../tests/src/core/SafeStartTest.cpp(186)]`.
- Local red, real binary (Qt6 box; an `LD_PRELOAD` installs a Qt5-shaped handler in the child at the
  `fork()` return point, i.e. in the same order relative to the test's own code):

  ```
  LD_PRELOAD=~/.cache/wplat-lin/forkhook.so QT_QPA_PLATFORM=offscreen \
      ./SafeStartTest realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe
  -> EXIT=1, "FAIL! : … 'WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV' returned FALSE …
     Loc: [tests/src/core/SafeStartTest.cpp(186)]"
  ```

  The unfixed binary is kept at `~/.cache/wplat-lin/SafeStartTest.unfixed`
  (md5 `98138c99640c58626bc70008aa2c985c`).
- Local red/green against the job's OWN QtTest — ubuntu 22.04 `libqt5test5 5.15.3+dfsg-2ubuntu0.2`
  + `libqt5core5a` + `qtbase5-dev` + `qtbase5-dev-tools` + `libicu70`, unpacked into
  `~/.cache/wplat-lin/qt5` (source `~/.cache/wplat-lin/ft5/forktest.cpp`):

  ```
  fork + child null-deref, no reset    -> PARENT-VERDICT: signaled=1 termsig=6  -> FAIL (EXIT=1)
  fork + child resets SIG_DFL first    -> PARENT-VERDICT: signaled=1 termsig=11 -> 3 passed, 0 failed (EXIT=0)
  ```

- Green, final binaries: under the same fork-hook → `EXIT=0`, 3 passed; whole binary headless →
  `EXIT=0`, `Totals: 9 passed, 0 failed`; `env -u DISPLAY ctest -R "SafeStartTest|ControlShutdownHookTest"`
  → `CTEST_EXIT=0`, 2/2.

**Residual.** The fix is proved on Qt6 and against the job's own Qt5.15.3 **in isolation**, not on a
Qt5 build of the whole test binary — this box has no Qt5 (`WANT_QT6=ON` is the only local
configuration).

---

## Lane gates (after the last commit, all green)

All run unpiped, all zero, as recorded in `RESUME-NOTES.md`:

```
bash tests/complexity-gate.sh --check      -> COMPLEXITY_EXIT=0   (PASS, baseline not written)
bash tests/file-length-gate.sh --check     -> FILELENGTH_EXIT=0   (PASS)
python3 tests/scripted/check-namespace     -> NAMESPACE_EXIT=0    (0 errors)
bash tests/fork-sources-gate.sh            -> FORKSOURCES_EXIT=0   (660 fork / 1104 all-sources / 40 tools / 0 stale)
bash tests/no-upstream-regression-gate.sh  -> UPMOD_EXIT=0        (422 changed paths declared; ledger 460)
bash tests/all-sources-reproduce.sh        -> ALLSOURCES_EXIT=0   (REPRODUCES)
```

## Re-verified while writing this report (2026-09-17, tip `c9b980d36`)

The five built targets were re-run on this worktree — not a merge tip — from `build/tests`:

```
env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest|ControlShutdownHookTest|SafeStartTest"
-> CTEST_EXIT=0, "100% tests passed, 0 tests failed out of 5" (4.65 s)
   #16 ClipLinkTest Passed 1.54s · #17 ClipLinkPersistenceTest Passed 1.47s ·
   #39 ControlShutdownHookTest Passed 0.06s · #48 SafeStartTest Passed 1.24s ·
   #57 ImportDetectionTest Passed 0.32s
```

## Residuals and what is owed to the merge lane

- **(a)** No full-suite `ctest -j4` run was made in this lane — only the five targets were built.
  The lane's proof is the five targets plus the gates above, not the 213-test suite.
- **(b)** Item 2's local green is address-luck (see its residual): the assertion no longer depends
  on it, but this box does not reproduce the runner's allocator reuse.
- **(c)** Item 3 is proved on Qt6 and on the job's own Qt5.15.3 in isolation, not on a Qt5 build of
  the whole binary.
- **Owed to the merge lane:** the five-target sanity re-run on the merge tip —

  ```
  cd build/tests && env -u DISPLAY ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest|ControlShutdownHookTest|SafeStartTest"
  ```

  expect 5/5, `CTEST_EXIT=0`. The run recorded above is on this lane's tip, not on a merge tip.

**Where the numbers come from.** The three commit messages (`87475243a`, `f387a2a57`, `272c9c42c`)
and `RESUME-NOTES.md` on this branch; the lane's raw logs were written under `/tmp/wplat-lin/` and
are not present on this box any more, so the durable receipt is the commits and the notes. Build
tree `build/` (fresh Qt6 RelWithDebInfo, the five targets) is left in place in the worktree.
