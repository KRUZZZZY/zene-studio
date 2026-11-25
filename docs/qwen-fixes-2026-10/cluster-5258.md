FALSE-POSITIVE

# Cluster 5258: multiband splits use too few all-pass sections

**Verdict: FALSE-POSITIVE.** One second-order all-pass section per higher crossover is not "too
few": it is exactly the phase alignment a Linkwitz-Riley 4th-order band set needs, and the bands do
sum flat. The section the finding asks for is wrong - adding it unbalances the product's own sum.

## Where the finding points, and what the code does

* `src/core/RackSplit.cpp:120` (line 119 when the finding was written) adds **one**
  `BiquadShape::AllPass` section per crossover above band `position`.
* `src/core/RackSplit.cpp:99-104` (`addStage`) designs every section at `Q = 1/sqrt(2)`: "two
  Butterworth sections in a row make a Linkwitz-Riley 4th order, and the all-pass is what such a
  low-pass plus high-pass pair sums to". So the low/high paths are LP2+LP2 and HP2+HP2 while the
  alignment section is a single AP2 - deliberately different orders, because that is the identity.
* Algebra, with `LP_k`, `HP_k`, `AP_k` the LR4 low-pass, high-pass and all-pass of crossover `k`
  (LTI, so they commute), and `LP_k + HP_k = AP_k` for LR4 (a 4th-order LR pair sums to a
  **second-order** all-pass: `(1 + s^4)/(s^2 + sqrt(2)s + 1)^2 = (s^2 - sqrt(2)s + 1)/(s^2 + sqrt(2)s + 1)`).
  Three bands: `LP0*AP1 + HP0*LP1 + HP0*HP1 = LP0*(LP1+HP1) + HP0*(LP1+HP1) = (LP0+HP0)(LP1+HP1)
  = AP0*AP1`. Four bands: `AP2*(LP0*AP1 + HP0*LP1) + HP0*HP1*AP2 = AP0*AP1*AP2`. In general the sum
  is `prod AP_k`, magnitude exactly 1. Two AP sections per higher crossover give `AP_k^2` and the
  cancellation no longer telescopes.
* The section budget is not what limits it: the worst chain needs 10 of `MaxStages = 12`
  (`include/RackSplit.h:116`), and the proposed doubling also fits in 10, so the finding's own fix
  would not have been silently truncated there either.
* The summed chains are a plain sum with no polarity flip (`src/core/RackNodes.cpp:144-148`,
  `RackSumNode::process` -> `sumInputs`).
* Intended contract: `include/RackSplit.h:49-52` - "the bands are Linkwitz-Riley (24 dB / octave)
  crossovers with each lower band phase-aligned to the ones above it, so they sum flat in level".

## Evidence: the product's own bands sum flat, and the proposed fix breaks them

New deterministic test `DeviceToolsTest::bandsSumFlatThroughEveryCrossover`
(`tests/src/core/DeviceToolsTestParts1.h`, declared in `tests/src/core/DeviceToolsTest.cpp`): for
3, 4 and 6 bands it runs every band's `RackSplitFilter`, sums them, and requires the summed level to
be 1 within 0.5% at ~39 geometric probes from 40 Hz to 14 kHz, each on the 10 Hz grid so the
half-block measurement window (4410 samples = a whole number of cycles at 44.1 kHz) leaks nothing.
No timing, no sleeps, no threads: the probes are computed, and the only state is the filter's own.

