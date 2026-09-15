# The VST3 chunk probe: a purpose-built, in-tree MIT fixture that witnesses the
# host's chunking contract (CODE-4) - what block sizes the host asked it to
# process, and whether any call exceeded the block size setupProcessing()
# declared.
#
# It is the subject of Vst3ChunkProbeTest. It mirrors
# tests/data/clap-test-plugin/clap-test-gain.c for the CLAP host and
# tests/data/vst3-test-instrument/CMakeLists.txt for the layout: two targets,
# the fixture MODULE and the plug-in-side SDK sources it needs, laid out as the
# SDK loader expects. It is NEVER installed - a test subject, not a product
# plug-in.
#
# An ordinary plug-in links the SDK's own sdk/sdk_common libraries. This
# repository compiles only the HOSTING subset into lmms_vst3_sdk, so the
# plug-in-side translation units are listed here instead; both sets come from
# the SDK's cmake/modules/SMTG_VST3_SDK.cmake (smtg_create_public_sdk_target).

execute_process(COMMAND uname -m
	OUTPUT_VARIABLE LMMS_VST3_TEST_MACHINE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT LMMS_VST3_TEST_MACHINE)
	set(LMMS_VST3_TEST_MACHINE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(LMMS_VST3_CHUNK_PROBE_NAME "vst3-chunk-probe")
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_CHUNK_PROBE_NAME}.vst3")
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SO
	"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux/${LMMS_VST3_CHUNK_PROBE_NAME}.so")

# --- plug-in-side SDK sources (third-party) --------------------------------
# An OBJECT library, not a STATIC one, on purpose: a .vst3 module is loaded by
# dlopen, so nothing inside it references ModuleEntry/ModuleExit. In a static
# library the linker would therefore drop linuxmain.cpp.o entirely and the
# module would load with "does not export the required 'ModuleEntry' function".
# Object libraries are always linked whole.
add_library(${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstbus.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstcomponentbase.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstparameters.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
)
target_link_libraries(${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase PUBLIC lmms_vst3_sdk)
target_compile_features(${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase PUBLIC cxx_std_17)
set_target_properties(${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	SYSTEM ON)

# --- the fixture ------------------------------------------------------------
add_library(${LMMS_VST3_CHUNK_PROBE_NAME} MODULE
	vst3-chunk-probe.cpp
)
set_target_properties(${LMMS_VST3_CHUNK_PROBE_NAME} PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${LMMS_VST3_CHUNK_PROBE_NAME}"
)
target_link_libraries(${LMMS_VST3_CHUNK_PROBE_NAME} PRIVATE
	${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase)

# Lay the module out as the SDK's loader expects it.
add_custom_command(TARGET ${LMMS_VST3_CHUNK_PROBE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_CHUNK_PROBE_NAME}>"
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_SO}"
	COMMENT "Laying out the ${LMMS_VST3_CHUNK_PROBE_NAME}.vst3 bundle")

# The test lives in ../.. (tests/CMakeLists.txt) so that it sits with the other
# plug-in test executables; it needs this path, and variables do not travel up a
# directory level on their own.
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR "${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}" PARENT_SCOPE)
