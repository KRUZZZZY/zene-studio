# Offline stem separation (opt-in, see doc/STEM-SPLIT.md). The job manager and
# track builder are pure Qt and always available when the feature is enabled;
# the C++ ONNX Runtime backend is compiled only when the runtime was found.
IF(LMMS_HAVE_STEM_SPLIT)
	SET(LMMS_STEM_SRCS
		core/ExternalProcessStemSeparator.cpp
		core/StemJobManager.cpp
		core/StemModelStore.cpp
		core/StemTrackBuilder.cpp
		# The agent surface of the same engine (feature row 26, task #653): the
		# socket-facing half - the instance's one job manager, its availability
		# probe and its result writer (ControlStemSupport.h) - the model-store
		# half of it, the WAV writer, and the `stem.*` group itself (its read +
		# job half and its model-store half). Inside the option because without
		# it there is no engine for any of them to drive; each file is split the
		# way this tree splits a group, for the file-length ratchet.
		core/ControlStemSupport.cpp
		core/ControlStemModel.cpp
		core/ControlStemWav.cpp
		core/ControlCommandsStems.cpp
		core/ControlCommandsStemModel.cpp
		# Listed so that AUTOMOC finds the Q_OBJECT header (it lives in
		# include/StemSeparation/, not next to the .cpp).
		${CMAKE_SOURCE_DIR}/include/StemSeparation/StemJobManager.h
	)
	IF(LMMS_HAVE_ONNXRUNTIME)
		LIST(APPEND LMMS_STEM_SRCS core/OnnxRuntimeStemSeparator.cpp)
	ENDIF()
ENDIF()

# Opt-in telemetry (see docs/TELEMETRY-V1.md). The whole client is compiled only
# when the packager left the kill switch on. The consent model, the payload
# allowlist, the "what we send" builder and the networking transport are one
# client: -DZENE_TELEMETRY=OFF (the root CMakeLists.txt option) removes all of
# them from the build, so no translation unit of the client is compiled and the
# binary carries no telemetry symbol and no telemetry string. The sources are
# guarded by ZENE_TELEMETRY_ENABLED as well (see src/core/Telemetry.cpp and
# src/core/ControlCommandsTelemetry.cpp); this condition is what keeps the file
# - and its debug-info file name - out of the OFF build in the first place.
# Verified end to end by tests/telemetry-off-build.sh and
# docs/TELEMETRY-KILL-SWITCH.md.
SET(LMMS_TELEMETRY_SRCS)
IF(ZENE_TELEMETRY_ENABLED)
	LIST(APPEND LMMS_TELEMETRY_SRCS
		core/Telemetry.cpp
		core/TelemetryNetworkTransport.cpp
	)
ENDIF()

# The telemetry.* command group is the client's agent surface, so it travels with
# the client (SPEC A15). Kept in its own variable so the big source list below
# keeps the position it has always had.
SET(LMMS_TELEMETRY_COMMAND_SRCS)
IF(ZENE_TELEMETRY_ENABLED)
	LIST(APPEND LMMS_TELEMETRY_COMMAND_SRCS core/ControlCommandsTelemetry.cpp)
ENDIF()

# The wasm.* command group is the WASM DSP sandbox's agent surface, so it travels
# with the sandbox (SPEC A11-A16, item #614). The engine half is
# src/wasm/WasmSandboxHost.{h,cpp}; this is the surface half. Kept in its own
# variable so the big source list below keeps the position it has always had, and
# registerControlCommands() guards its call with #ifdef LMMS_HAVE_WASM, so a build
# without wasmtime neither compiles these translation units nor advertises ids
# whose handler could not exist.
SET(LMMS_WASM_COMMAND_SRCS)
IF(WANT_WASM)
	LIST(APPEND LMMS_WASM_COMMAND_SRCS
		core/ControlWasmSupport.cpp
		core/ControlCommandsWasm.cpp
		core/ControlCommandsWasmEdit.cpp
		# CODE-5 (feature row 73): the pool's own state and the deterministic
		# offline render with its measured tolerance verdict.
		core/ControlCommandsWasmRender.cpp
	)
ENDIF()

# Session View data layer (opt-in, see include/SessionModel.h). Pure Qt: the
# model, the clip slots and scenes, and the versioned <session> block.
IF(LMMS_HAVE_SESSION_VIEW)
	SET(LMMS_SESSION_SRCS
		core/SessionClip.cpp
		core/SessionModel.cpp
		core/SessionScheduler.cpp
		# The session.* control-surface group (SPEC A11-A16). It travels with
		# the data layer: with WANT_SESSION_VIEW=OFF the registry must not
		# advertise commands whose model is not compiled in, and
		# registerControlCommands() guards the two calls the same way.
		core/ControlCommandsSession.cpp
		core/ControlCommandsSessionLaunch.cpp
	)
