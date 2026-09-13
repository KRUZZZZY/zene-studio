# The VST3 test EFFECT: the Steinberg SDK's own `again_sampleaccurate` example,
# built from the pinned MIT SDK checkout (cmake/modules/Vst3Sdk.cmake) as a real
# .vst3 bundle. It is the subject of Vst3BusMapTest, Vst3HostTest and
# Vst3EffectIntegrationTest - the VST3 twins of the CLAP tests, whose subject is
# the in-tree tests/data/clap-test-plugin.
#
# WHY THE SDK'S OWN SAMPLE RATHER THAN AN IN-TREE PLUG-IN
# The three tests were written against a two-parameter gain effect with a stepped
# Bypass and a continuous Gain and assert the plug-in's own identity (class
# "AGain Sample Accurate", vendor "Steinberg") and its exact parameter shape
# (Vst3HostTest.cpp:47-53, :120-140). An in-tree stand-in would have to claim
# another vendor's identity to satisfy them, which would make the fixture lie
# rather than the test prove anything; so the fixture is the sample the tests
# were written for, taken from the same pinned checkout the product's own
# lmms_vst3_sdk target is built from. Nothing here is vendored: with no SDK
# checkout at LMMS_VST3_SDK_PATH the whole block is skipped by the caller.
#
# The bundle is NEVER installed - it is a test subject, not a product plug-in,
# like tests/data/vst3-test-instrument and tests/data/clap-test-plugin.
#
# Two targets, mirroring tests/data/vst3-test-instrument/CMakeLists.txt:
#   vst3-test-effect-sdkbase  every THIRD-PARTY translation unit the module
#                             needs (the sample plus the plug-in side of the
#                             SDK), compiled the way this tree compiles
#                             third-party code (SYSTEM, see ErrorFlags.cmake)
#   vst3-test-effect          the plug-in MODULE, laid out as the SDK loader
#                             expects it

