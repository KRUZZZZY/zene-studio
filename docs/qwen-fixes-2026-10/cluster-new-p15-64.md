FIXED

# cluster new-p15-64 — Python discovery blocked the constructing thread for minutes

Verdict: **FIXED**. The defect the patrol named was real: the default
constructor probed up to four interpreters synchronously, and every probe could
wait 5 s to start plus 30 s to import `onnxruntime`. The probe is now deferred out
of the constructor (it runs in `separate()`, on the job's worker thread) and the
whole discovery sequence is bounded by one 5 s deadline.

Round 1 shipped the deferral plus a first-come-first-served deadline. Review
round 2 found that this starved a working fallback behind a slow candidate
configured through `LMMS_STEM_PYTHON`; that regression is fixed here by sharing
the remaining budget between the remaining candidates, and it has its own
regression test.

## Evidence: the defect was real (round 1)

Historical line 143 is still the same statement on the base revision:

* `src/core/ExternalProcessStemSeparator.cpp:143` (base) — the default
  constructor:
  `m_python(pythonExecutable.isEmpty() ? locatePython() : std::move(pythonExecutable))`.
* `src/core/ExternalProcessStemSeparator.cpp:152-189` (base) — `locatePython()`
  ran `QProcess` for up to four candidates, each with `waitForStarted(5000)` and
  `waitForFinished(30000)`: a hanging `import onnxruntime` cost 35 s per
  candidate, about 2 minutes and 20 s for the sequence, on the calling thread.
* The caller chain that puts that on the GUI thread exists on this branch:
  * `src/gui/StemSplitController.cpp:172` and `:184` call
    `ControlRegistry::instance()->invoke("stem.get_state")` synchronously from
    the GUI.
  * The handler reaches `src/core/ControlStemSupport.cpp:493`
    (`static StemSurface* s_surface = new StemSurface();`), whose constructor at
    `src/core/ControlStemSupport.cpp:134` does
    `m_manager.setSeparator(std::make_unique<ExternalProcessStemSeparator>())`.
  * `src/core/ControlStemSupport.cpp:417` (`probe()`) constructs another default
    separator and calls `isAvailable()`, which (base
    `src/core/ExternalProcessStemSeparator.cpp:241`) called `locatePython()`
    again. `StemSurface::available()` remembers success but re-probes failure on
    every call (`src/core/ControlStemSupport.cpp:144-161`), so repeated
    `stem.get_state` calls repeated the hang.
* The class's own contract says the backend is used by the job worker and never
  by the GUI thread (`include/StemSeparation/StemSeparator.h:38-40`); a
  constructor that spawns and waits on children violates that on the caller's
  thread.

## Round 2 — the reviewer's starvation regression, and the fix

Reviewer's finding: `src/core/ExternalProcessStemSeparator.cpp:242-252` (round 1)
handed the *entire* remaining budget to each candidate. A slow failing candidate
therefore used the whole 5 s deadline, and no later candidate (the working
fallback) was ever started, on every retry. The reviewer measured base = found
the fallback, round-1 branch = "Python discovery ran out of time (5000 ms)".

Reproduced here, deterministically, with the round-1 loop and the committed test
seam (test clock injected; the "slow" candidate advances the clock by exactly
the timeout it is handed): `logs/test-red-2.log` (round-2 log folder), EXIT=8:

```
FAIL!  : StemPythonDiscoveryTest::slowCandidateDoesNotStarveTheFallback() 'probed.size() >= 2' returned FALSE.
   (the working fallback was never tried: the slow candidate consumed the whole budget)
Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 242ms
0% tests passed, 1 tests failed out of 1
```

The same scenario with real child processes was also run once, as a temporary
diagnostic that is **not** committed (it sleeps for real, which the test rules
forbid): a six-second failing `LMMS_STEM_PYTHON` followed by an importable
`/usr/bin/python3` returned `/usr/bin/python3` in 1263 ms
(`.../new-p15-64.r2/logs/manual-real-slow.log`, EXIT=0:

```
QINFO  : StemPythonDiscoveryTest::manualRealSlowProcessReproduction() real-process slow-candidate run: located='/usr/bin/python3' error=''
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 1263ms
```

). The diagnostic slot was removed and the committed suite rebuilt and re-run
(`test-green-3.log`, EXIT=0).

The fix: a candidate may spend only its share of what is left of the deadline,
`max(1, left / candidatesLeft)` (`src/core/ExternalProcessStemSeparator.cpp:294`),
so a slow or hanging candidate cannot consume the budget a later one needs. A
candidate that fails fast leaves its unused share to the others, so the normal
path still gives the remaining candidates more than a quarter each. The overall
budget is unchanged (5 s), the constructor still probes nothing, and a candidate
still cannot start once the deadline is reached.

## The fix (product files, current lines)

* `include/StemSeparation/ExternalProcessStemSeparator.h:44-48` — the
  constructor documents that the interpreter is discovered by `separate()`.
* `include/StemSeparation/ExternalProcessStemSeparator.h:65-70` — `locatePython()`
  documents the one deadline and the per-candidate share.
* `src/core/ExternalProcessStemSeparator.cpp:250-260` — the constructor only
  stores the caller's interpreter hint (`m_python(std::move(pythonExecutable))`,
  `:256`); the CLI and model path stay filesystem-only lookups.
