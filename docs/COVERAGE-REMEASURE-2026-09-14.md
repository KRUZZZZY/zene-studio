# Zene Studio — control-surface coverage, re-measured at the 0.3.0 release tip

| | |
|---|---|
| Tree measured | `3956ef589` on `release/0.3.0` (worktree `zene-030`) |
| Source of the measurement | `git -C zene-030 rev-parse HEAD` → `3956ef58969140b26628a434a10b8aeadb534a89` |
| Live cross-check | one headless instance of `zene-030/build/zene`, reaped by explicit PID |
| Writes | none in `zene-030`; this file and the corrected feature list are on `030/audit` |
| Date | 2026-09-14 |
| Supersedes | `docs/COVERAGE-MATRIX-2026-09-13.md` — a tip-pinned snapshot whose own header pins `ddf5f171d` and whose §1.2 computes **150 = 144 + 6** ids over **28 groups** (27 id prefixes + the helper-built `wasm`). Its "reconciliation" §9 records that the copies in circulation disagreed (28/150, 30/154, 31/170). It is kept, marked superseded, and points here. The `0ee78abed` tip some other documents attribute to it does not appear anywhere in that file, so it is **not verified from this box**. |

**This file is a measurement, not a copy.** Every number carries the command that produced it, and
the two tables in §1 and §5 are pasted verbatim from a script's own stdout rather than typed.

## Method

Tools used, all read-only:

```sh
# 1. the registry source at the release tip (mirrors scripts/zene-feature-tracker.py
#    measure_registered_ids(): A = .id=/id= in ControlCommands*.cpp,
#    B = QStringLiteral("a.b...") over src/core/*Control*.(cpp|h))
python3 /home/kruzzzzy/zene-030-audit-auditor/measure_surface.py surface.txt
# 2. one live instance, and one reading of it
QT_QPA_PLATFORM=offscreen zene-030/build/zene --control-socket <own dir>/audit.sock
python3 verification/ctl.py --socket <own dir>/audit.sock commands
# 3. every row's own probe results, from the tracker itself (imported, never --write)
python3 /home/kruzzzzy/zene-030-audit-auditor/emit_table.py
# 4. the registered ctests of the built tree
grep -oE '^add_test\([A-Za-z0-9_]+' zene-030/build/tests/CTestTestfile.cmake | sed 's/add_test(//' | sort -u
```

## 1. The surface at `3956ef589`, counted from the registry source

```
TREE                     /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030
A raw .id=QStringLiteral  185
A raw id=QStringLiteral   185
A distinct ids            185
B distinct total (tracker)192
B-only (helper-built)     ['tag.add', 'tag.remove', 'wasm.list', 'wasm.load', 'wasm.process', 'wasm.set_param', 'wasm.unload']
GROUPS (id prefixes)      35
```

### 1.1 Every group, and its ids

> Two rows below are **not** ids of the surface, and the surface is **33 groups / 185 ids**:
> `tag` is the tracker's verb-field false positive and `wasm` is compile-gated out of the build.
> Both are named and explained in §4; they are left in the table because it is pasted unedited
> from the instrument's own stdout.

| group | ids | n |
|---|---|---|
| `app` | `app.version` | 1 |
| `arrangement` | `arrangement.get_state` | 1 |
| `audio` | `audio.device_list`, `audio.device_set` | 2 |
| `automation` | `automation.add_point`, `automation.clear`, `automation.get_state`, `automation.mode_set`, `automation.remove_point` | 5 |
| `bounce` | `bounce.in_place` | 1 |
| `browser` | `browser.peaks`, `browser.query`, `browser.roots`, `browser.tag.add`, `browser.tag.remove`, `browser.tags` | 6 |
| `chain` | `chain.apply`, `chain.get_state`, `chain.list`, `chain.remove`, `chain.rename`, `chain.save` | 6 |
| `clip` | `clip.add`, `clip.crossfade`, `clip.delete`, `clip.duplicate`, `clip.move`, `clip.resize`, `clip.select`, `clip.set_fade`, `clip.set_gain`, `clip.split` | 10 |
| `clock` | `clock.get_state`, `clock.master_set`, `clock.slave_set` | 3 |
| `comp` | `comp.assign`, `comp.get_state`, `comp.lane_add`, `comp.lane_list`, `comp.lane_remove`, `comp.rebuild`, `comp.select` | 7 |
| `control` | `control.commands_list`, `control.ping`, `control.quit`, `control.redo`, `control.set_undo_coalescing`, `control.set_undo_depth`, `control.surface_report`, `control.transactions`, `control.undo`, `control.undo_depth`, `control.version` | 11 |
| `dsp` | `dsp.get_state` | 1 |
| `export` | `export.get_settings`, `export.set_dither`, `export.set_src_quality` | 3 |
| `freeze` | `freeze.region`, `freeze.track`, `freeze.unfreeze` | 3 |
| `groove` | `groove.apply`, `groove.extract`, `groove.list`, `groove.quantize`, `groove.remove`, `groove.rename`, `groove.set` | 7 |
| `link` | `link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_session_tempo`, `link.set_start_stop_sync` | 5 |
| `midi` | `midi.device_list`, `midi.learn_toggle`, `midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip` | 5 |
| `mixer` | `mixer.add_channel`, `mixer.get_state`, `mixer.remove_channel`, `mixer.set_pan`, `mixer.set_volume` | 5 |
| `modulator` | `modulator.create`, `modulator.depth_set`, `modulator.get_state`, `modulator.rate_set`, `modulator.remove`, `modulator.target_remove`, `modulator.target_set` | 7 |
| `note` | `note.add`, `note.expression_clear`, `note.expression_get`, `note.expression_set`, `note.move`, `note.remove`, `note.resize`, `note.select`, `note.velocity_set` | 9 |
| `plugin` | `plugin.bypass`, `plugin.list`, `plugin.load`, `plugin.param_get`, `plugin.param_set`, `plugin.preset_list`, `plugin.preset_load`, `plugin.preset_save`, `plugin.state_load`, `plugin.state_save`, `plugin.unload` | 11 |
| `project` | `project.get_state`, `project.open`, `project.restore_revision`, `project.save` | 4 |
| `rack` | `rack.add_chain`, `rack.get_state`, `rack.macro_add`, `rack.macro_remove`, `rack.macro_set`, `rack.macro_target_add`, `rack.macro_target_remove`, `rack.remove_chain`, `rack.set_selected`, `rack.zone_add`, `rack.zone_remove`, `rack.zone_resolve` | 12 |
| `record` | `record.journal_begin`, `record.journal_finish`, `record.journal_update`, `record.recovery_discard`, `record.recovery_get_state`, `record.recovery_restore` | 6 |
| `render` | `render.render` | 1 |
| `roll` | `roll.get_state` | 1 |
| `script` | `script.list`, `script.run` | 2 |
| `session` | `session.clear`, `session.clear_slot`, `session.get_state`, `session.launch_scene`, `session.launch_slot`, `session.set_grid`, `session.set_quantisation`, `session.set_scene`, `session.set_slot`, `session.stop_all`, `session.stop_slot` | 11 |
| `settings` | `settings.get`, `settings.set` | 2 |
| `tag` | `tag.add`, `tag.remove` | 2 |
| `telemetry` | `telemetry.consent`, `telemetry.status` | 2 |
| `track` | `track.add`, `track.folder_get_state`, `track.folder_set_collapsed`, `track.get_state`, `track.list`, `track.remove`, `track.rename`, `track.set_arm`, `track.set_folder`, `track.set_mute`, `track.set_pinned`, `track.set_routing`, `track.set_solo`, `track.visibility_set_apply`, `track.visibility_set_list`, `track.visibility_set_remove`, `track.visibility_set_save` | 17 |
| `transport` | `transport.get_state`, `transport.play`, `transport.punch_clear`, `transport.punch_get_state`, `transport.punch_set`, `transport.seek`, `transport.set_tempo`, `transport.stop`, `transport.tempo_map_add`, `transport.tempo_map_clear`, `transport.tempo_map_get`, `transport.tempo_map_remove`, `transport.tempo_map_set_active` | 13 |
| `warp` | `warp.add`, `warp.list`, `warp.move`, `warp.remove`, `warp.set` | 5 |
| `wasm` | `wasm.list`, `wasm.load`, `wasm.process`, `wasm.set_param`, `wasm.unload` | 5 |

