# The VST3 test instrument: a real VST3 module built from the pinned MIT VST3
# SDK (cmake/modules/Vst3Sdk.cmake), used as the subject of
# Vst3InstrumentFixtureProbe and of the instrument-hosting lane's host tests.
#
# It mirrors tests/data/clap-test-plugin for the CLAP host: a purpose-built,
# deterministic, in-tree plug-in that exists so the host can be exercised
# without a third-party binary. It is opt-in (WANT_VST3_TEST_INSTRUMENT, tested
# in ../CMakeLists.txt) and is NEVER installed - it is a test subject, not a
# product plug-in. With the option OFF an ordinary build is unchanged.
#
# Two targets:
#   vst3-test-instrument          the plug-in MODULE (the subject)
#   vst3-test-instrument-sdkbase  the plug-in-side SDK sources it needs, which
#                                 lmms_vst3_sdk (the host-side subset) does not
#                                 already compile
#
# An ordinary plug-in links the SDK's sdk/sdk_common libraries. This repository
# compiles only the HOSTING subset into lmms_vst3_sdk, so the plug-in-side
# translation units are listed here instead. Both sets come from the SDK's own
# cmake/modules/SMTG_VST3_SDK.cmake (smtg_create_public_sdk_target).

# The VST3 bundle on Linux is a DIRECTORY:
#   <name>.vst3/Contents/<uname -m>-linux/<name>.so
# The SDK's loader insists on that layout
# (public.sdk/source/vst/hosting/module_linux.cpp:getSOPath returns nothing at
# all for anything that is not a directory).
execute_process(COMMAND uname -m
	OUTPUT_VARIABLE LMMS_VST3_TEST_MACHINE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT LMMS_VST3_TEST_MACHINE)
	set(LMMS_VST3_TEST_MACHINE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(LMMS_VST3_TEST_INSTRUMENT_NAME "vst3-test-instrument")
set(LMMS_VST3_TEST_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_TEST_INSTRUMENT_NAME}.vst3")
set(LMMS_VST3_TEST_BUNDLE_SO
	"${LMMS_VST3_TEST_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux/${LMMS_VST3_TEST_INSTRUMENT_NAME}.so")

# --- plug-in-side SDK sources (third-party) --------------------------------
# Compiled separately from the fixture's own source so that the fixture stays
# under the repository's own warning policy while the SDK is compiled the way
# every other third-party tree here is (ErrorFlags.cmake keys off SYSTEM).
#
# An OBJECT library, not a STATIC one, on purpose: a .vst3 module is loaded by
# dlopen, so nothing inside it references ModuleEntry/ModuleExit. In a static
# library the linker would therefore drop linuxmain.cpp.o entirely and the
# module would load with "does not export the required 'ModuleEntry' function".
# Object libraries are always linked whole.
add_library(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstbus.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstcomponentbase.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstparameters.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
)
target_link_libraries(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase PUBLIC lmms_vst3_sdk)
target_compile_features(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase PUBLIC cxx_std_17)
set_target_properties(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	SYSTEM ON)

# --- the fixture ------------------------------------------------------------
add_library(${LMMS_VST3_TEST_INSTRUMENT_NAME} MODULE
	vst3-test-instrument.cpp
)
set_target_properties(${LMMS_VST3_TEST_INSTRUMENT_NAME} PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${LMMS_VST3_TEST_INSTRUMENT_NAME}"
)
target_link_libraries(${LMMS_VST3_TEST_INSTRUMENT_NAME} PRIVATE
	${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase)

# Lay the module out as the SDK's loader expects it.
add_custom_command(TARGET ${LMMS_VST3_TEST_INSTRUMENT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_TEST_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_TEST_INSTRUMENT_NAME}>"
		"${LMMS_VST3_TEST_BUNDLE_SO}"
	COMMENT "Laying out the ${LMMS_VST3_TEST_INSTRUMENT_NAME}.vst3 bundle")

# The probe lives in ../.. (tests/CMakeLists.txt) so that it sits with the other
# plug-in test executables; it needs this path, and variables do not travel up a
# directory level on their own.
set(LMMS_VST3_TEST_BUNDLE_DIR "${LMMS_VST3_TEST_BUNDLE_DIR}" PARENT_SCOPE)