* `src/core/ExternalProcessStemSeparator.cpp:73` — `PythonDiscoveryBudgetMs =
  5000`: one deadline for the whole sequence.
* `src/core/ExternalProcessStemSeparator.cpp:88-98` — `pythonDiscoveryBudgetMs()`
  reads the `LMMS_STEM_PYTHON_PROBE_MS` test hook (same shape as the existing
  `LMMS_STEM_CHUNK_DELAY_MS` hook at `:484`), clamped to `[0, default]`, so a
  value can only make discovery stricter.
* `src/core/ExternalProcessStemSeparator.cpp:101-128` — `probePython()` starts a
  candidate and finishes it within the timeout it is given; on timeout it kills
  and reaps the child.
* `src/core/ExternalProcessStemSeparator.cpp:131-164` — the `LMMS_TESTING`-only
  `DiscoveryHooks` accessors (`discoveryHooks()`, `discoveryNowMs()`,
  `probeInterpreter()`); with no hooks installed they return `steady_clock` and
  the real probe, exactly as before.
* `src/core/ExternalProcessStemSeparator.cpp:241-244` — the test-only setter.
* `src/core/ExternalProcessStemSeparator.cpp:265-323` — `locatePython()`: the
  deadline (`:282`), no process after it, and the fair share (`:294`). It
  reports "ran out of time" separately from "no interpreter found".
* `src/core/ExternalProcessStemSeparator.cpp:411-424` — `separate()` performs the
  deferred discovery once per instance, on the caller (worker) thread.

Every path into discovery is consistent:

* `isAvailable()` (`src/core/ExternalProcessStemSeparator.cpp:368-389`) still
  calls `locatePython()`, now bounded and fair; its behaviour is otherwise
  unchanged.
* `ControlStemSupport::fetchable()` (`src/core/ControlStemSupport.cpp:399-400`)
  calls the same bounded `locatePython()`.
* `StemSurface`'s constructor is cheap, and its `probe()` still measures the same
  three facts (python, CLI, model).
* A caller-provided interpreter path is used without a probe, exactly as before.

## Test seam (committed separately, inert)

