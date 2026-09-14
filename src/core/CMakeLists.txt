# Offline stem separation (opt-in, see doc/STEM-SPLIT.md). The job manager and
# track builder are pure Qt and always available when the feature is enabled;
# the C++ ONNX Runtime backend is compiled only when the runtime was found.
IF(LMMS_HAVE_STEM_SPLIT)
	SET(LMMS_STEM_SRCS
		core/ExternalProcessStemSeparator.cpp
		core/StemJobManager.cpp
		core/StemModelStore.cpp
		core/StemTrackBuilder.cpp
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
	core/ControlCommandsArrangement.cpp
	core/ControlCommandsClip.cpp
	core/ControlCommandsClipEdits.cpp
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
	core/ControlCommandsMixer.cpp
	# The modulation layer (#602): the layer + LFO half, the route half, and the
	# per-note expression group (docs/MODULATION.md).
	core/ControlCommandsModulator.cpp
	core/ControlCommandsModulatorRoutes.cpp
	core/ControlCommandsNoteExpression.cpp
	core/ControlCommandsMidi.cpp
	core/ControlCommandsNotes.cpp
	core/ControlCommandsPlugin.cpp
	core/ControlCommandsPluginParams.cpp
	core/ControlCommandsPluginPreset.cpp
	core/ControlCommandsPluginState.cpp
	core/ControlCommandsArrangementState.cpp
	core/ControlCommandsProject.cpp
	core/ControlCommandsProjectFiles.cpp
	core/ControlCommandsRack.cpp
	core/ControlCommandsRackMacros.cpp
	core/ControlCommandsRackZones.cpp
	core/ControlCommandsScript.cpp
	core/ControlCommandsExport.cpp
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
	${LMMS_TELEMETRY_COMMAND_SRCS}
	core/ControlCommandsTransport.cpp
	core/ControlCommandsTrackFolder.cpp
	core/ControlCommandsTrackFolderSets.cpp
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
	core/ControlEditSupport.cpp
	core/ControlModulationSupport.cpp
	core/ControlRackSupport.cpp
	core/ControlRegistry.cpp
	core/ControlReversibility.cpp
	# The contract table's three blocks, split by what the inverse IS (a live
	# checkpoint / a bounded recorded state / nothing): see
	# include/ControlReversibility.h.
	core/ControlReversibilityTable.cpp
	core/ControlReversibilityTableAction.cpp
	core/ControlReversibilityTablePassive.cpp
	core/ControlReversibilityTableSnapshot.cpp
	core/ControlReversibilityTableTrackFolder.cpp
	# The transaction record's accounting, and the rule that decides when two
	# commands are one undo step.
	core/ControlTransactions.cpp
	core/ControlUndoCoalescing.cpp
	core/ControlSchema.cpp
	core/ControlVocabulary.cpp
	core/ControlServer.cpp
	core/ControlServerSocket.cpp
	core/ControlSession.cpp
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
