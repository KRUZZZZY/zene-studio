FIXED

# Cluster new-p21-172 — sfizz Gate and Compressor stereo-link detectors read right2x[1]

## Finding

`plugins/SfzPlayer/sfizz/src/sfizz/effects/Gate.cpp`, stlink branch: the per-frame
detector key was built as `|left2x[i]| + |right2x[1]|`. The right-channel term used
a literal index 1 (one sample of the block) instead of the frame's own index `i`, so
the right channel's own samples never reached the stereo-link detector.

The same bug is in the sibling effect `plugins/SfzPlayer/sfizz/src/sfizz/effects/Compressor.cpp`,
stlink branch: its detector key `compIn2x[i]` was built as `|left2x[i]| + |right2x[1]|`,
with the same effect. Both are fixed in this cluster.

Scope: the finding's "default link path" wording is wrong. `gate_stlink` defaults to
false (`Defaults.cpp:192`, `BoolSpec gateSTLink { false, {0, 1}, 0 };`), so the default
path never reaches the affected line. Only instruments with `gate_stlink=1` are
affected.

The same holds for Compressor: `comp_stlink` defaults to false
(`plugins/SfzPlayer/sfizz/src/sfizz/Defaults.cpp:184`, `BoolSpec compSTLink { false, {0, 1}, 0 };`),
so the default path (`Compressor.cpp:101`, `if (!impl._stlink)`) never reaches the
affected line. Only instruments with `comp_stlink=1` are affected.

## Evidence (file:line)

- `Gate.cpp:129-130`: the stlink branch builds `gateIn2x[i]` in a per-frame loop,
  `for (unsigned i = 0; i < _oversampling * nframes; ++i)`. Before the fix line 130
  read `right2x[1]`.
- The stlink branch was the only literal-index read. The other per-sample reads of
  `right2x` use `[i]`: `Gate.cpp:101` (`right2x[i] *= inputGain`), `:124`
  (`right2x[i] *= rightGain2x[i]`), `:143` (`right2x[i] *= gain2x[i]`).
- `Gate.cpp:37` initialises `_stlink` from `Default::gateSTLink`; `Gate.cpp:189` reads
  `gate_stlink` per instrument.
- `Compressor.cpp:126-127`: the stlink branch (the `else` of `if (!impl._stlink)` at
  `:101`) builds `compIn2x[i]` in a per-frame loop,
  `for (unsigned i = 0; i < _oversampling * nframes; ++i)`. Before the fix line 127
  read `right2x[1]`.
- The other per-sample reads of `right2x` in Compressor.cpp use `[i]`: `:98`
  (`right2x[i] *= inputGain`), `:121` (`right2x[i] *= rightGain2x[i]`), `:140`
  (`right2x[i] *= gain2x[i]`).
- `Compressor.cpp:34` initialises `_stlink` from `Default::compSTLink`; `:188-189` reads
  `comp_stlink` per instrument.

## Fix

One token in each effect:

- `Gate.cpp:130`: `std::abs(right2x[1])` → `std::abs(right2x[i])` (commit `ac80b7022d`).
- `Compressor.cpp:127`: `std::abs(right2x[1])` → `std::abs(right2x[i])` (this commit).

No other product change.

## Test

New file `tests/src/plugins/SfzGateStereoLinkTest.cpp`, registered in
`tests/CMakeLists.txt`, `tests/fork-sources.txt` (cluster new-p21-172 entry) and
`tests/all-sources.txt:3658` (`:3657` before this commit added the Compressor entry
above it at `:3651`).

New file `tests/src/plugins/SfzCompressorStereoLinkTest.cpp` (this commit), registered in
`tests/CMakeLists.txt` (the comment and `list(APPEND LMMS_TESTS ...)` at `:2079-2080`,
and the `OR LMMS_TEST_NAME STREQUAL "SfzCompressorStereoLinkTest"` condition at `:2293`),
`tests/fork-sources.txt:4017-4018`, and `tests/all-sources.txt:3651`.

Per-slot results from the QTest logs (`PASS :` / `FAIL! :` lines):

| Slot | Test (line) | Role | Before fix (test-red.log) | Before fix (test-red-2.log, test-reversed-red.log) |
|---|---|---|---|---|
| (a) | stlinkRightOnlyOpensGate (:87) | discriminating | FAIL | FAIL |
| (b) | silentStereoGivesZeroOutput (:105) | guard | PASS | PASS |
| (c) | reconfiguredInstanceCarriesGateStateAcrossBlocks (:125) | discriminating | PASS | FAIL |
| (d) | unlinkedRightOnlyOpensRightGate (:147) | guard | PASS | PASS |

