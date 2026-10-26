# Offline stem separation GUI (on by default, owner decision 14; see doc/STEM-SPLIT.md).
IF(LMMS_HAVE_STEM_SPLIT)
	SET(LMMS_STEM_GUI_SRCS
		gui/StemSplitController.cpp
		# Listed so that AUTOMOC finds the Q_OBJECT header (it lives in
		# include/, not next to the .cpp). Without this the generated
		# AutogenInfo.json can reference a path that does not exist and the
		# build fails in AUTOGEN, not in compilation.
		${CMAKE_SOURCE_DIR}/include/StemSplitController.h
	)
ENDIF()

# Telemetry consent / "what we send" screen (opt-in, see docs/TELEMETRY-V1.md).
# Compiled only when the packager left the kill switch on. The header carries
# no Q_OBJECT on purpose, so AUTOMOC never generates a moc for it and the OFF
# build cannot fail on a moc'd slot whose .cpp the switch removed.
IF(ZENE_TELEMETRY_ENABLED)
	SET(LMMS_TELEMETRY_GUI_SRCS
		gui/TelemetryConsentDialog.cpp
	)
ENDIF()
# The collaboration panel (0.5.0 F6) needs the session, which needs Qt Network.
SET(LMMS_COLLAB_GUI_SRCS)
IF(ZENE_HAVE_COLLAB)
	SET(LMMS_COLLAB_GUI_SRCS gui/CollabPanel.cpp)
ENDIF()

