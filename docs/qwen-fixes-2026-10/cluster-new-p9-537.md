# Cluster new-p9-537

Verdict: **FIXED**

Finding: with `SA_DEBUG` enabled, `Analyzer::processImpl()` printed its timing report to `std::cout` on the audio thread.

Luna's round-1 points were not available to this session: the brief lists none, and neither the run folder (`brief.md`, `session.log`) nor the branch history contains review text or an earlier commit for this cluster. This fix is written against the brief's finding and rules only.

## The defect (original code)

- `plugins/SpectrumAnalyzer/Analyzer.cpp:95` (HEAD) writes `std::cout` inside the `isStatsIntervalElapsed` branch of `processImpl()` (HEAD lines 88-101). That is the audio callback, so the finding is real.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:89-101` (HEAD) also computes the report with `float` accumulators that are plain members written from the audio thread.

## The fix

- `plugins/SpectrumAnalyzer/Analyzer.cpp:106-113`: `processImpl()` only takes a timestamp at its start and, at its end, adds to three lock-free atomics (`m_dump_count`, `m_sum_execution_ns`, and a CAS-maintained `m_max_execution_ns`). It does not print.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:122-137`: `reportAudioTiming()` runs on the GUI thread. It exchanges the counters to zero and prints the same `Analyzer audio thread: ... ms avg / ... ms peak.` line, but only when `isStatsIntervalElapsed` says five seconds have passed and the count is non-zero, which matches the original behaviour.
- `plugins/SpectrumAnalyzer/Analyzer.cpp:76-79`: the constructor connects a `QTimer` (`m_reportTimer`, 1 s period) to `reportAudioTiming()`. Being on the constructing thread, the timer belongs to the GUI thread.
- `plugins/SpectrumAnalyzer/Analyzer.h:94-104`: the counters become `std::atomic` (nanoseconds, `std::uint64_t`), with a `static_assert` that the 64-bit atomic is lock-free. `m_reportTimer` is declared last, so it is destroyed before the counters it reads. `reportAudioTiming()` is declared at `Analyzer.h:76-77`.

Every change sits inside `#ifdef SA_DEBUG`. No CMake file in this tree defines `SA_DEBUG`, so release builds are unchanged.

## The regression

- `tests/src/plugins/AudioPluginTest.cpp:772-790`: `analyzerDebugTimerKeepsStdoutOffAudioThread`. It reads `plugins/SpectrumAnalyzer/Analyzer.cpp` via `__FILE__` and asserts that the text of `Analyzer::processImpl()` does not contain `std::cout` (line 788). It also asserts that the report text `Analyzer audio thread:` still exists in the file.
- Why a source contract: `AudioPluginTest` does not build the Spectrum Analyzer controls. `Analyzer` needs `SaControls`, which has `Q_OBJECT` and no moc output in this target. Constructing `Analyzer` in-process therefore needs a CMake change to the test target. This follows the existing precedent `tests/src/core/SaSpectrumViewTest.cpp`, which also checks source text because its view cannot be constructed in its target.

## Evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-537.r2/logs/`. Each exit code below is the one shown by the command.

