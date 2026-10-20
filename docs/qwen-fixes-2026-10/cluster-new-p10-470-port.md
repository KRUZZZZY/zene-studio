# Cluster new-p10-470-port — the SpectrumAnalyzer's SA_DEBUG members are initialized where they are declared

Verdict: **FIXED** on this branch (`bugs/qwen-new-p10-470-port`).

## What the finding said, and what the tree actually held

The finding (Luna's patrol, `plugins/SpectrumAnalyzer/Analyzer.h:78` in the tree it
inspected) was: *"`m_last_dump_time` has no initializer, and the constructor does not
initialize it. The debug processing path reads it on every audio callback ... the first
call to `processImpl()` reads the uninitialized member, which is undefined behavior.
fix: Initialize the debug counters and timer fields."*

That was true of the tree the patrol walked: the declaration carried no initializer and
the constructor did not name the members, so under `SA_DEBUG` the members were
default-initialized (indeterminate) and the debug path read them - the counters were even
used as a divisor (`m_sum_execution / m_dump_count`).

On this branch the sibling cluster new-p10-471 (commit `13d8b4604`) had already changed
the timer's type to `std::chrono::steady_clock::time_point` and had added a constructor
initialization for the four members, so the indeterminate read was no longer reachable
*through the constructor* - but the declaration still carried no initializer, the
initialization depended on the constructor remembering to name the members, and the
sibling class `SaProcessor` had the same defect with no constructor initialization at all.

## The fix (file:line)

The initialization now lives with the declaration in both classes of the plugin, so it
cannot be lost by a later constructor change, and the constructor no longer repeats it:

- `plugins/SpectrumAnalyzer/Analyzer.h:88-93` - the four `SA_DEBUG` members carry
  declaration-level initializers; the debug timer is anchored with the construction-time
  steady clock, so the five second reporting window starts when the plugin is built (the
  value the constructor used to write).
- `plugins/SpectrumAnalyzer/Analyzer.cpp:59-69` - the constructor no longer names the
  `SA_DEBUG` members.
- `plugins/SpectrumAnalyzer/SaProcessor.h:160-165` - the same four members of the data
  processor carry declaration-level initializers. This is the sibling of the finding:
  `plugins/SpectrumAnalyzer/SaProcessor.cpp:153-165` reads `m_last_dump_time` and
  `m_dump_count` on every analysed block and divides the accumulated time by the dump
  counter, and the `SaProcessor` constructor (`SaProcessor.cpp:50-60`) never named them.

Values at construction are unchanged with respect to the pre-port tree
(`13d8b4604`): the timer is the construction-time clock reading, the counters are zero.
Nothing outside the `SA_DEBUG` blocks changed, so the plugin's behaviour, its saved
settings and the project files it writes are untouched.

## The regression test