### 1.2 Every id, and the file:line it is registered at

This is the research-first table: any claim about an id in
`docs/FEATURE-LIST-0.3.0.md` resolves to one of these lines.

| id | source | extracted by |
|---|---|---|
| `app.version` | src/core/ControlCommandsSettings.cpp:438 | A .id |
| `arrangement.get_state` | src/core/ControlCommandsArrangementState.cpp:46 | A .id |
| `audio.device_list` | src/core/ControlCommandsSettings.cpp:256 | A .id |
| `audio.device_set` | src/core/ControlCommandsSettings.cpp:276 | A .id |
| `automation.add_point` | src/core/ControlCommandsAutomationEdit.cpp:195 | A .id |
| `automation.clear` | src/core/ControlCommandsAutomationEdit.cpp:249 | A .id |
| `automation.get_state` | src/core/ControlCommandsAutomation.cpp:180 | A .id |
| `automation.mode_set` | src/core/ControlCommandsAutomation.cpp:204 | A .id |
| `automation.remove_point` | src/core/ControlCommandsAutomationEdit.cpp:225 | A .id |
| `bounce.in_place` | src/core/ControlCommandsFreeze.cpp:351 | A .id |
| `browser.peaks` | src/core/ControlCommandsBrowser.cpp:275 | A .id |
| `browser.query` | src/core/ControlCommandsBrowser.cpp:216 | A .id |
| `browser.roots` | src/core/ControlCommandsBrowser.cpp:196 | A .id |
| `browser.tag.add` | src/core/ControlCommandsBrowserTags.cpp:173 | A .id |
| `browser.tag.remove` | src/core/ControlCommandsBrowserTags.cpp:200 | A .id |
| `browser.tags` | src/core/ControlCommandsBrowser.cpp:254 | A .id |
| `chain.apply` | src/core/ControlCommandsChainEdit.cpp:147 | A .id |
| `chain.get_state` | src/core/ControlCommandsChain.cpp:248 | A .id |
| `chain.list` | src/core/ControlCommandsChain.cpp:198 | A .id |
| `chain.remove` | src/core/ControlCommandsChainEdit.cpp:266 | A .id |
| `chain.rename` | src/core/ControlCommandsChainEdit.cpp:189 | A .id |
| `chain.save` | src/core/ControlCommandsChain.cpp:282 | A .id |
| `clip.add` | src/core/ControlCommandsClip.cpp:85 | A .id |
| `clip.crossfade` | src/core/ControlCommandsClipEdits.cpp:268 | A .id |
| `clip.delete` | src/core/ControlCommandsClip.cpp:306 | A .id |
| `clip.duplicate` | src/core/ControlCommandsClip.cpp:362 | A .id |
| `clip.move` | src/core/ControlCommandsClip.cpp:144 | A .id |
| `clip.resize` | src/core/ControlCommandsClip.cpp:190 | A .id |
| `clip.select` | src/core/ControlCommandsClip.cpp:413 | A .id |
| `clip.set_fade` | src/core/ControlCommandsClipEdits.cpp:200 | A .id |
| `clip.set_gain` | src/core/ControlCommandsClipEdits.cpp:143 | A .id |
| `clip.split` | src/core/ControlCommandsClip.cpp:237 | A .id |
| `clock.get_state` | src/core/ControlCommandsClock.cpp:209 | A .id |
| `clock.master_set` | src/core/ControlCommandsClock.cpp:240 | A .id |
| `clock.slave_set` | src/core/ControlCommandsClock.cpp:268 | A .id |
| `comp.assign` | src/core/ControlCommandsComp.cpp:281 | A .id |
| `comp.get_state` | src/core/ControlCommandsCompEdits.cpp:266 | A .id |
| `comp.lane_add` | src/core/ControlCommandsComp.cpp:106 | A .id |
| `comp.lane_list` | src/core/ControlCommandsComp.cpp:231 | A .id |
| `comp.lane_remove` | src/core/ControlCommandsComp.cpp:158 | A .id |
| `comp.rebuild` | src/core/ControlCommandsCompEdits.cpp:197 | A .id |
| `comp.select` | src/core/ControlCommandsCompEdits.cpp:113 | A .id |
| `control.commands_list` | src/core/ControlCommandsControl.cpp:186 | A .id |
| `control.ping` | src/core/ControlCommandsControl.cpp:140 | A .id |
| `control.quit` | src/core/ControlCommandsControl.cpp:392 | A .id |
| `control.redo` | src/core/ControlCommandsControl.cpp:360 | A .id |
| `control.set_undo_coalescing` | src/core/ControlCommandsUndo.cpp:290 | A .id |
| `control.set_undo_depth` | src/core/ControlCommandsUndo.cpp:259 | A .id |
| `control.surface_report` | src/core/ControlCommandsSurface.cpp:301 | A .id |
| `control.transactions` | src/core/ControlCommandsControl.cpp:203 | A .id |
| `control.undo` | src/core/ControlCommandsControl.cpp:334 | A .id |
| `control.undo_depth` | src/core/ControlCommandsUndo.cpp:228 | A .id |
| `control.version` | src/core/ControlCommandsControl.cpp:164 | A .id |
| `dsp.get_state` | src/core/ControlCommandsDsp.cpp:152 | A .id |
| `export.get_settings` | src/core/ControlCommandsExport.cpp:76 | A .id |
| `export.set_dither` | src/core/ControlCommandsExport.cpp:105 | A .id |
| `export.set_src_quality` | src/core/ControlCommandsExport.cpp:160 | A .id |
| `freeze.region` | src/core/ControlCommandsFreeze.cpp:418 | A .id |
| `freeze.track` | src/core/ControlCommandsFreeze.cpp:385 | A .id |
| `freeze.unfreeze` | src/core/ControlCommandsFreeze.cpp:454 | A .id |
| `groove.apply` | src/core/ControlCommandsGrooveEdit.cpp:260 | A .id |
| `groove.extract` | src/core/ControlCommandsGroove.cpp:245 | A .id |
| `groove.list` | src/core/ControlCommandsGroove.cpp:226 | A .id |
| `groove.quantize` | src/core/ControlCommandsGrooveEdit.cpp:293 | A .id |
| `groove.remove` | src/core/ControlCommandsGroovePool.cpp:331 | A .id |
| `groove.rename` | src/core/ControlCommandsGroovePool.cpp:353 | A .id |
| `groove.set` | src/core/ControlCommandsGroovePool.cpp:300 | A .id |
| `link.get_state` | src/core/ControlCommandsLink.cpp:154 | A .id |
| `link.set_enabled` | src/core/ControlCommandsLink.cpp:204 | A .id |
| `link.set_quantum` | src/core/ControlCommandsLink.cpp:253 | A .id |
| `link.set_session_tempo` | src/core/ControlCommandsLink.cpp:344 | A .id |
| `link.set_start_stop_sync` | src/core/ControlCommandsLink.cpp:304 | A .id |
| `midi.device_list` | src/core/ControlCommandsSettings.cpp:351 | A .id |
| `midi.learn_toggle` | src/core/ControlCommandsSettings.cpp:392 | A .id |
| `midi.retro_capture_arm` | src/core/ControlCommandsMidi.cpp:163 | A .id |
| `midi.retro_capture_status` | src/core/ControlCommandsMidi.cpp:214 | A .id |
| `midi.retro_capture_to_clip` | src/core/ControlCommandsMidi.cpp:360 | A .id |
| `mixer.add_channel` | src/core/ControlCommandsMixer.cpp:199 | A .id |
| `mixer.get_state` | src/core/ControlCommandsMixer.cpp:96 | A .id |
| `mixer.remove_channel` | src/core/ControlCommandsMixer.cpp:251 | A .id |
| `mixer.set_pan` | src/core/ControlCommandsMixer.cpp:171 | A .id |
| `mixer.set_volume` | src/core/ControlCommandsMixer.cpp:122 | A .id |
| `modulator.create` | src/core/ControlCommandsModulator.cpp:202 | A .id |
| `modulator.depth_set` | src/core/ControlCommandsModulatorRoutes.cpp:267 | A .id |
| `modulator.get_state` | src/core/ControlCommandsModulator.cpp:176 | A .id |
| `modulator.rate_set` | src/core/ControlCommandsModulator.cpp:313 | A .id |
| `modulator.remove` | src/core/ControlCommandsModulator.cpp:269 | A .id |
| `modulator.target_remove` | src/core/ControlCommandsModulatorRoutes.cpp:326 | A .id |
| `modulator.target_set` | src/core/ControlCommandsModulatorRoutes.cpp:205 | A .id |
| `note.add` | src/core/ControlCommandsNotes.cpp:91 | A .id |
| `note.expression_clear` | src/core/ControlCommandsNoteExpression.cpp:242 | A .id |
| `note.expression_get` | src/core/ControlCommandsNoteExpression.cpp:193 | A .id |
| `note.expression_set` | src/core/ControlCommandsNoteExpression.cpp:137 | A .id |
| `note.move` | src/core/ControlCommandsNotes.cpp:202 | A .id |
| `note.remove` | src/core/ControlCommandsNotes.cpp:152 | A .id |
| `note.resize` | src/core/ControlCommandsNotes.cpp:267 | A .id |
| `note.select` | src/core/ControlCommandsNotes.cpp:371 | A .id |
| `note.velocity_set` | src/core/ControlCommandsNotes.cpp:319 | A .id |
| `plugin.bypass` | src/core/ControlCommandsPlugin.cpp:367 | A .id |
| `plugin.list` | src/core/ControlCommandsPlugin.cpp:79 | A .id |
| `plugin.load` | src/core/ControlCommandsPlugin.cpp:256 | A .id |
| `plugin.param_get` | src/core/ControlCommandsPluginParams.cpp:119 | A .id |
| `plugin.param_set` | src/core/ControlCommandsPluginParams.cpp:172 | A .id |
| `plugin.preset_list` | src/core/ControlCommandsPluginPreset.cpp:114 | A .id |
| `plugin.preset_load` | src/core/ControlCommandsPluginPreset.cpp:230 | A .id |
| `plugin.preset_save` | src/core/ControlCommandsPluginPreset.cpp:156 | A .id |
| `plugin.state_load` | src/core/ControlCommandsPluginState.cpp:135 | A .id |
| `plugin.state_save` | src/core/ControlCommandsPluginState.cpp:55 | A .id |
| `plugin.unload` | src/core/ControlCommandsPlugin.cpp:302 | A .id |
| `project.get_state` | src/core/ControlCommandsProjectFiles.cpp:224 | A .id |
| `project.open` | src/core/ControlCommandsProject.cpp:172 | A .id |
| `project.restore_revision` | src/core/ControlCommandsProjectFiles.cpp:144 | A .id |
| `project.save` | src/core/ControlCommandsProjectFiles.cpp:62 | A .id |
| `rack.add_chain` | src/core/ControlCommandsRack.cpp:141 | A .id |
| `rack.get_state` | src/core/ControlCommandsRack.cpp:107 | A .id |
| `rack.macro_add` | src/core/ControlCommandsRackMacros.cpp:84 | A .id |
| `rack.macro_remove` | src/core/ControlCommandsRackMacros.cpp:155 | A .id |
| `rack.macro_set` | src/core/ControlCommandsRackMacros.cpp:395 | A .id |
| `rack.macro_target_add` | src/core/ControlCommandsRackMacros.cpp:219 | A .id |
| `rack.macro_target_remove` | src/core/ControlCommandsRackMacros.cpp:312 | A .id |
| `rack.remove_chain` | src/core/ControlCommandsRack.cpp:209 | A .id |
| `rack.set_selected` | src/core/ControlCommandsRack.cpp:283 | A .id |
| `rack.zone_add` | src/core/ControlCommandsRackZones.cpp:102 | A .id |
| `rack.zone_remove` | src/core/ControlCommandsRackZones.cpp:179 | A .id |
| `rack.zone_resolve` | src/core/ControlCommandsRackZones.cpp:243 | A .id |
| `record.journal_begin` | src/core/ControlCommandsRecording.cpp:224 | A .id |
| `record.journal_finish` | src/core/ControlCommandsRecording.cpp:267 | A .id |
| `record.journal_update` | src/core/ControlCommandsRecording.cpp:245 | A .id |
| `record.recovery_discard` | src/core/ControlCommandsRecordingRecovery.cpp:233 | A .id |
| `record.recovery_get_state` | src/core/ControlCommandsRecordingRecovery.cpp:172 | A .id |
| `record.recovery_restore` | src/core/ControlCommandsRecordingRecovery.cpp:201 | A .id |
| `render.render` | src/core/ControlCommandsProject.cpp:368 | A .id |
| `roll.get_state` | src/core/ControlCommandsNotes.cpp:432 | A .id |
| `script.list` | src/core/ControlCommandsScript.cpp:288 | A .id |
| `script.run` | src/core/ControlCommandsScript.cpp:258 | A .id |
| `session.clear` | src/core/ControlCommandsSession.cpp:455 | A .id |
| `session.clear_slot` | src/core/ControlCommandsSession.cpp:418 | A .id |
| `session.get_state` | src/core/ControlCommandsSession.cpp:190 | A .id |
| `session.launch_scene` | src/core/ControlCommandsSessionLaunch.cpp:158 | A .id |
| `session.launch_slot` | src/core/ControlCommandsSessionLaunch.cpp:101 | A .id |
| `session.set_grid` | src/core/ControlCommandsSession.cpp:231 | A .id |
| `session.set_quantisation` | src/core/ControlCommandsSession.cpp:266 | A .id |
| `session.set_scene` | src/core/ControlCommandsSession.cpp:301 | A .id |
| `session.set_slot` | src/core/ControlCommandsSession.cpp:364 | A .id |
| `session.stop_all` | src/core/ControlCommandsSessionLaunch.cpp:262 | A .id |
| `session.stop_slot` | src/core/ControlCommandsSessionLaunch.cpp:224 | A .id |
| `settings.get` | src/core/ControlCommandsSettings.cpp:117 | A .id |
| `settings.set` | src/core/ControlCommandsSettings.cpp:145 | A .id |
| `tag.add` | src/core/ControlCommandsBrowserTags.cpp:175 | B helper |
| `tag.remove` | src/core/ControlCommandsBrowserTags.cpp:202 | B helper |
| `telemetry.consent` | src/core/ControlCommandsTelemetry.cpp:235 | A .id |
| `telemetry.status` | src/core/ControlCommandsTelemetry.cpp:257 | A .id |
| `track.add` | src/core/ControlCommandsArrangement.cpp:111 | A .id |
| `track.folder_get_state` | src/core/ControlCommandsTrackFolder.cpp:386 | A .id |
| `track.folder_set_collapsed` | src/core/ControlCommandsTrackFolder.cpp:310 | A .id |
| `track.get_state` | src/core/ControlCommandsTransport.cpp:295 | A .id |
| `track.list` | src/core/ControlCommandsTransport.cpp:266 | A .id |
| `track.remove` | src/core/ControlCommandsArrangement.cpp:186 | A .id |
| `track.rename` | src/core/ControlCommandsArrangement.cpp:273 | A .id |
| `track.set_arm` | src/core/ControlCommandsArrangementState.cpp:97 | A .id |
| `track.set_folder` | src/core/ControlCommandsTrackFolder.cpp:283 | A .id |
| `track.set_mute` | src/core/ControlCommandsArrangement.cpp:416 | A .id |
| `track.set_pinned` | src/core/ControlCommandsTrackFolder.cpp:361 | A .id |
| `track.set_routing` | src/core/ControlCommandsTrackFolder.cpp:333 | A .id |
| `track.set_solo` | src/core/ControlCommandsArrangement.cpp:437 | A .id |
| `track.visibility_set_apply` | src/core/ControlCommandsTrackFolderSets.cpp:298 | A .id |
| `track.visibility_set_list` | src/core/ControlCommandsTrackFolderSets.cpp:344 | A .id |
| `track.visibility_set_remove` | src/core/ControlCommandsTrackFolderSets.cpp:322 | A .id |
| `track.visibility_set_save` | src/core/ControlCommandsTrackFolderSets.cpp:273 | A .id |
| `transport.get_state` | src/core/ControlCommandsTransport.cpp:240 | A .id |
| `transport.play` | src/core/ControlCommandsTransport.cpp:113 | A .id |
| `transport.punch_clear` | src/core/ControlCommandsPunch.cpp:259 | A .id |
| `transport.punch_get_state` | src/core/ControlCommandsPunch.cpp:285 | A .id |
| `transport.punch_set` | src/core/ControlCommandsPunch.cpp:226 | A .id |
| `transport.seek` | src/core/ControlCommandsTransport.cpp:159 | A .id |
| `transport.set_tempo` | src/core/ControlCommandsTransport.cpp:204 | A .id |
| `transport.stop` | src/core/ControlCommandsTransport.cpp:139 | A .id |
| `transport.tempo_map_add` | src/core/ControlCommandsTransportMap.cpp:268 | A .id |
| `transport.tempo_map_clear` | src/core/ControlCommandsTransportMap.cpp:393 | A .id |
| `transport.tempo_map_get` | src/core/ControlCommandsTransportMap.cpp:238 | A .id |
| `transport.tempo_map_remove` | src/core/ControlCommandsTransportMap.cpp:335 | A .id |
| `transport.tempo_map_set_active` | src/core/ControlCommandsTransportMap.cpp:434 | A .id |
| `warp.add` | src/core/ControlCommandsWarpEdit.cpp:317 | A .id |
| `warp.list` | src/core/ControlCommandsWarp.cpp:286 | A .id |
| `warp.move` | src/core/ControlCommandsWarpEdit.cpp:338 | A .id |
| `warp.remove` | src/core/ControlCommandsWarpEdit.cpp:359 | A .id |
| `warp.set` | src/core/ControlCommandsWarpEdit.cpp:382 | A .id |
| `wasm.list` | src/core/ControlCommandsWasm.cpp:180 | B helper |
| `wasm.load` | src/core/ControlCommandsWasmEdit.cpp:80 | B helper |
| `wasm.process` | src/core/ControlCommandsWasm.cpp:286 | B helper |
| `wasm.set_param` | src/core/ControlCommandsWasmEdit.cpp:185 | B helper |
| `wasm.unload` | src/core/ControlCommandsWasmEdit.cpp:75 | B helper |