(a) fails with `'outPeak > 0.25f * inPeak' returned FALSE` (right output peak 0). The
green runs (`test-green-1.log`, `test-reapplied-green.log`) are ctest-level only
(`1/1 Test #725: SfzGateStereoLinkTest ... Passed`); they carry no per-slot lines.

Note on (c): `test-red.log` was produced before slot (c) was rewritten. The first version of
(c) only checked a second tone block, which also passes under the bug, so that run showed
`5 passed, 1 failed`. The rewrite made (c) discriminating. The `test-red-2.log` and
`test-reversed-red.log` columns come from the final test source, and there (c) FAILs under
the bug.

Per-slot results for the Compressor test (`SfzCompressorStereoLinkTest.cpp`). The
test-line column gives the test function's line in the source; the log columns give
the line of the `FAIL!` / `PASS` entry in that log.

| Slot | Test (line) | Role | test-comp-red.log (whole-block version, pre-tail-window) | test-comp-red-2.log (final source, fix absent) | test-comp-reversed-red.log (final source, fix reverted) | qtest-trial-slots.log (fix present) |
|---|---|---|---|---|---|---|
| (a) | stlinkRightOnlyCompressesRight (:99) | discriminating | FAIL (log :7) | FAIL (log :7) | FAIL (log :7) | PASS (:20) |
| (b) | reconfiguredInstanceCarriesCompressorStateAcrossBlocks (:120) | discriminating | FAIL (log :9) | FAIL (log :9) | FAIL (log :9) | PASS (:21) |
| (c) | unlinkedRightOnlyCompressesRight (:141) | guard | FAIL (log :11) | PASS (log :11) | PASS (log :11) | PASS (:22) |

(a) and (b) fail with `'outPeak < compressedShare * inPeak' returned FALSE`, with the right
output peak 0.500022 against input peak 0.499997 in the final source with the fix absent
(`test-comp-reversed-red.log:7`, `:9`). In the whole-block `test-comp-red.log` the
right output peaks are 0.501027 for (a) and (b), and 0.461211 for (c).

Note on the Compressor (c) guard: `test-comp-red.log` is the earlier whole-block version
of the test. Its levels were measured over the whole block, and under that version the
guard (c) also FAILs (peak 0.461211). That is why the levels are now measured over the
tail of the block, frames `tailStart..numFrames-1` with `tailStart = 256`
(`SfzCompressorStereoLinkTest.cpp:34`, comment at `:31`). `test-comp-red.log` does not
show the behaviour of the final test. `test-comp-red-2.log` and `test-comp-reversed-red.log`
come from the final source.

The green Compressor logs (`test-comp-trial-green.log`, `test-comp-green.log`,
`test-comp-reapplied-green.log`) are ctest-level only (`1/1 Test #726: SfzCompressorStereoLinkTest ... Passed`).
Their per-slot PASS lines are in `qtest-trial-slots.log:20-22`, a QTest run with no EXIT line.

## Registration

`tests/upstream-modifications.txt:4660` (Gate.cpp entry) keeps its original text and
appends the reason that Gate.cpp is now changed by this cluster: the one-index fix at
Gate.cpp:130 and the regression test `tests/src/plugins/SfzGateStereoLinkTest.cpp`.
feature_check passes after that edit (`feature-check-2.log`, EXIT=0).

`tests/upstream-modifications.txt:4648` (Compressor.cpp entry) gets the same appended
reason: the one-index fix at Compressor.cpp:127 and the regression test
`tests/src/plugins/SfzCompressorStereoLinkTest.cpp`. That is the only changed line in the
file (diff hunk `@@ -4648 +4648 @@`). feature_check passes after this edit
(`feature-check-3.log:10` RESULT: PASS, EXIT=0 at `:11`).

## Commands and exit codes

Exit codes are copied from a log only where the log has an EXIT line. Where it has
none, the cell says so: either "EXIT=n as printed by the command in the session", or
"not recorded in log".