| Step | Exact command (cwd: worktree root unless noted) | Exit | Log |
| --- | --- | ---: | --- |
| Build, first attempt (ccache could not write its temp file under `/run/user/1000`) | `cmake --build build --target AudioPluginTest` | 2 | build-1.log |
| Build with writable ccache temp dir | `CCACHE_TEMPDIR=/tmp/zene-qwen-p9-537-ccache-tmp cmake --build build --target AudioPluginTest` | 0 | build-2.log |
| Test run aborted in `initTestCase` (`libeq.so` not built). Not RED evidence. | `ctest -R '^AudioPluginTest$' --output-on-failure` (cwd `build/tests`) | 8 | test-1.log |
| Build the Eq plugin module the test loads | `CCACHE_TEMPDIR=... cmake --build build --target eq` | 0 | build-3.log |
| **RED**: original code, new test | `ctest -R '^AudioPluginTest$' --output-on-failure` (cwd `build/tests`) | 8 | test-2.log: `FAIL! : analyzerDebugTimerKeepsStdoutOffAudioThread() '!callback.contains(QStringLiteral("std::cout"))' returned FALSE`, `AudioPluginTest.cpp(788)` |
| Build after the fix | `CCACHE_TEMPDIR=... cmake --build build --target zene analyzer AudioPluginTest` | 0 | build-4.log |
| Run after the fix, before the other artefacts existed | `ctest -R '^AudioPluginTest$' --output-on-failure` | 8 | test-3.log: new slot PASS; `namHarnessRejectsNegativeWarmup` and `gateRecoversAfterNanInput` FAIL because `nam_harness` and the Gate module were not built |
| Build the artefacts those two tests load | `CCACHE_TEMPDIR=... cmake --build build --target gate nam_harness` | 0 | build-5.log |
| **GREEN**: fix in place | `ctest -R '^AudioPluginTest$' --output-on-failure` (cwd `build/tests`) | 0 | test-4.log: `100% tests passed out of 1` |
| SA_DEBUG branch of the `.cpp` type-checks (the module build has no `SA_DEBUG`) | `c++ <CXX_DEFINES/INCLUDES/FLAGS of analyzer.dir/flags.make> -DSA_DEBUG -Werror -fsyntax-only plugins/SpectrumAnalyzer/Analyzer.cpp` | 0 | sa-debug-syntax-1.log |
| Proof, step 1: save and reverse only the product patch | `git diff -- plugins/SpectrumAnalyzer/Analyzer.h plugins/SpectrumAnalyzer/Analyzer.cpp > fix.patch && git apply -R fix.patch` | 0 | proof-1.log |
| Proof, step 2: rebuild with the product patch reversed | `CCACHE_TEMPDIR=... cmake --build build --target analyzer AudioPluginTest` | 0 | build-6.log |
| **Proof RED**: product patch reversed | `ctest -R '^AudioPluginTest$' --output-on-failure` (cwd `build/tests`) | 8 | test-5.log: the same `FAIL!` at `AudioPluginTest.cpp(788)` |
| Proof, step 3: reapply the product patch | `git apply fix.patch` | 0 | proof-2.log (both files byte-identical to the saved fixed copies) |
| Proof, step 4: rebuild | `CCACHE_TEMPDIR=... cmake --build build --target analyzer AudioPluginTest` | 0 | build-7.log |
| **Proof GREEN**: product patch restored | `ctest -R '^AudioPluginTest$' --output-on-failure` (cwd `build/tests`) | 0 | test-6.log |
| Affected family, verbose | `ctest -R '^AudioPluginTest$' -V` (cwd `build/tests`) | 0 | test-7.log: 1 ctest test; 22 QTest slots `PASS`, 0 `FAIL!`, including `analyzerDebugTimerKeepsStdoutOffAudioThread` |
| Test-name check: is there an `^Analyzer$` ctest? | `ctest -N -R 'Analyzer\|SaControls\|AudioPluginTest'` (cwd `build/tests`) | 0 | ctest-N-1.log: only `AudioPluginTest` (#492) matches |
| Complexity gate, before the change | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity-pre-1.log |
| File-length gate, before the change | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length-pre-1.log |
| **Complexity gate, after the fix** | `bash tests/complexity-gate.sh --check` | 1 | gate-complexity-2.log |
| **File-length gate, after the fix** | `bash tests/file-length-gate.sh --check` | 1 | gate-file-length-2.log |

Gate caveat: both ratchet gates already exit 1 on the untouched tree, and that is still true after the fix. Their `REGRESSION:` lines are identical before and after (checked with `diff`). The failures are in files this change does not touch, for example `tests/src/gui/XyPadPairTest.cpp` (`dotCentreAlong`, CCN 16) and `tests/src/core/RevisionTimelineTest.cpp` (600 lines). I did not fix them, because they are outside this cluster, and I did not change any gate limit or baseline. This change adds no gate entry.

## Limits

- The regression is a source contract. It proves the callback body has no `std::cout`. It does not run the callback. A runtime test needs `Analyzer` built in the test target, which needs a CMake change for `SaControls` and its moc. That is an owner decision.
- The report is not executed here. The `SA_DEBUG` branch of `Analyzer.cpp` was only type-checked (`-fsyntax-only -Werror`), so I have not seen the printed line at runtime.
- The reporter prints from a `QTimer` on the thread that constructed the `Analyzer`. In normal use that is the GUI thread. On a thread without a Qt event loop, the debug report stays silent.
- The report is checked once a second and printed when at least five seconds have passed since the last one, so the period is 5–6 seconds, not exactly 5.
- The counters are read with separate `exchange` calls, so a callback that lands between them is counted in the next report. This is stated in the code comment.
- The brief's `ctest -R '^Analyzer$'` target does not exist in this tree (`ctest-N-1.log`). The regression therefore runs in `AudioPluginTest`, as the brief's file list already pointed.
- Environment only: the ccache temp directory under `/run/user/1000` is not writable in this session, so the builds used `CCACHE_TEMPDIR=/tmp/zene-qwen-p9-537-ccache-tmp`. The `eq`, `gate` and `nam_harness` build targets were built to satisfy the test's plugin lookups. No source was changed for this.

## Files committed

- `plugins/SpectrumAnalyzer/Analyzer.cpp`
- `plugins/SpectrumAnalyzer/Analyzer.h`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt` (three entries for this cluster)
- `docs/qwen-fixes-2026-10/cluster-new-p9-537.md`
