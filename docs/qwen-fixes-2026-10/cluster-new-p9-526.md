FIXED

# cluster new-p9-526: publish the Wave Shaper curve to the audio thread as an immutable snapshot

## Problem

The Wave Shaper audio callback read the same mutable `graphModel` sample array
that the GUI edits in place. A drag writes the array before it emits
`samplesChanged`, so `processImpl()` could read a half-finished curve. The
hand-off is now an immutable snapshot: the GUI publishes a copy, and the audio
thread only reads the published copy.

## Evidence (current tree, file:line)

- Old reader, now replaced: `plugins/WaveShaper/WaveShaper.cpp:77` reads
  `m_wsControls.m_curveSnapshot.samples()` instead of
  `m_wsControls.m_wavegraphModel.samples()`. The read happens once per
  `processImpl()` block (`WaveShaper.cpp:68`); the snapshot read is line 77.
- GUI writers of the mutable table: `src/gui/widgets/Graph.cpp:217`
  (`Graph::drawLineAt()`), which writes through `drawSampleAt()` at line 274
  and only then emits `samplesChanged()` at line 279; the mouse handler writes
  through `setSampleAt()` at line 307; `graphModel::setSampleAt()` at line 539
  writes at 541 and emits `samplesChanged()` at 542. This is the half-finished
  window the snapshot closes.
- Snapshot class: `plugins/WaveShaper/WaveShaperControls.h:80`
  (`WaveCurveSnapshot`), constructor at 86, reader entry `samples()` at 99,
  writer `publish()` at 111 (copy at 114), atomic published/reader/reading
  state at 152/154/156, GUI-thread-only retired/free lists at 158/160.
- Publication hook: `plugins/WaveShaper/WaveShaperControls.cpp:60`
  (`samplesChanged()` calls `publishCurve()` at 62); `publishCurve()` itself at
  69, with the 200-sample length guard at 73 and the copy hand-off at 77.
  Declaration at `WaveShaperControls.h:175`, member at 182.
- Regression slot:
  `tests/src/gui/GraphWaveformEditorGuiTest.cpp:138`
  (`waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot()`).

## Test matrix

The five new cases are in the new slot; the save/load case below is the
existing slot. The durable ctest family log proves the full executable passed
(1/1, 100%).

| Case | Assertion | Line |
| --- | --- | --- |
| default publication | default straight line renders 0.5 at sample 99 | `GraphWaveformEditorGuiTest.cpp:167` |
| completed UI edit | `setSampleAt(99, 0.25f)` is visible to the next block | 171 |
| unpublished half-finished edit | direct write of 0.75 without `samplesChanged` is not visible; audio still sees 0.25 | 176 |
| real click | click on the graph publishes the new sample value | 181 |
| many-edit reuse | 50 alternating edits plus a final 0.4 leaves 0.4 published | 186 |
| save/load unchanged | existing slot `theEditorShowsTheLoadedCurveKeepsItOnDamageAndSavesWhatTheUserDraws()` (`GraphWaveformEditorGuiTest.cpp:82`) round-trips `saveState`/`restoreState` unchanged and rejects a short curve | 82-132 |
| audio-thread constraints | publish runs only from the GUI `samplesChanged` path (`WaveShaperControls.cpp:60-62`); the reader takes the pointer and returns `data` with no lock, allocation, or logging (`WaveShaperControls.h:99-108`, `WaveShaper.cpp:77`) | — |

## Command log (from the run logs; no result is invented)

The p4 logs record tool output, not shell command lines. The forms below are
the commands those logs correspond to; where the log prints an exit code it is
quoted, and where it does not the recorded result is given instead.

