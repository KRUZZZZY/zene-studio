include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AudioBusHandleTest.cpp
	src/core/AudioBusTest.cpp
	src/core/AudioEngineTeardownTest.cpp
	src/core/AudioPortsModelTest.cpp
	src/core/AudioPortsTest.cpp
	src/core/AudioResamplerRatioTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/AutomationModesTest.cpp
	# The browser's engine half (W8 tag/metadata search + the waveform peak
	# cache): the metadata probe against a byte-written RIFF/WAVE, the tag
	# store's round-trip through its file, and the peak cache's hit/miss
	# behaviour. Headless: no Engine, no window, no audio device.
	src/core/BrowserCatalogTest.cpp
	src/core/ClipEditsTest.cpp
	src/core/ClipFadesRenderTest.cpp
	src/core/ClipSerialisationTest.cpp
	src/core/ClipWarpPersistenceTest.cpp
	src/core/ConfigMigrationTest.cpp
	src/core/ControlAutomationScriptTest.cpp
	# The browser.* command surface (W8): the ids, the schemas, the typed
	# refusals the agent-surface sweep needs, and the A16 undo proof - a tag's
	# inverse is the paired COMMAND, so control.undo dispatches it.
	src/core/ControlBrowserCommandsTest.cpp
	src/core/ControlDeviceCatalogueTest.cpp
	src/core/ControlEditCommandsTest.cpp
	src/core/ControlRegistryTest.cpp
	src/core/ControlTempoMapCommandsTest.cpp
	src/core/ControlWarpCommandsTest.cpp
	src/core/CrashReporterTest.cpp
	src/core/DataFileFormatTest.cpp
	src/core/DataFileSaveIntegrityTest.cpp
	# The export dither (TPDF) and the explicit SRC quality: the statistical
	# proof of the dither, the byte-level off-by-default proof, the converter
	# ratio convention, and the quality reaching the resampler.
	src/core/ExportDitherTest.cpp
	src/core/ExportWavDitherTest.cpp
	src/core/LoudnessReportTest.cpp
	src/core/LufsMeterTest.cpp
	src/core/MasteringTest.cpp
	src/core/MathTest.cpp
	src/core/MidiLearnTest.cpp
	src/core/MidiLearnThreadTest.cpp
	src/core/MidiProbabilityPersistenceTest.cpp
	src/core/MixerAbRegressionTest.cpp
	src/core/MixerConcurrencyTest.cpp
	src/core/MixerRoutingBackwardCompatTest.cpp
	src/core/MpeExpressionTest.cpp
	src/core/MpeInputPathTest.cpp
	src/core/MpeNoteStorageTest.cpp
	# The #602 modulation layer: the ENGINE half (the LFO arithmetic, the target
	# resolver, the per-block relative write and the persistence round trip) and
	# the SURFACE half (the ten modulator.*/note.expression.* commands, their
	# A16 classes and their inverses). docs/MODULATION.md is the design record.
	src/core/ModulationLayerValueTest.cpp
	src/core/ModulationLayerTest.cpp
	src/core/ControlModulatorCommandsTest.cpp
	src/core/ControlNoteExpressionCommandsTest.cpp
	src/core/MultiTrackRecorderTest.cpp
	src/core/NoteRandomTest.cpp
	src/core/NoteTransformTest.cpp
	src/core/PhaseDSidechainTest.cpp
	src/core/PhaseFChannelScaleTest.cpp
	src/core/PluginLogoResourceTest.cpp
	src/core/PluginAudioPortsTest.cpp
	src/core/PluginScanCacheTest.cpp
	src/core/ProjectOpenIntegrityTest.cpp
	src/core/ProjectRecoveryTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RackMacrosTest.cpp
	src/core/RackTest.cpp
	src/core/RackZonesTest.cpp
	src/core/RecordClipTest.cpp
	src/core/RecordRingBufferTest.cpp
	src/core/RecordingRealtimeTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/ReversibilityContractTest.cpp
	src/core/ReversibilityUndoTest.cpp
	# The bounded, coalescing undo (task #623): the declared depth caps, the
	# drag rule, the byte budget and the refusal of an evicted step.
	src/core/UndoBoundsTest.cpp
	src/core/TakeLaneCompTest.cpp
	# The take lanes' own half of the comping tests (the composite half stays
	# in TakeLaneCompTest.cpp); the fixtures both halves use are in
	# tests/src/core/TakeLaneTestSupport.h.
	src/core/TakeLaneTest.cpp
	src/core/RemotePluginAudioPortsTest.cpp
	src/core/RemotePluginClientE2ETest.cpp
	src/core/RenderJobQueueTest.cpp
	src/core/RoutingGraphLiveTest.cpp
	src/core/RoutingGraphTest.cpp
	src/core/PdcMixerTest.cpp
	src/core/VcaGroupTest.cpp
	src/core/ScriptBindingsTest.cpp
	src/core/ScriptEngineTest.cpp
	src/core/ScriptStabilisationTest.cpp
	src/core/SlideNotesTest.cpp
	src/core/StableTrackIdsTest.cpp
	src/core/StemExportTest.cpp
	src/core/TelemetryTest.cpp
	src/core/TempoMapPersistenceTest.cpp
	src/core/TempoMapTest.cpp
	src/core/TimelineTest.cpp
	src/core/TwoTrackRecordingHarness.cpp
	src/core/WarpMarkersTest.cpp
	src/gui/MidiLearnGuiTest.cpp
	src/tracks/AutomationTrackTest.cpp
	src/tracks/SampleClipWarpTest.cpp
	src/tracks/SampleClipWindowTest.cpp
)

# Offline stem separation tests (opt-in, see doc/STEM-SPLIT.md).
IF(LMMS_HAVE_STEM_SPLIT)
	LIST(APPEND LMMS_TESTS
		src/core/StemJobManagerTest.cpp
		src/core/StemModelStoreTest.cpp
		src/core/StemSplitPipelineTest.cpp
	)
	IF(LMMS_HAVE_ONNXRUNTIME)
		LIST(APPEND LMMS_TESTS src/core/OnnxRuntimeStemSeparatorTest.cpp)
	ENDIF()
ENDIF()

# Session View data layer tests (opt-in, see include/SessionModel.h).
IF(LMMS_HAVE_SESSION_VIEW)
	LIST(APPEND LMMS_TESTS
		src/core/SessionModelTest.cpp
		# Launch scheduler + quantisation engine (task #595): the pure launch
		# decision, the LaunchMode state machine, the lock-free hand-off.
		src/core/SessionSchedulerTest.cpp
		# Behaviour-preservation proof: a real headless render is byte-identical
		# with nothing launched, and differs with a launched clip.
		src/core/SessionSchedulerRenderTest.cpp
	)
ENDIF()

# gcov coverage instrumentation (see WANT_COVERAGE in the top-level
# CMakeLists.txt). Applied to the unit-test executables only; the Part C
# reference/migration targets below keep the plain debug build so behaviour
# comparisons stay instrumentation-independent.
set(LMMS_COVERAGE_FLAGS "")
if(WANT_COVERAGE)
	set(LMMS_COVERAGE_FLAGS "--coverage")
endif()
function(lmms_add_coverage TARGET)
	if(LMMS_COVERAGE_FLAGS)
		target_compile_options(${TARGET} PRIVATE ${LMMS_COVERAGE_FLAGS})
		target_link_options(${TARGET} PRIVATE ${LMMS_COVERAGE_FLAGS})
	endif()
