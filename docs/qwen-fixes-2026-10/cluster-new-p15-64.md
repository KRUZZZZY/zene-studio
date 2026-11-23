FIXED

# cluster new-p15-64 — Python discovery blocked the constructing thread for minutes

Verdict: **FIXED**. The defect was real, in the code the patrol named; the
constructor probed up to four interpreters synchronously and every probe could
wait 5 s to start plus 30 s to import `onnxruntime`. The interpreter probe is now
deferred out of the constructor (it runs in `separate()`, on the job's worker
thread) and the whole discovery sequence is bounded by one 5 s deadline.

## Evidence: the defect was real

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

The GUI freeze therefore needs no speculation: the path above is the shipping
one, and it is the path `StemSplitController` takes when the stem menu opens.

## The fix (product files)

* `include/StemSeparation/ExternalProcessStemSeparator.h:44-49` and `:64-68` — the
  constructor documents that the interpreter is discovered by `separate()`, not
  by the constructor, and `locatePython()` documents the bounded sequence.
* `src/core/ExternalProcessStemSeparator.cpp:207-217` — the constructor now only
  stores the caller's interpreter hint (`m_python(std::move(pythonExecutable))`).
  It still resolves the CLI path and the model path (filesystem checks only, no
  child process).
* `src/core/ExternalProcessStemSeparator.cpp:72` — `PythonDiscoveryBudgetMs =
  5000`: one deadline for the whole probe sequence.
* `src/core/ExternalProcessStemSeparator.cpp:87-96` — `pythonDiscoveryBudgetMs()`
  reads the `LMMS_STEM_PYTHON_PROBE_MS` test hook (same shape as the existing
  `LMMS_STEM_CHUNK_DELAY_MS` hook at `:440`), clamped to `[0, default]`, so a
  value can only make discovery stricter.
* `src/core/ExternalProcessStemSeparator.cpp:100-128` — `probePython()` starts
  the candidate and finishes it within the remaining budget; on timeout it kills
  and reaps the child, so no probe outlives the call.
* `src/core/ExternalProcessStemSeparator.cpp:222-275` — `locatePython()` checks
  the deadline before each candidate, starts no process once it is exhausted,
  and reports "ran out of time" separately from "no interpreter found".
* `src/core/ExternalProcessStemSeparator.cpp:366-380` — `separate()` performs the
  deferred discovery once per instance, on the caller (worker) thread.

Every path into discovery is consistent:

* `isAvailable()` (`src/core/ExternalProcessStemSeparator.cpp:324-345`) still
  calls `locatePython()`, now bounded; its behaviour is otherwise unchanged.
* `ControlStemSupport::fetchable()` (`src/core/ControlStemSupport.cpp:399-400`)
  calls the same bounded `locatePython()`.
* `StemSurface`'s constructor is now cheap, and its `probe()` still measures the
  same three facts (python, CLI, model).
* A caller-provided interpreter path is used without a probe, exactly as before.

Trade-off, stated: the deadline is first-come-first-served. If the first
candidate hangs for the whole budget, later candidates are not started on that
call; the next call probes again. Before, the loop moved on after each 30 s
timeout, but only by holding the caller for minutes. A rejection of this
trade-off would need a per-candidate share, which would fail a legitimately slow
cold import on the first candidate; 5 s is ~10x the measured reference start
(`doc/STEM-SPLIT.md`: CLI start + import + inference begins in ~0.45 s).

## Regression test

`tests/src/core/StemPythonDiscoveryTest.cpp` (new, fork-NEW; registered in
`tests/fork-sources.txt` and `tests/all-sources.txt`; added to
`tests/CMakeLists.txt` under the existing `LMMS_HAVE_STEM_SPLIT` block).

No timing, no sleeps and no races: interpreter launches are observed through a
`sitecustomize.py` placed on `PYTHONPATH` that appends one line to a marker file
whenever an interpreter starts. One line = one probe. The budget boundary is
driven through `LMMS_STEM_PYTHON_PROBE_MS=0`, which needs no wait at all.
Environment variables are restored by an RAII guard.

It lives in a new file rather than `tests/src/core/StemSplitPipelineTest.cpp`
because that file's `initTestCase()` calls `QSKIP` when the external backend is
unavailable, which skips the whole case on this host
(`logs/test-pipeline-skip.log`:

```
SKIP : StemSplitPipelineTest::initTestCase() external-process backend unavailable: No Python interpreter with the 'onnxruntime' module found ...
Totals: 0 passed, 0 failed, 1 skipped, 0 blacklisted, 552ms
```

).

