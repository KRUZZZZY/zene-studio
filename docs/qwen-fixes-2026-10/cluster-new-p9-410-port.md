# Cluster new-p9-410-port: waterfall history painting race

**Verdict: FIXED** (port of `9cc73ff29` from `bugs/qwen-new-p9-410` onto the 050 train; not merged, cherry-picked or rebased).

**Gate status: both ratchet gates exit 1.** They fail identically on the base commit `b02a0ff2b` with no new findings from this change (see Gates below). The failures are in unrelated files and are not fixed here.

## Was the bug still present on the newest line?

Yes. Before the port, the newest line (`b02a0ff2b`) had the original race:

- `plugins/SpectrumAnalyzer/SaWaterfallView.cpp:147-151` (pre-fix) wrapped `m_processor->getHistory()` in a `QImage` with no copy, holding only `m_reallocationAccess`. The lock was released at line 152, and `temp.scaled()` read the buffer at line 155 afterwards, so a reallocation could free the memory being read.
- `plugins/SpectrumAnalyzer/SaProcessor.cpp:285-288` (pre-fix) assigned `m_history = m_history_work` under `m_dataAccess`, but the view read `m_history` under `m_reallocationAccess` only, so the two were not mutually exclusive.
- `plugins/SpectrumAnalyzer/SaProcessor.h` (pre-fix) had `bool m_flipRequest`, written from the GUI thread and read/cleared by the analyzer thread with no atomic.
- A separate earlier fix (`new-p10-358`) had already initialised `m_flipRequest = false`. That part was ALREADY-FIXED on this line. The copy race and the non-atomic handoff were not.

## Fix (ported)

| File | Change |
|---|---|
| `plugins/SpectrumAnalyzer/SaProcessor.h:31-33` | `#include <QImage>` |
| `plugins/SpectrumAnalyzer/SaProcessor.h:63` | `flipRequest()` uses `m_flipRequest.store(true, relaxed)` |
| `plugins/SpectrumAnalyzer/SaProcessor.h:75` | declares `QImage historySnapshot();` |
| `plugins/SpectrumAnalyzer/SaProcessor.h:147` | `std::atomic<bool> m_flipRequest{false};` |
| `plugins/SpectrumAnalyzer/SaProcessor.cpp:285` | worker flip uses `m_flipRequest.exchange(false, relaxed)` |
| `plugins/SpectrumAnalyzer/SaProcessor.cpp:499-504` | `historySnapshot()` takes `m_reallocationAccess` then `m_dataAccess` (the documented order) and returns a deep `.copy()` before either lock is released |
| `plugins/SpectrumAnalyzer/SaWaterfallView.cpp:147-155` | paints from `m_processor->historySnapshot()` |

`historySnapshot()` is GUI-thread only. The worker `analyze()` runs on `DataprocLauncher`, not on the audio thread, so the added GUI-side lock does not touch the realtime rule. `getHistory()` is kept unchanged (no caller remains in the product tree).

Cost: each painted frame deep-copies the history once (the original fix did the same). The worker is blocked on `m_dataAccess` for the duration of that copy.

## Regression

`tests/src/plugins/PluginPortsMigrationTest.cpp:2787` (`waterfallHistoryIsCopiedUnderProcessorLocks`, declared at line 721). It is the same source-inspection regression as the original commit. It asserts:

- the view scales `m_processor->historySnapshot()` and no longer wraps `getHistory()`;
- `historySnapshot()` takes the reallocation lock, then the data lock, then calls `.copy()`;
- the worker uses `m_flipRequest.exchange(false, ...)`.

Limitation: this is a text-level check, not a runtime race test. It proves the shape of the fix, not that no race remains.

## Evidence (run from the worktree; logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-410-port.r1/logs/`)

Note: ccache's temp directory is under `/run/user/1000`, which the write-jail blocks. Builds after `build-1` set `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-410`.

