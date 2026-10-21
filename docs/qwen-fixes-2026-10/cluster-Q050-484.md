IMPLEMENTED

Q050-484: IMPLEMENTED
Q050-103: IMPLEMENTED
Q050-087: IMPLEMENTED (cables are editable on chains the engine routes through its graph; on chains of the shipped audio-ports devices the canvas shows them read-only, with the engine's reason, and still adds / reorders / removes devices - see the scope note)
Q050-071: IMPLEMENTED (the matrix is published and reproducible; it records a VST3/CLAP state-restore failure it found, which is not fixed here)

Branch `feat/050-Q050-484-oc` (from train 9, `b61e5f965`), 10 commits (one per unit plus test-split, portability, ledger and report commits), built and run on Linux/GCC only. Feature records:
`docs/050-features/Q050-484.md`, `Q050-103.md`, `Q050-087.md`, `Q050-071.md`. Logs of every run below are in the run folder
(`.../runs/Q050-484.r1/logs/`).

## Q050-484 - SFZ instrument

| Criterion | Proof | Exit |
| --- | --- | --- |
| A built-in SFZ instrument (sfizz, BSD-2-Clause) loads .sfz files with their samples, plays velocity layers, round robins and key switches, saves and restores state | `SfzPlayerTest`: `loadsAnSfzWithItsSamples`, `velocityLayersPickTheirSample`, `roundRobinsFollowTheirSequence`, `aKeySwitchChangesTheNextNote`, `eventsStartAtTheirFrameOffset`, `stateSavesAndRestores` (renders the real module on a fixture written to a temp folder; levels identify the sample) | 0 |
| Available on every shipped platform, registered in the browser and the instrument list, reachable through `plugin.load` and `plugin.param_*` (snapshot regenerated live) | sfizz and its dependencies are **vendored and built into the module unconditionally** (`plugins/SfzPlayer/CMakeLists.txt`, no `if(LMMS_HAVE_...)`, libsndfile is the only outside dependency); `SfzPlayerTest::theInstrumentIsReachableThroughThePluginCommands` (descriptor, `.sfz` extension, `plugin.list` loadable, `plugin.load`, `plugin.param_get/set`), `browserLoadOpensAnSfzOnANewTrack`, `openFileLoadsAndUndoes`, `theWindowOpensAFileThroughTheCommand`; `commands_snapshot.json` regenerated live with `snapshot_live.py` (841 ids), `doc-figures-gate.py` PASS, `ReversibilityContractTest` PASS (A16 line 850/423) | 0 |
| Real-time safe: no allocation, locks or file I/O on the audio thread, streaming on a worker | `SfzPlayerRealtimeTest` (allocation probe: **0 allocations, 0 frees** across noteOn/queue/render of a 3-second streamed sample over 200 periods; Linux: the sample reads are intercepted - they happen during playback and never on the render thread) | 0 |
| Reverted-change proof | module removed (`libsfzplayer.so` moved aside): `SfzPlayerTest` 8, `SfzPlayerRealtimeTest` 8; `plugin.open_file` hunks reverted + rebuilt: `SfzPlayerTest` 8 (`openFileLoadsAndUndoes`, `theWindowOpensAFileThroughTheCommand` fail); restored: 0 / 0 | 8 / 0 |

## Q050-103 - Sampler envelopes and released-voice policy

| Criterion | Proof | Exit |
| --- | --- | --- |
| Render overlapping notes to verify attack, release, and released-voice reuse | `SamplerEnvelopeTest::attackRampsEachNoteFromSilence`, `releaseTailFollowsTheModel`, `releaseFadesLinearly`; `SamplerVoiceReuseTest::releasedVoiceIsReusedBeforeAHeldOne`, `oldestHeldVoiceIsReusedWhenNoneIsReleased`, `releasedVoicesAreTakenInReleaseOrder`, `reusedVoiceStartsFresh`, `everyNoteSoundsWhenItStarts`, `noteStartAndVoiceReuseDoNotAllocate` | 0 |
| Automate envelope values, save/load, existing loop controls stable | `SamplerEnvelopeTest::automationDrivesTheEnvelope` (automation through `automation.add_point` + the transport; `plugin.param_set/get`, `control.undo`), `settingsRoundTripAndTheLoopControlsStayStable`, `oldProjectsLoadWithTheOldBehaviour`, `windowControlsDriveTheModels` | 0 |
| Old projects render as before | `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour` (the sample-exact A/B of the sampler against the pre-migration reference: identical); `AudioFileProcessorPlayheadTest`, `AudioFileProcessorWaveViewTest` | 0 (the one failing case in that binary, `nonRealtimeLadspaEffectsAreInvalid`, needs the SWH LADSPA fixture my targeted build did not make) |
| Reverted-change proof | `plugins/AudioFileProcessor` reverted to the base + module rebuilt: `SamplerEnvelopeTest` 8, `SamplerVoiceReuseTest` 8; restored: 0 / 0 | 8 / 0 |

## Q050-087 - Device-chain patcher canvas

| Criterion | Proof | Exit |
| --- | --- | --- |
| Open a chain canvas; add, connect, disconnect, reorder; round-trip through `patcher.get_state` / `patcher.set_wiring` | `PatcherCanvasTest`: `theChannelMenuOpensTheCanvas`, `theCanvasShowsTheChainAsTheAgentReadsIt`, `draggingPortToPortConnects`, `selectingACableAndPressingDeleteDisconnects`, `aCutThatStrandsTheOutputIsRefusedAndNothingChanges`, `reorderingMovesTheDeviceInTheChain`, `draggingANodeSidewaysReordersTheChain`, `addingADeviceAppendsANode`, `removingTheSelectedDeviceUnloadsIt`, `resetWiringRestoresTheDerivedWiring` (real mouse/key events; after every edit the canvas graph equals `patcher.get_state`; one undo step each) | 0 |
| Shift-clicking a cable adds a second destination; an agent command test verifies the same graph | `shiftClickingACableAddsASecondDestination` (the original cable stays; an agent `patcher.set_wiring` with the canvas's edges reads back the same graph), `clickingACableWithoutShiftOnlySelectsIt` | 0 |
| Backend unchanged | `PatcherCommandsTest` | 0 |
| Reverted-change proof | the Shift handling removed from `PatcherCanvas::mousePressEvent` + rebuilt: `shiftClickingACableAddsASecondDestination` fails (8); restored: 0. (Removing the whole canvas makes the test fail to compile.) | 8 / 0 |

## Q050-071 - Third-party plug-in compatibility matrix

| Criterion | Proof | Exit |
| --- | --- | --- |
| Run and publish a reproducible matrix (host format, OS/package, load, editor, state round-trip, known failures) | `python3 tests/plugin_compat_matrix.py --zene build/zene --probe build/tests/PluginCompatEditorProbe --root /tmp/zene-matrix/root --display :97 --out docs/plugin-compat/results-linux-x86_64.json --pin`: 15 pinned plug-ins (4 VST3, 4 CLAP, 7 LV2; packages pinned by sha256) -> committed `results-linux-x86_64.json` and the generated `docs/PLUGIN-COMPATIBILITY.md`; `PluginCompatMatrixGate` + `PluginCompatMatrixGateSelfTest` (page == render of the results, pins, vocabulary, details) | 0 / 0 / 0 |
| Keep third-party failures separate from fixtures; rerun the named regressions on the next build | the page's fixtures section and the gate's separation check; the rerun: same command with `--baseline docs/plugin-compat/results-linux-x86_64.json` against the same pinned files: `0 case(s) changed, 0 regression(s)` | 0 |
| Docs agree | `PluginFormatsDocsGate`, `PluginFormatsDocsGateSelfTest` (the blanket "no third-party plug-in tested" claim replaced) | 0 |
| Reverted-change proof | page removed: gate 1 (`FAIL: docs/PLUGIN-COMPATIBILITY.md is missing`); restored: 0 | 1 / 0 |

First-run findings (recorded, not fixed): load 15/15; editor VST3 4/4 and CLAP 4/4 open/draw/close, LV2 7/7 unsupported
(the host opens no LV2 UI); state round-trip passes on 10 rows and **fails on five VST3/CLAP rows** (moved parameters come
back at other values after `plugin.state_load`). Two apparent failures turned out to be the method's, and are fixed in it: an
absolute symlink in the Calf package, and a run-time library (`freeverb3`) Dragonfly needs - now pinned as a dependency.

## Gates and regressions

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py` (run through a copy whose log folder is inside /tmp, because the
original writes next to the worktree): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity, file-length: all PASS (no new item in my files). 23 related ctests passed in one run:
`PatcherCommandsTest`, `ReversibilityContractTest`, `ReversibilityUndoTest`, `ReversibilityUndoDeviceTest`, `DevicePanelTest`,
`RackMenuTest`, `EffectRackAddTest`, `AgentSurfaceReportTest`, `UndoHonestyTest`, `ControlRegistryTest`, `BrowserCatalogTest`,
`LibraryBrowserTest`, `ControlCommandsSnapshot` and the new ones. Not run / not mine: `ControlSocketIntegration` fails in my
targeted build because `ladspaeffect` is not built there (`the LADSPA host plugin 'ladspaeffect' is not in this build`);
`ControlDeviceCatalogueTest` needs the Patman module, also not built here.

## Round 2 (review: REWORK) - what changed and the proof

| Blocking point | Fix | Proof (exit) |
| --- | --- | --- |
| sfizz garbage collection (spin mutex, moves data) in the render callback | `Synth::renderBlock` no longer runs it under `ZENE_SFIZZ_NO_RENDER_THREAD_WAITS`, always defined by `plugins/SfzPlayer/CMakeLists.txt` (the one change to vendored sfizz, declared in `LICENSE-NOTICE.md` and the Gate 6 ledger). Cost: streamed samples stay in memory until the synth is rebuilt (documented) | `SfzRenderThreadGate` (reads the built library's symbol table: `Synth.cpp.o` must not reference `FilePool::triggerGarbageCollection`): 0; with the define removed and rebuilt: 8; restored: 0 |
| Export enables freewheeling (waits for loaders in the callback) | the player never enables freewheeling (`setFreewheeling` removed); the same define also removes the wait. An export instead loads whole samples first: `ProjectRenderer::startProcessing` emits new `Song::exportPreparing` on the main thread, the player rebuilds with a whole-sample preload; `Song::exportFinished` (queued from the render thread's end) returns it to streaming | `SfzPlayerTest::anExportPreloadsWholeSamplesAndThenStreamsAgain`: 0 (8192 -> 2^30 -> 8192 frames, plays throughout) |
| Q050-103 automation emits `dataChanged` on the render thread | `AutomatableModel::setDeferChangeSignalOnRenderThread` / `flushDeferredChange` (opt-in; every other model is unchanged); the three new sampler models hold the signal back on the render thread and the window's poll timer emits it | `SamplerEnvelopeTest::automationDrivesTheEnvelope` now renders with `AudioEngine::renderNextPeriod` and asserts 0 signals on the render thread, 1 after the flush, and that `plugin.param_set` still emits at once: 0; with the deferral disabled: 8; restored: 0 |
| Q050-484 polyphony/oversampling connected to `scheduleReload` by automation | all four SFZ models defer on the render thread; a 100 ms timer on the player's own thread emits them, which is what asks for the reload | `SfzPlayerTest::automatedSettingsEmitNothingOnTheRenderThread` (automation of Volume and Polyphony applied by the engine's render: 0 signals and 0 reloads there; reload and playback follow): 0; with the deferral disabled: 8; restored: 0 |
| `plugin.open_file` accepts only `/`-paths | `QFileInfo::isAbsolute()` (platform rule: `C:/x`, `C:\x`, UNC) | `SfzPlayerTest::openFileTakesThePlatformsAbsolutePaths` (absolute and native-separator spellings accepted, relative refused `invalid_args`): 0. Only the Linux spellings could be run here |

Re-run after the changes (one ctest run, exit 0, 18/18): `SamplerEnvelopeTest`, `SamplerVoiceReuseTest`, `SfzPlayerTest`,
`SfzPlayerRealtimeTest` (0 allocations / 0 frees, streamed reads only off the render thread), `SfzRenderThreadGate`,
`AudioFileProcessorPlayheadTest`, `AudioFileProcessorWaveViewTest`, `PatcherCanvasTest`, `PatcherCommandsTest`,
`ReversibilityContractTest`, `DevicePanelTest`, `RackMenuTest`, `EffectRackAddTest`, `AgentSurfaceReportTest`, `ControlRegistryTest`,
`ControlCommandsSnapshot`, `PluginCompatMatrixGate`, `PluginFormatsDocsGate`. No command was added or changed in round 2 (the
snapshot is unchanged). Round-2 logs: `.../runs/Q050-484.r2/logs/`.

Honest notes: the existing sampler models (amplify, start, end, loop, ...) and every other automatable model in the product
still emit their change signal where an automation tick changes them on the render thread; only the seven models these units add
hold it back. sfizz's `renderBlock` still takes its own try-lock for control calls the player never makes on a live synth. The
synth hand-over and voice pool remain argued, not measured (no ThreadSanitizer here).

## Self-check

1. **Each new test fails with the product change reverted and passes restored** - yes, exit codes in the tables above:
   103: 8 / 8 reverted, 0 / 0 restored; 484: 8 / 8 with the module absent, 8 with `plugin.open_file` reverted, 0 restored;
   087: 8 with the Shift handling removed, 0 restored (a full revert of the canvas does not compile, so a mutation was used);
   071: gate exit 1 with the page removed, 0 restored.
2. **Every criterion proven by a test I ran** - see the tables (names and exit codes). Windows / macOS / Qt 6.8 / Qt 5.15 builds
   were **not run**: the SFZ module's sfizz build follows sfizz's own recipe and is by construction on those platforms, which is
   stated in the feature record and `KNOWN-LIMITATIONS.md`, not claimed as measured.
3. **One undo step per edit, honest classes** - `plugin.open_file` (new) is `true_inverse` (action checkpoint restoring the previous
   instrument state; a refused file records no step - tested); canvas edits go through `patcher.set_wiring` (snapshot),
   `plugin.load` (snapshot), `plugin.move` / `plugin.unload` (true_inverse), each one `control.undo` step (tested); sampler
   parameters are `plugin.param_set` (true_inverse). A16 line 850 rows / 423 true_inverse; snapshot 841 ids, regenerated live.
4. **Audio thread** - sampler (`AudioFileProcessor::playNoteImpl`, `applyAttack`, `deleteNotePluginData`, `SamplerVoicePool`):
   atomics only, bounded scans, voices preallocated at construction (the old per-note `new PlaybackState` is gone), the attack
   is a bounded loop; `noteStartAndVoiceReuseDoNotAllocate` counts 0. SFZ (`SfzPlayer::handleMidiEvent`, `drainEvents`,
   `configureSynth`, `renderPeriod`, `playImpl`, `SfzSynth::render`, `RtPublished::Guard`): lock-free queue push, fixed-array
   insertion sort, two atomic increments + one load around the synth, sfizz `renderBlock` into member buffers; 0 allocations
   and 0 frees counted; file reads only on sfizz's workers (Linux interposition). Honest caveats: sfizz's `renderBlock` takes its
   own try-lock spin mutex (never contended - nothing calls a locking method on a live synth); race-freedom of the voice pool and
   of the synth hand-over is **argued, not measured** (ThreadSanitizer does not run on this machine). No Qt signals are emitted
   on the audio path (`publishPlayhead` stays an atomic store).
5. **Old projects** - the sampler's new settings default to the old behaviour (attack 0, release 3 ms, limit 64) and a project
   without them loads that way (`oldProjectsLoadWithTheOldBehaviour`); the sampler's output is sample-identical to its pre-change
   reference (`PluginPortsMigrationTest`); the SFZ player and the canvas add no data to existing elements, and the patch
   wiring is still session state. A sampler that sounded more than 64 notes at once now fades the oldest/released ones (the limit's
   default, documented).
6. **Portability** - no Qt-6-only API without a guard (`QMouseEvent::pos()`, `mapToGlobal(QPoint)`), no `near`/`far`/`small`/`interface`/`min`/`max` identifiers, no `std::filesystem` outside sfizz's vendored ghc
   (not `std::filesystem`). Plugin CMake is conditional only on compiler and processor.

Not done / out of scope (also in the feature records): Windows/macOS/arm builds of sfizz are unmeasured; the canvas does not
make audio-ports devices routable through the graph; authored wiring is still not saved in projects; the VST3/CLAP state
failure the matrix found is not fixed; other platforms, distributions and plug-ins are untested.