| Phase | Command | Recorded result |
| --- | --- | --- |
| RED build | `cmake --build build --target GraphWaveformEditorGuiTest` | build succeeded, log ends `[100%] Built target GraphWaveformEditorGuiTest` (`logs/build-1-red.log`) |
| RED test | `cd build/tests && ./GraphWaveformEditorGuiTest waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot` | exit 1 — `Totals: 2 passed, 1 failed`; `FAIL! ... Compared floats are not the same (fuzzy compare)` (`logs/test-1-red.log`) |
| GREEN build | `cmake --build build --target GraphWaveformEditorGuiTest` | build succeeded, `[100%] Built target waveshaper` and `GraphWaveformEditorGuiTest` (`logs/build-2-fix.log`) |
| GREEN test | `cd build/tests && ./GraphWaveformEditorGuiTest waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot` | exit 0 (log `Totals: 3 passed, 0 failed`) (`logs/test-2-green.log`) |
| 4b reverse build | `cmake --build build --target waveshaper` | exit 0 (`logs/build-3-reverse.log`) |
| 4b reverse test | `cd build/tests && ./GraphWaveformEditorGuiTest waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot` | exit 1 — `Totals: 2 passed, 1 failed`, same fuzzy-compare failure (`logs/test-4-red.log`) |
| 4b reapply build | `cmake --build build --target waveshaper` | exit 0 (`logs/build-4-reapply.log`) |
| 4b reapply test | `cd build/tests && ./GraphWaveformEditorGuiTest waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot` | exit 0 — `Totals: 3 passed, 0 failed` (`logs/test-5-green.log`) |
| family gate | `cd build/tests && ctest -R GraphWaveformEditorGuiTest --output-on-failure` | 1/1 passed, `100% tests passed out of 1` (`logs/test-3-family.log`) |
| build gate | `cmake --build build --target waveshaper GraphWaveformEditorGuiTest` | build succeeded, both targets built (`logs/build-5-gates.log`) |
| feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-526` | `RESULT: PASS`; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all pass (`logs/feature-check-1.log`) |

The 4b pair is the fix-dependence check: with the fix reversed the new slot
fails, and after reapplying it the same slot passes.

## Self-check

The final completion step runs
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py`, and its
result is recorded then.

- Regression is fix-dependent: with the product change reversed,
  `cd build/tests && ./GraphWaveformEditorGuiTest waveShaperCurveEditsReachTheAudioAsAPublishedSnapshot`
  exited 1 (`Totals: 2 passed, 1 failed`, fuzzy compare; `logs/test-4-red.log`);
  after reapplying it the same command exited 0 (`Totals: 3 passed, 0 failed`;
  `logs/test-5-green.log`).
- Deterministic: no wall-clock timing, sleeps or races; the test injects fixed
  values through `setSampleAt`, one direct write with no `samplesChanged`, and a
  synthetic click, then compares exact expected floats. The 50-edit burst also
  uses injected values only.
- Cause and completeness: the GUI `samplesChanged` slot publishes once through
  `publishCurve()` (`plugins/WaveShaper/WaveShaperControls.cpp:60-77`); the
  audio reader takes the pointer with a single atomic load and returns `data`
  (`WaveShaperControls.h:98-108`); `WaveShaper.cpp:77` reads that published
  array. Every Wave Shaper edit path emits `samplesChanged` — Graph click/drag
  through `setSampleAt` or `drawLineAt`'s final emit, reset, smooth, add/sub,
  and `loadGraph` — so no Wave Shaper caller keeps a stale read; `setLength` and
  `setRange` emit `lengthChanged`/`rangeChanged` instead, are not used by this
  plugin, and its model stays fixed at 200.
- No collateral damage: the existing save/load slot
  (`GraphWaveformEditorGuiTest.cpp:82`) still round-trips `saveState`/
  `restoreState` and rejects a short curve; the ctest family run passed 1/1
  (`logs/test-3-family.log`). The only added check is `publishCurve`'s
  `length() == 200` guard, which matches the plugin's fixed 200-sample model and
  `loadGraph`'s exact-count requirement, so it cannot reject a legitimate Wave
  Shaper edit; no other caller uses `publishCurve`. `loadSettings` keeps loading
  old projects through the same `waveform::loadGraph` path.
- ALREADY-FIXED / FALSE-POSITIVE: not applicable; the verdict is FIXED, as the
  red/green pair above shows.
- Sibling commands: no command file was changed. Commit `b6c9e36` touches only
  `plugins/WaveShaper/WaveShaper.cpp`, `plugins/WaveShaper/WaveShaperControls.cpp`,
  `plugins/WaveShaper/WaveShaperControls.h`, the GUI test,
  `tests/upstream-modifications.txt` and this report. `app.about`, `app.ask`,
  `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`,
  `app.tours`, `app.update_check_get`, `app.update_check_run`,
  `app.update_check_set`, `app.version`, `app.whats_new`,
  `arrangement.fit_tracks` and `arrangement.get_state` do not call the Wave
  Shaper publish path and are unaffected.

Pre-commit checks also showed `git diff --check` exited 0 and
`git status --porcelain` was empty after the commit.

## Other findings

The reverse experiment confirms the regression slot is sensitive to the
snapshot fix alone rather than to unrelated test setup. No product change was
made after the commit; the commit sha is recorded in the worker report for the
p6 proof manifest.