| Step | Log | What the log shows | EXIT |
|---|---|---|---|
| build (target generation and build) | build-1.log | last line `Built target SfzEQPoolExhaustionTest` | EXIT=0 as printed by the command in the session; the log file itself has no EXIT line |
| build | build-2.log | target generation and build | EXIT=0 as printed by the command in the session; the log file itself has no EXIT line |
| red run 1 (fix absent) | test-red.log | `Totals: 5 passed, 1 failed`; (a) FAIL | EXIT=8 as printed by the command in the session; the log file itself has no EXIT line |
| red run 2 (fix absent) | test-red-2.log | `Totals: 4 passed, 2 failed`; (a) and (c) FAIL | EXIT=8 as printed by the command in the session; the log file itself has no EXIT line |
| build + green run 1 | build-4.log, test-green-1.log | build ends `Built target zene`; `1/1 ... SfzGateStereoLinkTest ... Passed` | not recorded in log |
| build + family run 2 | build-5.log, test-family-2.log | `100% tests passed out of 15` | not recorded in log |
| family run 1 (before build-5) | test-family-1.log | `20% tests passed, 12 tests failed out of 15`; the 12 failures are `Not Run` executables | not recorded in log |
| revert: `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/effects/Gate.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix.patch` | none (no log file) | reverts the fix | EXIT=0 as printed by the command in the session |
| build after the revert | build-6.log | no EXIT line in the log | not recorded in log |
| reversed red run | test-reversed-red.log | `Totals: 4 passed, 2 failed`; (a) and (c) FAIL | EXIT=8 (in log) |
| reapply: `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix.patch` | none (no log file) | reapplies the fix | EXIT=0 as printed by the command in the session |
| reapply build | build-7.log | build of the fixed tree | EXIT=0 (in log) |
| reapplied green run | test-reapplied-green.log | `1/1 ... SfzGateStereoLinkTest ... Passed` | EXIT=0 (in log) |
| all-sources reproduce | quiet-run log `001-reproduce-p4.log` | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` | 0 (printed by quiet-run in the session) |
| feature_check (rerun after the upstream-modifications.txt edit) | feature-check-2.log | `RESULT: PASS`; 8 PASS lines, 0 FAIL lines. feature-check-1.log (earlier run) has no EXIT line | EXIT=0 (in log) |
| build (stereo-link target) | build-8.log | `[100%] Built target SfzGateStereoLinkTest` | EXIT=0 (in log) |
| stereo-link test | test-green-2.log | `1/1 Test #725: SfzGateStereoLinkTest ... Passed`; `100% tests passed out of 1` | EXIT=0 (in log) |
| family run 3 | test-family-3.log | `100% tests passed out of 15` | EXIT=0 (in log) |
| Compressor build, fix applied | build-13.log | copy of the quiet-runner log for `cmake --build build --target SfzCompressorStereoLinkTest zene` (runner: `exit 0 after 18 s`) | EXIT=0 (in log, `:77`; the line was appended from the runner's exit 0) |
| Compressor red, whole-block version | test-comp-red.log | `Totals: 2 passed, 3 failed`; (a), (b), (c) FAIL | EXIT=8 (in log, `:25`) |
| Compressor red run 2 (fix absent, final source) | test-comp-red-2.log | `Totals: 3 passed, 2 failed`; (a) and (b) FAIL | EXIT=8 (in log, `:24`) |
| Compressor trial green | test-comp-trial-green.log | `1/1 Test #726 ... Passed`; `100% tests passed out of 1` | EXIT=0 (in log, `:8`) |
| Compressor green run (fix applied) | test-comp-green.log | `1/1 Test #726 ... Passed`; `100% tests passed out of 1` | EXIT=0 (in log, `:8`) |
| Compressor family run (fix applied) | test-family-4.log | `100% tests passed out of 16` (`:35`) | EXIT=0 (in log, `:38`) |
| Compressor reversal: `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/effects/Compressor.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix-comp.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix-comp.patch` | fix-comp.patch (13 lines) | reverts Compressor.cpp:127 | EXIT=0 as printed by the command in the session (no log file) |
| build after the reversal | build-15.log | `cmake --build build --target SfzCompressorStereoLinkTest` | EXIT=0 (in log, `:57`) |
| Compressor reversed red run | test-comp-reversed-red.log | `Totals: 3 passed, 2 failed`; (a) `:7` and (b) `:9` FAIL, (c) `:11` PASS | EXIT=8 (in log, `:24`) |
| reapply: `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p21-172.r1/logs/fix-comp.patch` | none (no log file) | reapplies Compressor.cpp:127 | EXIT=0 as printed by the command in the session (no log file) |
| build after the reapply | build-16.log | `cmake --build build --target SfzCompressorStereoLinkTest zene` | EXIT=0 (in log, `:73`) |
| Compressor reapplied green run | test-comp-reapplied-green.log | `1/1 Test #726 ... Passed`; `100% tests passed out of 1` | EXIT=0 (in log, `:8`) |
| feature_check (Compressor) | feature-check-3.log | `RESULT: PASS` (`:10`) | EXIT=0 (in log, `:11`) |

### ctest and build commands

No log in the run's logs directory contains the text `ctest`, so the verbatim
invocations are not recorded. The commands below reproduce the rows; they are not a
record of what ran.

- Stereo-link rows: `cd build/tests && ctest -R SfzGateStereoLinkTest --output-on-failure`
- Family rows (`test-family-*.log`): a ctest run over the 15-test family; the selector
  is not recorded.
- Build rows: build the target named in the row's log (for example
  `SfzGateStereoLinkTest`); the build command is not recorded.