`tests/src/plugins/Analyzer.cpp` (ctest `Analyzer`, registered in `tests/CMakeLists.txt`
and `tests/all-sources.txt`; it compiles the plugin's own sources with `SA_DEBUG`):

- `AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback` poisons the exact
  storage the `Analyzer` will occupy with the byte pattern `0xAB`, constructs the plugin
  in place, and asserts that the debug members do not hold that pattern and that the
  counters are zero; it then drives one `processImpl()` callback - the path the finding
  names - and checks that the debug counters count it once.
- `AnalyzerTest::processorDebugMembersAreInitializedBeforeTheFirstAnalysis` does the same
  for the sibling `SaProcessor`, which is the class that divides by the dump counter.

Determinism: the test injects the "uninitialized" value itself (the poisoned storage)
instead of relying on luck, and it never measures a duration - the only clock value it
compares is the stored timestamp against a `now()` read *after* the construction, which is
a monotonic invariant, never a timing race. No sleep, no timer and no thread
synchronisation are involved. The five second predicate itself is covered with injected
timestamps by the sibling cluster's `AudioPluginTest::analyzerDebugTimerUsesMonotonicTimePoint`.

Stability was measured: 10 runs of the unfixed build failed every time (no crash, the
objects are torn down by a guard even when an assertion aborts the test), 20 runs of the
fixed build passed every time.

## Evidence

All commands ran from the worktree root unless another directory is stated. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-470-port.r3/logs`.

| Step | Exact command | Exit |
| --- | --- | ---: |
| RED, product fix reversed | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-470-port.r3/logs/fix.patch` | 0 |
| RED, rebuild | `cmake --build build --target Analyzer > .../logs/build-red-8.log 2>&1` | 0 |
| RED, regression fails | `cd build/tests && ctest -R '^Analyzer$' --output-on-failure > .../logs/test-red-8.log 2>&1` | 8 |
| GREEN, product fix restored | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-470-port.r3/logs/fix.patch` | 0 |
| GREEN, rebuild | `cmake --build build --target Analyzer > .../logs/build-green-6.log 2>&1` | 0 |
| GREEN, regression passes | `cd build/tests && ctest -R '^Analyzer$' --output-on-failure > .../logs/test-green-6.log 2>&1` | 0 |
| Family build (zene + plugin + both test binaries) | `cmake --build build --target zene analyzer Analyzer AudioPluginTest > .../logs/build-family-2.log 2>&1` | 0 |
| Family run (2 ctest targets) | `cd build/tests && ctest -R '^(Analyzer\|AudioPluginTest)$' --output-on-failure > .../logs/test-family-6.log 2>&1` | 0 |
| Gates (all of them) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py . > .../logs/gate-feature-check-2.log 2>&1` | 0 |
| Complexity ratchet (raw) | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-2.log 2>&1` | 1 |
| File-length ratchet (raw) | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length-2.log 2>&1` | 1 |

`fix.patch` holds the complete product fix and was produced with
`git diff -- plugins/SpectrumAnalyzer/Analyzer.h plugins/SpectrumAnalyzer/Analyzer.cpp plugins/SpectrumAnalyzer/SaProcessor.h > .../logs/fix.patch`
against a staged baseline in which the four `SA_DEBUG` members of both classes have no
initializer and the constructor does not name them - that is the state the finding
describes ("`m_last_dump_time` has no initializer, and the constructor does not initialize
it"). The patch therefore contains exactly the declaration-level initializers, and
reverting it reproduces the uninitialised members.

The RED log records the two failures of the unfixed build:
```
FAIL!  : AnalyzerTest::debugMembersAreInitializedBeforeTheFirstAudioCallback() Compared values are not the same
   Actual   (analyzer->m_dump_count): -1298971184
   Expected (0)                     : 0
FAIL!  : AnalyzerTest::processorDebugMembersAreInitializedBeforeTheFirstAnalysis() Compared values are not the same
   Actual   (processor->m_dump_count): 32765
   Expected (0)                     : 0
```

The GREEN log records `1/1 Test #476: Analyzer ... Passed` and `100% tests passed out of 1`.

Affected family: 2 ctest targets (`Analyzer`, `AudioPluginTest`), both green; the QtTest
binaries report 4/4 and 21/21 test functions. `AudioPluginTest`'s `nam` and `gate` cases
need the optional fixtures `nam_harness` and the `gate` plugin module, which are
`EXCLUDE_FROM_ALL` and were not built in this build tree; after
`cmake --build build --target nam_harness gate` (exit 0) the whole family is green.

The two raw ratchet gates exit 1 because they are red on the train on purpose: they list
6 (complexity) and 14 (file-length) items, none of them in a file this change touches -
`feature_check.py` compares both lists against the base commit and reports
`no new item in your files` for each, so all eight gates pass.

## Files committed

- `plugins/SpectrumAnalyzer/Analyzer.h` - declaration-level initializers for the four `SA_DEBUG` members.
- `plugins/SpectrumAnalyzer/Analyzer.cpp` - the constructor no longer names the `SA_DEBUG` members.
- `plugins/SpectrumAnalyzer/SaProcessor.h` - the same for the data processor's `SA_DEBUG` members.
- `tests/src/plugins/Analyzer.cpp` - the regression test (two cases).
- `tests/upstream-modifications.txt` - the divergence reasons for the three inherited files.
- `docs/qwen-fixes-2026-10/cluster-new-p10-470-port.md` - this report.

## Self-check

**Does the regression test fail with the product change reverted and pass with it?**
Yes - `ctest -R '^Analyzer$'` exits 8 with the fix reversed (`test-red-8.log`, the two
assertions quoted above) and exits 0 with the fix applied (`test-green-6.log`).

**Is the test deterministic (no wall-clock timing, sleeps or races)?**
Yes - the seam is the poisoned storage: the test writes the `0xAB` pattern over the
object's storage before the placement construction, so an uninitialised member has a
deterministic value instead of whatever the stack held. The only clock value compared is
the stored timestamp against a `now()` read after the construction (a monotonic
invariant, never a duration). There is no sleep, no timer and no thread synchronisation;
10 unfixed runs failed every time and 20 fixed runs passed every time.

**Is the fix at the cause and complete - every caller and sibling checked?**
The cause is a member that no initialiser defines; the fix defines the members at their
declaration. Every reader of the four members was checked: `Analyzer::processImpl`
(`plugins/SpectrumAnalyzer/Analyzer.cpp:80-112`) and the data processor's debug block
(`plugins/SpectrumAnalyzer/SaProcessor.cpp:153-165`) - the sibling class with the same
defect, which is fixed in the same commit and covered by the second test case. A tree-wide
grep shows the four names only in `Analyzer.h`/`Analyzer.cpp`, `SaProcessor.h`/`SaProcessor.cpp`
and the test; the reference copies under `tests/reference/SpectrumAnalyzer/` are the
pre-port snapshot and are not built. The five second predicate
(`plugins/SpectrumAnalyzer/Analyzer.h:66-72`) and the timer type are unchanged, so the
sibling cluster's `AudioPluginTest::analyzerDebugTimerUsesMonotonicTimePoint` still holds.
One related defect was found but deliberately **not** changed: the data processor still
measures its interval with a 32-bit `high_resolution_clock` counter
(`plugins/SpectrumAnalyzer/SaProcessor.cpp:154`), which is the truncated-counter problem
cluster new-p10-471 fixed on the other side of the plugin; fixing it would change the
debug output cadence and is outside this finding.

**No collateral damage?** The change touches only the `SA_DEBUG` blocks, which exist only
in debug builds. The plugin has no serialised state of its own (its controls are the
project's own models), so old projects and saved data load exactly as before, and no
limit or check was added that other callers could hit. The values at construction are the
same as before the port (timer = construction-time clock, counters = 0), so the debug
output covers the same period with the same content. Nothing on the audio thread
allocates, frees, locks, waits or logs because of this change: the debug path already
read a clock and printed every five seconds, and the change only decides where the
initial value comes from - a clock read during construction, not during processing.
No control command was added, so the `ControlCommandsSnapshot` canary does not move.

**Files changed and their effect**

| File | Effect |
| --- | --- |
| `plugins/SpectrumAnalyzer/Analyzer.h` | the four `SA_DEBUG` members get declaration-level initializers (timer anchored at construction) |
| `plugins/SpectrumAnalyzer/Analyzer.cpp` | the constructor stops naming the `SA_DEBUG` members |
| `plugins/SpectrumAnalyzer/SaProcessor.h` | the same four members of the data processor get declaration-level initializers |
| `tests/src/plugins/Analyzer.cpp` | the regression test for both classes |
| `tests/upstream-modifications.txt` | the divergence reasons for the three inherited files |
| `docs/qwen-fixes-2026-10/cluster-new-p10-470-port.md` | this report |
