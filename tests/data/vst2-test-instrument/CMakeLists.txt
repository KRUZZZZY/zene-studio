# Q050-481.1: the VST2 test instrument, the subject of CarlaVst2HostingTest.
#
# One C source (no Steinberg header: zene_vst2_abi.h restates the clean-room
# VeSTige layout), built twice:
#   vst2-test-instrument       the native module, loaded by Carla in process
#                              -> ${CMAKE_CURRENT_BINARY_DIR}/native/
#   vst2-test-instrument-wine  a Windows PE .dll built with winegcc, which Carla
#                              can only reach through its Wine bridge
#                              -> ${CMAKE_CURRENT_BINARY_DIR}/wine/
# Each output directory holds that one plug-in, so a test can point the VST2
# search path (ZENE_VST2_PATH) at exactly the fixture it means. Never installed.
#
# The PE build is best effort: winegcc needs a PE cross toolchain behind it
# (clang + lld, or mingw-w64). Where it cannot build, the .dll is simply absent
# and the bridged half of the test reports that it has no fixture.

set(LMMS_VST2_TEST_NATIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/native" PARENT_SCOPE)
set(LMMS_VST2_TEST_WINE_DIR "${CMAKE_CURRENT_BINARY_DIR}/wine" PARENT_SCOPE)

add_library(vst2-test-instrument MODULE vst2-test-instrument.c)
set_target_properties(vst2-test-instrument PROPERTIES
	PREFIX ""
	OUTPUT_NAME "ZeneVst2TestInstrument"
	C_VISIBILITY_PRESET hidden
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native")

find_program(LMMS_VST2_TEST_WINEGCC winegcc)
if(LMMS_VST2_TEST_WINEGCC AND NOT WIN32 AND NOT APPLE)
	add_custom_target(vst2-test-instrument-wine
		COMMAND ${CMAKE_COMMAND}
			-DWINEGCC=${LMMS_VST2_TEST_WINEGCC}
			-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/vst2-test-instrument.c
			-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wine/ZeneVst2TestInstrument.dll
			-P ${CMAKE_CURRENT_SOURCE_DIR}/BuildWineFixture.cmake
		COMMENT "Building the Windows (PE) VST2 test instrument with winegcc"
		VERBATIM)
endif()

# ci-d14f7617bae6160d: the hosted macos-x86_64 build failed because the fixture's
# _POSIX_C_SOURCE 199309L hid Darwin's C99 snprintf declaration. No macOS SDK exists on
# every builder, so the check compiles the fixture's real feature-test preamble with the
# C preprocessor and asserts the Darwin feature level (see darwin-feature-level-check.py).
find_program(ZENE_VST2_PYTHON3 NAMES python3 python3.exe python)
if(ZENE_VST2_PYTHON3)
	add_test(NAME Vst2FixtureDarwinFeatureLevel
		COMMAND ${ZENE_VST2_PYTHON3}
			${CMAKE_CURRENT_SOURCE_DIR}/darwin-feature-level-check.py
			${CMAKE_C_COMPILER}
			"${CMAKE_C_COMPILER_ID}"
			"${CMAKE_C_COMPILER_FRONTEND_VARIANT}")
	set_tests_properties(Vst2FixtureDarwinFeatureLevel PROPERTIES TIMEOUT 120)
endif()

# ci-d14f7617bae6160d: the ZNV2 chunk contract checked directly - the written text, the
# parse, the 128-byte read guard and the rejection of malformed input. Carla only ever
# hands a host well-formed chunks, so those branches are unreachable through
# CarlaVst2HostingTest.
add_executable(vst2-chunk-contract-check chunk-contract-check.c)
add_test(NAME Vst2ChunkContractCheck COMMAND vst2-chunk-contract-check)