Test matrix:

| path | test |
|---|---|
| construction (deferral) | `constructorDoesNotProbeForAnInterpreter` |
| deferred work placement | `separateResolvesTheInterpreterOnDemand` |
| normal discovery | `discoveryFindsAnInterpreterThatImportsOnnxRuntime` |
| budget exhaustion | `exhaustedBudgetLaunchesNoProbe` |
| failure + reconfiguration | `failedProbeIsRetriedAfterReconfiguration` |
| audio thread | none touched: no audio-path file is in the diff; construction, `isAvailable()` and `separate()` are control/worker code |
| platforms | `QProcess` + `QStandardPaths::findExecutable` + `QDir::listSeparator`; the fake module/site are plain Python files, so the test is not POSIX-only; it skips only when no `python3`/`python` exists at all |

## Commands and EXIT codes (copied from the logs)

Log directory:
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-64.r1/logs`.

Build tree needed regenerating once for the new test target:

```
cmake --build build --target rebuild_cache          # configure-1.log      EXIT=0
```

RED (test first, product code unchanged):

```
cmake --build build --target StemPythonDiscoveryTest -j4    # build-red-1.log  EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-red-1.log 2>&1                                # test-red-1.log   EXIT=8
```

`test-red-1.log` (first version of the file, 4 test slots):

```
FAIL!  : StemPythonDiscoveryTest::constructorDoesNotProbeForAnInterpreter() '!QFile::exists(marker)' returned FALSE. ...
FAIL!  : StemPythonDiscoveryTest::exhaustedBudgetLaunchesNoProbe() '!QFile::exists(marker)' returned FALSE. ...
Totals: 4 passed, 2 failed, 0 skipped, 0 blacklisted, 224ms
```

GREEN:

```
cmake --build build --target zene StemPythonDiscoveryTest StemSplitPipelineTest StemSplitMenuTest -j4
                                                             # build-green-1.log EXIT=0
cd build/tests && ctest -R "StemPythonDiscoveryTest|StemSplitPipelineTest|StemSplitMenuTest" \
    --output-on-failure                                     # test-green-1.log EXIT=0
```

The final file has a fifth slot (`separateResolvesTheInterpreterOnDemand`), so
the 4b proof below was re-run against the final file.

## 4b — the test catches the defect (final test file)

```
git diff -- include/StemSeparation/ExternalProcessStemSeparator.h \
            src/core/ExternalProcessStemSeparator.cpp > .../fix.patch
git apply -R .../fix.patch                                  # EXIT=0
cmake --build build --target StemPythonDiscoveryTest -j4    # build-red-3.log  EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-red-3.log 2>&1                               # test-red-3.log   EXIT=8
```

`test-red-3.log`:

```
FAIL!  : StemPythonDiscoveryTest::constructorDoesNotProbeForAnInterpreter() '!QFile::exists(marker)' returned FALSE. ...
FAIL!  : StemPythonDiscoveryTest::exhaustedBudgetLaunchesNoProbe() '!QFile::exists(marker)' returned FALSE. ...
FAIL!  : StemPythonDiscoveryTest::separateResolvesTheInterpreterOnDemand() '!QFile::exists(marker)' returned FALSE. ...
Totals: 4 passed, 3 failed, 0 skipped, 0 blacklisted, 261ms
0% tests passed, 1 tests failed out of 1
```

Restored and re-ran:

```
git apply .../fix.patch                                     # EXIT=0
cmake --build build --target zene StemPythonDiscoveryTest -j4  # build-green-4.log EXIT=0
cd build/tests && ctest -R StemPythonDiscoveryTest --output-on-failure \
    > .../test-green-8.log 2>&1                             # test-green-8.log EXIT=0
```

`100% tests passed out of 1`. The final binary run directly
(`./StemPythonDiscoveryTest`, `test-green-7.log`, EXIT=0):

```
PASS   : StemPythonDiscoveryTest::constructorDoesNotProbeForAnInterpreter()
PASS   : StemPythonDiscoveryTest::exhaustedBudgetLaunchesNoProbe()
PASS   : StemPythonDiscoveryTest::separateResolvesTheInterpreterOnDemand()
PASS   : StemPythonDiscoveryTest::discoveryFindsAnInterpreterThatImportsOnnxRuntime()
PASS   : StemPythonDiscoveryTest::failedProbeIsRetriedAfterReconfiguration()
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 241ms
```

Family: the `ctest -R "Stem"` family, all 16 registered targets built first:

```
cd build/tests && ctest -R "Stem" --output-on-failure       # test-green-9.log EXIT=0
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