| Step | Command (abbreviated) | Log | EXIT | Result |
|---|---|---|---|---|
| Build, first try | `cmake --build build --target zene PluginPortsMigrationTest -j2` | build-1.log | 2 | ccache permission error on `/run/user/1000` (environment, not code) |
| Build | same, with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-410` | build-2.log | 0 | built |
| Build LADSPA fixture | `cmake --build build --target decimator_1202` | build-3.log | 0 | `build/plugins/ladspa/decimator_1202.so` produced |
| **RED** (before fix, after decimator fixture) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | test-2.log | **8** | `waterfallHistoryIsCopiedUnderProcessorLocks()` FAILED: `'snapshotUse >= 0 && scaledUse > snapshotUse' returned FALSE`; Totals 45 passed, 1 failed, 2 skipped |
| Build with fix | `cmake --build build --target zene PluginPortsMigrationTest AudioPluginTest -j2` | build-4.log | 0 | `SaProcessor.cpp`/`SaWaterfallView.cpp` recompiled |
| Build analyzer | `cmake --build build --target analyzer -j2` | build-5.log | 0 | already current |
| **GREEN** | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | test-3.log | **0** | 100% tests passed, 1/1 CTest entries |
| AudioPluginTest (touches `m_flipRequest` via memcpy) | `cd build/tests && ctest -R '^AudioPluginTest$' --output-on-failure` | test-4.log | 8 | 19 passed, 2 failed (see below); `waterfallFlipRequestStartsClear` PASS |
| Save product patch | `git diff -- plugins/SpectrumAnalyzer/SaProcessor.cpp plugins/SpectrumAnalyzer/SaProcessor.h plugins/SpectrumAnalyzer/SaWaterfallView.cpp > logs/fix.patch` | — | 0 | |
| Reverse fix | `git apply -R logs/fix.patch` | — | 0 | only the test file remained modified |
| Build reverted | `cmake --build build --target zene PluginPortsMigrationTest -j2` | build-6.log | 0 | |
| **RED proof** (reverted) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | test-5.log | **8** | same assertion failed; 45 passed, 1 failed, 2 skipped |
| Re-apply fix | `git apply logs/fix.patch` | — | 0 | |
| Build re-applied | `cmake --build build --target zene PluginPortsMigrationTest -j2` | build-7.log | 0 | |
| **GREEN proof** (re-applied) | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | test-6.log | **0** | 100% tests passed, 1/1 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | gate-1.log | **1** | 6 REGRESSION lines, none in the four files changed here |
| File-length gate | `bash tests/file-length-gate.sh --check` | gate-2.log | **1** | 14 REGRESSION lines, none in the four files changed here |
| Complexity gate, base | same, in a `git archive b02a0ff2b` export under `/tmp` | gate-base-1.log | **1** | same 6 REGRESSION lines (identical set) |
| File-length gate, base | same, in the base export | gate-base-2.log | **1** | same 14 REGRESSION lines (identical set) |

Affected-family count: `PluginPortsMigrationTest` is one CTest entry (`ctest -N` gave `Total Tests: 1`) and passes 1/1 on the fix. Its QTest suite is 45 passed, 2 skipped (SF2 not built here), 0 failed once the fix is applied; the RED state's only failure was the new regression.

`AudioPluginTest` (19 passed, 2 failed) has two failures that are environmental: `namHarnessRejectsNegativeWarmup` needs `build/plugins/nam_harness`, which does not exist in this build tree, and `gateRecoversAfterNanInput` needs a built Gate module, which was not found. Neither uses the analyzer code. I did not re-run `AudioPluginTest` on the reverted tree, so the claim that it fails identically without the fix rests on the missing artifacts.

## Gates

Both gates exit 1 on this commit and on the base `b02a0ff2b` alike. The regression sets are identical:

- complexity: `XyPadPairTest.cpp dotCentreAlong`, `DawProjectReadTracks.cpp parseNotes`, `ControlCommandsMixerTools.cpp adjust`, `ControlCommandsMixerStrip.cpp setStrip`, `ProjectRevisions.cpp rotateProjectRevision`, `ControlCommandsShortcuts.cpp importedOverrides`;
- file length: 14 files over 500 lines (for example `src/core/TakeLane.cpp` 520, `tests/src/core/CollabTransportTest.cpp` 541).

Neither gate lists `SaProcessor`, `SaWaterfallView` or `PluginPortsMigrationTest`. I did not change any gate, limit or baseline.

## Files committed

- `plugins/SpectrumAnalyzer/SaProcessor.cpp`
- `plugins/SpectrumAnalyzer/SaProcessor.h`
- `plugins/SpectrumAnalyzer/SaWaterfallView.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt` (four entries; also terminated the previously unterminated last line)
- `docs/qwen-fixes-2026-10/cluster-new-p9-410-port.md`

No new product files, so `tests/fork-sources.txt` is unchanged.

## Notes

- The brief said earlier round-0 QA points would be listed; none were provided beyond the port job, and this branch has no earlier commits for this cluster (`git log --grep=new-p9-410` is empty).
- `tests/reference/SpectrumAnalyzer/` contains extracted reference copies of these files. They are not wired into CMake, so I did not change them.