## 2. The live cross-check — one instance, reaped by explicit PID

```
$ pgrep -a zene
1319566 .../zene-030/build/zene --control-socket /home/kruzzzzy/zene-030-audit-auditor/audit.sock
$ python3 verification/ctl.py --socket /home/kruzzzzy/zene-030-audit-auditor/audit.sock ping
{"id": 1, "ok": true, "result": {"audio": {"device": "SDL (Simple DirectMedia Layer)",
 "sound_output": true, "start_failed": false, "state": "ok"}, "engine_ready": true,
 "pong": true, "proto": 1, "version": "0.2.1-alpha.159+571016f"}}
$ python3 verification/ctl.py --socket .../audit.sock commands
# 185 command(s)
...
$ kill 1319566          # explicit PID, never pkill -f
$ pgrep -a zene
$ echo EXIT=$?
EXIT=1                  # no instance left
```

**The live reading is `185` command ids.** Set-compared against the source's `.id = QStringLiteral`
assignment set it is an **exact match**: 185 == 185, with an empty difference in both directions.

Two honesty notes on the live reading:

- The binary was **built at `571016ff8`**, the parent of the tip — its own version string says so
  (`0.2.1-alpha.159+571016f`). The tip is registry-identical to it:
  `git diff --stat 571016ff8..3956ef589` → `tests/control-freeze-commands-transcript.py`,
  `tests/freeze_bounce_evidence.py` only, and `git diff --name-only 571016ff8..3956ef589 -- src/core/`
  is **empty**. So the live id set is the tip's id set.
