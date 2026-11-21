# Cluster new-p12-224 — Pitch-preserving playback ignores speed fades

**Verdict: FIXED** (the two ratchet gates are **not green** — pre-existing regressions outside this
diff, proven identical at HEAD; see "Gates" below. Nothing in this fix adds a gate regression.)

## The bug (Luna patrol BUG 16)

`src/core/SamplePlayHandle.cpp` — a clip's speed curve (ED-152 / ED-179: `speedFrom`/`speedTo`,
`speedInTicks`/`speedOutTicks`) had **no effect in PreservePitch or RubberBand mode**:

1. `m_speeds = m_edits.hasSpeed() && !m_preservePitch;` (old line 241) dropped the speed curve for
   any clip that renders through the stretcher, and
2. `renderPreservingPitch()` computed the stretcher's source-consumption speed from `warpRatio()`
   alone, never `speedHere()`.

The resample path divides the speed in (`ratio = warpRatio() / speedHere()`, line 514), so the two
modes did **not** "put the same source frames on the same timeline" — the contract stated in
`include/SamplePlayHandle.h` (`renderPreservingPitch` doc) and in `renderPreservingPitch`'s own
comment ("the two modes therefore consume the source identically and the difference between them is
only the waveform"). `docs/KNOWN-LIMITATIONS.md`'s pitch-stretch section does not list this as a
stated limit; the user-visible effect was a silently ignored edit.

## The fix (smallest change that honours the contract)

- `src/core/SamplePlayHandle.cpp:245` — `m_speeds = m_edits.hasSpeed();` (no longer drops the curve
  for a preserving clip; zero behaviour change for the resample path, where the flag was already set
  identically).
- `src/core/SamplePlayHandle.cpp:641` — `renderPreservingPitch()` now builds the same converter
  ratio the resample path uses — `warpRatio() / speedHere()` — before taking its reciprocal, so the
  stretcher (WSOLA **and** Rubber Band, which share this `speed`) consumes the source at the rate
  the warp mapping *and* the clip's speed edits ask for. Pure arithmetic on snapshotted PODs: no
  allocation, no locking, no logging (audio-thread safe, same as before).
- `include/SamplePlayHandle.h` — the `renderPreservingPitch` doc formula updated to match (comment
  only; declarations unchanged).

## Regression test

`tests/src/tracks/SampleClipStretchTest.cpp:575` — `aSpeedEditMovesTheSourceInBothModes` (added to
the existing pitch-stretch clip-path suite; helpers `makeBurst` / `makeBurstClip` / `burstCenter` in
the file's anonymous namespace). Fixture: a Hann-windowed burst at a quarter of a 2 s source, a 2x
warp (so the clip renders through the stretcher) and a constant half speed (`speedFrom = speedTo =
50`). The 2x warp and the half speed cancel, so the render consumes 1 source frame per output frame
and the burst must land at the **middle** of the (half-length) output — in **both** modes. With the
bug the preserving render lands it at a quarter of the output.

Measured (evidence lines from the test binary):

| render | burst centre / output length (buggy) | burst centre / output length (fixed) |
| --- | --- | --- |
| Resample (speed applied) | 0.5500 | 0.5500 |
| PreservePitch | **0.2801** (speed ignored) | **0.5483** (matches resample) |

## Evidence table

All logs under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-224.r1/logs/`.
Commands run from the worktree root unless noted `cd build/tests`.

| Step | Command | EXIT | Log | Result |
| --- | --- | --- | --- | --- |
| RED build | `cmake --build build --target SampleClipStretchTest > LOGS/build-1.log 2>&1; echo EXIT=$?` | 0 | build-1.log | test built on unfixed product code |
| **RED run** | `cd build/tests && ctest -R '^SampleClipStretchTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | **8** | test-1.log | `FAIL! 'stretchedAt > 0.40 && stretchedAt < 0.60' returned FALSE. (preserve_pitch landed the burst at 0.280136 of the render; ... the resample mode landed it at 0.550023)` — Totals: 8 passed, **1 failed** |
| GREEN build (after fix) | `cmake --build build --target zene SampleClipStretchTest TakeLaneCompPlaybackTest SampleClipWindowTest SampleClipWarpTest SampleClipRubberBandTest W3EngineTest AudioEditingTest AudioClipLoopTest ClipFadesRenderTest ClipOffsetFadeTest ClipOffsetFadePairTest FrozenTakeRoundTwoTest FrozenTakeHandoffTest MixerToolsTest > LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | build-2.log | `zene` + every target including the changed header rebuilt |
| **GREEN run** | `cd build/tests && ctest -R '^SampleClipStretchTest$' --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?` | **0** | test-2.log | 1/1 passed |
| GREEN detail | `cd build/tests && QT_QPA_PLATFORM=offscreen ./SampleClipStretchTest > LOGS/test-3.log 2>&1; echo EXIT=$?` | 0 | test-3.log | Totals: **9 passed, 0 failed**; burst resample 0.5500 / preserve 0.5483 |
| family build | `cmake --build build --target AudioStretcherTest ClipTransposeTest > LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | build-3.log | two family targets not yet built in this build tree |
| **family run** | `cd build/tests && ctest -R 'SampleClipStretchTest\|SampleClipRubberBandTest\|SampleClipWarpTest\|AudioStretcherTest\|ClipOffsetFadePairTest\|ClipOffsetFadeTest\|ClipFadesRenderTest\|FrozenTakeRoundTwoTest\|FrozenTakeHandoffTest\|TakeLaneCompPlaybackTest\|ClipTransposeTest\|SampleClipWindowTest' --output-on-failure > LOGS/test-5.log 2>&1; echo EXIT=$?` | **0** | test-5.log | **100% tests passed out of 12** (0 not-run; affected-family count = 12) |
| 4b extract | `git diff -- src/core/SamplePlayHandle.cpp include/SamplePlayHandle.h > LOGS/fix.patch; echo EXIT=$?` | 0 | fix.patch | product diff only (66 lines) |
| 4b reverse | `git apply -R LOGS/fix.patch; echo APPLY-R-EXIT=$?` then `cmake --build build --target zene SampleClipStretchTest > LOGS/build-4.log 2>&1; echo BUILD-EXIT=$?` | 0 / 0 | build-4.log | bug restored |
| **4b RED** | `cd build/tests && ctest -R '^SampleClipStretchTest$' --output-on-failure > LOGS/test-6.log 2>&1; echo EXIT=$?` | **8** | test-6.log | same failing assertion: burst at 0.280136 — **the test catches the bug** |
| 4b re-apply | `git apply LOGS/fix.patch; echo APPLY-EXIT=$?` then `cmake --build build --target zene SampleClipStretchTest > LOGS/build-5.log 2>&1; echo BUILD-EXIT=$?` | 0 / 0 | build-5.log | fix restored |
| **4b GREEN** | `cd build/tests && ctest -R '^SampleClipStretchTest$' --output-on-failure > LOGS/test-7.log 2>&1; echo EXIT=$?` | **0** | test-7.log | 1/1 passed |
| 4b GREEN detail | `cd build/tests && QT_QPA_PLATFORM=offscreen ./SampleClipStretchTest > LOGS/test-8.log 2>&1; echo EXIT=$?` | 0 | test-8.log | Totals: 9 passed, 0 failed |

## Gates (NOT green — pre-existing debt, none from this fix)

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| complexity | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity-1.log 2>&1; echo EXIT=$?` | **1** | gate-complexity-1.log |
| file length | `bash tests/file-length-gate.sh --check > LOGS/gate-length-1.log 2>&1; echo EXIT=$?` | **1** | gate-length-1.log |

Both gates fail on **regressions that exist at HEAD and are untouched by this fix**. Proof, in the
log folder:

- The working-tree delta of this session is exactly `src/core/SamplePlayHandle.cpp`,
  `include/SamplePlayHandle.h`, `tests/src/tracks/SampleClipStretchTest.cpp`,
  `tests/upstream-modifications.txt` and this report (`git status --short`). Neither gate log
  mentions any of them (verified with grep over `gate-complexity-1.log` / `gate-length-1.log`).
- `git archive HEAD` extracted read-only to `/tmp/opencode/head-gate-check` and both gates re-run
  there (`gate-complexity-head.log`, `gate-length-head.log`) give the **same EXIT 1 / EXIT 1 and a
  byte-identical REGRESSION set** (diff of the REGRESSION/FAIL lines between HEAD and working-tree
  runs is empty).
- Pre-existing regressions (from earlier merged work, e.g. `6fb450db1` / `7afad9b7f`, which land
  `SmfInterchangeReader.cpp`, `TakeLane.cpp`, `XyPadPairTest.cpp`, …):
  - complexity, new functions over CCN 10 with no baseline entry: `dotCentreAlong` (CCN 16,
    `tests/src/gui/XyPadPairTest.cpp`), `parseNotes` (13, `src/core/DawProjectReadTracks.cpp`),
    `adjust` (12, `src/core/ControlCommandsMixerTools.cpp`), `setStrip` (11,
    `src/core/ControlCommandsMixerStrip.cpp`), `rotateProjectRevision` (11,
    `src/core/ProjectRevisions.cpp`), `importedOverrides` (11,
    `src/gui/ControlCommandsShortcuts.cpp`).
  - file length, fork-scope files over 500 lines with no baseline entry: `SmfInterchangeReader.cpp`
    (504), `TakeLane.cpp` (520), `CollabTransportTest.cpp` (541), `ControlAutomationScriptTest.cpp`
    (521), `ControlEditCommandsTest.cpp` (511), `ControlDeviceCatalogueTest.cpp` (567),
    `CustomAccessTest.cpp` (556), `DeviceDspTest.cpp` (752), `HardwareIoTest.cpp` (556),
    `NotePlaybackTest.cpp` (624), `RackMacrosTest.cpp` (535), `RevisionTimelineTest.cpp` (600),
    `VideoTrackTest.cpp` (589), `ArrangerTimelineTest.cpp` (689).

Making those green is impossible inside this task's rules: `--check` never writes the baseline, a
baseline refresh is a re-anchor ("never re-anchor a gate"), and restructuring 14 unrelated files
would violate "change only the buggy behaviour". Reported rather than faked; no gate limit or
baseline was changed.

## Files committed

- `src/core/SamplePlayHandle.cpp` (the fix)
- `include/SamplePlayHandle.h` (doc formula, declarations unchanged)
- `tests/src/tracks/SampleClipStretchTest.cpp` (regression test)
- `tests/upstream-modifications.txt` (two inherited-file declarations for this change)
- `docs/qwen-fixes-2026-10/cluster-new-p12-224.md` (this report)

No new files were created, so `tests/fork-sources.txt` is unchanged. No commands, controls or
serialisation were added (the ControlCommandsSnapshot canary does not move).
