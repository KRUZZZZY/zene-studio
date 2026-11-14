# Q050-481.2: the VST2 test effect, the subject of CarlaVst2EffectHostingTest.
#
# One C source (no Steinberg header: it shares the clean-room ABI restatement
# ../vst2-test-instrument/zene_vst2_abi.h), built twice, as the test instrument is:
#   vst2-test-effect       the native module, loaded by Carla in process
#                          -> ${CMAKE_CURRENT_BINARY_DIR}/native/
#   vst2-test-effect-wine  a Windows PE .dll built with winegcc, which Carla can
#                          only reach through its Wine bridge
#                          -> ${CMAKE_CURRENT_BINARY_DIR}/wine/
# The PE build is best effort (../vst2-test-instrument/BuildWineFixture.cmake):
# where winegcc cannot build, the .dll is absent and the bridged cases say so.
# Never installed.

set(LMMS_VST2_TEST_EFFECT_NATIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/native" PARENT_SCOPE)
set(LMMS_VST2_TEST_EFFECT_WINE_DIR "${CMAKE_CURRENT_BINARY_DIR}/wine" PARENT_SCOPE)

add_library(vst2-test-effect MODULE vst2-test-effect.c)
target_include_directories(vst2-test-effect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../vst2-test-instrument")
set_target_properties(vst2-test-effect PROPERTIES
	PREFIX ""
	OUTPUT_NAME "ZeneVst2TestEffect"
	C_VISIBILITY_PRESET hidden
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native")

find_program(LMMS_VST2_TEST_WINEGCC winegcc)
if(LMMS_VST2_TEST_WINEGCC AND NOT WIN32 AND NOT APPLE)
	add_custom_target(vst2-test-effect-wine
		COMMAND ${CMAKE_COMMAND}
			-DWINEGCC=${LMMS_VST2_TEST_WINEGCC}
			-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/vst2-test-effect.c
			-DINCLUDE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../vst2-test-instrument
			-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/wine/ZeneVst2TestEffect.dll
			-P ${CMAKE_CURRENT_SOURCE_DIR}/../vst2-test-instrument/BuildWineFixture.cmake
		COMMENT "Building the Windows (PE) VST2 test effect with winegcc"
		VERBATIM)
endif()