- `pgrep -af 'zene-030/build'` after the reap still matches **the shell running that command**. That is
  the documented hazard this lane was warned about, and it is why the reap was done with `kill <PID>`.
  The authoritative check is `pgrep -a zene` → exit 1.

### 2.1 The three registered refusals, read live

```
$ ctl.py describe track.set_arm
 "description": "Arm or disarm a track for recording. Refused: this tree has no record-arm on a song track."
$ ctl.py describe mixer.set_pan
 "description": "Set a channel pan. Refused: this tree has no pan on a mixer channel."
$ ctl.py describe automation.mode_set
 "description": "Set a parameter's automation mode. Refused: this build has automation modes in the engine
                  but no way to select or persist one (docs/KNOWN-LIMITATIONS.md), so no write is faked."
```

The feature list's section *The three commands that exist only to refuse* is therefore **still true at
the tip** and is not edited.

## 3. Registered ctests in the built tree

```
$ grep -oE '^add_test\([A-Za-z0-9_]+' zene-030/build/tests/CTestTestfile.cmake | sed 's/add_test(//' | sort -u | wc -l
140
```

The proofs the corrected rows name, each present in that list:

| proof ctest | in the build's CTestTestfile |
|---|---|
| `TrackFolderTest`, `ControlTrackFolderTranscript` | yes |
| `ControlChainPresets`, `ControlChainPresetTest` | yes |
| `MidiClockTest`, `ControlClockCommands` | yes |
| `MidiRetroCaptureTest`, `ControlRetroCapture` | yes |
| `ControlPunchTranscript` | yes |
| `ControlRecordingRecovery` | yes |
| `ControlFreezeCommandsTranscript` | yes |
| `ControlGrooveCommands`, `GrooveTemplateTest` | yes |

