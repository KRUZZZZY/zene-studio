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

# A VST3 module is a DIRECTORY, laid out per platform the way the SDK's own
# loader (public.sdk/source/vst/hosting/module_<platform>) looks for it, and
# entered through the SDK's per-platform entry file. Q050-490 adds the Windows
# and macOS halves, so the editor tests run on those hosted jobs too; the recipe
# is tests/data/vst3-test-effect/CMakeLists.txt's, whose header records the
# hosted-CI failures each part of it answers:
#   Linux    <name>.vst3/Contents/<uname -m>-linux/<name>.so      linuxmain.cpp
#            (module_linux.cpp:getSOPath returns nothing for a non-directory)
#   Windows  <name>.vst3/Contents/<arch>-win/<name>.vst3 (a DLL)  dllmain.cpp
#            (module_win32.cpp:loadAsPackage; InitDll/ExitDll)
#   macOS    <name>.vst3/Contents/MacOS/<name> + Contents/Info.plist   macmain.cpp
#            (module_mac.mm: CFBundleCreate + CFBundleLoadExecutable, bundleEntry)
execute_process(COMMAND uname -m
	OUTPUT_VARIABLE LMMS_VST3_TEST_MACHINE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT LMMS_VST3_TEST_MACHINE)
	set(LMMS_VST3_TEST_MACHINE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(APPLE)
	set(LMMS_VST3_TEST_ENTRY "macmain.cpp")
	set(LMMS_VST3_TEST_BUNDLE_SUBDIR "Contents/MacOS")
	set(LMMS_VST3_TEST_BUNDLE_SUFFIX "")
elseif(WIN32)
	# The arch directory is the TARGET's (what the loader compiles in), so ask the
	# compiler, as the effect fixture does (MSYS2's uname -m answers x86_64 on arm64).
	include(CheckCXXSourceCompiles)
	check_cxx_source_compiles("
#if !(defined(_M_ARM64) || defined(__aarch64__)) || defined(_M_ARM64EC)
#error not a native ARM64 target
#endif
int main() { return 0; }" LMMS_VST3_INSTRUMENT_FIXTURE_TARGET_ARM64)
	if(LMMS_VST3_INSTRUMENT_FIXTURE_TARGET_ARM64)
		set(LMMS_VST3_TEST_BUNDLE_SUBDIR "Contents/arm64-win")
	elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(LMMS_VST3_TEST_BUNDLE_SUBDIR "Contents/x86_64-win")
	else()
		set(LMMS_VST3_TEST_BUNDLE_SUBDIR "Contents/x86-win")
	endif()
	set(LMMS_VST3_TEST_ENTRY "dllmain.cpp")
	# A Windows VST3 module is a PE DLL under the .vst3 extension, named after the bundle directory.
	set(LMMS_VST3_TEST_BUNDLE_SUFFIX ".vst3")
else()
	set(LMMS_VST3_TEST_ENTRY "linuxmain.cpp")
	set(LMMS_VST3_TEST_BUNDLE_SUBDIR "Contents/${LMMS_VST3_TEST_MACHINE}-linux")
	set(LMMS_VST3_TEST_BUNDLE_SUFFIX ".so")
endif()
if(NOT EXISTS "${LMMS_VST3_SDK_PATH}/public.sdk/source/main/${LMMS_VST3_TEST_ENTRY}")
	message(FATAL_ERROR "The VST3 instrument fixture needs the SDK's ${CMAKE_SYSTEM_NAME} entry point "
		"public.sdk/source/main/${LMMS_VST3_TEST_ENTRY}, which is not in ${LMMS_VST3_SDK_PATH}.")
endif()

set(LMMS_VST3_TEST_INSTRUMENT_NAME "vst3-test-instrument")
set(LMMS_VST3_TEST_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_TEST_INSTRUMENT_NAME}.vst3")

set(LMMS_VST3_MONO_TEST_INSTRUMENT_NAME "vst3-test-mono-instrument")
set(LMMS_VST3_MONO_TEST_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_MONO_TEST_INSTRUMENT_NAME}.vst3")

# Cluster stress-3: the three-channel (L R C) output variant, an odd channel count.
set(LMMS_VST3_THREE_TEST_INSTRUMENT_NAME "vst3-test-three-channel-instrument")
set(LMMS_VST3_THREE_TEST_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME}.vst3")
set(LMMS_VST3_THREE_TEST_BUNDLE_SO
	"${LMMS_VST3_THREE_TEST_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux/${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME}.so")

# --- plug-in-side SDK sources (third-party) --------------------------------
# Compiled separately from the fixture's own source so that the fixture stays
# under the repository's own warning policy while the SDK is compiled the way
# every other third-party tree here is (ErrorFlags.cmake keys off SYSTEM).
#
# An OBJECT library, not a STATIC one, on purpose: a .vst3 module is loaded by
# dlopen, so nothing inside it references ModuleEntry/ModuleExit. In a static
# library the linker would therefore drop linuxmain.cpp.o entirely and the
# module would load with "does not export the required 'ModuleEntry' function".
# Object libraries are always linked whole. (The same holds for InitDll and
# bundleEntry, the Windows and macOS entries.)
add_library(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/${LMMS_VST3_TEST_ENTRY}"
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
if(MSVC)
	# As for lmms_vst3_sdk (cmake/modules/Vst3Sdk.cmake): the SDK picks its C++ level
	# from __cplusplus, which MSVC reports as 199711L without this flag. PUBLIC, so the
	# fixture's own source (which includes the same SDK headers) agrees with it.
	target_compile_options(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase PUBLIC "/Zc:__cplusplus")
	# The fixture reads its test switches with std::getenv; that is what it is for.
	target_compile_definitions(${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase PUBLIC _CRT_SECURE_NO_WARNINGS)
endif()

#! One fixture module, laid out at @a bundle as its platform's loader expects it - the
#! Linux tree, the Windows package, or the CFBundle with the Info.plist that names it.
function(lmms_vst3_test_module name bundle)
	add_library(${name} MODULE vst3-test-instrument.cpp)
	set_target_properties(${name} PROPERTIES
		PREFIX ""
		OUTPUT_NAME "${name}"
	)
	target_link_libraries(${name} PRIVATE ${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase)
	set(steps
		COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}/${LMMS_VST3_TEST_BUNDLE_SUBDIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:${name}>"
			"${bundle}/${LMMS_VST3_TEST_BUNDLE_SUBDIR}/${name}${LMMS_VST3_TEST_BUNDLE_SUFFIX}")
	if(APPLE)
		set(LMMS_VST3_TEST_PLIST_NAME "${name}")
		configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
			"${CMAKE_CURRENT_BINARY_DIR}/${name}-Info.plist" @ONLY)
		list(APPEND steps
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${CMAKE_CURRENT_BINARY_DIR}/${name}-Info.plist"
				"${bundle}/Contents/Info.plist")
	endif()
	add_custom_command(TARGET ${name} POST_BUILD ${steps}
		COMMENT "Laying out the ${name}.vst3 bundle")
endfunction()

# --- the fixture ------------------------------------------------------------
lmms_vst3_test_module(${LMMS_VST3_TEST_INSTRUMENT_NAME} "${LMMS_VST3_TEST_BUNDLE_DIR}")
lmms_vst3_test_module(${LMMS_VST3_MONO_TEST_INSTRUMENT_NAME} "${LMMS_VST3_MONO_TEST_BUNDLE_DIR}")
target_compile_definitions(${LMMS_VST3_MONO_TEST_INSTRUMENT_NAME} PRIVATE
	ZENE_TEST_INSTRUMENT_MONO)

add_library(${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME} MODULE
	vst3-test-instrument.cpp
)
set_target_properties(${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME} PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME}"
)
target_compile_definitions(${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME} PRIVATE
	ZENE_TEST_INSTRUMENT_THREE_CHANNEL)
target_link_libraries(${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME} PRIVATE
	${LMMS_VST3_TEST_INSTRUMENT_NAME}-sdkbase)
add_custom_command(TARGET ${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_THREE_TEST_BUNDLE_DIR}/Contents/${LMMS_VST3_TEST_MACHINE}-linux"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME}>"
		"${LMMS_VST3_THREE_TEST_BUNDLE_SO}"
	COMMENT "Laying out the ${LMMS_VST3_THREE_TEST_INSTRUMENT_NAME}.vst3 bundle")

# The probe lives in ../.. (tests/CMakeLists.txt) so that it sits with the other
# plug-in test executables; it needs this path, and variables do not travel up a
# directory level on their own.
set(LMMS_VST3_TEST_BUNDLE_DIR "${LMMS_VST3_TEST_BUNDLE_DIR}" PARENT_SCOPE)
set(LMMS_VST3_MONO_TEST_BUNDLE_DIR "${LMMS_VST3_MONO_TEST_BUNDLE_DIR}" PARENT_SCOPE)
set(LMMS_VST3_THREE_TEST_BUNDLE_DIR "${LMMS_VST3_THREE_TEST_BUNDLE_DIR}" PARENT_SCOPE)