The Compressor rows were run by the session through the quiet runner, with the output
written to the named log and its EXIT line appended. The commands were:

- `cmake --build build --target SfzCompressorStereoLinkTest zene` (build-13, build-16)
- `cmake --build build --target SfzCompressorStereoLinkTest` (build-15)
- `cd build/tests && ctest -R SfzCompressorStereoLinkTest --output-on-failure` (test-comp-*.log)
- `cd build/tests && ctest -R 'Sfz|HistoricalBuffer|FilterPool' --output-on-failure` (test-family-4.log)
- `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/effects/Compressor.cpp > .../logs/fix-comp.patch && git apply -R .../logs/fix-comp.patch`, then `git apply .../logs/fix-comp.patch` (the reapply)
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (feature-check-3)

## Realtime

The change is one index in an existing per-sample loop in each effect
(`Gate.cpp:129-130`, `Compressor.cpp:126-127`). It adds no allocation, lock, wait or log
call. No threading code changed. Local TSan was not run in this session.

## Other findings

- none

## Self-check

Answers to the six ASK items of the pre-submit check. Each cites the log or file:line
that shows it.

1. **Regression test fails with the change reverted and passes with it.** Yes, on the
   final test source. Both reverted runs, `test-red-2.log` and `test-reversed-red.log`,
   show `Totals: 4 passed, 2 failed`, with slot (a) `stlinkRightOnlyOpensGate` FAIL at
   `:7` and slot (c) `reconfiguredInstanceCarriesGateStateAcrossBlocks` FAIL at `:10` in
   both files. Both exit 8: `test-reversed-red.log:25` EXIT=8 is in the log, and the
   `test-red-2.log` EXIT=8 is as printed by the command in the session (the file has no
   EXIT line). With the fix applied, the test passes: `test-green-2.log:8` EXIT=0 and
   `test-reapplied-green.log:8` EXIT=0. These are ctest-level results; the logs carry no
   per-slot lines, and a passing run means every slot passed.
   The first red run, `test-red.log`, predates the rewrite of slot (c) (see the note under
   the table). It shows `Totals: 5 passed, 1 failed`, with only (a) FAIL at `:7`.
   Compressor: the reversed run `test-comp-reversed-red.log:24` EXIT=8 (in log) shows (a)
   `stlinkRightOnlyCompressesRight` FAIL at `:7` and (b)
   `reconfiguredInstanceCarriesCompressorStateAcrossBlocks` FAIL at `:9`, with the guard (c)
   `unlinkedRightOnlyCompressesRight` PASS at `:11`. `test-comp-red-2.log:24` EXIT=8 (in log)
   shows the same without the revert. With the fix applied, `test-comp-green.log:8` EXIT=0
   and `test-comp-reapplied-green.log:8` EXIT=0 are the ctest-level passes; the per-slot
   PASS lines are `qtest-trial-slots.log:20-22`. `test-comp-red.log:25` EXIT=8 is the
   whole-block version and does not count as evidence for the final test.
2. **Deterministic.** Repeatability evidence: the two red runs on the final test source,
   `test-red-2.log` and `test-reversed-red.log`, agree slot for slot (the same FAILs at `:7`
   and `:10`, and the same `4 passed, 2 failed`). The test source has no timing, RNG or
   thread code: `rg -n 'chrono|rand|thread|sleep|time'` on
   `tests/src/plugins/SfzGateStereoLinkTest.cpp` finds none. Its includes are QtTest, std
   headers, and the sfizz headers it tests. The input is fixed: the constants at `:23-24`
   (44100 Hz, 512 frames) are used by the gate-building helper (comment at `:30`, with the
   `Gate::makeInstance` call at `:41`); `fillTone()` (`:49`) computes the sine from the
   absolute frame index; each slot builds a fresh gate.
   Compressor: the two red runs on the final source, `test-comp-red-2.log` and
   `test-comp-reversed-red.log`, agree slot for slot (FAIL at `:7` and `:9`, PASS at `:11`,
   right output peak 0.500022 in both). The test source has no timing, RNG or thread code:
   `rg -n 'chrono|rand|thread|sleep|\btime\b'` on `tests/src/plugins/SfzCompressorStereoLinkTest.cpp`
   finds only a comment at `:54` ("not random"). The input is set by constants at `:23-28`
   (`sampleRate`, `numFrames`, `toneHz`, `toneAmplitude`).
