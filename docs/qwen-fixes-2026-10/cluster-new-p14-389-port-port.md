# Cluster new-p14-389-port-port

Verdict: **FIXED**

The finding (Luna's patrol, cluster new-p14-389): with `SA_DEBUG` enabled, the analyzer's
debug timing report mixes samples from different intervals, because the GUI thread
exchanged the count, the sum and the peak separately
(`plugins/SpectrumAnalyzer/Analyzer.cpp:120` in the finding's line numbering).

This is a **port** of `bugs/qwen-new-p14-389-port` (commits `f2baa9ab3`, `a5bdb8710`), which
could not go on the newest train. The fix is re-applied by hand onto this line (base
`07d6d02bd`, `050/m0-train-12`): no merge, no cherry-pick, no rebase. The regression is RED
without the fix and GREEN with it (exit codes below).

Verdict **REAL** on this branch: the pre-fix code is still here, unchanged, so the defect
exists in this tree exactly as reported. `git log --oneline -3 --
plugins/SpectrumAnalyzer/Analyzer.cpp` shows no fix on the newest line.

## The defect (before this change)

`Analyzer::reportAudioTiming()` read the three counters with three separate
`exchange(0, relaxed)` calls (`plugins/SpectrumAnalyzer/Analyzer.cpp:127-129`):

```cpp
const int count = m_dump_count.exchange(0, std::memory_order_relaxed);
const auto sum_ns = m_sum_execution_ns.exchange(0, std::memory_order_relaxed);
const auto max_ns = m_max_execution_ns.exchange(0, std::memory_order_relaxed);
```

The three exchanges are not one snapshot. An audio callback between two of them puts its
`fetch_add` on the count into the *next* report while its `fetch_add` on the sum lands in the
*current* one, so the printed average of the current report is
`sum_of_interval_plus_one_callback / count_of_interval`, and the next report starts with a
count that already contains that callback and a sum that does not. The peak has the same
problem. The pre-fix code documented this as accepted behaviour
(`plugins/SpectrumAnalyzer/Analyzer.cpp:120-121`: "A callback that lands between the
exchanges below is counted in the next report").

The counters were three unrelated atomics (`plugins/SpectrumAnalyzer/Analyzer.h:201-203`
before this change: `m_dump_count`, `m_sum_execution_ns`, `m_max_execution_ns`), so nothing
tied the three values to one point in time. The path is behind `#ifdef SA_DEBUG`, which no
CMake file in this tree defines, so release builds are unaffected - but the debug path was
wrong exactly as reported.

## The fix (current state)

`Analyzer::TimingCollector` (`plugins/SpectrumAnalyzer/Analyzer.h:81-167`) is a lock-free
single-producer queue of *measurements*, not of three separately resettable counters:

- `record(executionNs)` (`Analyzer.h:97-114`, audio thread) stores one measurement per
  callback into a fixed ring of 65536 slots and publishes the new end of the queue with one
  release store. It never allocates, never locks, never waits and never logs. When the queue
  is full - the GUI thread has not consumed it for a very long time - the measurement is
  dropped and counted, so a report can never mix two callbacks.
- `request()` (`Analyzer.h:116-120`, GUI thread) closes the interval that ends now: it
  remembers the current end of the queue as the cut of the interval.
- `take(snapshot)` (`Analyzer.h:122-146`, GUI thread) computes the count, the sum and the peak
  from exactly the measurements between the previous cut and this one, and advances the start
  of the queue.

Consequences:

- One report can never mix samples from two intervals: the count, the sum and the peak are
  computed by the GUI thread from one contiguous set of measurements, and a callback that
  lands after the request belongs to the next interval as a whole.
- The accumulated timings of an interval stay reportable when **no callback follows the
  request**, because the GUI thread reads the measurements itself instead of waiting for the
  audio thread to publish them. This preserves the baseline behaviour
  (`plugins/SpectrumAnalyzer/Analyzer.cpp:127-134` before this change printed the accumulated
  count, sum and peak every five seconds, whether or not the audio thread ran again).
- The report cadence, the printed line, its format and the "print only when the count is
  non-zero" rule are unchanged (`Analyzer.cpp:119-136`).

The audio thread's part is a handful of atomic operations (`Analyzer.cpp:105-109` calls
`m_timing.record(execution_ns)`): one relaxed load of the write index, one acquire load of
the read index, one relaxed store of the measurement and one release store of the write
index. `std::atomic<std::uint64_t>` is asserted to be lock-free (`Analyzer.h:164-165`). The
65536 slots are allocated once in the collector's constructor (`Analyzer.h:95`), which runs
on the thread that creates the plugin - never on the audio thread.

### Callers and siblings checked

- `Analyzer::reportAudioTiming()` is the only reader of the collector and the only caller of
  `m_timing.request()` / `m_timing.take()`; it is called from the `QTimer` lambda in the
  constructor (`Analyzer.cpp:74-78`) and from nowhere else.
- `Analyzer::processImpl()` is the only writer; `m_timing.record()` replaced the three
  `fetch_add`/CAS updates there (`Analyzer.cpp:105-109`).
- `m_dump_count` / `m_sum_execution_ns` / `m_max_execution_ns` have no other user in the tree.
  The identically named members of the sibling class `SaProcessor`
  (`plugins/SpectrumAnalyzer/SaProcessor.h:163-165`) are a different, per-second CPU report of
  the *processor* thread and are untouched.
- `tests/reference/SpectrumAnalyzer/Analyzer.{h,cpp}` is the frozen pre-migration source of
  the behaviour-preservation harness (`partc_ref_analyzer`), not the shipped plugin, and is
  left unchanged - as the accepted fix left it. `PluginPortsMigrationTest` still renders
  sample-exactly like the reference (it passes, see the evidence table).
- The constructor guard in `AudioPluginTest::analyzerConstructorInitializesTheDebugMembers`
  (`tests/src/plugins/AudioPluginTest.cpp:968-1006`) listed `m_dump_count`,
  `m_sum_execution_ns` and `m_max_execution_ns` unconditionally. Those members no longer
  exist, so the guard would fail a correct fix; it was adapted to check only the SA_DEBUG
  scalar members that the header still declares (`m_last_dump_time`, and the counters if they
  ever come back). This is not a weakening: it still fails if the constructor stops
  initializing a member that exists.
- `tests/upstream-modifications.txt` gained one entry for each of the two product files.

## The regressions

All four live in `tests/src/plugins/AudioPluginTest.cpp` (the only ctest of this family -
`ctest -N` shows no `^Analyzer$` and no `^SaControls$` test in this tree):

- `analyzerTimingSnapshotStaysCoherent` (`AudioPluginTest.cpp:1008`, probe at `:212`) drives
  `Analyzer::TimingCollector` directly and checks four consecutive intervals: the count, the
  sum and the peak of every snapshot describe exactly the callbacks of one interval, a
  callback that arrives after a request lands in the next interval, and a `take()` without a
  request returns nothing.
- `analyzerTimingSurvivesNoCallbackAfterRequest` (`AudioPluginTest.cpp:1017`, probe at
  `:320`) records two measurements, requests the interval, makes no further callback and
  verifies that the measurements are still reportable - the baseline behaviour the previous
  collector version lost.
- `analyzerTimingReportUsesOneCoherentSnapshot` (`AudioPluginTest.cpp:1027`) is the source
  contract: the reporter may not use separate `.exchange(0` counter resets, the audio
  callback must call `m_timing.record(`, and the reporter must close one interval
  (`m_timing.request(`) and take that interval (`m_timing.take(`).
- `analyzerTimingCollectorCoversMaximumSixSecondReportInterval` checks that the bounded ring
  retains all 36000 measurements produced by six seconds at the supported 192 kHz / 32-frame
  maximum callback rate.

The probes inject every measurement; there is no clock, no sleep, no thread and no race.
The `TimingCollector` is looked up through the `Analyzer` type with a `requires` guard, so
the test also fails (instead of failing to compile) on a tree that has no collector yet.

## Evidence table

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-389-port-port`
Logs: `build/qwen-logs/new-p14-389-port-port.r1/` (git-ignored). The brief's log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-389-port-port.r1/logs` is **not
writable in this session** (`touch` there returns `Permission denied`, also at every parent
level and in the worktree's parent), so every log was written to the worktree's ignored
`build/` tree instead; the commands and exit codes below are copied from those files.

| Step | Command (cwd) | Exit | Result |
|---|---|---|---|
| Baseline build | `cmake --build build --target AudioPluginTest PluginPortsMigrationTest -j 20` | 0 | `build-baseline-audioplugintest.log` |
| **RED** (no fix) | `ctest -R '^AudioPluginTest$' --output-on-failure` (in `build/tests`) | **8** | 23 passed, **3 failed**: the three new slots; `test-red-baseline2.log` |
| Build with fix | `cmake --build build --target zene AudioPluginTest PluginPortsMigrationTest -j 20` | 0 | `build-green.log` |
| **GREEN** | `ctest -R '^AudioPluginTest$' --output-on-failure` (in `build/tests`) | **0** | `test-green-audioplugintest.log`; 26 passed / 0 failed |
| Family | `ctest -R '^(AudioPluginTest|PluginPortsMigrationTest|OpulenZ)$' --output-on-failure` (in `build/tests`) | **0** | 3/3 tests: PluginPortsMigrationTest 73 passed, OpulenZ, AudioPluginTest 26 passed; `test-family-green2.log` |
| 4b: save patch | `git diff -- plugins/SpectrumAnalyzer/Analyzer.h plugins/SpectrumAnalyzer/Analyzer.cpp > .../fix.patch` | 0 | 211 lines |
| 4b: revert | `git apply -R .../fix.patch` | 0 | product files restore to the baseline |
| 4b: build reverted | `cmake --build build --target zene AudioPluginTest -j 20` | 0 | `build-reverted-red.log` |
| **4b RED** | `ctest -R '^AudioPluginTest$' --output-on-failure` (in `build/tests`) | **8** | 23 passed, **3 failed**; `test-reverted-red.log` |
| 4b: restore | `git apply .../fix.patch` | 0 | fix back |
| 4b: build restored | `cmake --build build --target zene AudioPluginTest -j 20` | 0 | `build-restored-green.log` |
| **4b GREEN** | `ctest -R '^AudioPluginTest$' --output-on-failure` (in `build/tests`) | **0** | `test-restored-green.log`; 1/1 test passed |

Note: two unrelated `AudioPluginTest` slots (`gateRecoversAfterNanInput`,
`namHarnessRejectsNegativeWarmup`) fail in a tree whose `gate`/`nam_harness` targets were
never built; building those targets (`libgate.so`, `nam_harness`) makes them pass, which is
how the 26/26 GREEN run was recorded. This is the build tree's state, not the change.

### Gate table

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot
run in this session: it creates `<worktree>/../<name>-feature-check/` and the worktree's
parent is not writable (`EXIT=1`, `PermissionError: [Errno 13] ... zene-qwen-new-p14-389-port-port-feature-check`,
`feature-check.log`). The unmodified script was therefore run through a wrapper that only
redirects where its log files land into the worktree's ignored `build/qwen-logs/` - the gate
commands, worktree and logic are untouched (`build/qwen-logs/run_feature_check_redirected.py`).

```
feature-check zene-qwen-new-p14-389-port-port (base 07d6d02bd, 5 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```
`EXIT=0` (`feature-check-table.log`).

The raw gates were also run individually, before and after:

- `bash tests/fork-sources-gate.sh`: before the manifest fix **EXIT=1**
  (`gate-bash_tests_fork-sources-gate.sh.log`: "a scope manifest does not reproduce from its
  own header recipe"), after **EXIT=0**
  (`gate-fork-sources.log`: 1976 fork-NEW, 1214 inherited, 48 tooling).
- `bash tests/all-sources-reproduce.sh`: before **EXIT=1**, after **EXIT=0**
  (`gate-all-sources-rederived.log`: "REPRODUCES").
- `bash tests/no-upstream-regression-gate.sh` **EXIT=0**; `tests/scripted/check-namespace`
  **EXIT=0**; `bash tests/no-tautology-gate.sh` **EXIT=0**;
  `python3 tests/doc-figures-gate.py` **EXIT=0**.
- `bash tests/complexity-gate.sh --check` **EXIT=1** and
  `bash tests/file-length-gate.sh --check` **EXIT=1**: red on the train on purpose. No new
  item is in a file this change touched: 7 complexity regressions and 19 file-length
  regressions are all pre-existing lines, checked against the cached base listings for
  `07d6d02bd` exactly as `feature_check.py` does (`new-in-my-files: 0` for both).

### The pre-existing `all-sources.txt` breakage

`tests/all-sources.txt` on the base listed 1148 vendored `plugins/SfzPlayer/sfizz/**` paths
that the manifest's own `Regenerate with` recipe excludes (`grep -vE
'^(src/3rdparty/|tests/reference/|...|plugins/SfzPlayer/sfizz/)'`), so
`fork-sources-gate.sh` and `all-sources-reproduce.sh` were red on the base before this change
touched anything (the train-12 snapshot regenerated the file and put those paths back). The
file was re-derived from its header recipe, exactly as the accepted fix did:

```
{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' \
    | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; \
  git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u
```

1148 lines removed; `all-sources-reproduce.sh` now prints REPRODUCES. No first-party source
was added or removed by this.

## Files committed

- `plugins/SpectrumAnalyzer/Analyzer.h` - `TimingCollector`, the `#include <memory>`, and the
  collector member replacing the three counters.
- `plugins/SpectrumAnalyzer/Analyzer.cpp` - `m_timing.record()` in `processImpl()`;
  `reportAudioTiming()` closes and takes one interval; constructor no longer initializes the
  removed counters.
- `tests/src/plugins/AudioPluginTest.cpp` - the three regressions, their injected-measurement
  probes, and the adapted constructor guard.
- `tests/upstream-modifications.txt` - one entry per product file.
- `tests/all-sources.txt` - re-derived from the manifest's own recipe (pre-existing gate
  breakage, 1148 vendored paths removed).
- `docs/qwen-fixes-2026-10/cluster-new-p14-389-port-port.md` - this report.

## Behaviour outside the bug

Unchanged. `SA_DEBUG` is the only preprocessor gate touched and no CMake file in this tree
defines it, so a release build compiles exactly as before (the header's `#ifdef SA_DEBUG`
block, including `<memory>`, is not compiled at all). The report cadence (a check every
second, a print every five), the line text and the guard against a zero count are the same.
Nothing new runs on the audio thread: no allocation, free, lock, wait, I/O or signal - the
collector's allocation happens in the plugin constructor. No control command, undo step or
saved-data format is involved.

## Self-check

Answers to every ASK item from `submit_check.py`.

**? The regression test FAILS with your product change reverted and PASSES with it (exit codes).**

The exact sequence, run and recorded (logs in `build/qwen-logs/new-p14-389-port-port.r1/`):

1. `git diff -- plugins/SpectrumAnalyzer/Analyzer.h plugins/SpectrumAnalyzer/Analyzer.cpp > .../fix.patch` -> `EXIT=0` (211 lines).
2. `git apply -R .../fix.patch` -> `EXIT=0` (product code back to baseline).
3. `cmake --build build --target zene AudioPluginTest -j 20` -> `EXIT=0` (`build-reverted-red.log`).
4. `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure` -> **`EXIT=8`**, "23 passed, 3 failed"; the three failures are exactly
   `analyzerTimingSnapshotStaysCoherent` ("Analyzer has no coherent timing snapshot collector"),
   `analyzerTimingSurvivesNoCallbackAfterRequest` (same) and
   `analyzerTimingReportUsesOneCoherentSnapshot` ("Analyzer resets the count, the sum and the peak with separate exchanges")
   (`test-reverted-red.log`).
5. `git apply .../fix.patch` -> `EXIT=0`.
6. `cmake --build build --target zene AudioPluginTest -j 20` -> `EXIT=0` (`build-restored-green.log`).
7. `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure` -> **`EXIT=0`**, 1/1 test passed
   (`test-restored-green.log`); `./AudioPluginTest` prints 26 passed, 0 failed, and the three slots PASS.

The same RED (`EXIT=8`) was recorded before any fix was written (`test-red-baseline2.log`).

**? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**

`Analyzer::TimingCollector` is the seam. The probes instantiate it and inject the duration of
every callback as a plain `std::uint64_t` (`collector.record(100); collector.record(300); ...`),
then call `request()`/`take()` on one thread. There is no clock read (`std::chrono` is not
called), no `sleep`, no thread, no timer and no race: the values and the interval boundaries
are fixed data, and the expected count/sum/peak of each interval are hand-computed
(`2/400/300`, `1/50/50`, `3/270/110`, `1/10/10`). The third test reads the product source and
asserts the exchange sequence is gone; that is a source contract, not a timing measurement.

**? The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**

- `reportAudioTiming()` is the only reader: it is the only caller of `m_timing.request()` /
  `m_timing.take()` and is reached only from the `QTimer` lambda in the constructor
  (`Analyzer.cpp:74-78`).
- `processImpl()` is the only writer: `m_timing.record()` replaced the three
  `fetch_add`/CAS operations (`Analyzer.cpp:105-109`).
- The removed members `m_dump_count` / `m_sum_execution_ns` / `m_max_execution_ns` have no
  other user in the tree; the same-named `SaProcessor` members are a different per-second CPU
  report and are untouched (`plugins/SpectrumAnalyzer/SaProcessor.h:163-165`).
- The constructor guard that listed the removed counters was adapted to the SA_DEBUG scalars
  the header still declares (`tests/src/plugins/AudioPluginTest.cpp:968-1006`), so it keeps
  guarding `m_last_dump_time` and still fails if a member that exists is left uninitialized.
- `tests/reference/SpectrumAnalyzer/Analyzer.{h,cpp}` is the frozen pre-migration copy of the
  behaviour-preservation harness, not the shipped plugin; it is unchanged and
  `PluginPortsMigrationTest` passes (73 passed, 0 failed).
- The wider type is consistent: count is `std::uint64_t` through `record()`, `Snapshot` and
  the printed division (`static_cast<double>(snapshot.sumNs) / 1.0e6 / snapshot.count`), so no
  narrowing remains. The three exchange calls produced `int` for the count, which is gone.

**? No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**

The change is entirely inside `#ifdef SA_DEBUG` (no CMake file in this tree defines it) and
touches no serialization, no settings key and no control command, so old projects and saved
data load exactly as before; a release build is byte-for-byte the same code path. The
collector's 65536-slot ring is debug-only. At the supported maximum of 192 kHz with 32-frame
periods, the audio thread runs 6000 times per second; a six-second report interval needs
36000 slots, and the added regression covers that case. A GUI stall long enough to fill the
ring still drops and counts measurements instead of overwriting entries awaiting a report.
No other caller uses the collector.

**? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**

Not applicable: the verdict is FIXED and the defect was present on this branch before the
change (`plugins/SpectrumAnalyzer/Analyzer.cpp:127-129` pre-fix, the three separate
`exchange(0)` calls; the counters at `plugins/SpectrumAnalyzer/Analyzer.h:201-203` pre-fix).

### Gates in this session

`submit_check.py` runs `feature_check.py` as a subprocess; that script creates
`<worktree>/../<name>-feature-check/` and this session cannot write the worktree's parent or
the harness log folder (`EXIT=1`, `PermissionError`). The unmodified `feature_check.py` was
run through `build/qwen-logs/run_feature_check_redirected.py`, which redirects only where its
log files land (into the worktree's ignored `build/qwen-logs/`); the gate commands, the
worktree and the gate logic are untouched. Its table:

```
feature-check zene-qwen-new-p14-389-port-port (base 07d6d02bd, 5 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

`EXIT=0`. Run by hand in this worktree, each gate's own exit code agrees: fork-sources 0,
all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0,
doc-figures 0; complexity and file-length exit 1 with no regression line in a file this
change touched (checked against the cached base listings for `07d6d02bd`, 0 new items in my
files for both).

### submit_check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-389-port-port`
cannot run unmodified here for the same reason: its nested `feature_check.py` call creates
`<worktree>/../<name>-feature-check/`. It was run through
`build/qwen-logs/run_submit_check_redirected.py`, which leaves every check in place and only
rewrites the one nested `feature_check.py` invocation to the log-redirecting wrapper above.
Result:

```
PRE-SUBMIT CHECK for new-p14-389-port-port
  PASS  commits on the branch
  PASS  working tree clean
  PASS  report docs/qwen-fixes-2026-10/cluster-new-p14-389-port-port.md
  PASS  tests added or changed - tests/src/plugins/AudioPluginTest.cpp
  PASS  gates (feature_check.py)
RESULT: PASS - submit
```

`EXIT=0` (`submit-check-redirected.log`). Without the wrapper the same command exits 1 with
`PermissionError` on that log directory and the gates row FAILs - an environment limit, not a
gate failure.

## QA review follow-up (2026-10-23)

The independent review found that the original 16384-slot ring held only 16383 measurements,
while the engine supports 192 kHz and 32-frame periods (6000 callbacks/second). A normal
six-second interval can therefore contain 36000 measurements and overflow the original
capacity. `Analyzer.h` now uses 65536 slots, and
`analyzerTimingCollectorCoversMaximumSixSecondReportInterval` checks that all 36000 samples
are present with no drops. The test change is recorded in `tests/upstream-modifications.txt`.

Verification after this correction:

- `cmake --build build --target zene AudioPluginTest opulenz PluginPortsMigrationTest -j12` — EXIT=0.
- `ctest -R '^(AudioPluginTest|PluginPortsMigrationTest|OpulenZ)$' --output-on-failure` from `build/tests` — EXIT=0, 3/3 CTests passed.
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-389-port-port` — EXIT=0, all rows passed.
