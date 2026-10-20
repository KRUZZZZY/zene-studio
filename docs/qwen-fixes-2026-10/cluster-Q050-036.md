IMPLEMENTED
Q050-036 Complete the specified warp mode set — every acceptance criterion met and proven by a test run in this session.

| Criterion | Test | Command and EXIT |
| --- | --- | --- |
| Every specified mode appears in clip state and can be selected through the UI and agent command. | SampleClipWarpModesTest::everySpecifiedModeIsSelectableThroughTheCommandAndReportedBack + theLegacyNamesStillSelectTheirModes; SampleClipWarpModeMenuTest::theSpecifiedModesAreOfferedAndDriveWarpStretch (+ linear refusal, + MIDI gets no menu) | `ctest -R "SampleClipWarpModesTest\|SampleClipWarpModeMenuTest" --output-on-failure` from build/tests — EXIT=0 (both pass; full warp set 7/7 pass, EXIT=0, log ctest-3-final.log) |
| Render deterministic fixtures for each mode and verify duration, transient behavior, and pitch behavior against mode-specific expectations. | SampleClipWarpModesTest::aRenderPerModeMeetsItsPitchAndDurationExpectations + theTransientFixtureSeparatesTheOnsetBehavior | same ctest run — EXIT=0. Evidence: Re-Pitch bins 0.0000/0.0000/0.5000/0.3000, keepers ≈0.50/≈0.30/<0.001/<0.001, all keeper pairs differ 0.026–0.42; onsets Re-Pitch +0.02 ms, Beats −9.6 ms (pre-echo 0.06), Complex −1.7 ms (pre-echo 0.06), WSOLA modes +3…+8 ms with zero leakage, all within ±20 ms |
| Save and reload a project with different warp modes on separate clips and verify the selections persist. | SampleClipWarpModesTest::modesRoundTripThroughAProjectFile | same ctest run — EXIT=0 (Beats + Texture + Complex save/reload intact; XML carries stretch="beats"/"texture") |

Build: `cmake --build build --target zene SampleClipWarpModesTest SampleClipWarpModeMenuTest SampleClipStretchTest SampleClipRubberBandTest ClipTransposeTest ControlWarpCommandsTest ReversibilityContractTest -- -j4` — EXIT=0 (log build-5-final.log).
Transcript: `QT_QPA_PLATFORM=offscreen python3 tests/control-pitch-stretch-transcript.py build/zene` — EXIT=0 (log transcript-1-pitch.log).
Snapshot: `python3 research/reviews/m1-session-2026-10-12/scratch/m0/snapshot_live.py $PWD` — EXIT=0, 828 ids / 76 groups unchanged (log snapshot-1-live.log).
Doc figures: `python3 tests/doc-figures-gate.py` — EXIT=0 (828 ids / 76 groups).
Gates: complexity --check EXIT=1, file-length --check EXIT=1 (both red on train 5 on purpose; zero warp hits in either regression list — no file of this lane newly listed); fork-sources-gate EXIT=0; all-sources-reproduce EXIT=0; no-upstream-regression-gate EXIT=0; check-namespace EXIT=0; no-tautology-gate EXIT=0.
ReversibilityContractTest — EXIT=0 (no new command ids; warp.stretch stays snapshot class).
No criterion is BLOCKED. Realtime: audio thread does no allocation/lock/wait/IO/signal (WSOLA params are fixed stack values, Rubber Band voices prebuilt on the control thread, pool claim is one CAS). Portability: no Qt-6-only API, no windows.h macro identifiers, out-of-line SampleClip destructor for the unique_ptr array.
Known limits: local TSan not run on this box (design reasoning instead); no bench CPU figure R3 vs WSOLA.