SET(LMMS_SRCS
	${LMMS_SRCS}
	gui/ActionGroup.cpp
	gui/AudioAlsaSetupWidget.cpp
	gui/AudioDeviceSetupWidget.cpp
	gui/AutomatableModelView.cpp
	gui/AutomationModeMenu.cpp
	gui/BrowserTagMenu.cpp
	gui/ControlLayout.cpp
	gui/ControllerDialog.cpp
	gui/ControllerRackView.cpp
	gui/ControllerTemplateMenu.cpp
	gui/ControllerView.cpp
	gui/Controls.cpp
	gui/EffectControlDialog.cpp
	gui/EffectRackView.cpp
	gui/EffectView.cpp
	gui/ExportQualityControls.cpp
	gui/embed.cpp
	gui/FileBrowser.cpp
	gui/GrooveMenu.cpp
	gui/FileRevealer.cpp
	gui/FileSearchJob.cpp
	# The window.* command group: the editors' windows as registered commands
	# (include/ControlWindowCommands.h).
	gui/ControlCommandsWindow.cpp
	# window.note_editor_set: the note editor's view (docs/NOTE-EDITOR.md).
	gui/ControlCommandsNoteEditor.cpp
	gui/ControlCommandsSelection.cpp
	# M3.2: the main window's own verbs (fullscreen, dialogs, save as new version).
	gui/ControlCommandsShell.cpp
	gui/ControlCommandsShortcuts.cpp
	gui/ControlCommandsArrangerView.cpp
	gui/ControlCommandsLayouts.cpp
	gui/ControlCommandsMixerView.cpp
	gui/ControlCommandsHelp.cpp
	gui/ControlCommandsMacros.cpp
	# 0.5.0 W3 L13 / L14: customisation and access.
	gui/ControlCommandsTheme.cpp
	gui/CommandListsCustom.cpp
	gui/WorkspaceMenus.cpp
	gui/TrackIcons.cpp
	gui/ControlCommandsWorkspace.cpp
	gui/WorkspaceWindows.cpp
	gui/ControlCommandsViewMemory.cpp
	gui/ArrangerAids.cpp
	# 0.5.0 Q050-141: a finger's tap and drag as the mouse gesture the widgets already handle.
	gui/TouchInput.cpp
	gui/AccessibilityAudit.cpp
	gui/AccessibleMode.cpp
	gui/AccessibleWidgets.cpp
	# 0.5.0 wave 3 themes 10/17: canvases by keyboard, the shared editable item list, announcements, the staged audit.
	gui/AccessibleAnnouncements.cpp
	gui/AccessibleCanvas.cpp
	gui/ArrangerClipKeys.cpp
	gui/ControlCommandsAccessibleEditing.cpp
	gui/ItemListPanel.cpp
	gui/ItemListSurfaceSources.cpp
	gui/editors/ItemListAutomationSource.cpp
	gui/ControlGuiSupport.cpp
	# M3.4: the command palette (menu actions + argument-free commands, searchable).
	gui/ArrangerOverlays.cpp
	gui/ArrangerFileDrop.cpp
	gui/ArrangerEditPreview.cpp
	gui/ArrangerRulerLanes.cpp
	gui/VideoLane.cpp
	gui/VideoWindow.cpp
	gui/ArrangerZoom.cpp
	gui/ArrangerRulerTempo.cpp
	gui/ArrangerLandmarks.cpp
	gui/CommandForm.cpp
	gui/CommandFormFields.cpp
	gui/CommandMetadata.cpp
	gui/CommandRun.cpp
	gui/LayoutMenu.cpp
	gui/CommandListDialog.cpp
	gui/ListTable.cpp
	gui/HelpTasks.cpp
	gui/WorkflowRun.cpp
	gui/WorkflowRunReport.cpp
	gui/WorkflowRecordingIndicator.cpp
	gui/CommandLists.cpp
	gui/InstrumentCommandMenus.cpp
	gui/LayerRackPanel.cpp
	gui/LibraryCommandMenus.cpp
	gui/ReferenceProjectPanel.cpp
	gui/CompSwipeStrip.cpp
	gui/ContextHelp.cpp
	gui/CommandPalette.cpp
	gui/MacroMenu.cpp
	gui/MacroKnobs.cpp
	gui/MenuBarExtras.cpp
	gui/RecordLaunchMenus.cpp
	# Q050-156: the sampler views' "Record take" button, over the same registered record.sampler_capture* commands.
	gui/SamplerTakeButton.cpp
	gui/ShortcutMap.cpp
	gui/ShortcutPresets.cpp
	gui/UiConfigStore.cpp
	gui/PluginNativeEditor.cpp
	gui/SessionGridMenus.cpp
	gui/SessionGridView.cpp
	gui/SessionAnnouncements.cpp
	gui/SessionGridCells.cpp
	gui/SessionGridDrop.cpp
	gui/SessionGridSelect.cpp
	gui/SessionSyncAction.cpp
	gui/SessionTransportBar.cpp
	gui/PerformView.cpp
	gui/ShortcutsPage.cpp
	gui/StartHub.cpp
	gui/TakeLanesPanel.cpp
	gui/TakeWaveform.cpp
	gui/ModulatorPanel.cpp
	gui/TempoMapPanel.cpp
	# Q050-154: Edit > Live Code - the livecode.* schedule group from the interface.
	gui/LivecodePanel.cpp
	gui/ChordProgressionPanel.cpp
	gui/TrackFolderMenu.cpp
	gui/TrackPinStrip.cpp
	gui/TrackRenderMenu.cpp
	gui/AafImportMenu.cpp
	gui/UndoHistoryPanel.cpp
	gui/VisibilitySetMenu.cpp
	# M3.7: VCA group strips in the mixer + the channel's VCA menu.
	gui/VcaStripView.cpp
	# The 0.5.0 single-window shell (docs/UI-SHELL.md, docs/adr/ADR-0001-UI-TOOLKIT.md):
	# DesignTokens is the one source of colour/spacing/type, ShellLayout the widget-free
	# panel/region/view rules (testable without a display), Shell renders them, ShellChrome
	# is the header/footer/button, ShellPanel a panel's frame, BrowserPanel the Browser.
	# It replaced the Focus Desk and the MDI workspace as the main surface.
	# The 0.5.0 note editor (docs/NOTE-EDITOR.md): NoteGrid and NoteLanes are widget-free (the
	# geometry and the lane table, NoteGridTest), NoteCanvas paints and edits the notes, the parts
	# are the key column, ruler and expression lane, NoteEditor the container and its header.
	gui/noteeditor/ControllerLaneView.cpp
	gui/noteeditor/NoteBackgroundAudio.cpp
	gui/XyPad.cpp
	gui/ParameterGridDialog.cpp
	gui/shell/ChannelLevelMeter.cpp
	gui/FollowActionEditor.cpp
	gui/SampleEditorDialog.cpp
	# 0.5.0 ED-176: the crossfade editor (both sides of two overlapping clips' crossfade).
	gui/CrossfadeEditorDialog.cpp
	gui/SampleRangeFields.cpp
	gui/SampleSelectionView.cpp
	gui/ControlCommandsSampleEditor.cpp
	gui/MixerWindows.cpp
	gui/InputLevelMeter.cpp
	gui/PhaseScopeView.cpp
	gui/ArrangerNavigator.cpp
	gui/LevelBar.cpp
	gui/ModifierTools.cpp
	gui/noteeditor/NoteCanvas.cpp
	gui/noteeditor/LaneScale.cpp
	gui/noteeditor/NoteLanePaint.cpp
	gui/noteeditor/NoteCanvasGestures.cpp
	gui/noteeditor/NoteCanvasInput.cpp
	gui/noteeditor/NoteCanvasKeys.cpp
	gui/noteeditor/ItemListNoteSources.cpp
	gui/noteeditor/NoteCanvasAccess.cpp
	gui/noteeditor/NoteKeyNav.cpp
	gui/noteeditor/ArticulationMapEditor.cpp
	gui/noteeditor/NoteLaneKeys.cpp
	gui/noteeditor/NoteEditor.cpp
	gui/noteeditor/NoteEditorParts.cpp
	gui/noteeditor/NoteEditorMenus.cpp
	gui/noteeditor/NoteEditorToolbar.cpp
	gui/noteeditor/NoteGrid.cpp
	gui/noteeditor/NoteProperties.cpp
	gui/noteeditor/NoteLanes.cpp
	gui/shell/BrowserPanel.cpp
	gui/shell/DesignTokens.cpp
	gui/shell/DeviceCard.cpp
	gui/shell/DeviceActionMenu.cpp
	gui/shell/DeviceCardState.cpp
	gui/shell/DevicePicker.cpp
	gui/PatcherCanvas.cpp
	gui/PatcherGraph.cpp
	gui/MissingDevicesNotice.cpp
	gui/shell/DevicePanel.cpp
	gui/shell/InspectorClipFields.cpp
	gui/shell/InspectorChannelRouting.cpp
	gui/shell/InspectorPanel.cpp
	gui/shell/InspectorSections.cpp
	gui/shell/ModulatorCard.cpp
	gui/shell/Shell.cpp
	gui/shell/ShellChrome.cpp
	gui/shell/ShellCommandMenus.cpp
	gui/shell/ShellMenuParts.cpp
	gui/shell/ShellMenuEntries.cpp
	gui/shell/ShellFloating.cpp
	gui/shell/ShellTearOff.cpp
	gui/shell/ShellOverflow.cpp
	gui/shell/ShellRegionTabs.cpp
	gui/shell/ShellToolsMenu.cpp
	gui/shell/ShellLayoutScope.cpp
	gui/shell/ShellIcons.cpp
	gui/shell/ShellLayout.cpp
	gui/shell/ShellPanel.cpp
	gui/shell/ShellSelection.cpp
	gui/shell/TransportReadouts.cpp
	gui/shell/WaveformPreview.cpp
	gui/GuiApplication.cpp
	gui/LadspaControlView.cpp
	gui/LfoControllerDialog.cpp
	gui/LinkedModelGroupViews.cpp
	gui/LmmsPalette.cpp
	gui/LmmsStyle.cpp
	gui/Lv2ViewBase.cpp
	gui/MainApplication.cpp
	gui/MainWindow.cpp
	gui/MicrotunerConfig.cpp
	gui/MidiCCRackView.cpp
	gui/MidiLearnGui.cpp
	gui/MidiControllerMenu.cpp
	gui/ChainPresetMenu.cpp
	gui/ChannelPickDialog.cpp
	gui/BrowserSearchView.cpp
	gui/BrowserSearchViewMedia.cpp
	gui/BrowserResults.cpp
	gui/ClipEditsMenu.cpp
	gui/CrashReportMenu.cpp
	gui/EffectDeviceMenu.cpp
	gui/EffectHostingMenu.cpp
	gui/HelpCenter.cpp
	gui/FeedbackModeAction.cpp
	gui/MidiClockMenu.cpp
	gui/MidiReconnectAction.cpp
	gui/PluginQuarantineMenu.cpp
	gui/RackMenu.cpp
	gui/RecordingRecoveryMenu.cpp
	gui/RenderPresetMenu.cpp
	gui/RenderRangeMenu.cpp
	gui/ScriptBudgetAction.cpp
	gui/SessionTuningMenu.cpp
	gui/MidiSetupWidget.cpp
	gui/MpeInputAction.cpp
	gui/NoteTransformActions.cpp
	gui/MixerChannelView.cpp
	gui/MixerHardwareMenu.cpp
	gui/TaskIndicator.cpp
	gui/widgets/ControlCues.cpp
	gui/SurroundPanner.cpp
	gui/MixerOverlays.cpp
	gui/MixerSendsBanner.cpp
	gui/MixerStripInfo.cpp
	gui/MixerStripTags.cpp
	gui/MeterSpeech.cpp
	gui/MixerModeBar.cpp
	gui/MixerRoutingMatrix.cpp
	gui/MixerViewOptions.cpp
	gui/MixerView.cpp
	gui/ModelView.cpp
	gui/PeakControllerDialog.cpp
	gui/PinConnector.cpp
	gui/PluginBrowser.cpp
	gui/ProjectNotes.cpp
	gui/RowTableView.cpp
	gui/SampleTrackWindow.cpp
	gui/SampleThumbnail.cpp
	gui/SendButtonIndicator.cpp
	gui/SideBar.cpp
	gui/SideBarWidget.cpp
	gui/StringPairDrag.cpp
	gui/SubWindow.cpp
	gui/MainWindowGeometry.cpp
	gui/ToolWindowMemory.cpp
	gui/ToolPluginView.cpp

	gui/clips/AutomationClipView.cpp
	gui/clips/ClipView.cpp
	gui/clips/ClipViewTheme.cpp
	gui/clips/ClipLockFeedback.cpp
	gui/clips/MidiClipView.cpp
	gui/clips/PatternClipView.cpp
	gui/clips/SampleClipOverlays.cpp
	gui/clips/SampleClipView.cpp

	gui/editors/AutomationEditor.cpp
	gui/editors/AutomationEditorFades.cpp
	gui/editors/AutomationEditorKeys.cpp
	gui/editors/AutomationPointEdit.cpp
	gui/editors/Editor.cpp
	gui/editors/PatternEditor.cpp
	gui/editors/PianoRoll.cpp
	gui/editors/PositionLine.cpp
	gui/editors/Rubberband.cpp
	gui/editors/SongEditor.cpp
	gui/editors/StepRecorderWidget.cpp
	gui/editors/TimeLineWidget.cpp
	gui/editors/TrackContainerView.cpp

	gui/instrument/EnvelopeAndLfoView.cpp
	gui/instrument/EnvelopeGraph.cpp
	gui/instrument/InstrumentFunctionViews.cpp
	gui/instrument/InstrumentMidiIOView.cpp
	gui/instrument/InstrumentTuningView.cpp
	gui/instrument/InstrumentSoundShapingView.cpp
	gui/instrument/InstrumentTrackWindow.cpp
	gui/instrument/InstrumentView.cpp
	gui/instrument/LfoGraph.cpp
	gui/instrument/PianoView.cpp

	gui/menus/MidiPortMenu.cpp
	gui/menus/RecentProjectsMenu.cpp
	gui/menus/TemplatesMenu.cpp

	gui/modals/AboutDialog.cpp
	gui/modals/ColorChooser.cpp
	gui/modals/ControllerConnectionDialog.cpp
	gui/modals/EffectPickerOrganiser.cpp
	gui/modals/EffectSelectDialog.cpp
	gui/modals/ExportProjectDialog.cpp
	gui/modals/FileDialog.cpp
	gui/modals/RenameDialog.cpp
	gui/modals/SetupDialog.cpp
	gui/modals/VersionedSaveDialog.cpp

	gui/tracks/AutomationTrackView.cpp
	gui/tracks/FadeButton.cpp
	gui/tracks/FolderSummaryLane.cpp
	gui/tracks/TakeLaneRows.cpp
	gui/tracks/InstrumentTrackView.cpp
	gui/tracks/PatternTrackView.cpp
	gui/tracks/SampleTrackView.cpp
	gui/tracks/TrackContentWidget.cpp
	gui/tracks/TrackLabelButton.cpp
	gui/tracks/TrackOperationsWidget.cpp
	gui/tracks/TrackRenameLineEdit.cpp
	gui/tracks/TrackGrip.cpp
	gui/tracks/TrackView.cpp

	gui/widgets/AutomatableButton.cpp
	gui/widgets/AutomatableSlider.cpp
	gui/widgets/BarModelEditor.cpp
	gui/widgets/CPULoadWidget.cpp
	gui/widgets/CaptionMenu.cpp
	gui/widgets/ComboBox.cpp
	gui/widgets/Draggable.cpp
	gui/widgets/Fader.cpp
	gui/widgets/FloatModelEditorBase.cpp
	gui/widgets/Graph.cpp
	gui/widgets/GroupBox.cpp
	gui/widgets/Knob.cpp
	gui/widgets/LcdFloatSpinBox.cpp
	gui/widgets/LcdSpinBox.cpp
	gui/widgets/LcdWidget.cpp
	gui/widgets/LedCheckBox.cpp
	gui/widgets/LeftRightNav.cpp
	gui/widgets/MeterDialog.cpp
	gui/widgets/MixerChannelLcdSpinBox.cpp
	gui/widgets/NStateButton.cpp
	gui/widgets/Oscilloscope.cpp
	gui/widgets/PeakIndicator.cpp
	gui/widgets/PixmapButton.cpp
	gui/widgets/SimpleTextFloat.cpp
	gui/widgets/TabBar.cpp
	gui/widgets/TabWidget.cpp
	gui/widgets/TempoSyncBarModelEditor.cpp
	gui/widgets/TempoSyncKnob.cpp
	gui/widgets/TextFloat.cpp
	gui/widgets/TimeDisplayWidget.cpp
	gui/widgets/ToolButton.cpp

	${LMMS_STEM_GUI_SRCS}

	${LMMS_TELEMETRY_GUI_SRCS}
	${LMMS_COLLAB_GUI_SRCS}

	PARENT_SCOPE
)

