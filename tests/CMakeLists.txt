include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AudioBusHandleTest.cpp
	src/core/AudioBusTest.cpp
	src/core/AudioPortsModelTest.cpp
	src/core/AudioPortsTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/MidiProbabilityPersistenceTest.cpp
	src/core/MultiTrackRecorderTest.cpp
	src/core/NoteRandomTest.cpp
	src/core/NoteTransformTest.cpp
	src/core/PluginAudioPortsTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RecordRingBufferTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/RemotePluginAudioPortsTest.cpp
	src/core/RemotePluginClientE2ETest.cpp
	src/core/RoutingGraphTest.cpp
	src/core/PdcMixerTest.cpp
	src/core/ScriptBindingsTest.cpp
	src/core/ScriptEngineTest.cpp
	src/core/SlideNotesTest.cpp
	src/core/TimelineTest.cpp
	src/core/TwoTrackRecordingHarness.cpp
	src/tracks/AutomationTrackTest.cpp
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
	# binary must export its symbols exactly like the lmms executable does.
	if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
	endif()
endforeach()

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
	target_link_libraries(partc_ref_${PLUGIN_ID} PRIVATE lmms ${QT_LIBRARIES})
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
	# like the real lmms binary (src/CMakeLists.txt sets ENABLE_EXPORTS ON).
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

# Instrument the plugin/gate test executables as well - they compile fork
# sources directly, so their objects carry the coverage the ratchet tracks.
# PluginPortsMigrationReference is deliberately left un-instrumented: the
# reference side of the behaviour comparison must stay plain.
foreach(LMMS_GATE_TEST ClapBusMapTest ClapHostTest ClapEffectIntegrationTest
		WasmSandboxTest PluginPortsMigrationTest AudioPluginTest
		synthetic_audio_plugin)
	if(TARGET ${LMMS_GATE_TEST})
		lmms_add_coverage(${LMMS_GATE_TEST})
	endif()
endforeach()

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
# this target failed to link there while linking only Qt. Real plugins link the lmms
# target (cmake/modules/BuildPlugin.cmake), and the partc_ref_* harnesses above were
# fixed the same way — this target was the one left behind.
target_link_libraries(synthetic_audio_plugin PRIVATE lmms ${QT_LIBRARIES})
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