# The VST3 bundle on Linux is a DIRECTORY:
#   <name>.vst3/Contents/<uname -m>-linux/<name>.so
# The SDK's loader insists on that layout and derives the .so name from the
# bundle directory stem (public.sdk/source/vst/hosting/module_linux.cpp:129-152).
execute_process(COMMAND uname -m
	OUTPUT_VARIABLE LMMS_VST3_TEST_MACHINE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT LMMS_VST3_TEST_MACHINE)
	set(LMMS_VST3_TEST_MACHINE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(LMMS_VST3_TEST_EFFECT_NAME "vst3-test-effect")
set(LMMS_VST3_TEST_EFFECT_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_TEST_EFFECT_NAME}.vst3")
set(LMMS_VST3_TEST_EFFECT_BUNDLE_SO
	"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux/${LMMS_VST3_TEST_EFFECT_NAME}.so")

# --- the sample's generated version header --------------------------------
# The sample's version.h includes "projectversion.h", a file the SDK's own build
# generates from cmake/templates/projectversion.h.in with the version of the
# CMake project (SMTG_Bundle.cmake, smtg_target_configure_version_file). The SDK
# is built here as a subset of plain sources, with no add_subdirectory, so
# nothing generates it for us; it is generated here from the SDK's OWN template
# with the version read out of the SDK checkout's own CMakeLists.txt, so the
# macro set is the one the SDK's build would produce. The value reaches
# FULL_VERSION_STR/VERSION_STR in the sample's factory registration only - no
# assertion in any test reads it.
set(LMMS_VST3_TEST_VERSION_MAJOR 0)
set(LMMS_VST3_TEST_VERSION_MINOR 0)
set(LMMS_VST3_TEST_VERSION_PATCH 0)
file(READ "${LMMS_VST3_SDK_PATH}/CMakeLists.txt" LMMS_VST3_TEST_ROOT_CMAKE)
if(LMMS_VST3_TEST_ROOT_CMAKE MATCHES
		"project\\(vstsdk[ 	\r\n]+VERSION[ 	]+([0-9]+)\\.([0-9]+)(\\.([0-9]+))?")
	set(LMMS_VST3_TEST_VERSION_MAJOR "${CMAKE_MATCH_1}")
	set(LMMS_VST3_TEST_VERSION_MINOR "${CMAKE_MATCH_2}")
	if(CMAKE_MATCH_4)
		set(LMMS_VST3_TEST_VERSION_PATCH "${CMAKE_MATCH_4}")
	endif()
endif()
set(PROJECT_VERSION_MAJOR "${LMMS_VST3_TEST_VERSION_MAJOR}")
set(PROJECT_VERSION_MINOR "${LMMS_VST3_TEST_VERSION_MINOR}")
set(PROJECT_VERSION_PATCH "${LMMS_VST3_TEST_VERSION_PATCH}")
set(PROJECT_VERSION_TWEAK "0")
set(PROJECT_VERSION
	"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
set(SMTG_PROJECT_VERSION
	"${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
configure_file(
	"${LMMS_VST3_SDK_PATH}/cmake/templates/projectversion.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/projectversion.h")

# --- the sample, from the pinned checkout ----------------------------------
set(LMMS_VST3_SAMPLE_DIR
	"${LMMS_VST3_SDK_PATH}/public.sdk/samples/vst/again_sampleaccurate/source")

# --- third-party translation units -----------------------------------------
# The module side of the SDK. An ordinary plug-in links the SDK's own `sdk` and
# `sdk_common` libraries; this repository compiles only the HOSTING subset into
# lmms_vst3_sdk, so the plug-in-side list comes from the SDK's own
# cmake/modules/SMTG_VST3_SDK.cmake (smtg_create_public_sdk_target, the subset
# the sample's sources actually reference) plus the module entry point the
# SDK's own smtg_add_vst3plugin adds.
#
# An OBJECT library, not a STATIC one, on purpose: a .vst3 module is loaded by
# dlopen, so nothing inside it references ModuleEntry/ModuleExit and a static
# library would have linuxmain.cpp.o dropped by the linker - the module would
# then fail with "does not export the required 'ModuleEntry' function".
add_library(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SAMPLE_DIR}/agsa_controller.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa_factory.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa_processor.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa.h"
	"${LMMS_VST3_SAMPLE_DIR}/tutorial.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/tutorial.h"
	"${LMMS_VST3_SAMPLE_DIR}/version.h"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/utility/testing.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/utility/test/sampleaccuratetest.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/utility/test/rttransfertest.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstaudioeffect.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstbus.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstcomponent.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstcomponentbase.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vsteditcontroller.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstparameters.cpp"
)
target_link_libraries(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase PUBLIC lmms_vst3_sdk)
# the generated projectversion.h above (the sample's version.h includes it)
target_include_directories(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase PRIVATE
	"${CMAKE_CURRENT_BINARY_DIR}")
target_compile_features(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase PUBLIC cxx_std_17)
set_target_properties(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	SYSTEM ON)

# --- the bundle -------------------------------------------------------------
# Every object here is third-party, so the module target compiles nothing of its
# own and carries no warning policy of its own.
add_library(${LMMS_VST3_TEST_EFFECT_NAME} MODULE
	$<TARGET_OBJECTS:${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase>)
set_target_properties(${LMMS_VST3_TEST_EFFECT_NAME} PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${LMMS_VST3_TEST_EFFECT_NAME}")
target_link_libraries(${LMMS_VST3_TEST_EFFECT_NAME} PRIVATE lmms_vst3_sdk)

# Lay the module out as the SDK's loader expects it.
add_custom_command(TARGET ${LMMS_VST3_TEST_EFFECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_TEST_EFFECT_NAME}>"
		"${LMMS_VST3_TEST_EFFECT_BUNDLE_SO}"
	COMMENT "Laying out the ${LMMS_VST3_TEST_EFFECT_NAME}.vst3 bundle")

# The tests live in ../.. (tests/CMakeLists.txt) and need this path; variables do
# not travel up a directory level on their own. Same contract as
# tests/data/vst3-test-instrument/CMakeLists.txt's LMMS_VST3_TEST_BUNDLE_DIR.
set(LMMS_VST3_TEST_EFFECT_BUNDLE_DIR "${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}" PARENT_SCOPE)