`include/StemSeparation/ExternalProcessStemSeparator.h:73-85` declares and
`src/core/ExternalProcessStemSeparator.cpp:131-164,241-244` define a
`DiscoveryHooks` seam under `#ifdef LMMS_TESTING` (the same pattern as
`AudioResampler`'s `*ForTest` members, `include/AudioResampler.h:164`). It
replaces the probe function and the monotonic clock the deadline reads. The seam
has **no behaviour of its own**: with no hooks installed the shipped code is
`steady_clock` + `QProcess`. It is commit `ab76f2ce2`, separate from the
behaviour fix, so the 4b revert below removes only the behaviour and the test
still compiles and fails behaviourally (not with a build error).

## Regression test

`tests/src/core/StemPythonDiscoveryTest.cpp` (fork-NEW; registered in
`tests/fork-sources.txt` and `tests/all-sources.txt`; added to
`tests/CMakeLists.txt` under the existing `LMMS_HAVE_STEM_SPLIT` block).

No timing, no sleeps and no races: interpreter launches are observed through a
`sitecustomize.py` placed on `PYTHONPATH` that appends one line to a marker file
whenever an interpreter starts (one line = one probe); the budget boundary is
driven with `LMMS_STEM_PYTHON_PROBE_MS=0`; and the slow candidate is modelled by
injecting the clock and probe through `DiscoveryHooks` (the probe advances the
injected clock by the exact slice it was handed). Environment variables and hooks
are restored by RAII guards.

It lives in a new file rather than `tests/src/core/StemSplitPipelineTest.cpp`
because that file's `initTestCase()` calls `QSKIP` when the external backend is
unavailable, which skips the whole case on this host (round-1 log
`new-p15-64.r1/logs/test-pipeline-skip.log`):

```
SKIP : StemSplitPipelineTest::initTestCase() external-process backend unavailable: No Python interpreter with the 'onnxruntime' module found ...
Totals: 0 passed, 0 failed, 1 skipped, 0 blacklisted, 552ms
```

Test matrix:

| path | test |
|---|---|
| construction (deferral) | `constructorDoesNotProbeForAnInterpreter` |
| deferred work placement | `separateResolvesTheInterpreterOnDemand` |
| slow candidate + fallback (round 2) | `slowCandidateDoesNotStarveTheFallback` |
| normal discovery | `discoveryFindsAnInterpreterThatImportsOnnxRuntime` |
| budget exhaustion | `exhaustedBudgetLaunchesNoProbe` |
| failure + reconfiguration | `failedProbeIsRetriedAfterReconfiguration` |
| audio thread | none touched: no audio-path file is in the diff; construction, `isAvailable()` and `separate()` are control/worker code |
| platforms | `QProcess` + `QStandardPaths::findExecutable` + `QDir::listSeparator`; the fake module/site are plain Python files, so the test is not POSIX-only; it skips only when no `python3`/`python` exists at all |

## Commands and EXIT codes (round 2, copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-64.r2/logs`.

Seam commit, built with every target that includes the header:

```
cmake --build build --target zene StemPythonDiscoveryTest StemSplitPipelineTest StemSplitMenuTest -j4
                                                             # build-1.log      EXIT=0
```

RED for the round-2 regression (seam committed, round-1 loop in place):

```
cmake --build build --target StemPythonDiscoveryTest -j4      # build-red-1.log  EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-red-1.log 2>&1                                # test-red-1.log   EXIT=8
```

`test-red-1.log`: 7 passed, 1 failed — `slowCandidateDoesNotStarveTheFallback`.

GREEN:

```
cmake --build build --target zene StemPythonDiscoveryTest StemSplitPipelineTest StemSplitMenuTest -j4
                                                             # build-green-1.log EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-green-1.log 2>&1                              # test-green-1.log  EXIT=0
```

The final seven slots run directly (`test-green-2.log`, EXIT=0):

```
PASS   : constructorDoesNotProbeForAnInterpreter()
PASS   : exhaustedBudgetLaunchesNoProbe()
PASS   : separateResolvesTheInterpreterOnDemand()
PASS   : slowCandidateDoesNotStarveTheFallback()
PASS   : discoveryFindsAnInterpreterThatImportsOnnxRuntime()
PASS   : failedProbeIsRetriedAfterReconfiguration()
Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 246ms
```

## 4b — the test catches the round-2 regression

The patch holds only this round's behaviour hunks (the seam is committed):

```
git diff -- include/StemSeparation/ExternalProcessStemSeparator.h \
            src/core/ExternalProcessStemSeparator.cpp > .../fix.patch   # 72 lines
git apply -R .../fix.patch                                  # REVERSE_EXIT=0
cmake --build build --target StemPythonDiscoveryTest -j4    # build-red-2.log  EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-red-2.log 2>&1                               # test-red-2.log   EXIT=8
```

`test-red-2.log`:

```
FAIL!  : StemPythonDiscoveryTest::slowCandidateDoesNotStarveTheFallback() 'probed.size() >= 2' returned FALSE.
   (the working fallback was never tried: the slow candidate consumed the whole budget)
Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted, 242ms
0% tests passed, 1 tests failed out of 1
```

Restored and re-ran:

```
git apply .../fix.patch                                     # FORWARD_EXIT=0
cmake --build build --target zene StemPythonDiscoveryTest StemSplitPipelineTest StemSplitMenuTest -j4
                                                             # build-green-3.log EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-green-4.log 2>&1                              # test-green-4.log  EXIT=0
```

`100% tests passed out of 1`. This is a behavioural red, not a build break: the
test compiles against the reverted code because the seam is a separate commit.

## Family

All 16 registered `Stem` targets built first (`build-stem-family-1.log`,
EXIT=0), then:

```
cd build/tests && ctest -R "Stem" --output-on-failure       # test-stem-family-1.log EXIT=0
```

```
 1/16 StemExportTest ...................   Passed
 2/16 ControlStemExportTest ............   Passed
 3/16 StemJobManagerTest ...............   Passed
 4/16 StemJobLoaderTest ................   Passed
 5/16 StemModelStoreTest ...............   Passed
 6/16 StemQualityTest ..................   Passed
 7/16 StemSplitPipelineTest ............   Passed
 8/16 StemSplitMenuTest ................   Passed
 9/16 StemSplitBackgroundTest ..........   Passed
10/16 ControlStemWavTest ...............   Passed
11/16 StemPythonDiscoveryTest ..........   Passed
12/16 OnnxRuntimeStemSeparatorTest .....   Passed
13/16 ControlStemExportVerb ............   Passed
14/16 ControlStemCommands ..............***Skipped
15/16 StemQualityBench .................***Skipped
16/16 StemAvailabilityDocsGate .........   Passed
100% tests passed out of 16
```

The two skips are environmental (no `onnxruntime` on this host), as round 1
recorded (`new-p15-64.r1/logs/test-skip-reason.log`).

## Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-64
                                                             # feature-check-1.log EXIT=0
```

```
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

## Files changed

Round 1 (`93ab9fb13`, `4eab52386`):

* `src/core/ExternalProcessStemSeparator.cpp` (fork-NEW, product fix)
* `include/StemSeparation/ExternalProcessStemSeparator.h` (fork-NEW, contract)
* `tests/src/core/StemPythonDiscoveryTest.cpp` (new regression test)
* `tests/CMakeLists.txt` (register the test)
* `tests/fork-sources.txt`, `tests/all-sources.txt` (registration)
* `doc/STEM-SPLIT.md` (documents the deferred, bounded discovery)
* the round-1 report

Round 2 (this session):

* `ab76f2ce2` — the `LMMS_TESTING` `DiscoveryHooks` seam (no behaviour change)
* the fair-share discovery loop, its regression, the doc sentence and this
  report (the commit named in `proof.json`)

## Other findings

* `include/RevisionTimeline.h:77` mentions this class's bounded probing in a
  comment; it describes the same intent and needed no edit.
* Not fixed here: `isAvailable()` probes the default interpreter even when the
  instance was constructed with an explicit `pythonExecutable`. That asymmetry
  predates this bug and changing it would alter availability semantics beyond
  the finding; left as is.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with
   it (exit codes).** Yes, twice:
   * Round 2 (the new starvation regression): `git apply -R` EXIT=0, rebuild
     EXIT=0, test EXIT=8 with exactly `slowCandidateDoesNotStarveTheFallback`
     failing (`test-red-2.log`); `git apply` EXIT=0, rebuild EXIT=0, test EXIT=0
     (`test-green-4.log`).
   * Round 1 (deferral + deadline): `new-p15-64.r1/logs/test-red-3.log` EXIT=8
     with three failing slots, then `test-green-8.log` EXIT=0.
   The round-2 red is a test assertion failure, not a build error: the seam was
   committed separately (`ab76f2ce2`) before the behaviour change, so reverting
   the behaviour leaves the test compiling against the old loop.
2. **The test is deterministic: no wall-clock timing, sleeps or races.** The
   committed tests never measure or wait for real time. Interpreter starts are
   counted with a `sitecustomize.py` marker file; the budget boundary is
   `LMMS_STEM_PYTHON_PROBE_MS=0`; the slow candidate injects the clock through
   `DiscoveryHooks` and advances it by the timeout it is handed; environment
   variables and hooks are restored by RAII. No `QTest::qWait`, no
   `QElapsedTimer`, no threads. (The real 6 s reproduction was a temporary,
   uncommitted diagnostic, logged and then removed, precisely because it uses
   real time.)
3. **The fix is at the cause and complete; callers and siblings checked.**
   Cause 1 (probe in the constructor) is gone (`:250-260`, `:411-424`). Cause 2
   (unbounded sequence) is gone (`:73`, `:265-323`), and the round-2 regression
   (one candidate consuming the whole sequence) is gone (`:294`). Every path into
   discovery uses the bounded, fair helper: `isAvailable()` (`:368`),
   `separate()` (`:415`), `ControlStemSupport::fetchable()` (`:399-400`),
   `StemSurface::probe()` (`:417`). CLI/model lookups and a caller-supplied
   interpreter path are unchanged. No command, undo or audio-path file is in the
   diff.
4. **No collateral damage.** No save/load, project, undo or model code changed,
   so old projects load exactly as before. The only new limits are the discovery
   budget and the per-candidate share; both apply only to the probe sequence, the
   hook `LMMS_STEM_PYTHON_PROBE_MS` can only lower the budget, and a candidate
   that fails fast leaves its share to the rest. A discovery that runs out of
   time is retried by the next call, and a successful discovery behaves exactly
   as before (`discoveryFindsAnInterpreterThatImportsOnnxRuntime` pins that the
   first candidate that imports wins). The stem family and the
   `ControlStemExportVerb` agent driver still pass (see above).
5. **ALREADY-FIXED / FALSE-POSITIVE code reference.** Not applicable: the
   verdict is FIXED, and the pre-fix code is quoted at the top of this report
   (`src/core/ExternalProcessStemSeparator.cpp:143` and `:152-189` on the base
   revision; the round-1 loop the reviewer rejected is visible in `git show
   ab76f2ce2:src/core/ExternalProcessStemSeparator.cpp`).
6. **Sibling commands in the command files you changed.** No command file is
   changed by this fix: the diff is `ExternalProcessStemSeparator.{h,cpp}`, the
   new test, `tests/CMakeLists.txt`, the two scope manifests, `doc/STEM-SPLIT.md`
   and this report. The `app.*`/`arrangement.*` ids the pre-submit check lists
   come from the branch's other commits since its merge-base (`git diff
   --name-only` since the base names 435 files, none of them `ControlCommands*`,
   `ControlReversibilityTable*` or `ControlRegistry*`). The `stem.*` commands are
   untouched, no command id is added, renamed or removed, and
   `tools/mcp-zene-control/zene_control/commands_snapshot.json` is unchanged.
