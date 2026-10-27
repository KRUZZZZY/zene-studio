IMPLEMENTED

# Q050-486 - Validate and own saved plugin waveform data through one shared loader

Branch `feat/050-Q050-486-oc`, cut from `050/m0-train-17` (`639e1d23e`). Feature record with the defect inventory, the
malformed-data policy and the known limits: `docs/050-features/Q050-486.md`. Logs: run folder
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-486.r1/logs/` (file names in brackets).

**Change.** `waveform::decode/loadGraph/reportRefused` (`include/WaveformData.h`, `src/core/WaveformData.cpp`) bound the
encoded text before allocating, decode strict base64, require whole floats, an exact (or BitInvader's two legal)
count and finite values, and hand back `std::unique_ptr<float[]>` + count; `graphModel::setSamples(const float*, int)`
never copies past its source. Watsyn, MultitapEcho, WaveShaper, FreeBoy, Vibed, BitInvader, Xpressive and
DynamicsProcessor load through it. A damaged graph keeps the current/default shape and adds one line to
`Song::collectError` (GUI error report box, `project.open` `errors`); an absent/empty one loads silently as before.

## Acceptance

All commands from the worktree with the brief's environment (plus `CCACHE_TEMPDIR=/tmp/q486/ccache-tmp`, see Self-check).
"Prior" = the eleven product files checked out from `639e1d23e` and `WaveformData.{h,cpp}` moved out, tests unchanged,
rebuilt; restored with `git checkout HEAD -- <files>` and rebuilt afterwards.

| # | Criterion | Test (command) | Prior | Fixed |
| --- | --- | --- | --- | --- |
| 1 | Direct loader + >= 2 plug-in paths fail on prior code; every migrated caller covered | `cmake --build build --target WaveformDataTest`; `./GraphWaveformLoadTest <fn>` per plug-in with `ASAN_OPTIONS=detect_leaks=1:leak_check_at_exit=0:use_sigaltstack=0` | WaveformDataTest build EXIT=2 (no shared loader) [build-12-revert-direct]; watsynWaves 1, multitapEchoSteps 1, waveShaperShape 1, freeBoyShape 1, vibedStrings 1, bitInvaderShape 1 (each `AddressSanitizer: heap-buffer-overflow`, e.g. `READ of size 880 ... graphModel::setSamples(float const*) ... Watsyn.cpp:590`), xpressiveWaves 9, dynamicsProcessorShape 9 [test-7-revert-*]; truncated rows: Watsyn READ 880, MultitapEcho READ 64, WaveShaper READ 800 [test-8-revert-*] | `ctest -R "^(WaveformDataTest\|GraphWaveformLoadTest\|GraphWaveformEditorGuiTest\|ControlGraphWaveformLoad)$"` EXIT=0, 4/4 [test-24-final-new]; `./GraphWaveformLoadTest` 93 passed EXIT=0 [test-18]; `./WaveformDataTest` 21 passed EXIT=0 [test-19] |
| 2 | Empty/invalid/truncated/excessive/misaligned/nonfinite; bounded memory; no out-of-range access; ASan | `WaveformDataTest::refusals` (13 rows) + `aHugePayloadIsRefusedWithBoundedMemory` + `theEncodedBoundIsExact` + `theLengthCheckedSetterStaysInsideBothBuffers`; `GraphWaveformLoadTest` 11 damage rows x 8 plug-ins; both linked with libasan (`ldd ... \| grep -c asan` = 1 each) | see row 1 | EXIT=0 under ASan [test-25]; `./WaveformDataTest aHugePayloadIsRefusedWithBoundedMemory` EXIT=0 (no allocation >= 4096 B while refusing 8 Mi floats) [test-26-bounded] |
| 3 | Validated lengths + owning storage in every caller; repeated loads leak nothing under the selected checker (LeakSanitizer) | `./GraphWaveformLoadTest repeatedLoadsLeaveNothingBehind` | EXIT=1, `LeakSanitizer: detected memory leaks`, three direct leaks of 44000 B in 50 objects (Watsyn) [test-7-revert-repeatedLoadsLeaveNothingBehind] | passes inside the 93/93 run, EXIT=0 [test-18] |
| 4 | Valid presets load/save identical; render equivalently | `./GraphWaveformLoadTest bundledPresetsRoundTripExactly` (139 bundled graphs); `ControlGraphWaveformLoad` (drawn wave/curve identical after save, reopened render 0.000 LSB vs floor 0.000, negative control fails at 19201 LSB); `zene render` of `Crunk(Demo).mmp`, `DnB.mmpz`, `Thaledric-Armageddon.mmpz` on prior and fixed builds compared with `golden_audio_lib` | presets case EXIT=0 on prior too (139 graphs) [test-16-revert2-presets] - the compatibility baseline | presets EXIT=0 [test-13-presets]; all three demos byte-identical prior vs fixed, verdict PASS [render-compare.txt]; ControlGraphWaveformLoad EXIT=0 [test-24] |
| 5 | Control-socket fixture loads an affected instrument/effect, reloads saved graph data, verifies state and usable audio | `ctest -R "^ControlGraphWaveformLoad$"` (`tests/control-graph-waveform-load.py`) | ctest EXIT=8: "project.open reports the truncated Watsyn wave", "...Wave Shaper curve with a NaN", "both damaged graphs keep the plug-ins' default waveforms" FAILED [test-15-revert2] | EXIT=0, 11/11 checks [test-24-final-new, test-12-socket] |

GUI: `GraphWaveformEditorGuiTest` (real Wave Shaper editor, mouse strokes on its `Graph`) prior ctest EXIT=8 (no
diagnostic) [test-23-revert3-gui], fixed EXIT=0 [test-22-gui-direct, test-24].

Neighbours (fixed build, `ctest -R ...` [test-20-neighbours]): WatsynSelectedGraphTest, WatsynWaveSnapshotTest,
WatsynParamHandoffTest, SampleEditorDialogTest, ExportQualityControlsTest, BitInvaderAllocationTest,
VibedWaveformHandoffTest, MultitapEchoBoundsTest, OpulenZ, OpulenZSpanSize pass. PluginPortsMigrationTest: 82 passed,
1 failed - `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"), which fails identically on
the prior build (`./PluginPortsMigrationTest nonRealtimeLadspaEffectsAreInvalid` EXIT=1) [test-9-revert-ppm-decimator];
its four waveform cases pass.

