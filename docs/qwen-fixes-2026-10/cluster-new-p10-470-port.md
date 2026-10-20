# Cluster new-p10-470-port — the SpectrumAnalyzer's SA_DEBUG debug timer

Verdict: **ALREADY-FIXED** on this branch (`bugs/qwen-new-p10-470-port`).

## The finding

Luna's patrol reported, for `plugins/SpectrumAnalyzer/Analyzer.h:78`: *"`m_last_dump_time`
has no initializer, and the constructor does not initialize it. The debug processing path
reads it on every audio callback ... the first call to `processImpl()` reads the
uninitialized member, which is undefined behavior. fix: Initialize the debug counters and
timer fields."*

## Evidence that the finding does not hold on this branch

The parent of this port, `ded355440871` (the commit this branch was cut from, and the
merge-base with the train `050/m0-train-8`), already initializes every `SA_DEBUG`
member of the plugin before the audio thread can read one:

- `plugins/SpectrumAnalyzer/Analyzer.cpp:67-72` — the constructor's initializer list names
  all four of them:
  ```cpp
  	#ifdef SA_DEBUG
  	, m_last_dump_time(std::chrono::steady_clock::now())
  	, m_dump_count(0)
  	, m_sum_execution(0.0f)
  	, m_max_execution(0.0f)
  	#endif
  ```
  A member that a constructor's initializer list names is not default-initialized, so
  none of the four can be indeterminate - the "uninitialized debug timer" the finding
  describes cannot exist here.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:90-99` — `processImpl()` reads `m_last_dump_time`
  (line 91, the `isStatsIntervalElapsed` predicate), reads `m_dump_count` (lines 93 and 96)
  and resets them (lines 98-99). `processImpl()` is a member function: it can only run
  after the constructor of the same object has returned, so every read happens on a
  defined value.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:113-115` — the same function increments
  `m_dump_count` and accumulates `m_sum_execution` / `m_max_execution`, again only on
  defined values.