ENDIF()

set(LMMS_SRCS
	${LMMS_SRCS}

	core/AudioBuffer.cpp
	core/AudioBus.cpp
	core/AudioBusHandle.cpp
	core/AudioEngine.cpp
	core/AudioEngineProfiler.cpp
	core/AudioEngineWorkerThread.cpp
	core/AudioPortsModel.cpp
	core/AudioResampler.cpp
	# Pitch-preserving time stretch (0.3.0 feature-list row 30): WSOLA. The
	# DSP the warp/clip path selects when a clip asks for PreservePitch.
	core/AudioStretcher.cpp
	core/AutomatableModel.cpp
	core/AutomationClip.cpp
	core/AutomationNode.cpp
	core/BandLimitedWave.cpp
	core/base64.cpp
	core/BrowserCatalog.cpp
	core/BrowserMetadata.cpp
	core/BrowserPeakCache.cpp
	core/BrowserTagStore.cpp
	core/BounceInPlace.cpp
	core/BufferManager.cpp
	core/Clipboard.cpp
	core/ComboBoxModel.cpp
	core/ConfigManager.cpp
	core/CrashReporter.cpp
	# The arm/disarm path (crash.enable / crash.disable, board task #643) pays
	# for its room in core/CrashReporter.cpp (zero-line tolerance in the file-length
	# baseline) by splitting that file: the async-signal-safe formatters and the
	# Windows no-op half live in their own translation units.
	core/CrashReporterFormat.cpp
	core/CrashReporterWindows.cpp
	core/ControlCommandsArrangement.cpp
	core/ControlCommandsClip.cpp
	core/ControlCommandsClipEdits.cpp
	# clip.trim / clip.slip (0.3.0 verb wave): the clip group's edge-editing verbs.
	# Split into its own TU because ControlCommandsClipEdits.cpp is at the file
	# ratchet; see the header of the file for the design rule both verbs follow.
	core/ControlCommandsClipTrim.cpp
	# Linked / smart clips (feature-list row 6, board task #645): the relation
	# (include/ClipLinks.h: the group id on the clip, the mirror that propagates
	# an edit to every member), the clip.link_* commands that drive it, and the
	# helpers the group's two translation units share. See docs/LINKED-CLIPS.md
	# for the design decision and the edit-kind table.
	core/ClipLinks.cpp
	core/ControlCommandsClipLink.cpp
	core/ControlCommandsClipLinkState.cpp
	# MIDI clock / MTC (0.3.0): the DAW as a clock master and as a clock slave.
	# The engine half is the MidiClock* sources below; this is the command group
	# that drives it. See docs/KNOWN-LIMITATIONS.md for the bound it states.
	core/ControlCommandsClock.cpp
	# Take lanes and the non-destructive composite (task #600): the take half,
	# the composite half, and the helpers the two share. See docs/COMPING.md.
	core/ControlCommandsComp.cpp
	core/ControlCommandsCompEdits.cpp
	core/ClipEdits.cpp
	core/ControlAutomationSupport.cpp
	core/ControlBrowserSupport.cpp
	core/ControlCommandsAutomation.cpp
	core/ControlCommandsAutomationEdit.cpp
	core/ControlCommandsBrowser.cpp
	core/ControlCommandsBrowserTags.cpp
	core/ControlCommandsControl.cpp
	core/ControlCompSupport.cpp
	core/ControlCommandsDsp.cpp
	core/ControlCommandsIdContract.cpp
	core/ControlCommandsMixer.cpp
	# The mixer group's routing verbs (mixer.route_to / send_to / sidechain_to /
	# route_remove) and the PDC / routing / bus / port groups - feature rows
	# 27-29 of docs/FEATURE-LIST-0.3.0.md. Separate translation units for the
	# reason the automation and warp groups are split: gate 7 measures a file.
	core/ControlMixerSupport.cpp
	core/ControlCommandsMixerRoutes.cpp
	core/ControlCommandsPdc.cpp
	core/ControlCommandsRouting.cpp
	core/ControlCommandsBus.cpp
	core/ControlCommandsPorts.cpp
	# Auto-mastering wave 1 (feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md,
	# docs/AUTO-MASTERING.md): the group's read half and its one writing verb, each
	# its own translation unit for the reason above, plus the shared helpers (the
	# child run's report, the instance's last-run record and the bounded
	# output-directory capture SPEC A16's inverse is recorded from).
	core/ControlMasteringSupport.cpp
	core/ControlCommandsMastering.cpp
	core/ControlCommandsMasteringRun.cpp
	# The loudness surface (feature row 24 of docs/FEATURE-LIST-0.3.0.md, "LUFS /
	# loudness metering"): the `meter.*` group - the live master tap and the file
	# measurement - and the passive tap it reads the master mix through
	# (include/MasterLoudnessTap.h). The measurement core itself is
	# core/LufsMeter.cpp and core/LoudnessReport.cpp, merged by earlier lanes and
	# untouched here: this group forks no DSP, it makes the merged meter
	# drivable.
	core/ControlCommandsMeter.cpp
	core/ControlCommandsMeterFile.cpp
	# ... which is also the group's shared JSON vocabulary and the target it
	# reports against (include/ControlMeterSupport.h), so the live readout and a
	# measured file cannot drift into two spellings of one number.
	core/MasterLoudnessTap.cpp
	# Standard MIDI File conductor interchange (feature row 33 of
	# docs/FEATURE-LIST-0.3.0.md, docs/SMF-INTERCHANGE.md): the tempo map written
	# as, and read back from, a format-1 conductor track another DAW can read.
	# Its own translation unit for the reason the automation and warp groups are
	# split - gate 7 measures a file, not a group.
	core/ControlCommandsInterchange.cpp
	# DAWproject import / export (feature row 37, docs/DAWPROJECT-INTERCHANGE.md):
	# the four dawproject.* ids. Its own translation unit for the same reason -
	# gate 7 measures a file, not a group.
	core/ControlCommandsDawProject.cpp
	# The modulation layer (#602): the layer + LFO half, the route half, and the
	# per-note expression group (docs/MODULATION.md).
	core/ControlCommandsModulator.cpp
	core/ControlCommandsModulatorRoutes.cpp
	core/ControlCommandsNoteExpression.cpp
	core/ControlCommandsMidi.cpp
	core/ControlCommandsNotes.cpp
	# note.probability_set (0.3.0 verb wave): the MIDI-depth probability verb the
	# engine has carried since docs/MIDI-DEPTH.md and never got an id for.
	core/ControlCommandsNoteProbability.cpp
	# The 0.3.0 note/scale/device wave (board task #648; feature-list rows 11, 66 and
	# 81): the note randomisation verbs (note.random_seed_get/_set, note.randomize),
	# the slide verbs (note.slide_set / note.slide_clear), the three note transforms,
	# the scale.* group (its shared vocabulary, its read half and its one clip-editing
	# verb) and the registry's first device.* group. Separate translation units for the
	# reason the automation, warp and mastering groups are split - Gate 7 measures a
	# file, not a group.
	core/ControlNoteShared.cpp
	core/ControlCommandsNoteRandom.cpp
	core/ControlCommandsNoteSlide.cpp
	core/ControlCommandsNoteTransform.cpp
	core/ControlScaleSupport.cpp
	core/ControlCommandsScale.cpp
	core/ControlCommandsScaleEdit.cpp
	core/ControlCommandsDevice.cpp
	core/ControlCommandsPlugin.cpp
	core/ControlCommandsPluginParams.cpp
	core/ControlCommandsPluginPreset.cpp
	core/ControlCommandsPluginState.cpp
	# The crash reporter's agent surface (feature row 54) and the plugin scan
	# cache + quarantine group (feature row 46: the read half, then the edit
	# half). Separate translation units for the reason the automation and warp
	# groups are split - gate 7 measures a file, not a group.
	core/ControlCommandsCrash.cpp
	core/ControlCommandsPluginScan.cpp
	core/ControlCommandsPluginScanEdit.cpp
	# plugin.host_chunking (feature row 82, CODE-4): the chunking contract both
	# host paths keep and the counters their audio paths increment. In the core
	# because the hosts are plugin modules: this is what makes the property
	# observable from --control-socket. Two files on the usual seam - the
	# counters (no engine dependency, so the host tests compile them next to the
	# host sources) and the command itself.
	core/PluginHostChunking.cpp
	core/ControlCommandsHostChunking.cpp
	core/ControlCommandsArrangementState.cpp
	core/ControlCommandsProject.cpp
	core/ControlCommandsProjectFiles.cpp
	# render.stems (0.3.0 verb wave): per-track stem export through the CLI path the
	# product already ships (`lmms exportstems`), with the same declared bound every
	# render-running command carries - see the file header and docs/RENDER-CHILD-WAIT.md.
	core/ControlCommandsRenderStems.cpp
	core/ControlCommandsRack.cpp
	core/ControlCommandsRackMacros.cpp
	core/ControlCommandsRackZones.cpp
	core/ControlCommandsScript.cpp
	core/ControlCommandsExport.cpp
	# The render/export preset store (feature row 70): the saved presets and the
	# settings the NEXT render is started with, plus the group's A16 rows. The
	# applied preset becomes the render CHILD's own command line
	# (controlExportPresetRenderArgs), so there is no second render path.
	core/ControlCommandsExportPresets.cpp
	core/ControlExportPresetSupport.cpp
	core/ControlCommandsFreeze.cpp
	# The session tempo/phase sync model and its wire (D11 "Ableton Link sync"):
	# the model, the transport seam, the UDP multicast transport, and the
	# link.* commands that make the whole of it drivable. See docs/LINK-SYNC.md.
	core/ControlCommandsLink.cpp
	core/LinkSync.cpp
	core/LinkSyncWire.cpp
	core/LinkUdpTransport.cpp
	core/ControlCommandsSettings.cpp
	core/ControlCommandsSurface.cpp
	# track.move: the structural half of track.* (task #664, feature row 75) - the
	# arrangement's ORDER as a drivable, undoable operation. Its own TU because
	# ControlCommandsArrangement.cpp is at the file-length ratchet.
	core/ControlCommandsStructure.cpp
	${LMMS_TELEMETRY_COMMAND_SRCS}
	core/ControlCommandsTransport.cpp
	core/ControlCommandsTrackFolder.cpp
	core/ControlCommandsTrackFolderSets.cpp
	# VCA / mix-and-edit groups (OWNER-31 item 11): the group+entity half, the
	# mix half (fader/mute/solo/membership) and the phase-locked edit half, each
	# its own translation unit (the folder-track, session, warp, rack, comp and
	# automation groups' split, for the file-length ratchet).
	core/ControlCommandsVca.cpp
	core/ControlCommandsVcaMix.cpp
	core/ControlCommandsVcaEdit.cpp
	core/ControlCommandsVcaEditSet.cpp
	core/TrackContainerFolders.cpp
	# The bounded-undo slice: the depth caps, the coalescing window, and the
	# commands that expose them (SPEC A16, task #623).
	core/ControlCommandsUndo.cpp
	core/ControlCommandsTransportMap.cpp
	# Punch in/out (0.3.0): the transport's punch region, on the transport group.
	core/ControlCommandsPunch.cpp
	# Recording crash recovery (0.3.0): the take journal and the two halves of the
	# record.* group (the journalling verbs and the recovery verbs).
	core/ControlCommandsRecording.cpp
	core/ControlCommandsRecordingRecovery.cpp
	core/ControlRecordingSupport.cpp
	# The recording engine surface (0.3.0, feature rows 14/16/64): the capture
	# IN path's plan and live state, the N-channel staging stage between a
	# capture thread and the render thread, the retrospective AUDIO capture and
	# its ring, and the group's two new command halves (the arm/input verbs and
	# the retro-capture verbs). docs/RECORD-INPUTS.md is the design record.
	core/AudioInputPath.cpp
	core/AudioWideInputStage.cpp
	core/RetroAudioCapture.cpp
	core/ControlCommandsRecordingInput.cpp
	core/ControlCommandsRecordingRoutes.cpp
	core/ControlCommandsRecordingRetro.cpp
	core/ControlCommandsWarp.cpp
	core/ControlCommandsWarpEdit.cpp
	# The groove pool and quantise surface (docs/GROOVE-POOL.md): the engine
	# value type and its extraction/application arithmetic, the project's named
	# pool, the group's shared helpers, its read half and its two mutating
	# halves.
	core/GrooveTemplate.cpp
	core/GroovePool.cpp
	core/ControlGrooveSupport.cpp
	core/ControlCommandsGroove.cpp
	core/ControlCommandsGrooveEdit.cpp
	core/ControlCommandsGroovePool.cpp
	${LMMS_WASM_COMMAND_SRCS}
	core/ControlDeviceCatalogue.cpp
	core/ControlDeviceHosted.cpp
	core/ControlDeviceState.cpp
	core/ControlDeviceSupport.cpp
	# The chain PRESET store and the chain.* group (the 0.3.0 ladder's
	# "plugin-chain presets", OWNER-31 item 2): the store, its read/capture half
	# and its edit half. A chain preset is NOT a rack chain - see
	# control/ControlCommandsChain.cpp.
	core/ControlChainPresetSupport.cpp
	core/ControlCommandsChain.cpp
	core/ControlCommandsChainEdit.cpp
	# The chord engine (feature row 35): the read-only view over the piano roll's
	# existing ChordTable, the persistent chord track, the detector and the
	# seeded progression generator. No new vocabulary: every name comes from
	# InstrumentFunctionNoteStacking::ChordTable. See docs/CHORD-TRACK.md.
	core/ChordVocabulary.cpp
	core/ChordTrack.cpp
	core/ChordDetect.cpp
	core/ChordProgression.cpp
	# ... and its command group: the read half + the shared wire helpers, then
	# the track-editing verbs and the two generators (the automation, warp and
	# chain groups' split - and here the split is also the A16 seam: the track
	# verbs record an action checkpoint, the generators reverse through the
	# clip's own checkpoint).
	core/ControlChordSupport.cpp
	core/ControlCommandsChord.cpp
	core/ControlCommandsChordEdit.cpp
	core/ControlCommandsChordWrite.cpp
	core/ControlEditSupport.cpp
	core/ControlModulationSupport.cpp
	core/ControlRackSupport.cpp
	core/ControlRegistry.cpp
	# The registry's COMPOSITION - which command groups exist and in what order -
	# split out of ControlRegistry.cpp when the vca group and the routing surface
	# were merged into the same train and pushed it over the file-length ratchet.
	core/ControlRegistryRegistrations.cpp
	core/ControlReversibility.cpp
	# The contract table's three blocks, split by what the inverse IS (a live
	# checkpoint / a bounded recorded state / nothing): see
	# include/ControlReversibility.h.
	core/ControlReversibilityTable.cpp
	# The live-checkpoint rows, split out of the join file when the wave-2
	# join entries took it to the file-length ratchet (a move, not a rewrite).
	core/ControlReversibilityTableLive.cpp
	core/ControlReversibilityTableAction.cpp
	core/ControlReversibilityTableChain.cpp
	# The STRUCTURAL group's four action rows - created / reordered / deleted
	# track, removed device (task #664, feature row 75). Its own TU for the same
	# reason as the chain rows: the action table was at the file ratchet.
	core/ControlReversibilityTableStructure.cpp
	core/ControlReversibilityTablePassive.cpp
	core/ControlReversibilityTableSnapshot.cpp
	core/ControlReversibilityTableTrackFolder.cpp
	core/ControlReversibilityTableVca.cpp
	# The routing surface's rows (feature rows 27-29 and the mixer group's
	# routing verbs): a GROUP's rows whatever their class, joined by
	# reversibilityRowTable() exactly as the folder-tracks group's are.
	core/ControlReversibilityTableRouting.cpp
	# The 0.3.0 verb wave's three LIVE-checkpoint rows (clip.trim, clip.slip,
	# note.probability_set). Its own TU because ControlReversibilityTable.cpp is at
	# the file ratchet; reversibilityRowTable() joins it, so the block keeps ONE
	# row count and each row's class still comes from the row.
	core/ControlReversibilityTableVerbs.cpp
	# The plugin scan-cache and crash-reporter groups' rows (feature rows 46
	# and 54), in their own file for the reason the routing surface's rows are:
	# the passive block and the live block are both at the file-length cap.
	core/ControlReversibilityTableScanAndCrash.cpp
	# The auto-mastering group's three rows (feature rows 25 and 72): one
	# recorded-action true_inverse row and the group's two not_mutating
	# inspectors. A GROUP file on the same seam as the routing rows above.
	core/ControlReversibilityTableMastering.cpp
	# The 0.3.0 note/scale/device wave's A16 rows (board task #648): a GROUP file on the
	# same seam as the folder, vca, routing, scan-and-crash and mastering files - the
	# class comes from each row, not from the file.
	core/ControlReversibilityTableNoteScale.cpp
	# The `stem.*` group's seven not_mutating rows (feature row 26, task #653):
	# the offline stem engine's read verb, its four job verbs and its two
	# model-store verbs. ALWAYS compiled, like every other table file, and EMPTY
	# (a count of 0) when WANT_STEM_SPLIT is off and the ids do not exist - so
	# the join in ControlReversibility.cpp needs no guard and the release
	# configuration's row count is untouched.
	core/ControlReversibilityTableStems.cpp
	# The loudness feature's four rows (feature row 24): meter.get_state,
	# meter.measure_file, meter.arm and the same feature's render-path half,
	# export.set_loudness_report. A GROUP file on the same seam as the rows
	# above.
	core/ControlReversibilityTableMeter.cpp
	# The render/export preset store's rows (feature row 70): three
	# recorded-action true_inverse rows and the group's one not_mutating read. A
	# GROUP file on the same seam as the mastering and routing rows above.
	core/ControlReversibilityTableExportPresets.cpp
	core/ControlReversibilityTableRecording.cpp
	# The interchange group's four rows (feature row 33: the Standard MIDI File
	# conductor track). A GROUP file on the same seam as the mastering rows
	# above, and it exists because ControlReversibilityTable.cpp is at 499 of the
	# 500 lines the file ratchet allows.
	core/ControlReversibilityTableInterchange.cpp
	# The dawproject.* group's four rows (feature row 37: the published DAWproject
	# format read and written; three not_mutating, one recorded-action
	# true_inverse for the whole-session import). A GROUP file on the same seam as
	# the interchange and chord rows above - the class comes from each row, not
	# from its file.
	core/ControlReversibilityTableDawProject.cpp
	# The chord group's nine rows (feature row 35): three not_mutating reads, four
	# recorded-action true_inverse rows for the track edits, two live-checkpoint
	# rows for the generators. A GROUP file on the same seam as the routing and
	# mastering rows above - the class comes from each row, not from its file.
	core/ControlReversibilityTableChord.cpp
	# The project-asset reference group's three rows (feature row 38): the two
	# not_mutating inspectors of a project FILE and the one recorded-action
	# true_inverse row the relink's file write needs. A GROUP file on the same
	# seam as the routing and mastering rows above.
	core/ControlReversibilityTableArchive.cpp
	# project.missing_assets / project.hash_assets / project.relink, the engine
	# half of feature row 38 split into its READ and WRITE translation units
	# (the automation/warp/vca/chain read-edit split, for the file-length
	# ratchet): the scan+hash half and the relink rewrite, sharing
	# core/ControlProjectAssetsShared.h.
	core/ControlCommandsProjectArchive.cpp
	core/ControlProjectAssets.cpp
	core/ControlProjectAssetsRelink.cpp
	# The mmpz-git depth group (feature row 42, task #612): the control-surface
	# wrappers around tools/mmpz-git/mmpz_git.py.
	core/ControlCommandsProjectMmpzGit.cpp
	core/ControlReversibilityTableMmpzGit.cpp
	# The plugin.host_chunking row (feature row 82, CODE-4). One row in its own
	# file for the same reason: a file-length cap, not a group boundary.
	core/ControlReversibilityTableHostChunking.cpp
	# The wasm.pool / wasm.render_offline rows (feature row 73, CODE-5), for the
	# same reason: the passive block that holds the rest of the wasm.* rows is at
	# the file-length cap.
	core/ControlReversibilityTableWasmRender.cpp
	# The automation-mode group's two rows (feature-list rows 10 and 63,
	# board task #647). A GROUP file on the same seam as the mastering and
	# routing rows above.
	core/ControlReversibilityTableAutomationModes.cpp

	# the controller.* group (feature row 19, board task #651): MIDI controller
	# soft-takeover, LED/feedback output and the mapping templates.
	core/ControlCommandsController.cpp
	# ... and its four FILE verbs (template save/list/apply/delete), split off at
	# the 500-line ratchet so both files stay under it.
	core/ControlCommandsControllerTemplates.cpp
	# The controller.* rows (feature row 19, board task #651): the MIDI controller
	# surface's soft-takeover, LED feedback and mapping templates. Two
	# not_mutating read verbs and five snapshot rows, in their own file so the
	# capped join file and the passive block both stay untouched.
	core/ControlReversibilityTableController.cpp
	# ControllerSurface.cpp - the mapping-template store of the controller
	# surface (feature row 19, board task #651). Missed by the lane's WIP commit
	# cbb717660: the file was written but never added to this list, so nothing
	# built it.
	core/ControllerSurface.cpp
	# The transaction record's accounting, and the rule that decides when two
	# commands are one undo step.
	core/ControlTransactions.cpp
	core/ControlUndoCoalescing.cpp
	core/ControlSchema.cpp
	core/ControlVocabulary.cpp
	core/ControlServer.cpp
	core/ControlServerSocket.cpp
	# The Windows half of the control transport: the same JSON-RPC contract, the
	# same command ids, served over a named pipe (CODE-9, feature row 83). Named
	# here because LMMS_SRCS is an explicit list rather than a glob; on POSIX the
	# file preprocesses to nothing and the listener stays the AF_UNIX socket
	# ControlServerSocket.cpp binds.
	core/ControlServerWin32.cpp
	core/ControlSession.cpp
	# The inverse of a STRUCTURAL operation - a deleted track (with its clips), a
	# reordered track, a removed device (task #664, feature row 75). Its own TU:
	# the three paths that build such an inverse must be ONE implementation, and
	# ControlEditSupport.cpp is the structural-helpers file's neighbour, not its
	# home.
	core/ControlStructuralSupport.cpp
	# Listed so that AUTOMOC sees the Q_OBJECT headers (they live in include/).
	${CMAKE_SOURCE_DIR}/include/ControlEdit.h
	${CMAKE_SOURCE_DIR}/include/ControlRegistry.h
	${CMAKE_SOURCE_DIR}/include/ControlServer.h
	core/Controller.cpp
	core/ControllerConnection.cpp
	core/DataFile.cpp
	core/DrumSynth.cpp
	core/Effect.cpp
	core/EffectChain.cpp
	core/Engine.cpp
	core/EnvelopeAndLfoParameters.cpp
	core/ExportDither.cpp
	core/ExportRenderSettings.cpp
	core/fft_helpers.cpp
	core/Mixer.cpp
	core/VcaGroup.cpp
	core/ImportFilter.cpp
	core/InlineAutomation.cpp
	core/Instrument.cpp
	core/InstrumentFunctions.cpp
	core/InstrumentPlayHandle.cpp
	core/InstrumentSoundShaping.cpp
	core/JournallingObject.cpp
	core/Keymap.cpp
	core/Ladspa2LMMS.cpp
	core/LadspaControl.cpp
	core/LatencyCompensation.cpp
	core/LoudnessReport.cpp
	core/LufsMeter.cpp
	core/MasteringChain.cpp
	core/MasteringJob.cpp
	# The auto-mastering run's report: the JSON document the CLI writes with
	# --report and the control surface reads back, plus the human table the CLI
	# prints (moved verbatim out of main.cpp, which an inherited file's line
	# ratchet may not grow).
	core/MasteringReport.cpp
	core/LadspaManager.cpp
	core/LfoController.cpp
	core/LinkedModelGroups.cpp
	core/LocklessAllocator.cpp
	core/MeterModel.cpp
	core/Metronome.cpp
	core/MicroTimer.cpp
	core/Microtuner.cpp
	core/MidiLearn.cpp
	core/MidiClock.cpp
	core/MidiClockState.cpp
	core/MidiClockTracker.cpp
	core/MixHelpers.cpp
	core/Model.cpp
	core/ModelVisitor.cpp
	core/Note.cpp
	core/NotePlayHandle.cpp
	core/NoteRandom.cpp
	core/NoteTransform.cpp
	core/Oscillator.cpp
	core/PathUtil.cpp
	core/PatternClip.cpp
	core/PatternStore.cpp
	core/PeakController.cpp
	core/PerfLog.cpp
	core/Piano.cpp
	core/PlayHandle.cpp
	core/Plugin.cpp
	core/PluginIssue.cpp
	core/PluginFactory.cpp
	core/PluginScanCache.cpp
	core/PresetPreviewPlayHandle.cpp
	core/ProjectJournal.cpp
	# The undo stack's two-sided bound and its coalescing primitive, in their own
	# translation unit (include/ProjectJournal.h documents both).
	core/ProjectJournalBounds.cpp
	# The STRUCTURAL undo step: an inverse carried by a measured document - a
	# deleted track's own XML, a removed device's state (task #664, row 75). Its
	# own translation unit for the same reason as the bound above.
	core/ProjectJournalStructural.cpp
	core/ProjectRecovery.cpp
	# The take journal: what makes a recording in progress recoverable after an
	# abnormal exit (0.3.0, docs/KNOWN-LIMITATIONS.md for the stated bound).
	core/RecordingJournal.cpp
	core/ProjectIds.cpp
	core/ProjectRenderer.cpp
	core/ProjectRevisions.cpp
	core/ProjectVersion.cpp
	core/RemotePlugin.cpp
	core/Rack.cpp
	core/RackMacros.cpp
	core/RackNodes.cpp
	core/RackZones.cpp
	core/RenderManager.cpp
	core/RetroMidiCapture.cpp
	core/RetroMidiCaptureSettings.cpp
	core/RetroMidiClipWriter.cpp
	core/RingBuffer.cpp
	core/RoutingChainNodes.cpp
	core/RoutingGraph.cpp
	core/RoutingNode.cpp
	core/RoutingNodes.cpp
	core/Sample.cpp
	core/SampleBuffer.cpp
	core/SampleClip.cpp
	core/SampleDecoder.cpp
	core/SamplePlayHandle.cpp
	core/SampleRecordHandle.cpp
	core/Scale.cpp
	core/ScriptApiVersion.cpp
	core/ScriptBindings.cpp
	core/ScriptCommandQueue.cpp
	core/ScriptConsole.cpp
	core/ScriptEngine.cpp
	core/ScriptPackage.cpp
	core/LmmsSemaphore.cpp
	core/SerializingObject.cpp
	core/Song.cpp
	core/TempoMap.cpp
	# The Standard MIDI File conductor track (feature row 33): the encoder and
	# the bounds-checked reader behind the interchange.* group. Hand-written
	# rather than reusing plugins/MidiExport's note-oriented MidiFile.hpp or the
	# portsmf vendored behind plugins/MidiImport, neither of which is reachable
	# from src/core; the convention it writes and reads is recorded in
	# include/SmfInterchange.h and docs/SMF-INTERCHANGE.md. The reader is its own
	# translation unit because the file ratchet allows 500 lines and a reader has
	# to survive whatever a foreign program meant, which is not the writer's job.
	core/SmfInterchange.cpp
	core/SmfInterchangeReader.cpp
	# DAWproject import / export (feature row 37 of docs/FEATURE-LIST-0.3.0.md,
	# docs/DAWPROJECT-INTERCHANGE.md): the published format's container, the model,
	# and the four conversions a round trip is made of. The ZIP container is
	# hand-written (STORE entries, no compression dependency) because DataFile's
	# qUncompress is zlib's framed format and cannot make a raw DEFLATE stream;
	# the XML writer and reader are one translation unit each, the session
	# coupling is its own (a reader has to survive whatever a foreign program
	# meant, which is not the writer's job - the same split SmfInterchange.cpp /
	# SmfInterchangeReader.cpp uses), and every file stays inside gate 7's cap.
	core/DawProjectZip.cpp
	core/DawProjectModel.cpp
	core/DawProjectWrite.cpp
	core/DawProjectRead.cpp
	core/DawProjectReadTracks.cpp
	core/DawProjectSession.cpp
	core/ModulationLayer.cpp
	core/TempoSyncKnobModel.cpp
	core/ThreadPool.cpp
	core/Timeline.cpp
	core/TimePos.cpp
	core/TakeLane.cpp
	core/ToolPlugin.cpp
	core/Track.cpp
	core/TrackContainer.cpp
	core/UnattendedRun.cpp
	core/UpgradeExtendedNoteRange.h
	core/UpgradeExtendedNoteRange.cpp
	core/Clip.cpp
	core/ValueBuffer.cpp
	core/VstSyncController.cpp
	core/StepRecorder.cpp

	core/audio/AudioAlsa.cpp
	core/audio/AudioDevice.cpp
	core/audio/AudioFileDevice.cpp
	core/audio/AudioFileMP3.cpp
	core/audio/AudioFileOgg.cpp
	core/audio/AudioFileFlac.cpp
	core/audio/AudioFileWave.cpp
	core/audio/AudioJack.cpp
	core/audio/AudioOss.cpp
	core/audio/AudioSndio.cpp
	core/audio/AudioPortAudio.cpp
	core/audio/AudioSoundIo.cpp
	core/audio/AudioPulseAudio.cpp
	core/audio/AudioSampleRecorder.cpp
	core/audio/AudioSdl.cpp
	core/audio/MultiTrackRecorder.cpp
	core/audio/TrackRecorder.cpp
	# (the capture path itself lives in core/audio/AudioAlsa.cpp, an inherited
	# file this fork already lists above)

	core/lv2/Lv2Basics.cpp
	core/lv2/Lv2ControlBase.cpp
	core/lv2/Lv2Evbuf.cpp
	core/lv2/Lv2Features.cpp
	core/lv2/Lv2Ports.cpp
	core/lv2/Lv2Proc.cpp
	core/lv2/Lv2Manager.cpp
	core/lv2/Lv2Options.cpp
	core/lv2/Lv2SubPluginFeatures.cpp
	core/lv2/Lv2UridCache.cpp
	core/lv2/Lv2UridMap.cpp
	core/lv2/Lv2Worker.cpp

	core/midi/MidiAlsaRaw.cpp
	core/midi/MidiAlsaSeq.cpp
	core/midi/MidiClient.cpp
	core/midi/MidiController.cpp
	core/midi/MidiEventToByteSeq.cpp
	core/midi/MidiJack.cpp
	core/midi/MidiOss.cpp
	core/midi/MidiSndio.cpp
	core/midi/MidiApple.cpp
	core/midi/MidiPort.cpp
	core/midi/MpeExpression.cpp
	core/midi/MidiWinMM.cpp

	${LMMS_STEM_SRCS}
	${LMMS_SESSION_SRCS}

	${LMMS_TELEMETRY_SRCS}

	PARENT_SCOPE
)
