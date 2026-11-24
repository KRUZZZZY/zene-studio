FIXED

# Cluster ci-f10986b0b93b91c1-port: port of "CI regression: build / mingw64"

ci-f10986b0b93b91c1-port: FIXED (port; the code fix had already landed, and this commit adds its regression test)

This replaces the round-2 NEEDS-CONTEXT report. The round-3 read-first note named the source
commit, so a CI log is no longer needed.

## Source and diagnosis
- **Source commit:** `86dff896ea` "fix(ci): CI regression: build / mingw64"
  (CI-Repair-Id ci-repair-ci-f10986b0b93b91c1-b7d26ed16e20).
- **Defect:** the hosted mingw64 build has `LMMS_HAVE_ALSA` undefined. In
  `tests/src/core/MidiOutTimingTest.cpp`, the anonymous-namespace helpers `measure()` and
  `measureAgainstRequested()` sat outside the `#ifdef LMMS_HAVE_ALSA` block, while their only
  callers were inside it. On a non-ALSA target they were defined but not used, and
  `-Werror=unused-function` (with `-DUSE_WERROR=ON`) broke the build.
- **Code fix already on this branch:** `f3b5426267` (train-sol, an ancestor of HEAD:
  `git merge-base --is-ancestor f3b5426267 HEAD` gave 0). `git show f3b5426267 -- tests/src/core/MidiOutTimingTest.cpp`
  moves `#ifdef LMMS_HAVE_ALSA` from before `struct Receiver` up to right after `namespace {`.
  It is now at `tests/src/core/MidiOutTimingTest.cpp:69`, so the constants and both helpers
  (`measure` at :84, `measureAgainstRequested` at :108) are guarded with their callers. This is
  the same change as the `MidiOutTimingTest.cpp` hunk of 86dff896ea. As the note instructed, I did
  not change that file.
- **What was missing:** 86dff896ea's regression test, a Linux-runnable reproduction of the
  non-ALSA TU. This commit ports it.

## Change (test-only)
- `tests/train-41-portability-check.py`: `mode_midi_out_timing_no_alsa`, identical to 86dff896ea.
  It compiles `MidiOutTimingTest.cpp` from the compile database with a shadow `lmmsconfig.h` that
  `#undef`s `LMMS_HAVE_ALSA`, plus a preinclude and `moc -ULMMS_HAVE_ALSA`, under
  `-Werror=unused-function`. It also adds the `MODES` entry and the mode in the
  `{render, wav-write-error, ...}` dispatch set (it is called with `root, build, moc`).
- `tests/CMakeLists.txt:9750`: adds `midi-out-timing-no-alsa` to the `foreach(mode ...)` list, which creates
  CTest `Train41-midi-out-timing-no-alsa`.
- Applied as `git show 86dff896ea -- tests/train-41-portability-check.py tests/CMakeLists.txt`
  > `86dff896ea-tests.patch`. `git apply --check` gave EXIT=0, then `git apply` gave EXIT=0 (the CMake hunk applied at an offset).
  No product file changed. The test is deterministic: it is a compile, with no timing and no threads.

## Commands and EXIT codes (logs in `.../ci-f10986b0b93b91c1-port.r3/logs/`)
| step | command | EXIT |
|---|---|---|
| configure (guarded) | `cmake -S . -B build` (01-configure.log) | 97, refused by bunny-guard ("configuring is the parent's job") |
| regenerate + build | `cmake --build build --target zene -j4` (10-build-regen.log); `ctest -N -R '^Train41-midi'` then lists `Test #1034: Train41-midi-out-timing-no-alsa`, Total Tests: 1 | 0 |
| GREEN | `ctest -R '^Train41-midi-out-timing-no-alsa$' --output-on-failure` (11-green.log), "100% tests passed out of 1" | 0 |
| RED (negative control) | moved `#ifdef LMMS_HAVE_ALSA` from :69 to just below `measureAgainstRequested` (12-negctl.diff), then the same ctest (13-red-negctl.log) | 8 |
| | 13-red-negctl.log: `MidiOutTimingTest.cpp:107:6: error: 'void {anonymous}::measureAgainstRequested(...)' defined but not used`, `MidiOutTimingTest.cpp:83:8: error: '{anonymous}::Jitter {anonymous}::measure(...)' defined but not used [-Werror=unused-function]` | |
| restore | `cp <backup> tests/src/core/MidiOutTimingTest.cpp && git diff --exit-code tests/src/core/MidiOutTimingTest.cpp` | 0 |
| GREEN again | same ctest (14-green-restored.log), "100% tests passed out of 1" | 0 |
| family | `ctest -R '^Train41-' --output-on-failure` (15-train41-family.log), "88% tests passed, 1 tests failed out of 8"; the only failure is `Train41-darwin-tu`, unrelated (see Other findings) | 8 |
| gates | `python3 .../feature_check.py <worktree>` (07-feature-check.log), `RESULT: PASS`, no FAIL rows | 0 |

Note on 4b: the product fix (f3b5426267) predates this lane, so the brief's `fix.patch` revert does not
apply: this lane has no product diff. The negative control above does the same job. It re-creates the
pre-f3b5426267 guard placement, and the test goes red with the exact mingw64 diagnostic class.

## Self-check
- Red/green: RED EXIT=8 with the guard moved back to its pre-fix spot (13-red-negctl.log); GREEN EXIT=0 before
  and after (11-green.log, 14-green-restored.log).
- Deterministic: yes. It is a single compiler invocation on a fixed TU, with no wall clock, sleeps or threads.
- Fix at the cause and complete: the cause is the guard placement in `MidiOutTimingTest.cpp`, already fixed
  by f3b5426267 (:69). Every non-ALSA-reachable helper is now inside the guard. The test's
  `-Werror=unused-function` compile is clean, which is what proves this.
- Collateral damage: none. The change is test-only (two test files). No product code, saved data or
  limits are touched.
- ALREADY-FIXED / FALSE-POSITIVE evidence: the code fix is cited above (f3b5426267, `MidiOutTimingTest.cpp:69`).
  This report is FIXED because the missing regression test is now ported.
- Sibling commands (app.*, arrangement.*): not affected. This lane changes no command file. The list in the
  check comes from the train merges already on the branch.
- Sibling modes: the other `Train41-*` modes are unchanged. Their dispatch set only gained the new name.
- git status: clean after commit.

Delegation: Haiku ran the first configure/ctest/gate pass. It correctly reported that `cmake -S . -B build`
is guarded (EXIT=97), that the new test was therefore unregistered (zero tests ran), and that
feature_check gave `RESULT: PASS` (07-feature-check.log). It stopped before the negative control rather than
proving nothing. After that I ran the regenerate build, the green run, the negative control and the restore
myself. Each was a single short command, and a second delegation would have cost more. Sonnet and DeepSeek
were not used: the port was a clean `git apply` of an existing patch, with nothing to design or write.

## Other findings
- `Train41-darwin-tu` fails in this build tree: `CrashReporterArmTest.cpp:361:10: fatal error:
  CrashReporterArmTest.moc: No such file or directory` (15-train41-family.log; Haiku saw the same failure
  before my change, in 03-train41-family.log). It is unrelated to this lane: a different mode and a different
  TU. Most likely the moc for a test target that isn't built here was never generated. Not fixed here.