| Step | Command | Result |
| --- | --- | --- |
| Baseline build | `cmake --build build --target DeviceToolsTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Baseline tests, unchanged tree | `cd build/tests && ctest -R '^DeviceToolsTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | **EXIT=0**, "100% tests passed out of 1" |
| Build with the new test (first version, unwindowed probes) | `... > .../logs/build-2.log` | **EXIT=0** |
| Run it | `... > .../logs/test-2.log` | **EXIT=8**: `3 bands, 54.0163 Hz: the bands sum to 0.991495`. My measurement window, not the filter: 0.1 s of a 54 Hz sine is 5.4 cycles, so a partial-cycle RMS leaks ~0.4%. Fixed by putting the probes on the 10 Hz grid. |
| Rebuild | `... > .../logs/build-3.log` | **EXIT=0** |
| ctest | `... > .../logs/test-3.log` | **EXIT=0** |
| Single test, direct | `cd build/tests && ./DeviceToolsTest bandsSumFlatThroughEveryCrossover > .../logs/test-4-single.log 2>&1; echo EXIT=$?` | **EXIT=0**, `PASS : DeviceToolsTest::bandsSumFlatThroughEveryCrossover()` |
| Apply the finding's proposed fix (two all-pass sections) | `git diff -- src/core/RackSplit.cpp > .../logs/proposed-fix.patch` (17 lines); `cmake --build build --target DeviceToolsTest -j4 > .../logs/build-4-two-allpass.log 2>&1` | **EXIT=0** (patch captured, build clean) |
| Run the test on the "fixed" code, 3 bands | `./DeviceToolsTest bandsSumFlatThroughEveryCrossover > .../logs/test-5-two-allpass.log 2>&1` | **EXIT=1**: `3 bands, 240 Hz: the bands sum to 0.994863, not 1` |
| Same, 6 bands only (temporary edit of the case list, restored below) | `... > .../logs/build-5-two-allpass-6band.log`, `... > .../logs/test-6-two-allpass-6band.log` | build **EXIT=0**; test **EXIT=1**: `6 bands, 80 Hz: the bands sum to 0.992792, not 1` |
| Revert the proposed fix, restore the test file | `git apply -R .../logs/proposed-fix.patch; echo REVERT_EXIT=$?` | **EXIT=0**; `git status` shows only `tests/` changes, `src/core/RackSplit.cpp:120` back to one `addStage` |
| Rebuild restored code | `... > .../logs/build-6-restored.log` | **EXIT=0** |
| ctest | `... > .../logs/test-7-restored.log` | **EXIT=0** |
| Single test, restored | `./DeviceToolsTest bandsSumFlatThroughEveryCrossover > .../logs/test-8-restored-single.log 2>&1` | **EXIT=0**, PASS |
| The other direction: drop the all-pass sections entirely | `git diff -- src/core/RackSplit.cpp > .../logs/no-allpass.patch`; `... > .../logs/build-7-no-allpass.log`, `... > .../logs/test-10-no-allpass.log` | patch/build **EXIT=0**; test **EXIT=1**: `3 bands, 240 Hz: the bands sum to 0.994864, not 1` |
| Same, 6 bands only (temporary edit, restored below) | `... > .../logs/build-8-no-allpass-6band.log`, `... > .../logs/test-11-no-allpass-6band.log` | build **EXIT=0**; test **EXIT=1**: `6 bands, 80 Hz: the bands sum to 0.992793, not 1` |
| Restore test file and product file, final | `git apply -R .../logs/no-allpass.patch` **EXIT=0**; `grep` shows `src/core/RackSplit.cpp:120` with one `addStage`; `git status` lists only the two `tests/` files and the new report | |
| Final rebuild | `... > .../logs/build-9-restored-final.log` | **EXIT=0** |
| Final ctest | `... > .../logs/test-12-restored-final.log` | **EXIT=0** |
| Final single test | `... > .../logs/test-13-restored-final-single.log` | **EXIT=0**, `PASS : DeviceToolsTest::bandsSumFlatThroughEveryCrossover()` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | **EXIT=0**, all 8 PASS |

