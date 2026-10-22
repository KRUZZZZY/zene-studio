# Cluster new-p10-470-port-port

Verdict: **ALREADY-FIXED**

The reported defect - `plugins/SpectrumAnalyzer/Analyzer.h`'s `m_last_dump_time` having no
initializer while the constructor never names it - does not exist on this branch. The
constructor initializes all four `SA_DEBUG` debug members before the first audio callback can
run, and the only read of `m_last_dump_time` happens on the GUI thread, not on the audio
callback. The port's final accepted shape (guard test + report, no production change) is
re-applied here by hand, with the guard fixed for the member rename that made the previous
port fail on 050/m0-train-10.

## File evidence

- `plugins/SpectrumAnalyzer/Analyzer.cpp:67-72` - the constructor's initializer list names
  `m_last_dump_time(std::chrono::steady_clock::now())`, `m_dump_count(0)`,
  `m_sum_execution_ns(0)` and `m_max_execution_ns(0)`. Landed with commit `13d8b4604`
  ("Fix analyzer debug timer (cluster new-p10-471)"), which is an ancestor of this branch's
  base `dbb15dded` (`git merge-base --is-ancestor 13d8b4604 dbb15dded` exits 0).
- `plugins/SpectrumAnalyzer/Analyzer.h:98-100` - the three counters additionally carry
  declaration-level `{0}` initializers (from `076e28348`, cluster new-p9-537).
- `plugins/SpectrumAnalyzer/Analyzer.cpp:91-117` - `processImpl()` (the audio callback) never
  reads `m_last_dump_time`; it reads only the three atomic counters, which are initialized at
  declaration and in the constructor. The finding's "the debug processing path reads it on
  every audio callback" does not match the code on this branch either.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:122-136` - `m_last_dump_time` is read only by
  `reportAudioTiming()`, which runs on the GUI thread via `m_reportTimer`
  (`Analyzer.cpp:77-78`), after the constructor has completed.
- `rg -n m_last_dump_time plugins/SpectrumAnalyzer` - the only product reads are
  `Analyzer.cpp:125` (GUI reporter) and `Analyzer.cpp:130` (its reset); no audio-thread read.

## Why the previous port could never pass on train-10

The port's final commit `9aaade45b` guarded the constructor by checking that its initializer
list contains `m_last_dump_time(`, `m_dump_count(`, `m_sum_execution(` and `m_max_execution(`.
But `076e28348` (cluster new-p9-537, already an ancestor of train-10's tip `f81196b43`;
`git merge-base --is-ancestor 076e28348 f81196b43` exits 0) renamed the members to
`m_sum_execution_ns` / `m_max_execution_ns`, so the substrings `m_sum_execution(` and
`m_max_execution(` no longer occur in `Analyzer.cpp`. The guard therefore failed the moment it
merged (merge `a66f270d3`, reverted by `839841cb2`), taking AudioPluginTest down with it. The
re-application below checks the current member names instead.

## The re-applied change (hand, not cherry-picked)

- `tests/src/plugins/AudioPluginTest.cpp` - adds the slot
  `analyzerConstructorInitializesTheDebugMembers()` to the test host that already covers this
  area (it already compiles `SaProcessor.cpp` and includes `Analyzer.h` with `SA_DEBUG`). The
  guard reads the constructor's initializer list out of the product source (the plugin
  implementation is not linked into this host, same constraint as the adjacent
  `analyzerDebugTimerKeepsStdoutOffAudioThread()`) and asserts it names `m_last_dump_time(`,
  `m_dump_count(`, `m_sum_execution_ns(` and `m_max_execution_ns(`. Deterministic: source-text
  check, no clocks, no sleeps, no threads.
- `tests/all-sources.txt` - repaired a pre-existing gate breakage, see the gate section.

No production file changed: `git diff --stat` over `plugins/` is empty.

## Regression proof (RED before / GREEN after)

The RED side is the counterfactual the finding claims: the constructor does not initialize the
debug members. Product file temporarily edited, patch captured, restored with `git apply -R`.

RED (initializers removed from `plugins/SpectrumAnalyzer/Analyzer.cpp`, uncommitted):