Gates: `feature_check.py` logic run with its log directory moved into the run folder (its default `<worktree>/../`
is outside this lane's write-jail; the copy differs only in `ROOT` and `logs`): every row PASS, RESULT PASS, EXIT=0
[feature-check-4.log]. The full suite was not run (the brief: targeted tests only).

## Self-check

- **Each new test fails with the product change reverted, passes restored.** Reverted the eleven product files to
  `639e1d23e` and moved `WaveformData.{h,cpp}` out, rebuilt (`build-11-revert`, `build-17-revert2`, `build-23-revert3`
  EXIT=0): `WaveformDataTest` does not compile (EXIT=2); `GraphWaveformLoadTest` ctest EXIT=8 (ASan
  heap-buffer-overflow), per-function exits listed in row 1; `ControlGraphWaveformLoad` ctest EXIT=8;
  `GraphWaveformEditorGuiTest` ctest EXIT=8. Restored and rebuilt (`build-24-restored3` EXIT=0): all four ctest EXIT=0.
- **Every acceptance criterion is proven by a test I ran.** Table above, one row per criterion with its exit codes.
- **One undo step; honest reversibility classes.** No command was added or changed (no file under
  `src/core/ControlCommands*` or the reversibility tables touched), so no snapshot regeneration and no A16 change. The
  load paths keep their existing undo behaviour; nothing here records an undo step.
- **Realtime.** Changed functions: `waveform::decode`, `waveform::loadGraph`, `waveform::reportRefused`,
  `graphModel::setSamples(const float*, int)` and the `loadSettings` of WatsynInstrument, MultitapEchoControls,
  WaveShaperControls, FreeBoyInstrument, Vibed, BitInvader, Xpressive and DynProcControls. All run while restoring
  saved XML (project/preset/state load, on the loading thread), never in a render callback; the allocation (one
  `float[count]` per graph) and `Song::collectError` happen there, exactly where the old code allocated with `new[]`.
  The audio-side hand-offs these plug-ins already have (Watsyn's/Vibed's published snapshots on `samplesChanged`) are
  unchanged and still triggered by the same signal. TSan cannot run here; no thread or shared state was added.
- **Old projects load and render exactly as before; new data round-trips.** 139 bundled graphs round-trip bit for bit
  on both builds; three demo projects render byte-identical prior vs fixed; drawn waveforms save/reopen identical and
  render equal (0.000 LSB). The writers did not change, so the file format is unchanged. Two demos
  (Jousboxx, Greippi) abort with `std::bad_alloc` in the CLI render on BOTH builds (EXIT=134) - pre-existing, not
  compared.
- **Portability.** `QByteArray::fromBase64Encoding`/`AbortOnBase64DecodingErrors` exist since Qt 5.15 (the minimum);
  no Qt-6-only API, no `std::filesystem`, no `min`/`max`/`small`/`near`/`far`/`interface` identifiers. The sanitizer
  pieces of the tests are behind `!defined(_MSC_VER)` + `__SANITIZE_ADDRESS__`/`__has_feature`, and the CMake ASan
  flags only apply on Linux GCC/Clang; elsewhere the tests build and run without them (the leak case QSKIPs).
- **Environment notes.** ccache's configured temp dir is outside the write-jail, so builds used
  `CCACHE_TEMPDIR=/tmp/q486/ccache-tmp`. ASan on this box needs `use_sigaltstack=0` (a library loaded by the engine
  swaps the thread's alternate signal stack and ASan fails to unmap it at thread exit; a plain ASan thread program
  works) - set in the test's exported `__asan_default_options` and its ctest environment.
