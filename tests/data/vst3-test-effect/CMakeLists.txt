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

# The VST3 bundle is a DIRECTORY, and its layout AND its entry point are the
# loader's contract, not ours. Three loaders, three layouts, three entry-point
# translation units - each stated with the file in the pinned SDK that demands it:
#
#   Linux    <name>.vst3/Contents/<uname -m>-linux/<name>.so
#            entry point: public.sdk/source/main/linuxmain.cpp - exports
#            ModuleEntry/ModuleExit, resolved by dlsym in
#            module_linux.cpp:172-177 ("The shared library does not export the
#            required 'ModuleEntry' function" is what its absence says).
#   macOS    <name>.vst3/Contents/MacOS/<name>  +  Contents/Info.plist
#            entry point: public.sdk/source/main/macmain.cpp - exports
#            bundleEntry/bundleExit, looked up with
#            CFBundleGetFunctionPointerForName in module_mac.mm:117-121. The
#            loader itself is CoreFoundation: CFBundleCreate(url) +
#            CFBundleLoadExecutableAndReturnError(bundle), and CFBundle looks for
#            the executable its Info.plist NAMES under Contents/MacOS - the layout
#            SMTG_Bundle.cmake's own `BUNDLE TRUE` + `BUNDLE_EXTENSION vst3`
#            produce for the SDK's samples.
#            Two macOS failures in this tree, in order: laying the fixture out in
#            the -linux location made both macOS jobs report "The bundle ...
#            couldn't be loaded because its executable couldn't be located"; the
#            layout was fixed and BOTH jobs then reported "Bundle does not export
#            the required 'bundleEntry' function" (jobs 103724228405 and
#            103724228370, run 34757467632) - because linuxmain.cpp was still the
#            only entry point compiled, so the symbol CoreFoundation was asked for
#            was never there. The symbol is the second half of the contract.
#   Windows  <name>.vst3/Contents/<arch>-win/<name>.vst3  (a PE DLL with a .vst3
#            extension; no Info.plist)
#            entry point: public.sdk/source/main/dllmain.cpp - provides DllMain
#            and exports InitDll/ExitDll. The arch directory and the file name
#            are the loader's own: module_win32.cpp:150-167 appends
#            "Contents"/<architectureString>/<bundle directory name>, where
#            architectureString is one of x86_64-win, arm64-win, arm64ec-win,
#            arm64x-win or x86-win (:85-110). Laying this fixture out in the
#            -linux location on Windows is why the msvc-x64 job reported
#            "LoadLibraryW failed for path ...\vst3-test-effect.vst3\Contents\
#            x86_64-win\vst3-test-effect.vst3: The specified module could not be
#            found" for Vst3HostTest and Vst3EffectIntegrationTest (job
#            103724228360, run 34757467632): the loader asked for a Windows
#            package and there was none.
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

if(APPLE)
	# Contents/MacOS/<stem>, plus the Info.plist whose CFBundleExecutable has to
	# name that same stem for CFBundle to find it.
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_SUBDIR "Contents/MacOS")
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_FILE "${LMMS_VST3_TEST_EFFECT_NAME}")
	set(LMMS_VST3_TEST_EFFECT_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
		"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist" @ONLY)
