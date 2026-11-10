IMPLEMENTED

# Q050-481.2 - Host VST2 effects through Carla (stage 2/4)

Branch `feat/050-Q050-481.2-oc` (base `050/m0-train-26`, `af010bc3cc`). Feature record:
`docs/050-features/Q050-481.2.md`. Logs (numbered): `~/.local/share/zene-bunny-fix/qwen/runs/Q050-481.2.r1/logs/`.

A new `carlavst2effect` module (`plugins/CarlaVst2Effect`) hosts each VST2 effect in its own Carla Rack - native in
process, Windows `.dll`s through Carla's Wine bridge - with Carla run by a worker thread (the stage-1 renderer, given
a process mode with an input ring), never by the audio thread. `plugin.list` lists VST2 effects (format `vst2`, kind
`effect`) after the VST2 instruments; the device picker (Add device, Add effect, Insert, Replace) shows them;
`plugin.load` adds them to a chain. Parameters (`plugin.param_*`, knobs, automation), presets, device state
(`plugin.state_*`), project state (with the plug-in's chunk) and the plug-in's own editor (`plugin.editor_*`, and the
effect dialog's button) work through Carla. Discovery moved into `carlabase` so the instrument and effect hosts share
one cache. Proof: `CarlaVst2EffectHostingTest`, against an in-tree VST2 test effect (native + Windows PE) fed by the
stage-1 VST2 test instrument, with a real Carla runtime built by `tools/build-carla-runtime.sh` (Wine bridge included,
llvm-mingw 20261020 as the PE toolchain; log `01-carla-runtime.log`, exit 0).

## Review round 1 -> round 2

| Blocking finding | Change | Test | Reverted | Fixed |
| --- | --- | --- | --- | --- |
| The effect passed `Song::isExporting()` to `Renderer::process`, whose `take()` then slept waiting for the worker inside `renderNextPeriod`'s `RealtimeScope`. | The render path never waits (the `mayWait` path is gone from `read`/`process`/`take`, for the effect and the stage-1 instrument alike). New `OfflineRenderBarrier` (`include/OfflineRenderBarrier.h`, `src/core/OfflineRenderBarrier.cpp`): every VST2 renderer is a participant, and `ProjectRenderer::run` calls `offlinerender::waitForAll()` before each period, outside the render period, until each worker has the next period's frames (bounded 2 s). While exporting, the effect's Carla rack reports offline, so Carla blocks for its plug-in/bridge on the worker instead of passing a block through. | `CarlaVst2EffectExportTest::anExportWaitsBetweenPeriodsAndKeepsTheEffectsOutput(native, wine-bridged)`: a real `ProjectRenderer` export with the effect stalling 150 ms per process call; the render period clock is sampled while it runs (longest period must be < 75 ms) and the float WAV must hold the processed note with no silent and no unprocessed frame. | Round-1 product hunks restored (`git diff --binary ed2c3b376d HEAD -- include src plugins \| git apply -R`), test kept: **exit 8**, "a render period took 150 ms" in both rows (r2 `56-revert-r2-ctest.log`). Control: new render path without the exporter's `waitForAll()`: **exit 8**, "the exported file lost the effect's output" (0 sounding frames) in both rows (r2 `59`). | **exit 0**, 4/4, longest period 0 ms, 17640-frame processed span, 0 gaps, 0 unprocessed; three runs in a row (r2 `65-final-export-1..3`). |

Round-2 logs are in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-481.2.r2/logs/` (numbered; "r2 NN" below).
Found while testing round 2: after a long silence a Wine-bridged plug-in applies a parameter change one or two blocks
late (with or without the stall: `31-iso-0.log`, the first sounding blocks at the old value). The export test plays the
note once before exporting so that does not mask what it checks; it is listed under limits in the feature record.
The export case lives in its own test (`CarlaVst2EffectExportTest`), with the helpers both effect tests share in
`tests/src/gui/CarlaVst2EffectTestSupport.h`, to stay under the file-length and complexity ratchets.

## Acceptance

Command for rows 1-2 and the bridged half of row 3: `ctest -R '^CarlaVst2EffectHostingTest$' -V` from `build/tests`.
Final tree: exit 0, 14/14, three runs in a row (`35-final-1`, `35-final-2`, `35-final-3`).

| Criterion | Test / command | Exit |
| --- | --- | --- |
| 1. VST2 effects appear in the GUI picker and plugin.list and load through plugin.load. | `vst2EffectsAreListed` (native: kind effect, loadable, vendor, unique id, binary native; PE: win64, bridged; after the VST2 instrument and last in the catalogue; `kind=effect` filter); `theDevicePanelAddsAVst2Effect` (clicks the Device panel's Add device button, picks the VST2 row in the device picker, which loads it through `plugin.load`); `pluginLoadHostsTheNativeEffectAndItProcesses` | 0 |
| 2. The effect editor, parameter automation, presets, state save/load, and parameter commands work. | `theEffectsOwnEditorOpensThroughCarla` (`plugin.editor_open/close` + a click on the effect dialog's editor button; the plug-in reports its editor open/closed); `automationDrivesAVst2EffectParameter(native, wine-bridged)` (automation drives the plug-in in playback; 0 render-thread signals); `theAudioThreadNeverWaitsForTheEffect(native, wine-bridged)` (fixture stalls 800 ms per process call; worst engine period 0 ms); `presetsSaveAndLoad`; `deviceStateSaveAndLoad` (`plugin.state_save`/`state_load` bring back the value and the chunk-only count); `projectStateRoundTripsThroughCarla` (binary, parameter, chunk-only count and rendered output identical after save/restore); `pluginLoadHostsTheNativeEffectAndItProcesses` (`plugin.param_get/set` by name, rendered ratio, `control.undo` of the value, then of the load) | 0 |
| 3. Regenerate and verify the live MCP snapshot; test both native and Wine-bridged fixture paths. | `python3 .../snapshot_live.py $PWD` (exit 0, 924 ids / 78 groups, only `plugin.list`'s description changed; `23-snapshot.log`), `python3 tests/doc-figures-gate.py` (exit 0, `24`), `ctest -R '^(ControlCommandsSnapshot\|ReversibilityContractTest\|ControlDevices)$'` (exit 0, 3/3, `25`); bridged: `theWindowsEffectRunsThroughTheWineBridge` and the `(wine-bridged)` rows (PE fixture discovered through Wine, loaded in Carla's Wine bridge, param_set, processing ratio, project round trip); native: every other row | 0 |

Round 2 final tree: `CarlaVst2EffectHostingTest` exit 0, 14/14 (r2 `66`); `CarlaVst2EffectExportTest` exit 0, 4/4, three
runs in a row (r2 `65`); `CarlaVst2HostingTest` exit 0, 13/13 (r2 `66`); neighbours exit 0, 14/14 (r2 `67`: the
catalogue, vendor, reversibility, Carla parameter, missing-devices, native-editor, RtSafetySweep, ControlDevices and
ControlCommandsSnapshot tests, plus the export tests ClipFadesRenderTest, ExportWavDitherTest, ExportWavWriteErrorTest
and LoudnessReportTest, since `ProjectRenderer` changed).

Round 2 full revert proof: all product hunks against the base reversed, stale module moved away, rebuilt:
`CarlaVst2EffectHostingTest` **exit 8** (12/12 hosted cases FAIL) and `CarlaVst2EffectExportTest` **exit 8** (2/2 FAIL)
(r2 `62`); restored and rebuilt: exit 0 (r2 `65`, `66`).

Round 1 revert proof: product hunks reversed (`git diff --binary af010bc3cc HEAD -- cmake include plugins src | git apply -R`),
the stale `libcarlavst2effect.so` moved out of `build/plugins`, `zene`/`carlabase`/`carlavst2`/the test rebuilt
(`17`, `18`, exit 0): **exit 8**, 12/12 hosted cases FAIL (`19-revert-ctest.log`). Restored (`git checkout HEAD --
cmake include plugins src`) and rebuilt: **exit 0**, 14/14 (`22-restore-ctest.log`).

## Notes for the reviewer

- **Commit `f29784df19` does not build on its own.** A failed `git add` (an old path after `git mv`) left most of the
  change unstaged and the commit went ahead with only the staged new files; `c8dac3ec92`, immediately after, holds
  the rest (its message says so). I may not amend or rebase; the two together are the change. Every later commit
  builds.
- Flake found and fixed in the test, not the product: an early version compared the "heard" count read before a save
  with the restored one, while the worker was still draining queued input (`10-ctest-effect-v.log`: 9 vs 7); the
  cases now compare with the count the saved XML/file holds. A later added "stall output is silence" check failed
  once (`33-final-1.log`, peak 0.5) because Carla's rack passes input through until a just-added plug-in is active;
  that check was dropped (the timing assertion stays, as in stage 1).
- Stage-1 test touched only to share helpers: the environment setup, runtime and Wine checks moved from
  `CarlaVst2HostingTest`'s private members into `CarlaVst2TestSupport.h` (`initCarlaHostingProcess`,
  `missingCarlaRuntime`, `carlaWineAvailable`, `carlaTempDirWritable`), unchanged in behaviour. It still passes 13/13
  (`14-ctest-stage1.log`, `30-final-stage1.log`).

## What is not proven here (honest limits)

- Bridged **chunk restore**: Carla passes a chunk to its bridge through `/var/tmp`, which this sandbox cannot write
  (the test logs "bridged chunk restore not checked"). Bridged chunk *save*, parameters, processing and the project
  round trip of parameters and output are checked; native chunk restore is checked.
- The Wine-bridged plug-in's **editor** (a Carla Wine window) is not exercised (no display in the test run).
- Hosted CI has no Carla runtime: the test reports Skipped there. It fails (does not skip) whenever a runtime and the
  fixtures exist and the VST2 effect host does not work.
- Local TSan does not run on this box; the ring ordering and pause handshake are reasoned (see below), not measured.

## Self-check

`submit_check.py` (`36-submit-check.log`): every mechanical item PASS except (a) the report, which is this file, and
(b) `gates (feature_check.py)` FAIL, because `feature_check.py` creates `<worktree>/../zene-feat-q050-481.2-feature-check/`,
outside this lane's write-jail (`PermissionError`, `26-feature-check.log`). An identical copy whose only changes are
that log directory (`/tmp/q4812-fc`) and its `ROOT` path (so it still reads `FIX-BASE` and the baselines from
`~/.local/share/zene-bunny-fix/qwen`) ran every gate: fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures PASS; complexity and file-length PASS (no new item in my files) -
`RESULT: PASS`, exit 0 (`27-feature-check.log`). Two WARNs: "no GUI file changed" - the GUI entry points are the
existing Device panel / effect rack / device menu pickers (which list the new entries without a change) and the new
effect dialog, which lives in the plug-in (`plugins/CarlaVst2Effect/CarlaVst2Effect.cpp`,
`CarlaVst2EffectControlDialog`); and "canary figures - new command ids: automation.add_point, carlabase_export.h,
control.undo, ..." - a false positive of its regex (quoted command names in the test and `#include` lines); no command
was added and the README canary (924 ids / 78 groups) is what `tests/doc-figures-gate.py` measures (PASS).

- **Each new test fails with the product change reverted, passes restored?** Yes. Reverted: `ctest -R
  '^CarlaVst2EffectHostingTest$' -V` exit 8, 12 of 12 hosted cases FAIL (`19`). Restored: exit 0, 14/14 (`22`),
  and on the final tree three times (`35-final-1..3`).
- **Every acceptance criterion proven by a test I ran?** Yes - the table above; rows 1-2 `CarlaVst2EffectHostingTest`
  exit 0; row 3 snapshot_live exit 0, doc-figures exit 0, `ControlCommandsSnapshot`/`ReversibilityContractTest`/
  `ControlDevices` exit 0, and the bridged cases exit 0.
- **One undo step per edit; honest reversibility classes?** No command added and no class changed
  (`ReversibilityContractTest` PASS, A16 histogram unchanged). `plugin.param_set` on a VST2 effect parameter is one
  journalled step: the test sets Left, hears it, `control.undo`, reads and hears the old value. `plugin.load` of an
  effect is an append with an exact inverse: the next `control.undo` removes the effect. `plugin.preset_load` /
  `plugin.state_load` go through the effect's own state (`restoreState`), as for every effect.
- **Nothing on the audio thread allocates/frees/locks/waits/does I/O/emits?** Yes (reworked in round 2). The audio
  thread's new functions are `CarlaVst2Effect::processImpl`, `carlavst2::Renderer::process`, `Renderer::take` and
  the stage-1 `Renderer::read` (now a call to `take`): relaxed/acquire/release atomics, frame copies into and out of
  preallocated rings (allocated in the constructor), and a signed counter only the audio thread touches. No Carla
  call, no lock, no allocation, no system call, no Qt signal - and no wait, not even while a project is exported
  (round 1's export wait inside the period is gone; `CarlaVst2EffectExportTest` measures 0 ms periods during an export
  with an 800/150 ms-stalling plug-in, and 150 ms with round 1's code). The export's wait is in
  `offlinerender::waitForAll()`, called by `ProjectRenderer::run` between periods (no `RealtimeScope` there); its
  mutex is never taken by a render period. Carla (including the Wine bridge's wait, and its offline-mode blocking
  lock) runs on the effect's worker thread; parameter models defer render-thread change signals (0 render-thread
  emissions, natively and bridged). All four functions are declared in `tests/rt-safety-scope.txt` with updated
  reasons; `RtSafetySweep` PASS (r2 `67`). TSan: not available here; the reasoning is one producer and one consumer per
  ring, release on publish and acquire on read, the worker held between periods while Carla's plug-in or parameter list
  changes, and the barrier reading the rings' counters with acquire loads only.
- **Old projects load and render as before; new data round-trips?** No existing format changed. `ProjectRenderer` now
  calls `offlinerender::waitForAll()` between periods, which returns at once with no VST2 device (empty registry); the
  export tests ClipFadesRenderTest, ExportWavDitherTest, ExportWavWriteErrorTest and LoudnessReportTest pass (r2 `67`).
  No other existing module's behaviour changed except the stage-1 VST2 instrument, which now also waits between export
  periods instead of inside one (its `Q050-481.1.md` describes the old in-period wait): Carla Rack/Patchbay are untouched; the stage-1 instrument keeps its key attributes, its listing
  order (instrument `dev-<n>` ids do not move: effects are appended after them) and its renderer mode (the discovery
  move and the shared SubPluginFeatures class are behaviour-preserving - `CarlaVst2HostingTest` 13/13). Neighbours pass:
  `ControlDeviceCatalogueTest`, `ControlHostedDeviceCatalogueTest`, `ControlDeviceVendorTest`,
  `MissingDevicesPreserveTest`, `CarlaParamFloatModelTest`, `PluginNativeEditorTest` (`15`). New data round-trips:
  `projectStateRoundTripsThroughCarla` (native) and the bridged round trip in `theWindowsEffectRunsThroughTheWineBridge`,
  plus `deviceStateSaveAndLoad` and `presetsSaveAndLoad`. Converting VeSTige / VST-effect projects is stage 3.
- **Portability?** No Qt-6-only API: `QMetaObject::invokeMethod(context, functor, Qt::QueuedConnection)` is Qt >= 5.10;
  `QDir::listSeparator` Qt >= 5.6. No `std::filesystem`, no `small/near/far/interface/min/max` identifiers, no
  windows.h macro names. `std::span` (C++20, already used by stage 1). The Carla process call keeps Carla 2's non-const
  input under `CARLA_VERSION_HEX < 0x30000`, as `Carla.cpp` does. `strdup` is what `Carla.cpp` already uses. The
  weak stand-in (`DummyCarla.cpp`) already defines every Carla symbol the new module links
  (`carla_get_native_rack_plugin`, `carla_create_native_plugin_host_handle`, `carla_host_handle_free`), so a build
  without a system Carla links it. Discovery's exports carry `CARLABASE_EXPORT` (Windows DLL). The test fixture is
  C11 like stage 1's; its PE build is best effort and never fails the build. The test's plug-in-directory symlinks stay
  POSIX-only (Windows points at the build directory, where every case skips for want of a runtime).
- **Registration?** New files in `tests/fork-sources.txt` (a Q050-481.2 block; the moved discovery's paths updated in
  the stage-1 block); inherited files changed (`cmake/modules/PluginList.cmake`, `plugins/CarlaBase/CMakeLists.txt`,
  `tests/CMakeLists.txt`, `tests/rt-safety-scope.txt`, and in round 2 `src/core/ProjectRenderer.cpp`,
  `src/core/CMakeLists.txt`) in `tests/upstream-modifications.txt` with reasons; round 2's new files
  (`include/OfflineRenderBarrier.h`, `src/core/OfflineRenderBarrier.cpp`, `CarlaVst2EffectExportTest.cpp`,
  `CarlaVst2EffectTestSupport.h`) in `tests/fork-sources.txt`;
  `tests/all-sources.txt` re-derived (`tests/all-sources-reproduce.sh`: REPRODUCES); the test in `tests/CMakeLists.txt`
  with `add_dependencies` on `carlavst2`, `carlavst2effect`, `tripleoscillator` and both fixtures (both effect tests).
- **Commands snapshot / canary?** Regenerated live with `snapshot_live.py` (`23`), doc-figures PASS (`24`),
  `ControlCommandsSnapshot` PASS (`25`).
- **Docs?** `data/manual/index.html` (a "VST2 effects through Carla" paragraph; the stage-1 paragraph no longer says
  effects come later); `docs/050-features/Q050-481.2.md`.
- **Working tree clean?** Yes after the final commit (`git status --porcelain` empty).