So the test is not the usual RED/GREEN pair, because there is no product change to make: it PASSES
on the code as it is (exit 0, three band counts, ~39 probes each) and FAILS the moment the all-pass
count is changed either way - two sections per higher crossover (the finding's fix) gives 0.994863 at
3 bands / 0.992792 at 6 bands with exit 1, and no sections at all gives 0.994864 / 0.992793 with
exit 1. One section is the only count that sums flat. QTest stops at the first failing probe, so each
of those runs reports one failing frequency and level rather than the worst in the sweep.

## Scope of the change

Product code: **none**. The commit adds one test method to the existing area file
(`tests/src/core/DeviceToolsTest.cpp`, `tests/src/core/DeviceToolsTestParts1.h`) and this report, so
save/load, undo, control commands (`ControlCommandsSnapshot`), the audio thread and every existing
behaviour are untouched. No registration list entry is needed: no `src/` or `include/` file changes.

## Self-check

* **Regression test fails without the product change and passes with it (exit codes).** There is no
  product change; the finding's own change is what the test refutes. With the finding's fix applied
  and rebuilt: `test-5-two-allpass.log` **EXIT=1**, `test-6-two-allpass-6band.log` **EXIT=1**. With
  the all-pass sections removed entirely: `test-10-no-allpass.log` **EXIT=1**,
  `test-11-no-allpass-6band.log` **EXIT=1**. With the branch's code as committed:
  `test-13-restored-final-single.log` **EXIT=0** and `test-12-restored-final.log` **EXIT=0**. The
  test therefore pins the count from both sides.
* **Deterministic, no timing or races.** The test builds each probe's sine from an index and a fixed
  frequency at the fixed `kRate = 44100`, runs `RackSplitFilter::process` on it, and measures the
  second half's RMS. No clock, no sleep, no thread, no filesystem; the only inputs are the split
  configuration and the probe frequencies. The 10 Hz probe grid is what keeps the RMS estimate
  integer-cycle (4410-sample window), not a tolerance tweak.
* **Fix at the cause and complete.** Not applicable: no code was changed. I checked the siblings of
  the reported line for a real defect and found none - the low/high pairs
  (`src/core/RackSplit.cpp:114-119`), the all-pass line (`:120`), the section budget
  (`include/RackSplit.h:116`), the mid/side and left/right modes (`src/core/RackSplit.cpp:126-133`,
  exact algebra, no filters) and the chain sum (`src/core/RackNodes.cpp:144-148`).
* **No collateral damage.** No product file is touched, so projects, saves and all callers of
  `RackSplitFilter::configure` behave exactly as before. The new test only reads the public
  `RackSplitFilter` API (`configure`, `active`, `process`).
* **ALREADY-FIXED / FALSE-POSITIVE evidence.** `src/core/RackSplit.cpp:120` (one all-pass section
  per higher crossover), `src/core/RackSplit.cpp:99-104` (every section at Q = 1/sqrt(2), so the
  LP2 cascade is LR4 and its sum with HP4 is a single AP2), `include/RackSplit.h:49-52` (the stated
  contract that the bands sum flat), and the measured product behaviour in the table above.
* **Sibling commands (`app.*`, `arrangement.*`, ...).** The pre-submit check lists them because the
  branch's diff against the fix base carries the train's earlier work, not because of this cluster:
  this commit touches only `tests/src/core/DeviceToolsTest.cpp`,
  `tests/src/core/DeviceToolsTestParts1.h` and this report. No command, handler or registry file is
  changed, so no sibling can differ. The rack split itself is reached through `rack.split`
  (`src/core/ControlCommandsDeviceTools.cpp:417`), which is untouched; the number of all-pass
  sections is internal to `RackSplitFilter` and is not observable through any command or saved data.

## Other findings

* `ctest -R 'DeviceTools|Rack'` (`test-9-family.log`, EXIT=8) shows `DeviceToolsTest` passing and
  every other rack target `Not Run` - their binaries were never built in this worktree (only `zene`
  and `DeviceToolsTest` are), so they prove nothing either way.
* In that same run `ControlDrumRackPads` and `ControlDrumRackChains` failed with
  `FAIL: build lists no loadable audiofileprocessor module` and `engine_ready` false. They drive the
  built `zene` binary over its control socket; I did not rebuild or modify `zene` and my diff is
  test-only, so this is a pre-existing harness/environment condition, not a regression from this
  work. I did not re-run them on the base commit to prove that.