The two skips are environmental, not caused by this change;
`test-skip-reason.log` (EXIT=0) says:

```
SKIP: no python interpreter with the onnxruntime module on this host, so the external-process backend cannot run
```

## Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-64
                                                             # feature-check-2.log EXIT=0
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

`bash tests/fork-sources-gate.sh` also ran on its own
(`fork-sources-gate-2.log`, EXIT=0): "PASS: every tracked source in scope is
registered (2761 fork-NEW, 1213 inherited, 49 tooling)."

## Files changed

* `src/core/ExternalProcessStemSeparator.cpp` (fork-NEW, product fix)
* `include/StemSeparation/ExternalProcessStemSeparator.h` (fork-NEW, contract)
* `tests/src/core/StemPythonDiscoveryTest.cpp` (new regression test)
* `tests/CMakeLists.txt` (register the test)
* `tests/fork-sources.txt`, `tests/all-sources.txt` (registration)
* `doc/STEM-SPLIT.md` (documents the deferred, bounded discovery)
* this report

## Other findings

* `include/RevisionTimeline.h:77` mentions this class's bounded probing in a
  comment; it describes the same intent and needed no edit.
* Not fixed here: `isAvailable()` probes the default interpreter even when the
  instance was constructed with an explicit `pythonExecutable`. That asymmetry
  predates this bug and changing it would alter availability semantics beyond
  the finding; left as is.

## Self-check

1. **Regression test fails with the product change reverted, passes with it.**
   Yes. Reverted (`git apply -R` EXIT=0), rebuilt (EXIT=0), test EXIT=8 with 3
   failing slots (`test-red-3.log`); restored (`git apply` EXIT=0), rebuilt
   (EXIT=0), test EXIT=0 (`test-green-8.log`). Both cycles are in
   `## 4b — the test catches the defect` above.
2. **Deterministic: no wall-clock timing, sleeps or races.**
   The test never measures time. Interpreter starts are counted through a
   `sitecustomize.py` marker file (one line per child), and the deadline
   boundary is observed by setting the test hook `LMMS_STEM_PYTHON_PROBE_MS=0`
   (an exhausted budget starts no process — asserted by the absent marker), then
   clearing it and asserting the same candidates do probe. No `QTest::qWait`,
   no `QElapsedTimer`, no threads are involved.
3. **Fix at the cause and complete; callers and siblings checked.**
   Cause 1 (probe in the constructor) is gone: the constructor at
   `src/core/ExternalProcessStemSeparator.cpp:207-217` only stores the hint;
   `separate()` (`:366-380`) resolves it on the worker. Cause 2 (unbounded
   sequence) is gone: `locatePython()` (`:222-275`) has one 5 s deadline and
   `probePython()` (`:100-128`) bounds both the start and the import.
   Every path into discovery uses the bounded helper: `isAvailable()` (`:324`),
   `separate()` (`:366-380`), `ControlStemSupport::fetchable()` (`:399-400`),
   `StemSurface::probe()` (`:417`). The CLI and model paths and the
   caller-supplied interpreter path are unchanged. No control command, undo path
   or audio-path file is touched (the diff has no `src/gui/`, `Song*`, `*Undo*`,
   `*Record*`, `*Transport*`, `Take*` or `AudioEngine*` file).
4. **No collateral damage.**
   No save/load, project, undo or model code changed, so old projects load
   exactly as before. The only new limit is the discovery budget; it applies
   only to the probe sequence and can only be lowered by the `LMMS_STEM_PYTHON_PROBE_MS`
   test hook (values outside `[0, 5000]` keep the default). A discovery that
   runs out of time is retried by the next call, and a successful discovery
   behaves exactly as before (`discoveryFindsAnInterpreterThatImportsOnnxRuntime`
   pins that the first candidate that imports wins). The stem family and the
   `ControlStemExportVerb` agent driver still pass (see above).
5. **ALREADY-FIXED / FALSE-POSITIVE code reference.** Not applicable: the
   verdict is FIXED, and the pre-fix code is quoted at the top of this report
   (`src/core/ExternalProcessStemSeparator.cpp:143` and `:152-189` on the base
   revision).