```
cmake --build build --target AudioPluginTest -j$(nproc)   # EXIT=0  (log build-6-red.log)
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioPluginTest$' --output-on-failure
# EXIT=8  (log test-5-red.log)
# FAIL!  : AudioPluginTest::analyzerConstructorInitializesTheDebugMembers()
#   'initializers.contains(QByteArray{member} + '(')' returned FALSE.
#   (the Analyzer constructor does not initialize m_last_dump_time)
```

GREEN (patch reversed, original constructor initialization restored):

```
git apply -R $LOGS/fix.patch                              # EXIT=0
cmake --build build --target AudioPluginTest -j$(nproc)   # EXIT=0  (log build-7-green.log)
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^AudioPluginTest$' --output-on-failure
# EXIT=0  (log test-6-green.log) - 100% tests passed out of 1
QT_QPA_PLATFORM=offscreen ./AudioPluginTest               # EXIT=0  (log test-7-green-direct.log)
# Totals: 23 passed, 0 failed, 0 skipped, 0 blacklisted, 1091ms
# PASS   : AudioPluginTest::analyzerConstructorInitializesTheDebugMembers()
```

`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-470-port-port.r1/logs`.
The counterfactual patch is saved there as `fix.patch`.

## Affected-family test runs (from build/tests only)

Baseline on the pristine tree first (the fresh build tree lacked runtime plugin modules;
built them, then the baseline is green):

```
cmake --build build --target AudioPluginTest -j$(nproc)   # EXIT=0  (build-1-baseline-audioplugintest.log)
ctest -R '^AudioPluginTest$'  -> EXIT=8 (missing build/plugins/libeq.so; environmental)
cmake --build build --target eq gate -j$(nproc)           # EXIT=0  (build-3-baseline-eq-gate.log)
ctest -R '^AudioPluginTest$'  -> EXIT=8 (missing build/plugins/nam_harness; environmental)
cmake --build build --target nam_harness -j$(nproc)       # EXIT=0  (build-5-baseline-namharness.log)
ctest -R '^AudioPluginTest$'  -> EXIT=0  GREEN baseline   (test-3-baseline-audioplugintest.log)
ctest -R '^SessionTuningTest$' -> EXIT=0  GREEN baseline  (test-4-baseline-sessiontuning.log)
```

After the change:

```
ctest -R '^AudioPluginTest$'  -> EXIT=0  (23 test functions, 1 new)   (test-6-green.log)
ctest -R '^SessionTuningTest$' -> EXIT=0                              (test-8-green-sessiontuning.log)
```

`TimelineSpeechTest` - the third train-10 casualty - does not exist on this line:
`ctest -N` lists `RevisionTimelineTest`, `TimelineTest`, `ArrangerTimelineTest` and no
`TimelineSpeechTest`, and no file under `tests/` references that name. The port's merged
content on train-10 was only this report plus the AudioPluginTest guard, so it could not have
touched SessionTuningTest/TimelineSpeechTest by content; on this tree SessionTuningTest passes
and TimelineSpeechTest is absent.

## Gates

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # first run: EXIT=1
#   FAIL fork-sources exit 1 | FAIL all-sources-reproduce exit 1
```

Both failures are pre-existing at the pristine base: with my test file reverted to HEAD the
same two scripts exit 1 as well (`bash tests/fork-sources-gate.sh` -> 1,
`bash tests/all-sources-reproduce.sh` -> 1; logs in
`~/.cache/zene-tmp/opencode/gate-baseline-*.log`). Cause: the train-10 snapshot automation
commit `5ef150729` added 1148 vendored `plugins/SfzPlayer/sfizz/external/abseil-cpp/...`
entries to `tests/all-sources.txt` even though the manifest's own "Regenerate with" recipe
excludes `plugins/SfzPlayer/sfizz/` (vendored). Repaired exactly as the gate prescribes -
regenerated the entry list with the header recipe, header comments untouched; the verify diff
prints `REPRODUCES`. Gate rerun:

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   # EXIT=0
#   PASS fork-sources / all-sources-reproduce / no-upstream-regression /
#       check-namespace / no-tautology / doc-figures / complexity / file-length
#   RESULT: PASS
```

