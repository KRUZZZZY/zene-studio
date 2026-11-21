FIXED — cluster ci-1e541b5d1279ed2d (CI regression: build / macos-arm64)

## Verdict and classification

**FIXED**, classified **test** (the failure is the crash-reporter ARM/DISARM test's own
compilation; no product runtime defect is established by the evidence, and none exists in
`src/core/CrashReporter.cpp`).

Reproduced on the exact pinned commit `40b4322860e9c2133f412a4b921b63b057dd710c` (this
worktree's `HEAD`). The hosted log at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/40b4322860e9c2133f412a4b921b63b057dd710c/run-43569206372-job-132369662860.log`
shows (lines 5952, 5958, 5965):

```
tests/src/core/CrashReporterArmTest.cpp:81:16: error: exception specification in declaration does not match previous declaration
tests/src/core/CrashReporterArmTest.cpp:198:13: error: expected unqualified-id
make[2]: *** [tests/CMakeFiles/CrashReporterArmTest.dir/src/core/CrashReporterArmTest.cpp.o] Error 1
```

## Cause (file:line, pre-fix)

- `tests/src/core/CrashReporterArmTest.cpp:81-82` — the test interposes `sigaction()` to
  inject one failed installation. It redeclared the function `noexcept`. glibc declares
  `sigaction` with `__THROW`, which is `noexcept (true)` in C++ (host
  `/usr/include/signal.h:243-244`), but Apple's libc declares it with **no** exception
  specification. C++17 makes the two declarations incompatible, which is the macos-arm64
  diagnostic. Dropping `noexcept` outright is not equivalent on Linux either: GCC accepts
  the mismatch only because the diagnostic for a system header is suppressed, and it
  becomes an error under `-Wsystem-headers` (measured, `$TMPDIR/darwinsim`). Deriving the
  specification from the library's own declaration keeps the Linux function exactly as it
  was instead of relying on that suppression.
- `tests/src/core/CrashReporterArmTest.cpp:198` — `::sigemptyset(...)`. On Apple
  `sigemptyset` is a function-like **macro**, so the leading `::` cannot parse ("expected
  unqualified-id"). On glibc it is a function, so the host build accepted it.
- `tests/src/core/CrashReporterTest.cpp:548` — the same `::sigemptyset` spelling in the
  sibling test, which the hosted job had not compiled yet when it stopped (its autogen
  target is in the log, its source is not), so CI had not reported it; leaving it would
  have made the next build red.
- `src/core/CrashReporter.cpp` has **no** defect: it calls `sigemptyset`, `sigaction` and
  `sigaltstack` in the unqualified, portable spelling (`:148`, `:152`, `:293`, `:328` for
  `sigemptyset`; `:149`, `:300`, `:304`, `:334`, `:355` for `sigaction`; `:275` for
  `sigaltstack`) and never redeclares them.

## Fix (all inside `tests/`)

1. `tests/src/core/CrashReporterArmTest.cpp:81-87` — the interposer's exception
   specification is now `noexcept(noexcept(::sigaction(0, nullptr, nullptr)))`, which is
   `noexcept(true)` on glibc and `noexcept(false)` on Apple, matching either declaration.
   Behaviour on Linux is byte-for-byte the same function (still `noexcept`); nothing about
   the injected failure, the signal set, rollback, arming/disarming or the real-signal
   cases changed.
2. `tests/src/core/CrashReporterArmTest.cpp:203-205` and
   `tests/src/core/CrashReporterTest.cpp:548-550` — `::sigemptyset` → `sigemptyset`; the
   macro on Apple expands to the same `(*(set) = 0, 0)` the function returns on glibc, so
   the assertion still measures the same thing (`QVERIFY(sigemptyset(...) == 0)`).
3. `tests/crash-reporter-apple-signal-check.py` (new) — the deterministic regression,
   registered as ctest **CrashReporterAppleSignalCheck** (`tests/CMakeLists.txt:6646-6677`,
   the registration itself at `:6664-6671`, the missing-driver message at `:6673-6676`).
   It compiles the two **real** translation units with their own recorded flags from
   `build/compile_commands.json` under a model of Apple's `<signal.h>`:
   a shadow `<sys/cdefs.h>` that empties `__THROW` (written only where the host declaration
   is actually `noexcept`) and a `sigemptyset` function-like macro. Two fixtures prove the
   model discriminates: the pre-fix shape (`noexcept` + `::sigemptyset`) must be rejected,
   the fixed shape must compile. macOS runs it natively; every other host runs the same
   model, so the regression is not macOS-only. It needs a Clang-family driver because GCC
   accepts the exception-specification mismatch — the host GCC build is the reason the
   defect was invisible locally.

Registration: the new script and the modified test files are declared in
`tests/fork-sources.txt` and `tests/upstream-modifications.txt`; the ctest is registered in
`tests/CMakeLists.txt`.

## Evidence table (exact commands and exit codes)

The mandated log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-1e541b5d1279ed2d.r1/logs`
rejects writes from this process with `Permission denied` (`touch` → exit 1, and a sandbox
`EACCES` on file creation), so every log below was written to the worktree-internal,
gitignored `build/qwen-fix-logs/ci-1e541b5d1279ed2d/` and mirrored in `$TMPDIR`.
The paths are copy-pasted from the commands actually run.

| Step | Command | Exit | Log |
|---|---|---|---|
| Pre-fix build of the affected targets (host GCC; compiles, as the finding predicts for non-Apple) | `cmake --build build --target CrashReporterArmTest CrashReporterTest -j8` | 0 | `build-01-prefix.log` |
| **RED** regression, probe run directly against unfixed sources | `python3 tests/crash-reporter-apple-signal-check.py --build-dir build --root . --cxx /usr/bin/clang++` | **1** (3/5: both real TUs fail with the hosted diagnostics) | `probe-05-red-refactor.log` |
| Undo the fix (`git apply -R` of the test hunks) and rebuild | `git apply -R $TMPDIR/zene-logs-ci-1e541b5d1279ed2d/fix.patch`; `cmake --build build --target CrashReporterArmTest CrashReporterTest -j8` | 0; 0 | `fix.patch`, `build-07-reverted-host.log` |
| Why the hosted model is required: host GCC builds and passes both tests **without** the fix | `ctest -R '^(CrashReporterArmTest\|CrashReporterTest)$' --output-on-failure` (from `build/tests`) | 0 (2/2) | `test-04-host-blind.log` |
| **RED** ctest of the new regression with the fix reverted | `ctest -R 'CrashReporterAppleSignalCheck' --output-on-failure` (from `build/tests`) | **8** (1 test failed, 3/5 checks) | `test-07-red-current.log` |
| Restore the fix | `git apply $TMPDIR/zene-logs-ci-1e541b5d1279ed2d/fix.patch` | 0 | — |
| Rebuild the affected targets | `cmake --build build --target CrashReporterArmTest CrashReporterTest -j8` | 0 | `build-08-restored.log` |
| **GREEN** crash-reporter family | `ctest -R 'CrashReporter' --output-on-failure` (from `build/tests`) | **0** (4/4: CrashReporterArmTest, CrashReporterTest, CrashReporterAppleSignalCheck, ControlCrashReporter) | `test-09-after-docstring.log` |
| Wider family, after building the siblings the tree had not built yet | `cmake --build build --target SafeStartTest CrashReportMenuTest -j8`; `ctest -R '^(CrashReporterArmTest\|CrashReporterTest\|CrashReporterAppleSignalCheck\|SafeStartTest\|CrashReportMenuTest)$' --output-on-failure` | 0; **0** (5/5) | `build-09-siblings.log`, `test-11-rebuilt-siblings.log` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** — all eight rows PASS | `gates-11-final.log` |
| Registration gate | `bash tests/unregistered-tests-gate.sh` | 0 | `gate-unregistered.log` |
| Evidence gate | `bash tests/evidence-gate.sh` | 0 | `gate-evidence.log` |
| Why the specification is derived rather than dropped: a plain redeclaration warns here and fails with `-Werror` (the derived one is clean) | `g++ -std=c++20 -Wsystem-headers -fsyntax-only plain_native.cpp`; `g++ -std=c++20 -Werror -Wsystem-headers -fsyntax-only plain_native.cpp`; `g++ -std=c++20 -Werror -Wsystem-headers -fsyntax-only derived_native.cpp` | 0 (warning); **1**; **0** | `probe-10-plain-vs-derived.log` |

The mandated log folder is not writable by this process: its mode is 755 and owned by this
user, yet `touch`, a Python `Path.write_text` and a `cp` into it all still return
`Permission denied` (an access restriction outside the mode bits, re-tested at the end of the
session). Every log below was therefore written to the worktree-internal, gitignored
`build/qwen-fix-logs/ci-1e541b5d1279ed2d/` and mirrored in `$TMPDIR`. The paths are
copy-pasted from the commands actually run.

The regression is deterministic: fixed fixture sources compiled five times (three fixtures
and the two real translation units), no timing, sleeps, ordering or shared state. `ctest` ran
the new entry as test #808 and the family as 4 executed tests (zero tests would have been an
error).

## Blast radius

- No production file changed: the product's signal installation, report directory fd,
  rollback, `handlersArmed()` and report format are untouched, so saved projects, undo,
  commands and the realtime path are unaffected by construction.
- The fix keeps every assertion, tolerance and test registration: only the two spellings
  that Apple's headers reject changed, and both expand/behave identically on the platforms
  that ran before. `ControlCrashReporter` (the socket transcript) still passes, so the
  agent/GUI arm-disarm path is unchanged.
- No new control command, no new audio-path code, no allocation/lock/log added.

## Self-check

- **Regression FAILS with the fix reverted and PASSES with it.** RED:
  `ctest -R CrashReporterAppleSignalCheck` → exit 8 (3/5, both real TUs rejected at
  `:81`/`:198`/`:548`); direct probe → exit 1. GREEN: same command → exit 0; family → exit 0
  (4/4). Logs `test-07-red-current.log`, `test-09-after-docstring.log`,
  `probe-05-red-refactor.log`.
- **The behaviour the test guards still executes, case by case.** `./build/tests/CrashReporterArmTest`
  → exit 0, `Totals: 8 passed, 0 failed, 0 skipped` (install-failure rollback, arm/disarm,
  armed crash reported, disarmed negative control, re-arm, second uninstall), and
  `./build/tests/CrashReporterTest` → exit 0, `Totals: 14 passed, 0 failed, 0 skipped`
  (`test-12-arm-verbose.log`, `test-13-reporter-verbose.log`). The assertion count is
  unchanged: 68 `QVERIFY/QCOMPARE` in the arm test and 126 in the reporter test, base and
  HEAD, and the diff removes no line but the two spellings (2 deletions in the arm test, 1 in
  the reporter test).
- **Deterministic.** The regression compiles fixed fixtures and the two checked-in
  translation units; no wall-clock, sleep, fork, socket or ordering dependency. The seam is
  the recorded `compile_commands.json` plus a synthetic Apple header model, both injected
  values.
- **Cause and completeness.** The exception specification is derived from the library
  declaration, so it is correct on glibc (still `noexcept`, unchanged) and on Apple (no
  specification). The sibling `CrashReporterTest.cpp:548` carried the identical macro
  defect and is fixed in the same commit; a tree-wide search for `::sigemptyset`,
  `::sigaddset`, `::sigdelset`, `::sigismember` found no other site. `sigaltstack` is a
  function on Apple and is left qualified.
- **No collateral damage.** No product code, limit, check, save/load format, undo step or
  command changed. The host GCC build of both tests still compiles and passes (the
  "host-blind" control above), so the fix does not depend on the model to be correct on
  Linux. A plain (no-specification) redeclaration was measured as a second option: it is
  accepted here only because GCC suppresses the system-header diagnostic, and fails under
  `-Werror -Wsystem-headers` (`g++ -std=c++20 -Werror -Wsystem-headers -fsyntax-only
  plain_native.cpp` → error `different exception specifier`), which is why the derived
  specification is used instead.
- **The registration cannot silently disappear.** `tests/CMakeLists.txt:6663-6677` registers
  the ctest only when a Clang-family driver exists (GCC cannot measure the specification
  half) and prints a configure-time `STATUS` line naming the missing driver otherwise.
- **Independent QA: PASS, no blockers.** A separate reviewer re-read HEAD, reproduced RED
  (3/5, exit 1) from the pre-fix sources, confirmed the sensitivity of both halves (restoring
  only `::sigemptyset` → 4/5; restoring only `noexcept` → 4/5), re-ran the fork-sources,
  unregistered-tests and evidence gates (all exit 0), and confirmed the 68/126 assertion
  counts. Its non-blocking remarks (the plain-`noexcept` rationale, the "next target built"
  wording, the product `sigaction` line list, the compiler-run count and the log-folder mode)
  are corrected in this report.
- **Verdict evidence for not ALREADY-FIXED/FALSE-POSITIVE:** not applicable — the verdict
  is FIXED; the pre-fix lines above are the code that produced the hosted diagnostic, and
  the RED run reproduces them.

## Files committed

- `tests/src/core/CrashReporterArmTest.cpp`
- `tests/src/core/CrashReporterTest.cpp`
- `tests/crash-reporter-apple-signal-check.py` (new)
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-ci-1e541b5d1279ed2d.md`