3. **Fix at the cause and complete.** The change is one token at `Gate.cpp:130`. The
   other per-sample reads of `right2x` in `Gate::process` (`Gate.cpp:87`) use `[i]`:
   `Gate.cpp:101`, `:124`, `:143`. `git show ac80b7022d -- Gate.cpp` shows 1 insertion
   and 1 deletion. `Gate::process` keeps its signature. `Gate.h` is not changed:
   `ac80b7022d` does not list it, and `git diff --stat ac80b7022d HEAD -- .../Gate.h` is
   empty. The search `rg -n 'fx::Gate|effects/Gate\.h|Gate::makeInstance|"Gate"'` over
   `plugins/SfzPlayer/sfizz/src` and `tests/src` finds: the factory registration at
   `plugins/SfzPlayer/sfizz/src/sfizz/Effects.cpp:39` (`registerEffectType("gate",
   fx::Gate::makeInstance)`); the `Gate::makeInstance` definition at
   `plugins/SfzPlayer/sfizz/src/sfizz/effects/Gate.cpp:151`; the test's include and call at
   `tests/src/plugins/SfzGateStereoLinkTest.cpp:16` and `:41`; and
   `tests/src/gui/PluginControlDialogsTest.cpp:123`, which matches only the string "Gate" in
   a GUI combo test. No other caller of the Gate class was found. The fix is in the body of
   `Gate::process` (defined at `Gate.cpp:87`) only.
   Sibling detector, Compressor: the one-token fix at `Compressor.cpp:127` (`right2x[1]` →
   `right2x[i]`). The other per-sample reads of `right2x` in `Compressor.cpp` use `[i]`
   (`:98`, `:121`, `:140`), so the stlink branch was the only literal-index read. The
   product diff against HEAD touches only `Compressor.cpp` under `plugins/`; `Compressor.h`
   is not changed. The factory registration is `plugins/SfzPlayer/sfizz/src/sfizz/Effects.cpp:38`
   (`registerEffectType("comp", fx::Compressor::makeInstance)`); the definition is
   `Compressor.cpp:148`; the test calls it at `SfzCompressorStereoLinkTest.cpp:47`. The
   earlier statement that the Compressor detector is not changed is withdrawn: this
   cluster fixes it.
4. **No collateral damage.** The diff adds no limit or check and touches no opcode
   parsing or saved state. The changed branch runs only when `gate_stlink=1`. Related
   evidence: `test-family-3.log:33` `100% tests passed out of 15` and `:36` EXIT=0;
   `feature-check-2.log:11` EXIT=0.
   Compressor: the same holds. The changed `else` branch runs only when `comp_stlink=1`.
   Related evidence: `test-family-4.log:35` `100% tests passed out of 16` and `:38` EXIT=0;
   `feature-check-3.log:11` EXIT=0.
5. **ALREADY-FIXED or FALSE-POSITIVE.** Not applicable. The verdict is FIXED (line 1),
   and `Gate.cpp:130` and `Compressor.cpp:127` now read `right2x[i]`.
6. **Sibling commands.** Not affected. Commit `ac80b7022d` changed no command file
   (`git show --stat ac80b7022d`: the report, `Gate.cpp`, `tests/CMakeLists.txt`,
   `tests/all-sources.txt`, `tests/fork-sources.txt`, the new test). Commit `4c9d7a4235`
   changed the report and `tests/upstream-modifications.txt`. This cluster's Compressor
   commit changes `Compressor.cpp`, `tests/src/plugins/SfzCompressorStereoLinkTest.cpp`,
   `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt`,
   `tests/upstream-modifications.txt` and this report.

## Re-verification (later session)

Product fixes were already on the branch (`rg -n 'right2x\[1\]'` over `plugins/SfzPlayer/sfizz/src/sfizz/effects/` finds nothing). Both fixes were reverted with `git apply -R fix-both.patch` (EXIT=0), rebuilt (`build-20.log`, EXIT=0), and `cd build/tests && ctest -R 'Sfz(Gate|Compressor)StereoLinkTest' --output-on-failure` gave EXIT=8 with 4 `FAIL!` lines (`test-red-s3.log`). After `git apply fix-both.patch` (EXIT=0) and rebuild (`build-21.log`, EXIT=0), the same ctest gave EXIT=0, `100% tests passed out of 2` (`test-green-s3.log`).