endfunction()

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME})

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
	lmms_add_coverage(${LMMS_TEST_NAME})
	# Lua scripting examples live in the source tree; tests run from build/tests.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LUA_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/data/scripts")
	# Lua scripting API version, from the same top-level CMakeLists.txt variables
	# src/CMakeLists.txt passes to lmmsobjs. Deliberate duplication of the
	# definition, not of the value: ScriptStabilisationTest compares what the
	# engine reports (compiled in lmmsobjs) against what this test binary was
	# compiled with, so a version hardcoded in either translation unit fails.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		ZENE_LUA_API_VERSION_MAJOR=${ZENE_LUA_API_VERSION_MAJOR}
		ZENE_LUA_API_VERSION_MINOR=${ZENE_LUA_API_VERSION_MINOR}
		ZENE_LUA_API_VERSION_PATCH=${ZENE_LUA_API_VERSION_PATCH})
	# Test fixtures (stub ONNX model) and the reference CLI path.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LMMS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
	IF(LMMS_HAVE_STEM_SPLIT)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_STEM_CLI_DEFAULT="${CMAKE_SOURCE_DIR}/tools/stem_split_cli.py")
	ENDIF()

	# ScriptEngineTest loads a real instrument plugin (tripleoscillator) from the
	# build tree to prove the Lua Instrument binding (spec G4). Plugin modules
	# resolve LMMS core symbols from the host process at load time, so the test
	# binary must export its symbols exactly like the zene executable does.
	# Tests that need the build tree's plugin modules (they resolve LMMS core
	# symbols from the host process at load time, exactly like the zene
	# executable) get ENABLE_EXPORTS and the plugin directory. ScriptEngineTest
	# does it for the Lua Instrument binding; ControlDeviceCatalogueTest does it
	# so PluginFactory discovers lv2effect/lv2instrument and the LV2 half of the
	# device catalogue is real rather than empty.  PluginScanCacheTest drives the
	# scan cache against a copy of the built tripleoscillator module;
	# ReversibilityContractTest/ReversibilityUndoTest load the real binary's
	# plugins to prove the A16 inverses (#623).
	if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest" OR LMMS_TEST_NAME STREQUAL "PluginScanCacheTest"
		OR LMMS_TEST_NAME STREQUAL "ControlDeviceCatalogueTest"
		OR LMMS_TEST_NAME STREQUAL "ReversibilityContractTest"
		OR LMMS_TEST_NAME STREQUAL "ReversibilityUndoTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
	endif()

	# PluginScanCacheTest drives the scan cache and the quarantine list through
	# the real PluginFactory, against a copy of the built tripleoscillator
	# module (the enumeration path, not a stub): the module has to exist before
	# ctest runs. It also constructs the plugin browser, so it needs a Qt
	# platform plugin even on a headless machine.
	if(LMMS_TEST_NAME STREQUAL "PluginScanCacheTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_SCAN_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
		add_dependencies(${LMMS_TEST_NAME} tripleoscillator)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
endforeach()

# RecordingRealtimeTest constructs a real Engine to measure the capture path
# (D9a/D9b), which needs a Qt platform plugin even though the test is guiless.
set_tests_properties(RecordingRealtimeTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The rack command-group tests (rack.* macros and key/velocity zones) construct a
# full Engine and drive the mixer's own save/load, so they need the same
# headless Qt platform as the test above - and the developer box has no display.
set_tests_properties(RackMacrosTest RackZonesTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The #602 modulation layer's two tests build a full Engine, a real Mixer and a
# rack chain, and drive the layer's own save/load, so they need the headless Qt
# platform for the same reason.
set_tests_properties(ModulationLayerValueTest ModulationLayerTest
	ControlModulatorCommandsTest ControlNoteExpressionCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# UndoBoundsTest constructs a full Engine, drives the control surface and starts
# a QThread-paced window test, so it needs the same headless Qt platform.
set_tests_properties(UndoBoundsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
# TakeLaneCompTest (comping: the composite and the comp.* command surface, task
# #600) and TakeLaneTest (the lane list, the clip's lane tag and the
# non-destructive property of the take audio) construct real SampleTracks with
# real SampleClips over real WAVs, and TakeLaneCompTest drives the comp.* group
# through the registry, so both need the same headless Qt platform as the tests
# above - the developer box has no display.
set_tests_properties(TakeLaneCompTest TakeLaneTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# PhaseDSidechainTest loads the native Compressor module.
#
# It points LMMS_PLUGIN_DIR at a private directory holding a link to the built
# Compressor, to prove sidechain ducking through the real plugin. The path comes
# in as PART_D_COMPRESSOR_LIBRARY; until now nothing defined that macro, so the
# file could not compile and was never registered - a test that looked like
# Phase D coverage and could not run in any way. $<TARGET_FILE:> resolves to the
# built module and makes the plugin a build dependency.
# ---------------------------------------------------------------------------
add_dependencies(PhaseDSidechainTest compressor)
target_compile_definitions(PhaseDSidechainTest PRIVATE
	PART_D_COMPRESSOR_LIBRARY="$<TARGET_FILE:compressor>")
# The Compressor module is dlopen()ed, so the test binary must export the
# lmms:: core symbols the module resolves from its host (same reason
# ScriptEngineTest and PluginScanCacheTest set this). Without it the load fails
# with "undefined symbol: _ZTIN4lmms16AutomatableModelE".
set_target_properties(PhaseDSidechainTest PROPERTIES ENABLE_EXPORTS ON)

# ---------------------------------------------------------------------------
# RoutingGraph on the mixer channel's effect chain (docs/ROUTING-GRAPH-LIVE.md).
#
# RoutingGraphLiveTest renders a channel whose chain is processed through a
# RoutingGraph and compares it byte-for-byte against a reference render captured
# before the graph was put on the path, then re-directs a connection and shows
# the render changes. The reference lives in tests/reference/.
# ---------------------------------------------------------------------------
target_compile_definitions(RoutingGraphLiveTest PRIVATE
	ROUTING_GRAPH_LIVE_REFERENCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/reference/routing-graph-live-render.raw"
)

# VcaGroupTest (#622) constructs a full Engine and drives Mixer::masterMix()
# synchronously, the same shape as the Phase D harnesses.
set_tests_properties(VcaGroupTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# MasteringTest drives a real Engine and the project render path, so it needs a
# Qt platform plugin even though the test itself is guiless (same reason as
# PluginScanCacheTest above).
set_tests_properties(MasteringTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The dither and SRC-quality tests build a real Engine and drive the real
# export path (AudioFileWave writes a WAV; Sample::play resamples), so they need
# a Qt platform plugin even though the tests themselves are guiless - the same
# reason as MasteringTest above.
set_tests_properties(ExportDitherTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ExportWavDitherTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AudioResamplerRatioTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# Remote plugin host<->client contract, driven end to end.
#
# RemotePluginClientE2ETest runs a real remote plugin client
# (RemoteZynAddSubFx, the only in-tree remote client that starts headless)
# through a real host, and pins the shared-audio-block layout with a
# hand-written client that speaks the same protocol and writes a known planar
# pattern (tests/src/plugins/FakeRemotePluginClient.cpp). The fake client uses
# POSIX sockets and shm_open directly, so it is only built on non-Windows
# hosts; the test reports that half as unexercised everywhere it is absent.
# ---------------------------------------------------------------------------
if(NOT WIN32)
	add_executable(FakeRemotePluginClient
		src/plugins/FakeRemotePluginClient.cpp
	)
	target_compile_features(FakeRemotePluginClient PRIVATE cxx_std_20)
	target_compile_definitions(RemotePluginClientE2ETest PRIVATE
		REMOTE_PLUGIN_E2E_FAKE_CLIENT="$<TARGET_FILE:FakeRemotePluginClient>"
	)
	add_dependencies(RemotePluginClientE2ETest FakeRemotePluginClient)
endif()

if(TARGET RemoteZynAddSubFx)
	target_compile_definitions(RemotePluginClientE2ETest PRIVATE
		REMOTE_PLUGIN_E2E_CLIENT_DIR="$<TARGET_FILE_DIR:RemoteZynAddSubFx>"
	)
	add_dependencies(RemotePluginClientE2ETest RemoteZynAddSubFx)
endif()

# The test constructs a full Engine and spawns the real client executable, so
# it needs a Qt platform plugin even though it is guiless (same reason as
# PluginPortsMigrationTest).
set_tests_properties(RemotePluginClientE2ETest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# StemExportTest drives the real render path (Engine + RenderManager +
# ProjectRenderer + AudioFileWave) on a project it writes to a temporary
# directory, so it needs the same headless Qt platform as the test above.
set_tests_properties(StemExportTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# PluginLogoResourceTest loads a QPixmap through the product's own artwork search
# path, so it needs a QGuiApplication and therefore a Qt platform plugin.  It
# exists because a resource that does not resolve is silent: embed::loadSvgPixmap
# warns and returns a 1x1 transparent pixmap, so nothing fails while every plugin
# dialog renders blank.
set_tests_properties(PluginLogoResourceTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# Part C (task #585) - behaviour-preservation harness for the AudioPorts
# plugin migration.
#
# The pre-migration DSP sources are extracted verbatim from the base commit
# 4ac5c3e38 into tests/reference/ (blob ids in tests/reference/ORIGIN.tsv,
# re-extract with tests/reference/extract-reference-sources.sh) and built here
# into partc_ref_* modules with the same PLUGIN_NAME define the real plugins
# use. PluginPortsMigrationReference renders them through the normal plugin
# entry point; PluginPortsMigrationTest renders the migrated plugins built by
# the normal build and compares both renders sample-exactly.
# ---------------------------------------------------------------------------
set(PART_C_REFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/reference")

set(PART_C_REFERENCE_MODULES
	"amplifier|Amplifier|Amplifier.cpp|AmplifierControls.cpp|AmplifierControlDialog.cpp"
	"bassbooster|BassBooster|BassBooster.cpp|BassBoosterControls.cpp|BassBoosterControlDialog.cpp"
	"bitcrush|Bitcrush|Bitcrush.cpp|BitcrushControls.cpp|BitcrushControlDialog.cpp"
	"dualfilter|DualFilter|DualFilter.cpp|DualFilterControls.cpp|DualFilterControlDialog.cpp"
	"waveshaper|WaveShaper|WaveShaper.cpp|WaveShaperControls.cpp|WaveShaperControlDialog.cpp"
	"flanger|Flanger|FlangerEffect.cpp|FlangerControls.cpp|FlangerControlsDialog.cpp|MonoDelay.cpp"
	"delay|Delay|DelayEffect.cpp|DelayControls.cpp|DelayControlsDialog.cpp|Lfo.cpp|StereoDelay.cpp"
	"compressor|Compressor|Compressor.cpp|CompressorControls.cpp|CompressorControlDialog.cpp"
	"crossovereq|CrossoverEQ|CrossoverEQ.cpp|CrossoverEQControls.cpp|CrossoverEQControlDialog.cpp"
	"dynamicsprocessor|DynamicsProcessor|DynamicsProcessor.cpp|DynamicsProcessorControls.cpp|DynamicsProcessorControlDialog.cpp"
	"lomm|LOMM|LOMM.cpp|LOMMControls.cpp|LOMMControlDialog.cpp"
	"multitapecho|MultitapEcho|MultitapEcho.cpp|MultitapEchoControls.cpp|MultitapEchoControlDialog.cpp"
	"reverbsc|ReverbSC|ReverbSC.cpp|ReverbSCControls.cpp|ReverbSCControlDialog.cpp|base.c|revsc.c|dcblock.c"
	"stereoenhancer|StereoEnhancer|StereoEnhancer.cpp|StereoEnhancerControls.cpp|StereoEnhancerControlDialog.cpp"
	"stereomatrix|StereoMatrix|StereoMatrix.cpp|StereoMatrixControls.cpp|StereoMatrixControlDialog.cpp"
	# Slice 3 (task #589): analysers, Dispersion, granular shifter and Eq.
	"dispersion|Dispersion|Dispersion.cpp|DispersionControls.cpp|DispersionControlDialog.cpp"
	"vectorscope|Vectorscope|Vectorscope.cpp|VecControls.cpp|VecControlsDialog.cpp|VectorView.cpp"
	"analyzer|SpectrumAnalyzer|Analyzer.cpp|SaControls.cpp|SaControlsDialog.cpp|SaProcessor.cpp|SaSpectrumView.cpp|SaWaterfallView.cpp"
	"granularpitchshifter|GranularPitchShifter|GranularPitchShifterEffect.cpp|GranularPitchShifterControls.cpp|GranularPitchShifterControlDialog.cpp"
	"eq|Eq|EqEffect.cpp|EqControls.cpp|EqControlsDialog.cpp|EqCurve.cpp|EqParameterWidget.cpp|EqSpectrumView.cpp"
	# Slice 4 (task #589): synth instruments.
	"freeboy|FreeBoy|FreeBoy.cpp|FreeBoy.h|GbApuWrapper.cpp|GbApuWrapper.h"
	"nes|Nes|Nes.cpp|Nes.h"
	"sid|Sid|SidInstrument.cpp|SidInstrument.h"
	"opulenz|OpulenZ|OpulenZ.cpp|OpulenZ.h"
	"sfxr|Sfxr|Sfxr.cpp|Sfxr.h"
	"bitinvader|BitInvader|BitInvader.cpp|BitInvader.h"
	"watsyn|Watsyn|Watsyn.cpp|Watsyn.h"
	"xpressive|Xpressive|Xpressive.cpp|Xpressive.h|ExprSynth.cpp|ExprSynth.h"
	"vibedstrings|Vibed|Vibed.cpp|Vibed.h|NineButtonSelector.cpp|NineButtonSelector.h|VibratingString.cpp|VibratingString.h"
	"kicker|Kicker|Kicker.cpp|Kicker.h|KickerOsc.h"
	# Slice 5 (task #589): multi-oscillator and sample-playback instruments.
	"tripleoscillator|TripleOscillator|TripleOscillator.cpp|TripleOscillator.h"
	"monstro|Monstro|Monstro.cpp|Monstro.h"
	"organic|Organic|Organic.cpp|Organic.h"
	"audiofileprocessor|AudioFileProcessor|AudioFileProcessor.cpp|AudioFileProcessor.h|AudioFileProcessorView.cpp|AudioFileProcessorView.h|AudioFileProcessorWaveView.cpp|AudioFileProcessorWaveView.h"
	# Slice 6 (task #589): LADSPA host and the Lb302 bass synth.
	"lb302|Lb302|Lb302.cpp|Lb302.h"
	"ladspaeffect|LadspaEffect|LadspaEffect.cpp|LadspaEffect.h|LadspaControls.cpp|LadspaControls.h|LadspaControlDialog.cpp|LadspaControlDialog.h|LadspaMatrixControlDialog.cpp|LadspaMatrixControlDialog.h|LadspaSubPluginFeatures.cpp|LadspaSubPluginFeatures.h|LadspaWidgetFactory.cpp|LadspaWidgetFactory.h"
	# Slice 7 (task #589): the last in-tree legacy-API plugins.
	"frequencyshifter|FrequencyShifter|FrequencyShifterEffect.cpp|FrequencyShifterControls.cpp|FrequencyShifterControlDialog.cpp|HilbertTransform.h"
	"oscilloscope|Oscilloscope|Oscilloscope.cpp|OscilloscopeControls.cpp|OscilloscopeControlDialog.cpp|OscilloscopeGraph.cpp|OscilloscopeGraph.h"
	"slewdistortion|SlewDistortion|SlewDistortion.cpp|SlewDistortionControls.cpp|SlewDistortionControlDialog.cpp"
)

# Slice 8 (task #589): the external-asset hosts. Each entry is only added when
# the build actually has the dependency (mirroring the guard in the plugin's
# own CMakeLists.txt) and, for Sf2Player, when a soundfont exists to render -
# a module the harness cannot drive would compare silence with silence and
# prove nothing.
if(LMMS_HAVE_LV2)
	list(APPEND PART_C_REFERENCE_MODULES
		"lv2effect|Lv2Effect|Lv2Effect.cpp|Lv2Effect.h|Lv2FxControls.cpp|Lv2FxControls.h|Lv2FxControlDialog.cpp|Lv2FxControlDialog.h"
		"lv2instrument|Lv2Instrument|Lv2Instrument.cpp|Lv2Instrument.h")
endif()
find_file(PART_C_SF2_FILE NAMES FluidR3_GM.sf2 default-GM.sf2
	HINTS /usr/share/sounds/sf2 /usr/local/share/sounds/sf2)
if(LMMS_HAVE_FLUIDSYNTH AND PART_C_SF2_FILE)
	list(APPEND PART_C_REFERENCE_MODULES
		"sf2player|Sf2Player|Sf2Player.cpp|Sf2Player.h|PatchesDialog.cpp|PatchesDialog.h|PatchesDialog.ui")
endif()
# GigPlayer renders from a .gig instrument file. With no file to load, both the
# migrated and the reference host render silence and the sample-exact
# comparison proves nothing, so the module is only added when the dependency
# AND an instrument file are both present. tests/data/gigproof/sine440.gig is
# vendored for exactly this purpose (see its README for provenance and how to
# regenerate it); the test reports gigplayer as UNPROVEN when it is absent.
find_file(PART_C_GIG_FILE NAMES sine440.gig
	HINTS "${CMAKE_SOURCE_DIR}/tests/data/gigproof"
	      "${CMAKE_SOURCE_DIR}/../tests/data/gigproof")
if(LMMS_HAVE_GIG AND PART_C_GIG_FILE)
	list(APPEND PART_C_REFERENCE_MODULES
		"gigplayer|GigPlayer|GigPlayer.cpp|GigPlayer.h|PatchesDialog.cpp|PatchesDialog.h|PatchesDialog.ui")
endif()
# Mallets renders from the STK rawwave directory; without it the instrument
# latches m_filesMissing in its constructor and renders silence, which would
# make the sample-exact comparison vacuous. So the module is only added when
# the waves are actually present.
find_path(PART_C_STK_DIR NAMES sinewave.raw
	HINTS "${CMAKE_SOURCE_DIR}/thirdparty/stk-rawwaves"
	      "${CMAKE_SOURCE_DIR}/../thirdparty/stk-rawwaves"
	      /usr/share/stk/rawwaves /usr/local/share/stk/rawwaves)
if(LMMS_HAVE_STK AND PART_C_STK_DIR)
	list(APPEND PART_C_REFERENCE_MODULES
		"malletsstk|Stk/Mallets|Mallets.cpp|Mallets.h")
endif()

set(PART_C_REFERENCE_TARGETS "")
set(PART_C_MIGRATED_TARGETS "")
set(PART_C_REFERENCE_DEFINITIONS "")
set(PART_C_MIGRATED_DEFINITIONS "")

foreach(ENTRY IN LISTS PART_C_REFERENCE_MODULES)
	string(REPLACE "|" ";" FIELDS "${ENTRY}")
	list(GET FIELDS 0 PLUGIN_ID)
	list(GET FIELDS 1 PLUGIN_DIR)
	list(REMOVE_AT FIELDS 0 1)

	set(PLUGIN_SOURCES "")
	foreach(SRC IN LISTS FIELDS)
		list(APPEND PLUGIN_SOURCES "${PART_C_REFERENCE_DIR}/${PLUGIN_DIR}/${SRC}")
	endforeach()
	if(PLUGIN_ID STREQUAL "delay" OR PLUGIN_ID STREQUAL "compressor" OR PLUGIN_ID STREQUAL "eq")
		# EqFader is header-only; AUTOMOC needs the header to emit its
		# metaobject and vtable, which DelayControlsDialog.cpp and
		# CompressorControlDialog.cpp reference.
		list(APPEND PLUGIN_SOURCES "${PART_C_REFERENCE_DIR}/Eq/EqFader.h")
	endif()

	add_library(partc_ref_${PLUGIN_ID} MODULE ${PLUGIN_SOURCES})
	target_include_directories(partc_ref_${PLUGIN_ID} PRIVATE
		"${PART_C_REFERENCE_DIR}"
		"${PART_C_REFERENCE_DIR}/${PLUGIN_DIR}"
		"${PART_C_REFERENCE_DIR}/Eq"
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	)
	target_compile_definitions(partc_ref_${PLUGIN_ID} PRIVATE PLUGIN_NAME=${PLUGIN_ID})
	target_compile_features(partc_ref_${PLUGIN_ID} PRIVATE cxx_std_20)
	target_link_libraries(partc_ref_${PLUGIN_ID} PRIVATE zene ${QT_LIBRARIES})
	# The reference sources are verbatim copies of the pre-migration plugin
	# (git blob ids in tests/reference/ORIGIN.tsv), so they cannot be edited the
	# way the migrated plugin was: rewriting one would destroy the evidence the
	# harness compares against. Under Qt 6 the frozen GUI code calls
	# QMouseEvent::x()/y(), which Qt 6 deprecates, and USE_WERROR promotes that
	# deprecation to an error on any compiler that does not treat Qt's
	# angle-bracket headers as external (MSVC's /external:anglebrackets hides it
	# there). The deprecation is not actionable in frozen evidence, so the
	# reference targets - and only they - opt out of it.
	target_compile_options(partc_ref_${PLUGIN_ID} PRIVATE
		$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-deprecated-declarations>)
	set_target_properties(partc_ref_${PLUGIN_ID} PROPERTIES PREFIX "" OUTPUT_NAME "partc_ref_${PLUGIN_ID}")

	# Slice 4 (task #589): instruments whose plugin CMakeLists.txt links extra
	# targets or sets extra defines - mirror them so the reference module is
	# built exactly like the pre-migration plugin was.
	if(PLUGIN_ID STREQUAL "slewdistortion")
		# The plugin links hiir itself; OversamplingHelpers.h pulls in the
		# hiir up/down-sampler headers.
		target_link_libraries(partc_ref_slewdistortion PRIVATE hiir)
	elseif(PLUGIN_ID STREQUAL "freeboy")
		target_link_libraries(partc_ref_freeboy PRIVATE gme)
	elseif(PLUGIN_ID STREQUAL "sid")
		target_link_libraries(partc_ref_sid PRIVATE resid)
	elseif(PLUGIN_ID STREQUAL "opulenz")
		target_link_libraries(partc_ref_opulenz PRIVATE adplug)
	elseif(PLUGIN_ID STREQUAL "watsyn")
		target_link_libraries(partc_ref_watsyn PRIVATE SampleRate::samplerate)
	elseif(PLUGIN_ID STREQUAL "xpressive")
		target_link_libraries(partc_ref_xpressive PRIVATE exprtk)
		# exprtk.hpp blows past the object-file section limit; the reference target
		# compiles the same header as the plugin, so it needs the same escape hatch
		# (CI mingw64: "/tmp/cc….s: Fatal error: … ExprSynth.cpp.obj: 'file too big'").
		# MSVC wants /bigobj, mingw wants the assembler-level flag.
		if(MSVC)
			target_compile_options(partc_ref_xpressive PRIVATE /bigobj)
		elseif(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)
			target_compile_options(partc_ref_xpressive PRIVATE -Wa,-mbig-obj)
		endif()
		target_compile_definitions(partc_ref_xpressive PRIVATE
			exprtk_disable_sc_andor
			exprtk_disable_return_statement
			exprtk_disable_break_continue
			exprtk_disable_comments
			exprtk_disable_string_capabilities
			exprtk_disable_rtl_io_file
			exprtk_disable_rtl_vecops)
	elseif(PLUGIN_ID STREQUAL "sf2player")
		# Mirror plugins/Sf2Player/CMakeLists.txt: fluidsynth + samplerate,
		# and PatchesDialog.ui is built with AUTOUIC like the real plugin.
		set_target_properties(partc_ref_sf2player PROPERTIES AUTOUIC ON)
		target_link_libraries(partc_ref_sf2player PRIVATE fluidsynth SampleRate::samplerate)
	elseif(PLUGIN_ID STREQUAL "gigplayer")
		# Mirror plugins/GigPlayer/CMakeLists.txt: libgig include dirs, the
		# forced exceptions libgig needs to not crash, samplerate link.
		set_target_properties(partc_ref_gigplayer PROPERTIES AUTOUIC ON)
		target_include_directories(partc_ref_gigplayer SYSTEM PRIVATE ${GIG_INCLUDE_DIRS})
		if(NOT MSVC)
			# -fexceptions is GNU-style: MSVC ignores it and enables C++
			# exceptions by default, so the guard mirrors plugins/GigPlayer/CMakeLists.txt.
			target_compile_options(partc_ref_gigplayer PRIVATE -fexceptions)
		endif()
		# The plugin does link_directories(${GIG_LIBRARY_DIRS}) before linking
		# ${GIG_LIBRARIES}; without the directory the reference target's link fails on
		# systems where libgig lives outside the default search path
		# (CI macos-arm64: "ld: library 'gig' not found").
		if(GIG_LIBRARY_DIRS)
			target_link_directories(partc_ref_gigplayer PRIVATE ${GIG_LIBRARY_DIRS})
		endif()
		target_link_libraries(partc_ref_gigplayer PRIVATE
			SampleRate::samplerate ${GIG_LIBRARIES})
	elseif(PLUGIN_ID STREQUAL "malletsstk")
		target_link_libraries(partc_ref_malletsstk PRIVATE unofficial::libstk::libstk)
	elseif(PLUGIN_ID STREQUAL "lv2effect" OR PLUGIN_ID STREQUAL "lv2instrument")
		# The LV2/Lilv/Suil headers the plugins include; the libraries arrive
		# through lmmsobjs, which both harness binaries link.
		target_include_directories(partc_ref_${PLUGIN_ID} PRIVATE
			${LV2_INCLUDE_DIRS} ${Lilv_INCLUDE_DIRS} ${SUIL_INCLUDE_DIRS})
	endif()

	list(APPEND PART_C_REFERENCE_TARGETS partc_ref_${PLUGIN_ID})
	list(APPEND PART_C_MIGRATED_TARGETS ${PLUGIN_ID})
	list(APPEND PART_C_REFERENCE_DEFINITIONS
		"PART_C_REF_${PLUGIN_ID}=\"$<TARGET_FILE:partc_ref_${PLUGIN_ID}>\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS
		"PART_C_MIGRATED_${PLUGIN_ID}=\"$<TARGET_FILE:${PLUGIN_ID}>\"")
endforeach()

# Slice 6 (task #589): LadspaEffect resolves its LADSPA libraries through
# LADSPA_PATH at runtime, and both the test and the reference renderer have to
# point at the in-tree SWH builds. The dir is taken from the plugin targets so
# no path is hardcoded; the two plugins the harness uses are also added as
# build dependencies of the test so they exist before ctest runs.
set(PART_C_LADSPA_TARGETS "")
if(TARGET amp_1181 AND TARGET dj_eq_1901)
	list(APPEND PART_C_REFERENCE_DEFINITIONS
		"PART_C_LADSPA_DIR=\"$<TARGET_FILE_DIR:amp_1181>\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS
		"PART_C_LADSPA_DIR=\"$<TARGET_FILE_DIR:amp_1181>\"")
	list(APPEND PART_C_LADSPA_TARGETS amp_1181 dj_eq_1901)
endif()

# Slice 8 (task #589): runtime assets. The STK rawwaves are vendored in the
# tree, so Mallets reads the same directory in both processes; the soundfont
# path is only defined when one was found, which is also what gates the
# sf2player module above.
if(TARGET malletsstk)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_STK_DIR=\"${PART_C_STK_DIR}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_STK_DIR=\"${PART_C_STK_DIR}\"")
endif()
if(PART_C_SF2_FILE)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_SF2_FILE=\"${PART_C_SF2_FILE}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_SF2_FILE=\"${PART_C_SF2_FILE}\"")
endif()
if(PART_C_GIG_FILE)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_GIG_FILE=\"${PART_C_GIG_FILE}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_GIG_FILE=\"${PART_C_GIG_FILE}\"")
endif()

add_executable(PluginPortsMigrationReference
	src/plugins/PluginPortsMigrationReference.cpp
	src/plugins/PluginPortsHarness.h
)
target_include_directories(PluginPortsMigrationReference PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_static_libraries(PluginPortsMigrationReference PRIVATE lmmsobjs)
target_link_libraries(PluginPortsMigrationReference PRIVATE ${QT_LIBRARIES})
target_compile_features(PluginPortsMigrationReference PRIVATE cxx_std_20)
target_compile_definitions(PluginPortsMigrationReference PRIVATE
	LMMS_TESTING
	${PART_C_REFERENCE_DEFINITIONS}
)
set_target_properties(PluginPortsMigrationReference PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(PluginPortsMigrationReference ${PART_C_REFERENCE_TARGETS})

add_executable(PluginPortsMigrationTest
	src/plugins/PluginPortsMigrationTest.cpp
	src/plugins/PluginPortsHarness.h
)
add_test(NAME PluginPortsMigrationTest COMMAND PluginPortsMigrationTest)
# The harness constructs a full Engine, which needs a Qt platform plugin even
# though the test itself is guiless. Without this, ctest aborts before the first
# assertion on any machine with no display (CI, headless shell).
set_tests_properties(PluginPortsMigrationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(PluginPortsMigrationTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_static_libraries(PluginPortsMigrationTest PRIVATE lmmsobjs)
target_link_libraries(PluginPortsMigrationTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(PluginPortsMigrationTest PRIVATE cxx_std_20)
target_compile_definitions(PluginPortsMigrationTest PRIVATE
	LMMS_TESTING
	PART_C_REFERENCE_EXE="$<TARGET_FILE:PluginPortsMigrationReference>"
	${PART_C_MIGRATED_DEFINITIONS}
)
set_target_properties(PluginPortsMigrationTest PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(PluginPortsMigrationTest
	PluginPortsMigrationReference
	${PART_C_MIGRATED_TARGETS}
	${PART_C_LADSPA_TARGETS}
)

if(WANT_WASM)
	# Gate tests for the WASM DSP sandbox. Kept out of LMMS_TESTS so the
	# pre-existing suite is byte-for-byte unchanged when WANT_WASM=OFF.
	# The WasmEffect plugin sources are compiled into the test binary as well
	# so registration, save/load and latency are testable without loading a
	# plugin .so (the plugin itself is still built as a module target).
	add_executable(WasmSandboxTest
		src/wasm/WasmSandboxTest.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffect.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffect.h
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControls.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControls.h
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControlDialog.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControlDialog.h
	)
	add_test(NAME WasmSandboxTest COMMAND WasmSandboxTest)

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(WasmSandboxTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/src/wasm
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect
		${CMAKE_BINARY_DIR}/plugins/WasmEffect
	)
	target_static_libraries(WasmSandboxTest PRIVATE lmmsobjs)
	target_link_libraries(WasmSandboxTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	# The plugin .so resolves LMMS core symbols from the host process, exactly
	# like the real zene binary (src/CMakeLists.txt sets ENABLE_EXPORTS ON).
	# Without it p5's PluginFactory load would fail on the first core symbol.
	set_target_properties(WasmSandboxTest PROPERTIES ENABLE_EXPORTS ON)

	target_compile_features(WasmSandboxTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmSandboxTest PRIVATE
		LMMS_TESTING
		PLUGIN_NAME=wasm_effect
		WASM_MODULES_DIR="${CMAKE_BINARY_DIR}/wasm-modules"
		WASM_WAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/modules/wasm"
	)
	add_dependencies(WasmSandboxTest wasm-modules)

	# #614: the conformance suite for docs/WASM-EFFECT-ABI.md. It asserts each documented
	# ABI property (and its documented limit) against modules assembled at run time from
	# .wat source with WasmSandbox::assembleWat, so it depends on no built fixture except
	# the example effect, tests/data/wasm-effect-abi/softclip.wat, which was itself
	# written from that document. Registered here, under the same WANT_WASM guard as
	# WasmSandboxTest, because it needs the wasmtime C API to link and run.
	#
	# IT HAS NOT BEEN EXECUTED. On a build without the wasmtime C API (WANT_WASM degrades
	# to OFF at CMakeLists.txt:957-963) neither this target nor WasmSandboxTest exists,
	# which is the case on the reference box; docs/KNOWN-LIMITATIONS.md records that.
	add_executable(WasmAbiConformanceTest
		src/wasm/WasmAbiConformanceTest.cpp
	)
	add_test(NAME WasmAbiConformanceTest COMMAND WasmAbiConformanceTest)
	target_include_directories(WasmAbiConformanceTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/src/wasm
	)
	target_static_libraries(WasmAbiConformanceTest PRIVATE lmmsobjs)
	target_link_libraries(WasmAbiConformanceTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(WasmAbiConformanceTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmAbiConformanceTest PRIVATE
		LMMS_TESTING
		WASM_ABI_EXAMPLE_WAT="${CMAKE_SOURCE_DIR}/tests/data/wasm-effect-abi/softclip.wat"
	)
endif()

# Native CLAP hosting tests. They only exist when the MIT licensed CLAP headers
# (>= 1.2.10) are available; see cmake/modules/ClapHeaders.cmake. The port map
# is pure data transformation, so it is tested without loading a plug-in.
if(TARGET lmms_clap)
	set(LMMS_CLAP_EFFECT_DIR ${CMAKE_SOURCE_DIR}/plugins/ClapEffect)

	# The test subject: a real CLAP stereo gain effect built from the pinned
	# headers (tests/data/clap-test-plugin/clap-test-gain.c).
	add_subdirectory(data/clap-test-plugin)

	add_executable(ClapBusMapTest
		src/plugins/ClapBusMapTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
	)
	add_test(NAME ClapBusMapTest COMMAND ClapBusMapTest)
	target_include_directories(ClapBusMapTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_CLAP_EFFECT_DIR}
	)
	target_compile_features(ClapBusMapTest PRIVATE cxx_std_20)
	target_link_libraries(ClapBusMapTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})

	add_executable(ClapHostTest
		src/plugins/ClapHostTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
	)
	add_test(NAME ClapHostTest COMMAND ClapHostTest)
	target_include_directories(ClapHostTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_CLAP_EFFECT_DIR}
	)
	target_compile_features(ClapHostTest PRIVATE cxx_std_20)
	target_link_libraries(ClapHostTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapHostTest PRIVATE
		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>")
	add_dependencies(ClapHostTest clap-test-gain)

	add_executable(ClapEffectIntegrationTest
		src/plugins/ClapEffectIntegrationTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffect.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffectControlDialog.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffectControls.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapParameter.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapSubPluginFeatures.cpp
	)
	add_test(NAME ClapEffectIntegrationTest COMMAND ClapEffectIntegrationTest)
	target_include_directories(ClapEffectIntegrationTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the clapeffect plug-in
		${CMAKE_BINARY_DIR}/plugins/ClapEffect
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_CLAP_EFFECT_DIR}
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(ClapEffectIntegrationTest clapeffect)
	target_static_libraries(ClapEffectIntegrationTest PRIVATE lmmsobjs)
	target_compile_features(ClapEffectIntegrationTest PRIVATE cxx_std_20)
	target_link_libraries(ClapEffectIntegrationTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapEffectIntegrationTest PRIVATE
		LMMS_TESTING
		PLUGIN_NAME=clapeffect
		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>"
	)
endif()

# Native VST3 hosting tests — the twins of the CLAP block above, and the reason
# tests/src/plugins/Vst3{BusMap,Host,EffectIntegration}Test.cpp exist.
#
# Until 0.2.1 these three compiled NOWHERE: their CLAP twins were registered and
# green while the VST3 three sat in the tree, and tests/unregistered-tests-gate.sh
# (Gate 10) carried them in its DECLARED table as "an open item, NOT a deliberate
# exclusion" — a test that never builds is invisible to every other gate (it is
# not covered by the coverage ratchet and not compiled by the complexity
# ratchet). This registers them exactly as the CLAP three are registered.
#
# Guarded on TARGET lmms_vst3_sdk, the target cmake/modules/Vst3Sdk.cmake creates
# only when the pinned MIT SDK checkout is present (WANT_VST3=AUTO skips without
# it, WANT_VST3=OFF disables it, Vst3Sdk.cmake FATAL_ERRORs when ON and absent).
# So a configure with no SDK skips these tests instead of failing, which is the
# same shape as the CLAP block's `if(TARGET lmms_clap)`.
#
# The subject is the SDK's own `again_sampleaccurate` gain effect, built as a
# .vst3 bundle from the same pinned checkout by
# tests/data/vst3-test-effect/CMakeLists.txt.
if(TARGET lmms_vst3_sdk)
	set(LMMS_VST3_EFFECT_DIR ${CMAKE_SOURCE_DIR}/plugins/Vst3Effect)

	# The test subject: a real VST3 stereo gain effect (two parameters, Bypass
	# and Gain) built from the pinned SDK's own sample.
	add_subdirectory(data/vst3-test-effect)

	add_executable(Vst3BusMapTest
		src/plugins/Vst3BusMapTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
	)
	add_test(NAME Vst3BusMapTest COMMAND Vst3BusMapTest)
	target_include_directories(Vst3BusMapTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_VST3_EFFECT_DIR}
	)
	target_compile_features(Vst3BusMapTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3BusMapTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
	)

	# Host-level proof: it compiles the shipped host sources itself, exactly as
	# ClapHostTest does for the CLAP host, and loads the fixture bundle.
	add_executable(Vst3HostTest
		src/plugins/Vst3HostTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiEvent.cpp
	)
	add_test(NAME Vst3HostTest COMMAND Vst3HostTest)
	target_include_directories(Vst3HostTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_VST3_EFFECT_DIR}
	)
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3HostTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(Vst3HostTest PRIVATE
		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}")
	add_dependencies(Vst3HostTest vst3-test-effect)

	# Product-level proof: the effect is loaded into an EffectChain on an
	# AudioBus and driven through the engine, its state round-trips through the
	# .mmp, and the renders before/after are written. It compiles the plug-in's
	# own sources (as ClapEffectIntegrationTest does for the CLAP effect) so that
	# the descriptor and controls under test are the shipped ones.
	add_executable(Vst3EffectIntegrationTest
		src/plugins/Vst3EffectIntegrationTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Effect.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControlDialog.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControls.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiEvent.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Parameter.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3SubPluginFeatures.cpp
	)
	add_test(NAME Vst3EffectIntegrationTest COMMAND Vst3EffectIntegrationTest)
	target_include_directories(Vst3EffectIntegrationTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the vst3effect plug-in
		${CMAKE_BINARY_DIR}/plugins/Vst3Effect
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_VST3_EFFECT_DIR}
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(Vst3EffectIntegrationTest vst3effect)
	target_static_libraries(Vst3EffectIntegrationTest PRIVATE lmmsobjs)
	target_compile_features(Vst3EffectIntegrationTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3EffectIntegrationTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(Vst3EffectIntegrationTest PRIVATE
		LMMS_TESTING
		PLUGIN_NAME=vst3effect
		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}"
	)
	add_dependencies(Vst3EffectIntegrationTest vst3-test-effect)
endif()

# Instrument the plugin/gate test executables as well - they compile fork
# sources directly, so their objects carry the coverage the ratchet tracks.
# PluginPortsMigrationReference is deliberately left un-instrumented: the
# reference side of the behaviour comparison must stay plain.
foreach(LMMS_GATE_TEST ClapBusMapTest ClapHostTest ClapEffectIntegrationTest
		Vst3BusMapTest Vst3HostTest Vst3EffectIntegrationTest
		WasmSandboxTest PluginPortsMigrationTest AudioPluginTest
		synthetic_audio_plugin)
	if(TARGET ${LMMS_GATE_TEST})
		lmms_add_coverage(${LMMS_GATE_TEST})
	endif()
endforeach()

# ---------------------------------------------------------------------------
# The per-plugin "run in a separate process" toggle (post-alpha/oop-hosting).
#
# ZynAddSubFx is the one plugin family in this tree that ships both an
# in-process implementation (LocalZynAddSubFx) and a client that runs as a
# separate process (RemoteZynAddSubFx), so the opt-in toggle lives there. The
# test loads the real plugin module and drives the setting the project file
# carries: the default (no attribute) must stay in-process, the stored choice
# must bring the client up as a child process of the host, the choice must
# round-trip through the plugin's own saveSettings/loadSettings, and killing
# the client must not take the host down.
# ---------------------------------------------------------------------------
if(TARGET zynaddsubfx AND TARGET RemoteZynAddSubFx)
	add_executable(ZynSeparateProcessTest src/plugins/ZynSeparateProcessTest.cpp)
	add_test(NAME ZynSeparateProcessTest COMMAND ZynSeparateProcessTest)
	target_include_directories(ZynSeparateProcessTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	)
	target_static_libraries(ZynSeparateProcessTest PRIVATE lmmsobjs)
	target_link_libraries(ZynSeparateProcessTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(ZynSeparateProcessTest PRIVATE cxx_std_20)
	target_compile_definitions(ZynSeparateProcessTest PRIVATE
		LMMS_TESTING
		ZYN_PLUGIN_PATH="$<TARGET_FILE:zynaddsubfx>"
		ZYN_REMOTE_CLIENT_PATH="$<TARGET_FILE:RemoteZynAddSubFx>"
	)
	# The plugin module's unresolved lmms symbols are bound from the loading
	# process, which is why the host has to export them (same as AudioPluginTest).
	set_target_properties(ZynSeparateProcessTest PROPERTIES ENABLE_EXPORTS ON)
	add_dependencies(ZynSeparateProcessTest zynaddsubfx RemoteZynAddSubFx)
	set_tests_properties(ZynSeparateProcessTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Synthetic plug-in host test (ported from the standards fork): builds a MODULE
# plug-in from src/plugins/SyntheticAudioPlugin.cpp and drives it through the
# AudioPlugin host path, so the host code is exercised for real.
add_library(synthetic_audio_plugin MODULE src/plugins/SyntheticAudioPlugin.cpp)
target_include_directories(synthetic_audio_plugin PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_compile_definitions(synthetic_audio_plugin PRIVATE PLUGIN_NAME=synthetic)
target_compile_features(synthetic_audio_plugin PRIVATE cxx_std_20)
# The test double subclasses lmms::AudioPlugin, so its object code references lmms
# symbols (AudioPortsModel's moc, Model's vtable, JournallingObject's dtor ...).
# Unix shared modules tolerate unresolved symbols; macOS, mingw and MSVC do not, so
# this target failed to link there while linking only Qt. Real plugins link the zene
# target (cmake/modules/BuildPlugin.cmake), and the partc_ref_* harnesses above were
# fixed the same way — this target was the one left behind.
target_link_libraries(synthetic_audio_plugin PRIVATE zene ${QT_LIBRARIES})
set_target_properties(synthetic_audio_plugin PROPERTIES PREFIX "")

add_executable(AudioPluginTest src/plugins/AudioPluginTest.cpp)
add_test(NAME AudioPluginTest COMMAND AudioPluginTest)
target_include_directories(AudioPluginTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_static_libraries(AudioPluginTest PRIVATE lmmsobjs)
target_link_libraries(AudioPluginTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(AudioPluginTest PRIVATE cxx_std_20)
target_compile_definitions(AudioPluginTest PRIVATE
	LMMS_TESTING
	SYNTHETIC_PLUGIN_PATH="$<TARGET_FILE:synthetic_audio_plugin>"
)
set_target_properties(AudioPluginTest PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(AudioPluginTest synthetic_audio_plugin)
set_tests_properties(AudioPluginTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
lmms_add_coverage(synthetic_audio_plugin)
lmms_add_coverage(AudioPluginTest)

# Native VST3 instrument fixture and its probe.
#
# Opt-in on purpose: cmake/modules/Vst3Sdk.cmake builds lmms_vst3_sdk whenever
# the SDK checkout exists, but an ordinary build has no business gaining a test
# instrument, so it is behind WANT_VST3_TEST_INSTRUMENT (default OFF) and is
# never installed. See tests/data/vst3-test-instrument/CMakeLists.txt and
# docs/VST3-INSTRUMENT-FIXTURE.md (which records why the fixture exists at all).
if(TARGET lmms_vst3_sdk)
	option(WANT_VST3_TEST_INSTRUMENT
		"Build the VST3 instrument test fixture and its probe (test subject only)" OFF)
	if(WANT_VST3_TEST_INSTRUMENT)
		add_subdirectory(data/vst3-test-instrument)

		# A probe, not a QTest class: it is the independent check that the
		# fixture really reads ProcessData::inputEvents and renders audio in
		# response. Helper executables are explicitly out of scope for Gate 3
		# (see the note at the bottom of tests/no-tautology-gate.sh); this one
		# still exits non-zero on a failed check, so ctest enforces it.
		add_executable(Vst3InstrumentFixtureProbe
			src/plugins/Vst3InstrumentFixtureProbe.cpp
		)
		target_include_directories(Vst3InstrumentFixtureProbe PRIVATE
			${CMAKE_SOURCE_DIR}/include
		)
		target_compile_features(Vst3InstrumentFixtureProbe PRIVATE cxx_std_17)
		target_link_libraries(Vst3InstrumentFixtureProbe PRIVATE
			lmms_vst3_sdk
			${CMAKE_DL_LIBS}
		)
		target_compile_definitions(Vst3InstrumentFixtureProbe PRIVATE
			VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
		)
		add_dependencies(Vst3InstrumentFixtureProbe vst3-test-instrument)
		add_test(NAME Vst3InstrumentFixtureProbe COMMAND Vst3InstrumentFixtureProbe)
	
		# Host-level proof of the MIDI event path: the host fills
		# ProcessData::inputEvents from pushMidiEvent(), and the timing is sample
		# accurate. It compiles the host sources itself, exactly as ClapHostTest
		# does for the CLAP host, so it exercises the shipped Vst3Host.
		add_executable(Vst3InstrumentTest
			src/plugins/Vst3InstrumentTest.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3BusMap.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3Host.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3MidiEvent.cpp
		)
		add_test(NAME Vst3InstrumentTest COMMAND Vst3InstrumentTest)
		target_include_directories(Vst3InstrumentTest PRIVATE
			${CMAKE_SOURCE_DIR}/include
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect
			# tests/src/core/AllocationProbe.h - the allocation counter the
			# real-time assertion uses; the tests beside it include it by
			# name, so this test does too.
			${CMAKE_CURRENT_SOURCE_DIR}/src/core
		)
		target_compile_features(Vst3InstrumentTest PRIVATE cxx_std_20)
		target_link_libraries(Vst3InstrumentTest PRIVATE
			${QT_LIBRARIES}
			${QT_QTTEST_LIBRARY}
			lmms_vst3_sdk
			${CMAKE_DL_LIBS}
		)
		target_compile_definitions(Vst3InstrumentTest PRIVATE
			VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
		)
		add_dependencies(Vst3InstrumentTest vst3-test-instrument)

		# Product-level proof: a MidiClip drives the real vst3instrument MODULE
		# through the track's MIDI path, and the instrument's state survives the
		# project file. It needs the plug-in target, which exists only where VST3
		# hosting is enabled at all.
		if(TARGET vst3instrument)
			add_executable(Vst3InstrumentIntegrationTest
				src/plugins/Vst3InstrumentIntegrationTest.cpp
			)
			add_test(NAME Vst3InstrumentIntegrationTest
				COMMAND Vst3InstrumentIntegrationTest)
			# It constructs a full Engine and opens the instrument's generated
			# view, so it needs a Qt platform plugin even when headless.
			set_tests_properties(Vst3InstrumentIntegrationTest PROPERTIES
				ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
			target_include_directories(Vst3InstrumentIntegrationTest PRIVATE
				$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
			)
			target_static_libraries(Vst3InstrumentIntegrationTest PRIVATE lmmsobjs)
			target_link_libraries(Vst3InstrumentIntegrationTest PRIVATE
				${QT_LIBRARIES}
				${QT_QTTEST_LIBRARY}
				${CMAKE_DL_LIBS}
			)
			target_compile_features(Vst3InstrumentIntegrationTest PRIVATE cxx_std_20)
			target_compile_definitions(Vst3InstrumentIntegrationTest PRIVATE
				LMMS_TESTING
				VST3_INSTRUMENT_PLUGIN_PATH="$<TARGET_FILE:vst3instrument>"
				VST3_INSTRUMENT_PLUGIN_DIR="$<TARGET_FILE_DIR:vst3instrument>"
				VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
			)
			# The module resolves LMMS core symbols from the host process, exactly
			# like the real lmms binary (src/CMakeLists.txt sets ENABLE_EXPORTS ON)
			# and like PluginPortsMigrationTest does.
			set_target_properties(Vst3InstrumentIntegrationTest PROPERTIES
				ENABLE_EXPORTS ON)
			add_dependencies(Vst3InstrumentIntegrationTest
				vst3instrument
				vst3-test-instrument)
		endif()
endif()
endif()
# ---------------------------------------------------------------------------
# Agent control surface (SPEC A12/A16). The ControlRegistryTest unit test runs
# in the LMMS_TESTS loop above; this one starts the real binary with
# --control-socket under QT_QPA_PLATFORM=offscreen and drives it from an
# EXTERNAL Python client over the AF_UNIX socket, so the socket, the wire
# framing and the command groups are exercised end to end.
# ---------------------------------------------------------------------------
find_program(PYTHON3_EXECUTABLE NAMES python3)
# The control surface is POSIX-only: the server itself refuses with a typed error on Windows
# (ControlServer::listen's !Q_OS_UNIX branch), so its suite has no socket to drive there. The
# tests were registered on every platform and failed on Windows in 0.07 s each - run #34731085749
# (msvc-x64: 10 control-surface failures out of 14) - which reads as a defect and is a platform
# fact. Registered on UNIX only; the Windows refusal path is covered by the unit tests that need
# no socket. The suite stays ACTIVE everywhere it can run, including the hardware-less CI runners,
# where its failure mode is the real thing it exists to catch.
IF(WIN32)
	SET(CONTROL_SUITE_AVAILABLE FALSE)
ELSE()
	SET(CONTROL_SUITE_AVAILABLE TRUE)
ENDIF()

if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlSocketIntegration
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-socket-integration.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	set_tests_properties(ControlSocketIntegration PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# Session View milestone M1 (SPEC-zene-studio A1/A2, NEXT-0.3.0-AGENT-PROMPT
	# 3.2): a saved project launches 4 clips across 2 scenes in sync at the next
	# bar, driven END TO END through --control-socket. It is registered only
	# when the session.* group is compiled in, because the commands it drives do
	# not exist otherwise - a test that could not run must not be registered as
	# one that passed. It takes no fixture: it builds the session through the
	# socket, saves it, and reopens it, which is the point of the milestone.
	if(LMMS_HAVE_SESSION_VIEW)
		add_test(NAME ControlSessionLaunch
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-session-m1.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlSessionLaunch PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
	endif()
	# wasm.* (item #614): host the ABI example effect in the WASM DSP sandbox
	# END TO END through --control-socket, and read back what the ABI does with it.
	# Registered only when the sandbox is compiled in (WANT_WASM, which needs the
	# wasmtime C API on the find path - see CMakeLists.txt:957-963), because the
	# commands it drives do not exist otherwise: a test that could not run must not
	# be registered as one that passed. It takes no fixture beyond the committed
	# example module, tests/data/wasm-effect-abi/softclip.wat, which was written
	# from docs/WASM-EFFECT-ABI.md alone - so this is the first thing that ever
	# executes that document's claims.
	if(WANT_WASM)
		add_test(NAME ControlWasmSandbox
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-wasm-sandbox.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlWasmSandbox PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
	endif()
else()
	message(STATUS "python3 not found: ControlSocketIntegration is not registered")
endif()

# ---------------------------------------------------------------------------
# Task #626: the shutdown and readiness defects of the control surface.
#
# ControlShutdown         - a clean shutdown for BOTH reproductions (undo/redo
#                           leaves the project modified -> the quit prompt used to
#                           block; an audio device that cannot open -> the startup
#                           audio dialog used to block before app->exec()).
# ControlReadiness        - connects BEFORE readiness and asserts the documented
#                           order: connect -> poll control.ping -> wait for
#                           engine_ready -> issue engine commands.
# ControlNoAudioDevice    - a started instance with no usable audio device reports
#                           the reason instead of answering `busy` forever.
# ControlNegativeControl  - proves each of those can fail: the pre-fix artefacts
#                           and real SIGKILL/SIGSTOP symptoms must be detected, so
#                           none of the checks above is a tautology.
#
# All four are bounded by ctest's TIMEOUT and by deadlines inside each script: a
# hang counts as a failure, never as a wait.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlShutdown
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-shutdown.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	add_test(NAME ControlReadiness
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-readiness.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	add_test(NAME ControlNoAudioDevice
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-no-audio-device.py
			$<TARGET_FILE:zene>
	)
	add_test(NAME ControlNegativeControl
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-negative-control.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlShutdown ControlReadiness ControlNoAudioDevice
		ControlNegativeControl PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 420
	)
else()
	message(STATUS "python3 not found: the #626 shutdown/readiness tests are not registered")
endif()

# ---------------------------------------------------------------------------
# agent_surface (SPEC A15 / task #620). The anti-drift gate: it reflects the
# LIVE menu/toolbar surface over the control socket, fails on any action that
# has no registered command (grandfathering the ones that do not, in
# tests/agent-surface-baseline.txt, under a shrink-only ratchet), checks that
# every declared command is either swept headless or allowlisted with a reason,
# and then sweeps each non-allowlisted command to a typed result under
# QT_QPA_PLATFORM=offscreen with a throwaway HOME.
#
# The gate is a Python file rather than a test binary on purpose: the reflection
# it needs lives in the running process (control.surface_report walks the real
# MainWindow), so the test drives a real instance exactly like an agent would.
# --check is passed explicitly: this gate must never rewrite its own baseline
# during a test run; only a deliberate --reanchor "reason" does that.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	# -DZENE_TELEMETRY=OFF: the telemetry.* group is compiled out with the
	# client, so this binary has no telemetry.consent for the allowlist entry to
	# name. The gate is told which ids the configuration removed instead of
	# being handed a second allowlist, and it checks the flag in both
	# directions - a name that the registry DOES declare is a failure, so the
	# flag cannot be used to excuse a real command (docs/TELEMETRY-KILL-SWITCH.md).
	if(ZENE_TELEMETRY_ENABLED)
		set(ZENE_AGENT_SURFACE_COMPILED_OUT "")
	else()
		set(ZENE_AGENT_SURFACE_COMPILED_OUT --compiled-out telemetry.consent)
	endif()
	add_test(NAME agent_surface
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/agent-surface-gate.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
			--check
			--report ${CMAKE_CURRENT_BINARY_DIR}/agent-surface-report.json
			${ZENE_AGENT_SURFACE_COMPILED_OUT}
	)
	set_tests_properties(agent_surface PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
else()
	message(STATUS "python3 not found: agent_surface is not registered")
endif()

# The notes / clips / tracks command group edits real Song model objects
# (tracks, MidiClips, notes) rather than only reading them, so it needs a Qt
# platform plugin even though the assertions never open a window.
set_tests_properties(ControlEditCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The warp.* group edits a SampleClip created through track.add/clip.add, which
# also wants a real Qt platform plugin even though nothing opens a window.
set_tests_properties(ControlWarpCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The browser.* group: BrowserCatalogTest drives libsndfile and the config
# manager's working directory, ControlBrowserCommandsTest constructs a full
# Engine and drives project-free commands through the registry. Neither opens a
# window, both want a real Qt platform plugin.
set_tests_properties(BrowserCatalogTest ControlBrowserCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The transport.tempo_map_* group constructs a full Engine and drives the map
# through the registry (and the engine's undo stack), so it needs a Qt platform
# plugin even though nothing opens a window - the same reason as the two groups
# above. TempoMapTest additionally drives Song::processNextBuffer() itself, so it
# needs the same headless platform.
set_tests_properties(ControlTempoMapCommandsTest TempoMapPersistenceTest TempoMapTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The stable-id tests construct a full Engine and drive project.open/save through
# the control registry, so they need a Qt platform plugin even though nothing
# opens a window.
set_tests_properties(StableTrackIdsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# Headless load path (task #625, SPEC A13/A15): no modal dialog may block an
# instance started with --control-socket.
#
# Each test starts the real binary under QT_QPA_PLATFORM=offscreen, drives it
# from an external client, and treats a HANG as a failure: every wait is
# bounded. Each also has an `--expect-blocked` mode that asserts the pre-fix
# defect (the startup path parked inside a modal's nested event loop), so the
# tests cannot pass by accident. Reproductions against the base commit
# 6b01b98eb are recorded in the lane's evidence.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	foreach(HEADLESS_TEST
			"ControlHeadlessProjectOpen|control-headless-project-open.py|${CMAKE_SOURCE_DIR}/data/projects/tutorials/editing_note_volumes.mmp ${CMAKE_CURRENT_SOURCE_DIR}/data/error-carrying-project.mmp"
			"ControlHeadlessWorkingDirectory|control-headless-working-directory.py|"
			"ControlHeadlessNoAudioDevice|control-headless-no-audio-device.py|")
		string(REPLACE "|" ";" FIELDS "${HEADLESS_TEST}")
		list(GET FIELDS 0 TEST_NAME)
		list(GET FIELDS 1 TEST_SCRIPT)
		list(GET FIELDS 2 TEST_ARGS)
		separate_arguments(TEST_ARGS_LIST UNIX_COMMAND "${TEST_ARGS}")
		add_test(NAME ${TEST_NAME}
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/${TEST_SCRIPT}
				$<TARGET_FILE:zene>
				${TEST_ARGS_LIST}
		)
		set_tests_properties(${TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
	endforeach()
endif()

# ---------------------------------------------------------------------------
# ControlSocketPathSafety (data loss, 2026-09-12). `--control-socket <path>` used
# to UNLINK whatever was already at <path> before bind(), so pointing it at a
# project destroyed that project and said nothing (reproduced on the release
# configuration, commit ba24a9578). The test starts the real binary with the path
# already occupied - a regular file, a directory, a symlink, a stale socket - and
# asserts the typed refusal the process now writes to stderr (the surface's own
# wire shape, since an instance that refused to start has no socket to answer on),
# that the file is byte-identical afterwards, that a stale socket is still
# replaced, and that a request line over ControlServer::MaxRequestLineBytes is
# refused instead of buffering without bound.
# See docs/CONTROL-SOCKET-PATH-SAFETY.md.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlSocketPathSafety
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-socket-path-safety.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlSocketPathSafety PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()

# ---------------------------------------------------------------------------
# ControlAbsentSocket (0.2.1 coverage gap 3b). The release notes claim the
# control surface's opt-in is INVISIBLE: "an instance that was not started that
# way has no socket at all". Every other test in this directory starts the binary
# WITH `--control-socket`, so nothing checked the other direction.
#
# The test starts the real binary without the flag and watches what it holds:
# /proc/<pid>/fd against /proc/net/unix answers "which AF_UNIX sockets with a
# filesystem path does this process have open right now", sampled every 20 ms for
# a window measured from a positive control (the same binary WITH the flag, whose
# time-to-socket this test records first, so the window is not a guess), plus a
# recursive S_ISSOCK scan of the instance's own directory. It then asks the
# instance to quit (SIGINT, the product's documented shutdown request for a GUI
# run) and asserts the shutdown was clean: session marker removed, no crash
# report, the product's own SIGINT exit code.
#
# Registered where the OTHER python tests that spawn the binary are registered
# (python3 AND a UNIX-like platform): the script spawns `zene`, and the tag it
# checks is platform-independent, so nothing false is claimed by its absence on
# Windows - this test simply does not run there, and docs/STATUS.md's vocabulary
# is what keeps that visible.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlAbsentSocket
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-absent-socket.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlAbsentSocket PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()

# ---------------------------------------------------------------------------
# RenderSoftwareTag (0.2.1 coverage gap 3c). The release notes claim the WAV
# files this build renders carry `Zene Studio (libsndfile-…)` in their software
# tag "where they used to credit LMMS"; the only witness was a committed log of a
# render parsed by hand once (tests/integration-logs-3d/final/chunk-parse.log).
#
# This renders a short project headlessly with the real binary and reads the tag
# out of the output's LIST/INFO/ISFT chunk, with a method control (a WAV the
# script writes itself carrying the PRE-FIX tag must be parsed and REJECTED, so a
# parser that finds nothing cannot pass). LMMS_PLUGIN_DIR is passed explicitly so
# the render does not depend on the caller's environment. Gated like the other
# scripts that spawn the binary; see the script's own header for what it does NOT
# cover (the FLAC writer's twin call).
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME RenderSoftwareTag
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/render-software-tag.py
			$<TARGET_FILE:zene>
			${CMAKE_BINARY_DIR}/plugins
			${CMAKE_SOURCE_DIR}/data/projects/tutorials/editing_note_volumes.mmp
	)
	set_tests_properties(RenderSoftwareTag PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()
# ControlCommandsSnapshot - the MCP tooling spine's freshness ratchet.
#
# `tools/mcp-zene-control/zene_control/registry.py` generates the bridge's tool
# list from a live instance's `control.commands_list` AT REQUEST TIME, so a new
# command group is automatically an MCP tool with no per-feature bridge work.
# The mode with no live instance (and no cache) is served from the committed
# snapshot `zene_control/commands_snapshot.json`, and nothing enforced that the
# snapshot was fresh: tests/test_units.py asserted only that the file existed and
# held five sample ids. It had drifted to 70 commands while the registry declared
# 108 - 38 missing, every one a 0.3.0 feature - so tooling for the new work
# looked absent in exactly the mode a reader tries first.
#
# This test is that ratchet, in the ControlSessionLaunch mould: it starts
# $<TARGET_FILE:zene> with --control-socket, asks it for
# control.commands_list, loads the committed snapshot through the bridge's own
# loader (`zene_control.registry.load_bundle`) and fails on drift in EITHER
# direction, naming the ids and the exact regeneration command.
#
# Registered whenever python3 exists - the same gate as every other Python
# control-surface test here - and SKIPPED (exit 77 -> ctest "Skipped", never
# "Passed") when the bridge package itself cannot be imported, so it can never
# pass by doing nothing. There is no WANT_* option that gates the bridge: it is
# Python under tools/, not a build target.
#
# The three options that move a whole command group at compile time are passed in
# as prefixes, so a non-release configuration is compared against the ids it could
# actually register instead of being failed for the options it chose:
# WANT_SESSION_VIEW=OFF drops the session.* group and ZENE_TELEMETRY=OFF drops
# telemetry.* (both `--compiled-out`, because the snapshot was captured from the
# release configuration, which has them), and WANT_WASM=ON with the wasmtime C API
# on the find path ADDS the wasm.* group (item #614) - six ids the release snapshot
# cannot carry, because the snapshot is one file and a configuration is not, so
# that one is `--compiled-in`. Every flag is checked in BOTH directions - a prefix
# declared compiled out that the binary DOES register is a failure, and one
# declared compiled in that the binary does NOT register (or that the snapshot
# already carries) is a failure too - so no flag can become a blindfold.
# SESSION_VIEW and TELEMETRY default ON (the configuration the snapshot was
# captured from); WANT_WASM defaults ON but degrades to OFF without the wasmtime C
# API, which is every CI job, so on a default build the sandbox's ids are absent
# on both sides and this test compares the release surface in full.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE)
	set(ZENE_SNAPSHOT_COMPILED_OUT "")
	if(NOT LMMS_HAVE_SESSION_VIEW)
		list(APPEND ZENE_SNAPSHOT_COMPILED_OUT --compiled-out session.)
	endif()
	if(NOT ZENE_TELEMETRY_ENABLED)
		list(APPEND ZENE_SNAPSHOT_COMPILED_OUT --compiled-out telemetry.)
	endif()
	set(ZENE_SNAPSHOT_COMPILED_IN "")
	if(WANT_WASM)
		list(APPEND ZENE_SNAPSHOT_COMPILED_IN --compiled-in wasm.)
	endif()
	add_test(NAME ControlCommandsSnapshot
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-commands-snapshot.py
			$<TARGET_FILE:zene>
			${ZENE_SNAPSHOT_COMPILED_OUT}
			${ZENE_SNAPSHOT_COMPILED_IN}
	)
	set_tests_properties(ControlCommandsSnapshot PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
		SKIP_RETURN_CODE 77
	)
else()
	message(STATUS "python3 not found: ControlCommandsSnapshot is not registered")
endif()