elseif(WIN32)
	# The arch directory the SDK's Windows loader looks in, spelled the way IT
	# spells it (module_win32.cpp:85-110): a name of our own invention here is a
	# bundle the loader never opens, which is exactly what the msvc-x64 job
	# reported. CMAKE_SYSTEM_PROCESSOR is AMD64 on MSVC x64, ARM64 on MSVC/LLVM
	# arm64; `uname -m` (Git Bash is on the runner's PATH) answers x86_64 on
	# Windows too. Anything else is reported rather than guessed.
	if(LMMS_VST3_TEST_MACHINE MATCHES "^(x86_64|X86_64|amd64|AMD64)$")
		set(LMMS_VST3_TEST_WIN_ARCH "x86_64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
		set(LMMS_VST3_TEST_WIN_ARCH "arm64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(x86|X86|i386|i686)$")
		set(LMMS_VST3_TEST_WIN_ARCH "x86-win")
	else()
		# Do not fail the build over a fixture: name the value, keep the shape the
		# loader expects, and say that this is the unloadable case. No job that
		# RUNS these tests is on an architecture outside the three above (only
		# msvc-x64 runs them, on AMD64).
		message(WARNING
			"VST3 test fixture: unrecognised machine '${LMMS_VST3_TEST_MACHINE}': "
			"laying the bundle out as Contents/${LMMS_VST3_TEST_MACHINE}-win, "
			"which the SDK's Windows loader will not open - "
			"add the architectureString this SDK uses "
			"(module_win32.cpp:85-110) to the mapping above.")
		set(LMMS_VST3_TEST_WIN_ARCH "${LMMS_VST3_TEST_MACHINE}-win")
	endif()
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_SUBDIR "Contents/${LMMS_VST3_TEST_WIN_ARCH}")
	# A Windows VST3 module is a PE DLL under the .vst3 extension, and the loader
	# takes the file name from the bundle directory's own name.
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_FILE
		"${LMMS_VST3_TEST_EFFECT_NAME}.vst3")
	set(LMMS_VST3_TEST_EFFECT_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp")
else()
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_SUBDIR
		"Contents/${LMMS_VST3_TEST_MACHINE}-linux")
	set(LMMS_VST3_TEST_EFFECT_BUNDLE_FILE "${LMMS_VST3_TEST_EFFECT_NAME}.so")
	set(LMMS_VST3_TEST_EFFECT_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp")
endif()

set(LMMS_VST3_TEST_EFFECT_BUNDLE_BIN
	"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}/${LMMS_VST3_TEST_EFFECT_BUNDLE_SUBDIR}/${LMMS_VST3_TEST_EFFECT_BUNDLE_FILE}")

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
# dlopen/LoadLibrary/CFBundle, so nothing inside it references the entry points
# below and a static library would have the entry-point object dropped by the
# linker - the module would then fail with "does not export the required
# 'ModuleEntry' function" on Linux, "'bundleEntry'" on macOS, or load with no
# InitDll on Windows. The entry-point translation unit is per-platform (the
# selection above): linuxmain.cpp, macmain.cpp, dllmain.cpp. Object libraries are
# always linked whole.
add_library(${LMMS_VST3_TEST_EFFECT_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SAMPLE_DIR}/agsa_controller.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa_factory.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa_processor.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/agsa.h"
	"${LMMS_VST3_SAMPLE_DIR}/tutorial.cpp"
	"${LMMS_VST3_SAMPLE_DIR}/tutorial.h"
	"${LMMS_VST3_SAMPLE_DIR}/version.h"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_TEST_EFFECT_ENTRY_MAIN}"
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

# Lay the module out as the SDK's loader expects it - the Linux tree, the
# CFBundle macOS's CFBundleCreate() requires (whose Info.plist names the stem), or
# the Windows package module_win32.cpp::loadAsPackage() opens.
set(LMMS_VST3_TEST_EFFECT_LAYOUT_STEPS
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}/${LMMS_VST3_TEST_EFFECT_BUNDLE_SUBDIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_TEST_EFFECT_NAME}>"
		"${LMMS_VST3_TEST_EFFECT_BUNDLE_BIN}")
if(APPLE)
	list(APPEND LMMS_VST3_TEST_EFFECT_LAYOUT_STEPS
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist"
			"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}/Contents/Info.plist")
endif()
add_custom_command(TARGET ${LMMS_VST3_TEST_EFFECT_NAME} POST_BUILD
	${LMMS_VST3_TEST_EFFECT_LAYOUT_STEPS}
	COMMENT "Laying out the ${LMMS_VST3_TEST_EFFECT_NAME}.vst3 bundle")

# The tests live in ../.. (tests/CMakeLists.txt) and need this path; variables do
# not travel up a directory level on their own. Same contract as
# tests/data/vst3-test-instrument/CMakeLists.txt's LMMS_VST3_TEST_BUNDLE_DIR.
set(LMMS_VST3_TEST_EFFECT_BUNDLE_DIR "${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}" PARENT_SCOPE)