- The initializer list arrived with commit `13d8b4604` ("Fix analyzer debug timer
  (cluster new-p10-471)"), an ancestor of the parent; that commit also changed the timer
  to `std::chrono::steady_clock::time_point` and added the five second predicate at
  `plugins/SpectrumAnalyzer/Analyzer.h:66-72`.
- The area keeps its registered regression: `tests/src/plugins/AudioPluginTest.cpp:764-769`
  (`analyzerDebugTimerUsesMonotonicTimePoint`, added by the same commit) checks the
  predicate with injected timestamps (four seconds is below the threshold, six seconds
  reaches it) and pins the timer's type. It runs in the registered ctest `AudioPluginTest`.

## Behavioural proof (throw-away probe, not part of this branch)

The code citation above was confirmed at run time. A probe test - the round-3 test file
plus its `Analyzer` ctest target, used only for the experiment and **not committed** -
was built against the parent's product code exactly as it is on this branch:

```
cmake --build build --target Analyzer > .../logs/build-probe-1.log 2>&1          EXIT=0
cd build/tests && ctest -R '^Analyzer$' --output-on-failure > .../logs/test-probe-1.log 2>&1   EXIT=8
```

`test-probe-1.log` records:

```
PASS   : AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback()
FAIL!  : AnalyzerTest::processorDebugMembersAreInitializedBeforeTheFirstAnalysis() ...
   Actual   (processor->m_dump_count): 32767
   Expected (0)                      : 0
```

The `Analyzer` case **passes** with the parent's product code: the constructor defines
the timer and the counters, so the reported bug is absent. The same probe was then run
against the state the finding describes - the constructor no longer naming the members -
and it fails there, which shows the probe is sensitive to the very thing the finding
describes:

```
cmake --build build --target Analyzer > .../logs/build-probe-2.log 2>&1          EXIT=0
cd build/tests && ctest -R '^Analyzer$' --output-on-failure > .../logs/test-probe-2.log 2>&1   EXIT=8
```

```
FAIL!  : AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback() ...
   Actual   (analyzer->m_dump_count): -1602742832
   Expected (0)                     : 0
```

After the experiment the probe file, its `tests/CMakeLists.txt` target and its
`tests/all-sources.txt` entry were removed again, and the product sources were restored
to the parent's content.

## What this port changes

Nothing in the product. The earlier rounds of this port had added declaration-level
initializers to `plugins/SpectrumAnalyzer/Analyzer.h` and
`plugins/SpectrumAnalyzer/SaProcessor.h`, removed the constructor's `SA_DEBUG`
initializer list, and added a new `Analyzer` regression target
(`tests/src/plugins/Analyzer.cpp` plus its `tests/CMakeLists.txt` and
`tests/all-sources.txt` registrations) and the matching `tests/upstream-modifications.txt`
entries. All of that is removed by this commit:

- `plugins/SpectrumAnalyzer/Analyzer.cpp` keeps the parent's constructor initialization
  (lines 67-72) - the fix that makes the reported bug impossible.
- `plugins/SpectrumAnalyzer/Analyzer.h` and `plugins/SpectrumAnalyzer/SaProcessor.h` are
  back to the parent's content.
- `tests/src/plugins/Analyzer.cpp` is deleted, together with its `Analyzer` ctest target
  and its `tests/all-sources.txt` entry; `ctest -N -R '^Analyzer$'` now reports
  `Total Tests: 0`.
- The `tests/upstream-modifications.txt` entries the port had added for the three
  inherited files are back to the parent's text.

## Out of scope: a separate defect in the sibling class `SaProcessor`

The probe above exposed a different, still-open defect in the same plugin, in the data
processor rather than in the plugin class. It is **not** the reported finding and is not
fixed by this port; it is tracked separately:

- `plugins/SpectrumAnalyzer/SaProcessor.h:160-165` — the data processor's `SA_DEBUG`
  members (`m_last_dump_time`, `m_dump_count`, `m_sum_execution`, `m_max_execution`)
  have no initializer, and its constructor (`plugins/SpectrumAnalyzer/SaProcessor.cpp:50-60`)
  does not name them, so they are indeterminate.
- `plugins/SpectrumAnalyzer/SaProcessor.cpp:153-165` — the analysis thread reads them on
  every analysed block and divides the accumulated time by the dump counter
  (`m_sum_execution / m_dump_count`, line 158) with an indeterminate divisor.
- The probe's `SaProcessor` case fails on the parent's code
  (`Actual (processor->m_dump_count): 32767`), which is the evidence for this separate
  issue.

A follow-up cluster should give the data processor the same declaration-level
initializers the plugin class received from new-p10-471. It is left untouched here
because the review of this port asks for the analyzer port to be reported as
ALREADY-FIXED with no production change.

## Verification on the current (already-fixed) code

Exact commands run in this session, from the worktree root unless another directory is
stated. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-470-port.r3/logs`.

| Check | Exact command | Exit |
| --- | --- | ---: |
| Build the product and the registered test binary | `cmake --build build --target zene analyzer AudioPluginTest > .../logs/build-final-2.log 2>&1` | 0 |
| The port's test target is gone | `cd build/tests && ctest -N -R '^Analyzer$' > .../logs/ctest-discovery-1.log 2>&1` | 0 (`Total Tests: 0`) |
| The registered coverage for this area passes | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > .../logs/test-final-3.log 2>&1` | 0 |
| The same binary, per case | `cd build/tests && QT_QPA_PLATFORM=offscreen ./AudioPluginTest > .../logs/test-final-4.log 2>&1` | 0 (21 passed, 0 failed) |
| All gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-feature-check-3.log 2>&1` | 0 |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-3.log 2>&1` | 1 |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length-3.log 2>&1` | 1 |
| Post-commit build of the committed tree | `cmake --build build --target zene analyzer AudioPluginTest > .../logs/build-final-3.log 2>&1` | 0 |
| Post-commit run of the targeted test | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure > .../logs/test-final-5.log 2>&1` | 0 |
| Post-commit gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-feature-check-4.log 2>&1` | 0 |

`test-final-3.log` records `1/1 Test #492: AudioPluginTest ... Passed` and
`100% tests passed out of 1`; `test-final-4.log` records
`PASS : AudioPluginTest::analyzerDebugTimerUsesMonotonicTimePoint()` and
`Totals: 21 passed, 0 failed`.

`feature_check.py` reports `PASS` for all eight gates. The two raw ratchet gates exit 1
because they are red on the train on purpose: they list 6 (complexity) and 14
(file-length) items, none of them in a file this port touches - `feature_check.py`
compares both lists against the base commit and reports `no new item in your files` for
each.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-470-port.md` - this report.
- `plugins/SpectrumAnalyzer/Analyzer.cpp`, `plugins/SpectrumAnalyzer/Analyzer.h`,
  `plugins/SpectrumAnalyzer/SaProcessor.h`, `tests/CMakeLists.txt`,
  `tests/all-sources.txt`, `tests/upstream-modifications.txt` - restored to the parent's
  content, so the branch's only difference from the parent is this report.

## Self-check

**The regression test fails with the product change reverted and passes with it.** This
port has no product change to revert - the finding is already fixed at the parent - so no
new regression is added; the area's registered regression
`AudioPluginTest::analyzerDebugTimerUsesMonotonicTimePoint` (ctest `AudioPluginTest`)
was rerun and passes (`ctest -R '^AudioPluginTest$'` exit 0, 21/21 test functions). The
behavioural proof of the already-fixed state is the throw-away probe documented above:
it passes against the parent's product code and fails when the parent's constructor
initialization is removed. `submit_check.py`'s mechanical item "tests added or changed"
therefore reports FAIL for this branch: an ALREADY-FIXED port carries no product change
and no new test, which is the convention of the other ALREADY-FIXED clusters in this
repository (`docs/qwen-fixes-2026-10/cluster-5371.md`, `cluster-1530.md`,
`cluster-3591.md` - their commits touch the report only).

**Is the test deterministic?** The registered test that covers the area injects its
timestamps (four and six seconds) instead of measuring wall-clock time; the throw-away
probe used a poisoned storage pattern rather than a timing race. No sleep, no timer and
no thread synchronisation are involved anywhere.

**Is the fix at the cause and complete?** There is nothing to fix in this port: the
cause (a member that no initialiser defines) does not exist on this branch, the
constructor defines all four members (`plugins/SpectrumAnalyzer/Analyzer.cpp:67-72`), and
every reader of them (`plugins/SpectrumAnalyzer/Analyzer.cpp:90-99` and `:113-115`) runs
after the constructor. The one remaining hole in the same plugin - the data processor's
own `SA_DEBUG` members - is a different defect, evidenced above and left to a separate
cluster.

**No collateral damage?** The product sources, the test registrations and the
divergence ledger are byte-for-byte the parent's content, so old projects, saved data and
the plugin's behaviour are exactly what the parent produced. The port adds no command, no
limit and no check, and it changes nothing on the audio thread.

**Files changed by this port** - the report only; every other path is restored to the
parent's content, which is what `git diff ded355440871` shows.

**Verdict: ALREADY-FIXED**