## 4. The tracker's own count, and the two defects in it

`python3 scripts/zene-feature-tracker.py` is the measuring instrument. Reproduced exactly, at the tip
its id extraction returns **192 ids over 35 groups** — 7 more than the live surface. The 7 reconcile
completely, and are **not** new features:

| extra id | where it comes from | why it is not a registered id |
|---|---|---|
| `wasm.list`, `wasm.load`, `wasm.process`, `wasm.set_param`, `wasm.unload` | `src/core/ControlCommandsWasm*.cpp` | real ids, but the group is behind `#ifdef LMMS_HAVE_WASM` and the release build does not define it: they are absent from the live list. `registerWasmCommands` is called under the gate. |
| `tag.add`, `tag.remove` | `src/core/ControlCommandsBrowserTags.cpp:175`, `:202` | **false positives**: those lines are `cmd.verb = QStringLiteral("tag.add")` / `("tag.remove")`, the *verb* field of the genuinely registered `browser.tag.add` / `browser.tag.remove`, not ids. The extraction's part-B regex matches any `QStringLiteral("a.b")`, including a verb. |

So the surface is stated on three bases, and they must not be mixed:

| basis | groups | ids | source |
|---|---|---|---|
| the build (`control.commands_list`, live) | **33** | **185** | §2 |
| the registry source, `.id =` only | **33** | **185** | §1 |
| the registry source, `.id =` **plus** the compile-gated `wasm` group | **34** | **190** | §1 |
| the tracker's regex over the source | **35** | **192** | §4 — inflated by the 2 verb-field false positives |

The feature list's own header and row 47 carried the older figures (170 ids / 31 groups). They are
corrected in `docs/FEATURE-LIST-0.3.0.md` to the first two rows of this table.

## 5. The probe-by-probe table — all 89 rows, at `3956ef589`

Produced by importing the tracker and calling its own `evaluate()`, so a cell here is the instrument's
verdict and not a re-implementation. `— (no probe)` means the row supplies nothing to measure that
part, which is **EXCLUDED** from the percentage rather than scored 0.

**This table is the state *after* the corrections this pass made to `docs/FEATURE-LIST-0.3.0.md`**
(commit `42f70eb4f` on `030/audit`), because the tracker reads the list from the branch, not the working
tree. The same instrument on the list *before* the corrections printed
`{'THIN-100': 10, 'ADDED': 13, 'IN-PROGRESS': 18, 'NOT-STARTED': 48}`; after, it prints the line below.
The five rows that moved from `NOT-STARTED` to `ADDED` are **5, 15, 41, 62 and 74**.