Log: `gate-2.log` in the log folder. No gate was re-anchored; the manifest was re-derived from
its own committed recipe.

## Files committed

- `tests/src/plugins/AudioPluginTest.cpp` (the guard; existing registered test source, no new
  file, so no `tests/fork-sources.txt` / `tests/upstream-modifications.txt` entry is needed)
- `tests/all-sources.txt` (gate repair by its own recipe, see above)
- `docs/qwen-fixes-2026-10/cluster-new-p10-470-port-port.md` (this report)

## Self-check

- *The regression test FAILS with your product change reverted and PASSES with it (exit
  codes): yes.* The counterfactual (the finding's claim: constructor does not initialize the
  debug members) was applied to `plugins/SpectrumAnalyzer/Analyzer.cpp` (uncommitted),
  rebuilt, run: ctest exit 8, assertion "the Analyzer constructor does not initialize
  m_last_dump_time" (test-5-red.log). Reversed with `git apply -R fix.patch`, rebuilt, run:
  ctest exit 0, 23/23 functions pass (test-6-green.log, test-7-green-direct.log).
- *The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses): yes.* The guard's seam is the same one the adjacent
  `analyzerDebugTimerKeepsStdoutOffAudioThread()` already uses in this host: it opens the
  product source `plugins/SpectrumAnalyzer/Analyzer.cpp` (located from `__FILE__`) and text-
  matches the constructor's initializer list against the four member names. No clock is read,
  no value is injected, no thread runs, nothing sleeps - the result depends only on the bytes
  of the source file.
- *The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent: yes.* No production file changed (`git diff HEAD~1 -- plugins
  src include` is empty), so there is no product caller to re-verify; the siblings of the
  debug state were checked and are consistent: the three atomic counters keep both their
  declaration initializers (`Analyzer.h:98-100`) and the constructor's
  (`Analyzer.cpp:69-71`), `m_last_dump_time`'s only reader is the GUI-thread reporter
  (`Analyzer.cpp:125`), and the guard's member-name list was checked against the current
  `Analyzer.cpp` constructor, which is exactly the check the previous port got wrong. The
  sibling class `SaProcessor` has its own, separate `SA_DEBUG` `m_last_dump_time`
  (`SaProcessor.h:162`, read at `SaProcessor.cpp:155`) that this branch's constructor does not
  name - that is a distinct defect outside this finding's scope (the finding's file:line is
  `plugins/SpectrumAnalyzer/Analyzer.h:78`, the Analyzer class), and the original port
  deliberately left it for a follow-up cluster; this cluster does the same and reports it
  here so it is not lost.
- *No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it: yes.* The change is
  test-only - no serializer, loader, limit or product check was added or altered, so project
  loading and saved data behave byte-identically. The only non-test edit,
  `tests/all-sources.txt`, is a gate measurement manifest: it now lists exactly the 3045
  paths its own committed recipe derives (verified with the header's verify diff, which
  prints `REPRODUCES`), contains no product behaviour, and no runtime caller reads it.
- *If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
  contract that shows it: yes.* `plugins/SpectrumAnalyzer/Analyzer.cpp:67-72` - the
  constructor's initializer list names `m_last_dump_time(std::chrono::steady_clock::now())`,
  `m_dump_count(0)`, `m_sum_execution_ns(0)`, `m_max_execution_ns(0)`; commit `13d8b4604`
  (cluster new-p10-471) introduced it and is an ancestor of the base (`git merge-base
  --is-ancestor 13d8b4604 dbb15dded` exits 0). The finding's second clause - the audio
  callback reads the timer - is also contradicted by the code: `processImpl()`
  (`Analyzer.cpp:91-117`) never touches `m_last_dump_time`; its only reads are
  `Analyzer.cpp:125` and the reset at `Analyzer.cpp:130`, both inside the GUI-thread
  `reportAudioTiming()` connected to `m_reportTimer` at `Analyzer.cpp:77-78`.

