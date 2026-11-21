FIXED

# Cluster new-p21-172 — sfizz Gate stereo-link detector reads right2x[1]

## Finding

`plugins/SfzPlayer/sfizz/src/sfizz/effects/Gate.cpp`, stlink branch: the per-frame
detector key was built as `|left2x[i]| + |right2x[1]|`. The right-channel term used
a literal index 1 (one sample of the block) instead of the frame's own index `i`, so
the right channel's own samples never reached the stereo-link detector.

Scope: the finding's "default link path" wording is wrong. `gate_stlink` defaults to
false (`Defaults.cpp:192`, `BoolSpec gateSTLink { false, {0, 1}, 0 };`), so the default
path never reaches the affected line. Only instruments with `gate_stlink=1` are
affected.

## Evidence (file:line)

- `Gate.cpp:129-130`: the stlink branch builds `gateIn2x[i]` in a per-frame loop,
  `for (unsigned i = 0; i < _oversampling * nframes; ++i)`. Before the fix line 130
  read `right2x[1]`.
- The stlink branch was the only literal-index read. The other per-sample reads of
  `right2x` use `[i]`: `Gate.cpp:101` (`right2x[i] *= inputGain`), `:124`
  (`right2x[i] *= rightGain2x[i]`), `:143` (`right2x[i] *= gain2x[i]`).
- `Gate.cpp:37` initialises `_stlink` from `Default::gateSTLink`; `Gate.cpp:189` reads
  `gate_stlink` per instrument.

## Fix

One token, `Gate.cpp:130`: `std::abs(right2x[1])` → `std::abs(right2x[i])`. No other
product change.

## Test

New file `tests/src/plugins/SfzGateStereoLinkTest.cpp`, registered in
`tests/CMakeLists.txt`, `tests/fork-sources.txt` (cluster new-p21-172 entry) and
`tests/all-sources.txt:3657`.

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

Note on (c): it fails in the reverted runs of test-red-2.log and test-reversed-red.log
but passes in test-red.log. The first red run therefore shows only (a) failing.

## Commands and exit codes

Exit codes are copied only where the log file records them. Otherwise the cell says
"not recorded in log".

| Step | Log | What the log shows | EXIT |
|---|---|---|---|
| build (target generation and build) | build-1.log | last line `Built target SfzEQPoolExhaustionTest` | not recorded in log |
| build | build-2.log | target generation and build | not recorded in log |
| red run 1 (fix absent) | test-red.log | `Totals: 5 passed, 1 failed`; (a) FAIL | not recorded in log |
| red run 2 (fix absent) | test-red-2.log | `Totals: 4 passed, 2 failed`; (a) and (c) FAIL | not recorded in log |
| build + green run 1 | build-4.log, test-green-1.log | build ends `Built target zene`; `1/1 ... SfzGateStereoLinkTest ... Passed` | not recorded in log |
| build + family run 2 | build-5.log, test-family-2.log | `100% tests passed out of 15` | not recorded in log |
| family run 1 (before build-5) | test-family-1.log | `20% tests passed, 12 tests failed out of 15`; the 12 failures are `Not Run` executables | not recorded in log |
| revert fix.patch + build | build-6.log, test-reversed-red.log | `Totals: 4 passed, 2 failed`; (a) and (c) FAIL | EXIT=8 (in log) |
| reapply fix.patch + build | build-7.log | build of the fixed tree | EXIT=0 (in log) |
| reapplied green run | test-reapplied-green.log | `1/1 ... SfzGateStereoLinkTest ... Passed` | EXIT=0 (in log) |
| all-sources reproduce | quiet-run log `duet/12-worker/tmp/001-reproduce-p4.log` | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` | 0 (quiet-run) |
| feature_check | feature-check-1.log | `RESULT: PASS`; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS | 0 (run in this session, not in log) |

The first family run failed only because 12 test executables were not built. build-5
fixed that, and the second family run passes 15 of 15.

## Realtime

The change is one index in an existing per-sample loop (`Gate.cpp:129-130`). It adds
no allocation, lock, wait or log call. No threading code changed. Local TSan was not
run in this session.

## Other findings

- `tests/upstream-modifications.txt:4660` (Gate.cpp entry) still says the vendored
  sfizz tree is unmodified. The feature_check registration gate passed without an
  update, so the entry was not changed. The entry is now inaccurate for Gate.cpp and
  the lead decides whether to update it.