| # | feature | engine | ids | proof | limits | measurable | % | state | doc says |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Session View — engine: clip/scene model (#594), launch semantics | — (no probe) | ✅ 11/11 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | PARTIAL |
| 2 | Clip fades, crossfades and clip gain — this is the whole of "cli | — (no probe) | ✅ 3/3 ids | ✅ ClipEditsTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 3 | Comping — take lanes and a non-destructive composite (W4) | — (no probe) | ✅ 7/7 ids | ✅ TakeLaneCompTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 4 | Phase-locked multitrack edit groups | ✅ include/VcaGroup.h | ❌ none yet — no command group declared | ✅ VcaGroupTest | — (no probe) | 3/4 | 66% | IN-PROGRESS | PARTIAL |
| 5 | Folder tracks | ✅ include/TrackFolder.h | ✅ 5/5 ids | ✅ TrackFolderTest | ✅ line found | 4/4 | 100% | ADDED | IN THE TREE |
| 6 | Linked / smart clips | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 60 | Clip trim | — (no probe) | ❌ 0/1 ids | — (no probe) | ✅ line found | 2/4 | 50% | IN-PROGRESS | TO BUILD |
| 61 | Clip slip | — (no probe) | ❌ 0/1 ids | — (no probe) | ✅ line found | 2/4 | 50% | IN-PROGRESS | TO BUILD |
| 62 | Folder tracks as a routing / mix group — the routing half of OWN | — (no probe) | ✅ 1/1 ids | ✅ TrackFolderTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 7 | Modulation layer (#602) and per-note expression | — (no probe) | ✅ 10/10 ids | ✅ ModulationLayerValueTest | ❌ no line in limitations/notes | 3/4 | 66% | IN-PROGRESS | IN THE TREE |
| 8 | MPE capture, storage and edit (#601) | — (no probe) | ❌ note.expression_.* = 0 ids | — (no probe) | ❌ no line in limitations/notes | 2/4 | 0% | NOT-STARTED | PARTIAL |
| 9 | Sample-accurate automation | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 10 | Automation modes | — (no probe) | ✅ 1/1 ids | ✅ ControlAutomationScriptTest | ✅ line found | 3/4 | 100% | ADDED | PARTIAL |
| 11 | Note random, note transform and slide notes | — (no probe) | ❌ none yet — no command group declared | ✅ NoteRandomTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 63 | `automation.record_mode_set` — the record-mode verb the boarded- | — (no probe) | ✅ 1/1 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | TO BUILD |
| 12 | Punch in / out | — (no probe) | ✅ 5/5 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | IN THE TREE |
| 13 | Recording crash recovery | — (no probe) | ✅ 7/7 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | IN THE TREE |
| 14 | Multi-track recorder | — (no probe) | ✅ 1/1 ids | ✅ MultiTrackRecorderTest | ✅ line found | 3/4 | 100% | ADDED | PARTIAL |
| 15 | Retrospective MIDI capture | ✅ include/RetroMidiCapture.h | ✅ 3/3 ids | ✅ MidiRetroCaptureTest | ✅ line found | 4/4 | 100% | ADDED | IN THE TREE |
| 16 | Retrospective audio capture | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 64 | Arbitrary input count / multiple simultaneous inputs | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 17 | MIDI learn | — (no probe) | ✅ 1/1 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | PARTIAL |
| 18 | MIDI controller auto-reconnection | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 19 | Controller soft-takeover, LED feedback, mapping templates | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 65 | Scale-aware root-note highlighting — the residual half of OWNER- | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | PARTIAL |
| 66 | `scale.*` — the scale command group the boarded-gaps list names | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 67 | `note.probability_set` — the probability verb the boarded-gaps l | — (no probe) | ❌ none yet — no command group declared | ✅ NoteRandomTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | TO BUILD |
| 20 | Freeze / bounce-in-place | — (no probe) | ✅ 4/4 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | IN THE TREE |
| 21 | Export dither and explicit SRC quality | — (no probe) | ✅ 3/3 ids | ✅ ExportDitherTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 22 | Warp markers and the clip warp tempo mode (W2) | — (no probe) | ✅ 5/5 ids | ✅ ControlWarpCommandsTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 23 | WASM DSP sandbox, and the documented WASM effect ABI (#614) | — (no probe) | ✅ 1/1 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | PARTIAL |
| 24 | LUFS / loudness metering | — (no probe) | ✅ 1/1 ids | ✅ LufsMeterTest | ✅ line found | 3/4 | 100% | ADDED | PARTIAL |
| 25 | Mastering chain / auto-mastering | ✅ src/core/MasteringChain.cpp | ❌ none yet — no command group declared | ✅ MasteringTest | — (no probe) | 3/4 | 66% | IN-PROGRESS | PARTIAL |
| 26 | Stem separation | — (no probe) | ❌ 0/1 ids | ✅ OnnxRuntimeStemSeparatorTest | ❌ no line in limitations/notes | 3/4 | 33% | IN-PROGRESS | PARTIAL |
| 27 | PDC and sidechain | — (no probe) | ❌ none yet — no command group declared | ✅ PdcMixerTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 28 | Routing graph | — (no probe) | ❌ none yet — no command group declared | ✅ RoutingGraphTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 29 | Audio ports / `AudioBus` | — (no probe) | ❌ none yet — no command group declared | ✅ AudioPortsTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 30 | Pitch-preserving time-stretch | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 68 | Stem export — per-track / per-bus, post-fader, tail convention,  | — (no probe) | ❌ 0/1 ids | ✅ StemExportTest | ✅ line found | 3/4 | 66% | IN-PROGRESS | PARTIAL |
| 69 | Patcher node-graph driving | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 70 | Render / export presets | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 71 | Selection-to-audio | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 72 | Auto-mastering wave 1 (`#610`) — candidate generation + objectiv | ✅ MasteringChain.cpp | ❌ none yet — no command group declared | ✅ MasteringTest | — (no probe) | 3/4 | 66% | IN-PROGRESS | PARTIAL |
| 73 | `CODE-5` — WASM worker: shared pool, real wake-ups, deterministi | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 31 | Tempo map — tempo and time-signature changes | — (no probe) | ✅ 5/5 ids | ✅ TempoMapTest | ❌ no line in limitations/notes | 3/4 | 66% | IN-PROGRESS | IN THE TREE |
| 32 | Groove pool and quantise | — (no probe) | ✅ 7/7 ids | ✅ GrooveTemplateTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 33 | Tempo-map export / SMF cross-DAW interchange | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 34 | Transient / BPM / key detection on import | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 35 | Chord track, chord detection, progression tools, generators | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 36 | Ableton-Link session sync (W6) | — (no probe) | ✅ 5/5 ids | ✅ ControlLinkCommandsTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 37 | DAWproject import / export | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 38 | Project collection / archive, hashing, relink | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 74 | MIDI clock / MTC — the DAW as clock master or slave | ✅ include/MidiClock.h | ✅ 3/3 ids | ✅ MidiClockTest | ✅ line found | 4/4 | 100% | ADDED | PARTIAL |
| 39 | Bounded, coalescing undo | — (no probe) | ✅ 5/5 ids | ✅ UndoBoundsTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 40 | Autosave / project recovery | — (no probe) | ✅ 1/1 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | PARTIAL |
| 41 | Plugin chains as reusable presets | — (no probe) | ✅ 8/8 ids | ✅ ControlChainPresetTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 42 | mmpz-git depth (#612) | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 75 | Undo robustness — structural-op journalling, and undo of a delet | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 76 | In-app revision timeline (OWNER-31 item 30) | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 77 | Safe-start mode after a crash — launch with third-party plugins  | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 43 | Browser tag/metadata search and the waveform peak cache (W7) | — (no probe) | ✅ 7/7 ids | ✅ BrowserCatalogTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 44 | Rack macros and key/velocity zones (W3) | — (no probe) | ✅ 12/12 ids | ✅ RackMacrosTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 45 | CLAP hosting on Windows | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 46 | Plugin scan cache and quarantine | — (no probe) | ❌ none yet — no command group declared | ✅ PluginScanCacheTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 78 | Third-party VST3 instrument hosting (`STATUS item 18`) | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | PARTIAL |
| 79 | CLAP instrument hosting | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 80 | Out-of-process plugin hosting / crash isolation | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | PARTIAL |
| 81 | `device.mpe_set` — the MPE device verb the boarded-gaps list nam | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 82 | `CODE-4` — plugin hosts process in chunks instead of truncating  | ❌ Vst3Host.cpp | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 2/4 | 0% | NOT-STARTED | TO BUILD |
| 83 | `CODE-9` — Windows named-pipe control transport | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 47 | The control surface itself: `--control-socket` and the in-app co | — (no probe) | ✅ 2/2 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | IN THE TREE |
| 48 | `ARCH-2` — the control registry as a `zene::api` boundary | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 49 | MCP bridge coverage of the tree's surface | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 50 | Lua API stabilisation (#613) | — (no probe) | ✅ 2/2 ids | ✅ ScriptBindingsTest | ❌ no line in limitations/notes | 3/4 | 66% | IN-PROGRESS | PARTIAL |
| 51 | Stable-ID contract, slice 2 | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | PARTIAL |
| 84 | Telemetry v1 (`#617`) — opt-in platform statistics and the `-DZE | — (no probe) | ✅ 2/2 ids | ✅ TelemetryTest | ✅ line found | 3/4 | 100% | ADDED | IN THE TREE |
| 85 | `telemetry.consent_set` — the consent verb the boarded-gaps list | — (no probe) | ✅ 2/2 ids | — (no probe) | ✅ line found | 2/4 | 100% | THIN-100 | TO BUILD |
| 86 | `CODE-6` — Lua: a memory budget beside the instruction budget | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 87 | `CODE-7` — telemetry transport: https only, never block the call | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 88 | `CODE-8` — control-server shutdown hook must survive its owner | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 89 | The A16 reversibility contract, and the row-count deliverable th | — (no probe) | ❌ none yet — no command group declared | ✅ ReversibilityContractTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 52 | Real-time-safety whole-tree verification programme | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 53 | Golden-audio integration programme | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 54 | Crash reporter | — (no probe) | ❌ none yet — no command group declared | ✅ CrashReporterTest | — (no probe) | 2/4 | 50% | IN-PROGRESS | PARTIAL |
| 55 | `REL-2` — a release job that cannot run from a red commit | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 56 | `REPO-2` — a gate refusing evidence file types and oversized fil | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |
| 57 | `REPO-4` — move lane reports and transcripts out of the reposito | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | PARTIAL |
| 58 | The whole-tree ratchet decision | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | IN THE TREE |
| 59 | `DOC-5` — commit or remove the specifications the docs and code  | — (no probe) | ❌ none yet — no command group declared | — (no probe) | — (no probe) | 1/4 | 0% | NOT-STARTED | TO BUILD |

```
state counts: {"THIN-100": 10, "ADDED": 18, "IN-PROGRESS": 18, "NOT-STARTED": 43}
```

## 6. What this changes against the 2026-09-13 matrix

The 2026-09-13 matrix pinned `ddf5f171d` and counted **150 ids over 28 groups** — `144` `.id =`
assignments plus `6` helper-built `wasm.*`. At `3956ef589` the source registers **185 ids over 33 id
prefixes** (34 group names including the gate-compiled `wasm` group, which is now `5` ids). The **41**
ids that are new against that base are exactly the groups merged after the snapshot:

| group | ids | newly registered |
|---|---|---|
| `chain` | 6 | `chain.list`, `chain.get_state`, `chain.save`, `chain.apply`, `chain.rename`, `chain.remove` |
| `clock` | 3 | `clock.get_state`, `clock.master_set`, `clock.slave_set` |
| folder tracks, on `track.*` | 9 | `track.set_folder`, `track.folder_set_collapsed`, `track.folder_get_state`, `track.set_pinned`, `track.set_routing`, `track.visibility_set_save`, `track.visibility_set_apply`, `track.visibility_set_remove`, `track.visibility_set_list` |
| `midi.retro_capture_*` | 3 | `midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip` |
| `record` | 6 | `record.journal_begin`, `record.journal_update`, `record.journal_finish`, `record.recovery_get_state`, `record.recovery_restore`, `record.recovery_discard` |
| `freeze` + `bounce` | 4 | `freeze.track`, `freeze.region`, `freeze.unfreeze`, `bounce.in_place` |
| `groove` | 7 | `groove.list`, `groove.extract`, `groove.apply`, `groove.quantize`, `groove.set`, `groove.rename`, `groove.remove` |
| `transport.punch_*` | 3 | `transport.punch_set`, `transport.punch_get_state`, `transport.punch_clear` |
| **total** | **41** | 6 + 3 + 9 + 3 + 6 + 4 + 7 + 3 = **41**, and 144 + 41 = 185 |

**Not present at the tip, measured** (a `grep` over `src/core/ControlCommands*.cpp`,
`src/core/*Control*.cpp` and `src/core/*Control*.h` for `QStringLiteral("<prefix>.")` returns nothing):

```
clip.trim 0   clip.slip 0   scale. 0   device.mpe_set 0   render.stems 0   stem. 0
mastering. 0  lufs. 0       meter. 0   patcher. 0        pdc. 0            vca. 0
```

`vca.` is 0 **in `zene-030` today** and rows 4/27/28/29 are left untouched: `030/vca-editgroups` and
`030/routing-surface` are two builds in flight adding exactly those groups.

## 7. What a row's percentage does and does not mean

Three limits of the instrument, measured here rather than assumed, and each visible in §5:

1. **A registered refusal scores 100 on the `ids` part.** `track.set_arm`, `mixer.set_pan` and
   `automation.mode_set` are registered (so rows 10, 14 and 63 measure 100 % on `ids`) while §2.1
   shows each answers "Refused". Rows 10 and 14 measure **ADDED** while their own prose records the
   feature as partial. The measurement wins over the document **only** in the direction of the
   document being corrected; where the document's prose is right and the probe is a registration
   count, the row keeps its prose verdict and the disagreement is recorded, not averaged away.
2. **A proof whose name does not end in `Test` is not probed.** The extraction only treats
   `^[A-Z][A-Za-z0-9_]*Test$` as a proof name, so the registered ctests `ControlPunchTranscript`,
   `ControlRecordingRecovery`, `ControlFreezeCommandsTranscript`, `ControlGrooveCommands`,
   `ControlChainPresets` and `ControlTrackFolderTranscript` are **registered and unprobed** — rows 12,
   13, 20, 32, 41 and 5 respectively. §3 proves each is registered.
3. **A declared group written with a trailing underscore is not matched.** `note.expression_*` in
   rows 7 and 8 is reduced by the parser to the prefix `note.expression_`, which matches no id, so row
   8's `ids` part measures 0 although `note.expression_set` / `_get` / `_clear` are registered
   (`src/core/ControlCommandsNoteExpression.cpp:137`, `:193`, `:242`).

None of the three is fixed here: the tracker is a shared instrument, and each of these changes a number
other lanes are reading. They are recorded so a reader can correct for them, and row 41 — the one the
feature queue was sending the fleet at — is corrected with a proof name that the instrument *can* see
(`ControlChainPresetTest`), so its verdict moves on the instrument's own terms.

## 8. Every row this pass did **not** touch, and why — 83 of 89

One clause per row, generated from the row's own measured probes. `untouched rows: 83  touched: 6  total: 89`.

| # | feature | measured | doc says | why it was not touched |
|---|---|---|---|---|
| 1 | Session View — engine: clip/scene model (#594), launch sem | THIN-100 | PARTIAL | already accurate: the row records the missing half; the probes the row supplies all pass |
| 2 | Clip fades, crossfades and clip gain — this is the whole o | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 3 | Comping — take lanes and a non-destructive composite (W4) | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 4 | Phase-locked multitrack edit groups | IN-PROGRESS | PARTIAL | left untouched **by instruction**: `030/vca-editgroups` and `030/routing-surface` are two builds in flight adding the `vca.*` and routing/PDC/bus groups; measured at `3956ef589`, `grep -h -oE 'QStringLiteral("vca\.' src/core/ControlCommands*.cpp src/core/*Control*.cpp` -> 0 hits |
| 6 | Linked / smart clips | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 60 | Clip trim | IN-PROGRESS | TO BUILD | already accurate in verdict: the engine half the row names is in the tree and its own command group is still absent (the row says so); no group to declare |
| 61 | Clip slip | IN-PROGRESS | TO BUILD | already accurate in verdict: the engine half the row names is in the tree and its own command group is still absent (the row says so); no group to declare |
| 7 | Modulation layer (#602) and per-note expression | IN-PROGRESS | IN THE TREE | already accurate in verdict: the row says in the tree and its own prose names the caveat the probe cannot see |
| 8 | MPE capture, storage and edit (#601) | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 9 | Sample-accurate automation | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 10 | Automation modes | ADDED | PARTIAL | left as *partial*: the probe reaches 100 % only because a registered id counts as satisfied, and the row's prose records a half that is genuinely absent — see the disagreement section |
| 11 | Note random, note transform and slide notes | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 63 | `automation.record_mode_set` — the record-mode verb the bo | THIN-100 | TO BUILD | not corrected: the row's own prose names a verb that does not exist while its declared absence lines pass — the probe cannot see the prose, so the verdict stands as 'to build' |
| 12 | Punch in / out | THIN-100 | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 13 | Recording crash recovery | THIN-100 | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 14 | Multi-track recorder | ADDED | PARTIAL | left as *partial*: the probe reaches 100 % only because a registered id counts as satisfied, and the row's prose records a half that is genuinely absent — see the disagreement section |
| 16 | Retrospective audio capture | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 64 | Arbitrary input count / multiple simultaneous inputs | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 17 | MIDI learn | THIN-100 | PARTIAL | already accurate: the row records the missing half; the probes the row supplies all pass |
| 18 | MIDI controller auto-reconnection | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 19 | Controller soft-takeover, LED feedback, mapping templates | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 65 | Scale-aware root-note highlighting — the residual half of  | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 66 | `scale.*` — the scale command group the boarded-gaps list  | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 67 | `note.probability_set` — the probability verb the boarded- | IN-PROGRESS | TO BUILD | already accurate in verdict: the engine half the row names is in the tree and its own command group is still absent (the row says so); no group to declare |
| 20 | Freeze / bounce-in-place | THIN-100 | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 21 | Export dither and explicit SRC quality | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 22 | Warp markers and the clip warp tempo mode (W2) | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 23 | WASM DSP sandbox, and the documented WASM effect ABI (#614 | THIN-100 | PARTIAL | already accurate: the row records the missing half; the probes the row supplies all pass |
| 24 | LUFS / loudness metering | ADDED | PARTIAL | left as *partial*: the probe reaches 100 % only because a registered id counts as satisfied, and the row's prose records a half that is genuinely absent — see the disagreement section |
| 25 | Mastering chain / auto-mastering | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 26 | Stem separation | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 27 | PDC and sidechain | IN-PROGRESS | PARTIAL | left untouched **by instruction**: `030/vca-editgroups` and `030/routing-surface` are two builds in flight adding the `vca.*` and routing/PDC/bus groups; measured at `3956ef589`, `grep -h -oE 'QStringLiteral("vca\.' src/core/ControlCommands*.cpp src/core/*Control*.cpp` -> 0 hits |
| 28 | Routing graph | IN-PROGRESS | PARTIAL | left untouched **by instruction**: `030/vca-editgroups` and `030/routing-surface` are two builds in flight adding the `vca.*` and routing/PDC/bus groups; measured at `3956ef589`, `grep -h -oE 'QStringLiteral("vca\.' src/core/ControlCommands*.cpp src/core/*Control*.cpp` -> 0 hits |
| 29 | Audio ports / `AudioBus` | IN-PROGRESS | PARTIAL | left untouched **by instruction**: `030/vca-editgroups` and `030/routing-surface` are two builds in flight adding the `vca.*` and routing/PDC/bus groups; measured at `3956ef589`, `grep -h -oE 'QStringLiteral("vca\.' src/core/ControlCommands*.cpp src/core/*Control*.cpp` -> 0 hits |
| 30 | Pitch-preserving time-stretch | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 68 | Stem export — per-track / per-bus, post-fader, tail conven | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 69 | Patcher node-graph driving | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 70 | Render / export presets | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 71 | Selection-to-audio | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 72 | Auto-mastering wave 1 (`#610`) — candidate generation + ob | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 73 | `CODE-5` — WASM worker: shared pool, real wake-ups, determ | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 31 | Tempo map — tempo and time-signature changes | IN-PROGRESS | IN THE TREE | already accurate in verdict: the row says in the tree and its own prose names the caveat the probe cannot see |
| 32 | Groove pool and quantise | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 33 | Tempo-map export / SMF cross-DAW interchange | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 34 | Transient / BPM / key detection on import | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 35 | Chord track, chord detection, progression tools, generator | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 36 | Ableton-Link session sync (W6) | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 37 | DAWproject import / export | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 38 | Project collection / archive, hashing, relink | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 39 | Bounded, coalescing undo | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 40 | Autosave / project recovery | THIN-100 | PARTIAL | already accurate: the row records the missing half; the probes the row supplies all pass |
| 42 | mmpz-git depth (#612) | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 75 | Undo robustness — structural-op journalling, and undo of a | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 76 | In-app revision timeline (OWNER-31 item 30) | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 77 | Safe-start mode after a crash — launch with third-party pl | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 43 | Browser tag/metadata search and the waveform peak cache (W | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 44 | Rack macros and key/velocity zones (W3) | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 45 | CLAP hosting on Windows | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 46 | Plugin scan cache and quarantine | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 78 | Third-party VST3 instrument hosting (`STATUS item 18`) | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 79 | CLAP instrument hosting | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 80 | Out-of-process plugin hosting / crash isolation | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 81 | `device.mpe_set` — the MPE device verb the boarded-gaps li | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 82 | `CODE-4` — plugin hosts process in chunks instead of trunc | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 83 | `CODE-9` — Windows named-pipe control transport | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 48 | `ARCH-2` — the control registry as a `zene::api` boundary | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 49 | MCP bridge coverage of the tree's surface | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 50 | Lua API stabilisation (#613) | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 51 | Stable-ID contract, slice 2 | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 84 | Telemetry v1 (`#617`) — opt-in platform statistics and the | ADDED | IN THE TREE | already accurate: every probe the row supplies passes and the row says in the tree |
| 85 | `telemetry.consent_set` — the consent verb the boarded-gap | THIN-100 | TO BUILD | not corrected: the row's own prose names a verb that does not exist while its declared absence lines pass — the probe cannot see the prose, so the verdict stands as 'to build' |
| 86 | `CODE-6` — Lua: a memory budget beside the instruction bud | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 87 | `CODE-7` — telemetry transport: https only, never block th | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 88 | `CODE-8` — control-server shutdown hook must survive its o | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 89 | The A16 reversibility contract, and the row-count delivera | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 52 | Real-time-safety whole-tree verification programme | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 53 | Golden-audio integration programme | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 54 | Crash reporter | IN-PROGRESS | PARTIAL | already accurate in verdict: the missing half is named and measured still missing |
| 55 | `REL-2` — a release job that cannot run from a red commit | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 56 | `REPO-2` — a gate refusing evidence file types and oversiz | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |
| 57 | `REPO-4` — move lane reports and transcripts out of the re | NOT-STARTED | PARTIAL | already accurate in verdict: the row's own prose is the only probe it supplies |
| 58 | The whole-tree ratchet decision | NOT-STARTED | IN THE TREE | no probe the tree can satisfy yet: the row names a decision/programme, not an id, test or file, so the instrument has nothing to measure |
| 59 | `DOC-5` — commit or remove the specifications the docs and | NOT-STARTED | TO BUILD | already accurate: the row records 'to build' and its declared absence measures absent |

## 9. Dispositions — the rows whose verdict changed

| # | feature | before | after | the measurement that moved it |
|---|---|---|---|---|
| 5 | Folder tracks | to build | **in the tree** | 9 `track.*` folder ids registered (`ControlCommandsTrackFolder.cpp:283,310,333,361,386`, `ControlCommandsTrackFolderSets.cpp:273,298,322,344`); `include/TrackFolder.h` + `src/tracks/TrackFolder.cpp` present; `TrackFolderTest` and `ControlTrackFolderTranscript` registered |
| 15 | Retrospective MIDI capture | to build | **in the tree** | `midi.retro_capture_arm`/`_status`/`_to_clip` (`ControlCommandsMidi.cpp:163,214,360`); `MidiRetroCaptureTest` + `ControlRetroCapture` registered |
| 41 | Plugin chains as reusable presets | to build | **in the tree** | `chain.*` 6 ids (`ControlCommandsChain.cpp:198,248,282`, `ControlCommandsChainEdit.cpp:147,189,266`); `ControlChainPresets` + `ControlChainPresetTest` registered |
| 62 | Folder routing / mix group | to build | **in the tree** | `track.set_routing` (`ControlCommandsTrackFolder.cpp:333`) — the `routing` mode that sums the children through the folder's own mixer channel |
| 74 | MIDI clock / MTC | to build | **partial** | `clock.*` 3 ids (`ControlCommandsClock.cpp:209,240,268`); `MidiClockTest` + `ControlClockCommands` registered — **MTC stays absent** (`mtc: "absent"`) |
| 47 | the control surface itself | figures stale | figures corrected | 33 groups / 185 ids at `3956ef589`, source and live agreeing exactly |

Rows that were **stale but already correct** — verified at the tip and left alone: **12** (punch),
**13** (recording crash recovery) and **20** (freeze/bounce), whose statuses already read *in the tree*
and whose named ctests (`ControlPunchTranscript`, `ControlRecordingRecovery`,
`ControlFreezeCommandsTranscript`) are all still registered; and **43, 44, 84** and the other `ADDED`
rows, whose every probe passes exactly as the row says.

**Rows that are genuinely open after the correction: 71 of 89** — by the instrument's own state,
`IN-PROGRESS` 18 + `NOT-STARTED` 43 + `THIN-100` 10. By the list's own prose the count is **68**
(`partial` 27 + `to build` 41). The two differ because a row can measure 100 % while its prose records a
half a registration count cannot see (row 74 above; rows 10 and 14 as well), and because a row can
measure 0 % while naming a decision rather than a probe (rows 52, 53, 55, 56, 58, 59). **Neither figure is
an estimate**: one is the instrument's, the other is the list's, and §7 says what each can and cannot see.
