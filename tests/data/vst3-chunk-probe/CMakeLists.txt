# The VST3 chunk probe: a purpose-built, in-tree MIT fixture that witnesses the
# host's chunking contract (CODE-4) - what block sizes the host asked it to
# process, and whether any call exceeded the block size setupProcessing() declared.
#
# It is the subject of Vst3ChunkProbeTest. It mirrors
# tests/data/clap-test-plugin/clap-test-gain.c for the CLAP host and
# tests/data/vst3-test-effect/CMakeLists.txt for the layout: two targets, the
# fixture MODULE and the plug-in-side SDK sources it needs, laid out as the
# SDK loader expects. It is NEVER installed - a test subject, not a product
# plug-in.
#
# An ordinary plug-in links the SDK's own sdk/sdk_common libraries. This
# repository compiles only the HOSTING subset into lmms_vst3_sdk, so the
# plug-in-side translation units are listed here instead; both sets come from
# the SDK's cmake/modules/SMTG_VST3_SDK.cmake (smtg_create_public_sdk_target).
#
# THE BUNDLE IS A DIRECTORY AND ITS LAYOUT IS THE LOADER'S CONTRACT. Three
# loaders, three layouts, three entry-point translation units - the same three
# the effect fixture states, with the file in the pinned SDK that demands each:
#
#   Linux    <name>.vst3/Contents/<uname -m>-linux/<name>.so
#            entry point: public.sdk/source/main/linuxmain.cpp - exports
#            ModuleEntry/ModuleExit, resolved by dlsym in module_linux.cpp.
#   macOS    <name>.vst3/Contents/MacOS/<name>  +  Contents/Info.plist
#            entry point: public.sdk/source/main/macmain.cpp - exports
#            bundleEntry/bundleExit, looked up with CFBundleGetFunctionPointerForName
#            in module_mac.mm. The loader itself is CoreFoundation:
#            CFBundleCreate(url) + CFBundleLoadExecutableAndReturnError(bundle).
#   Windows  <name>.vst3/Contents/<arch>-win/<name>.vst3  (a PE DLL under the .vst3
#            extension; no Info.plist)
#            entry point: public.sdk/source/main/dllmain.cpp - DllMain plus the
#            exported InitDll/ExitDll. module_win32.cpp appends
#            "Contents"/<architectureString>/<bundle directory name>, and
#            architectureString is a compile-time constant of the HOST
#            (module_win32.cpp:85-110): "x86_64-win" on 64-bit x86, "arm64-win"
#            on a native ARM64 build, "arm64ec-win" on ARM64EC.
#
# WHY THIS FILE CARRIES THE CONTRACT: this fixture hardcoded the -linux layout on
# every platform, and run 35126160372 measured what that costs on all three that
# run it:
#
#   macos-arm64 (job 104895806458) and macos-x86_64 (104895806868), 152/Vst3ChunkProbeTest:
#     FAIL!  : lmms::vst3::Vst3ChunkProbeTest::initTestCase() 'm_plugin.load(path, ...)'
#     returned FALSE. (The bundle "vst3-chunk-probe.vst3" couldn't be loaded because
#     its executable couldn't be located.)
#     Loc: [tests/src/plugins/Vst3ChunkProbeTest.cpp(132)]
#   and the same build log shows the entry point compiled was still
#   public.sdk/source/main/linuxmain.cpp.o (arm64 log line 2618), so even a
#   correct Contents/MacOS layout would have had no bundleEntry to resolve.
#
#   msvc-x64 (job 104895806855), 151/161, the same test:
#     FAIL!  : ... 'm_plugin.load(path, ...)' returned FALSE. (LoadLibraryW failed for
#     path .../build/tests/data/vst3-chunk-probe/vst3-chunk-probe.vst3\Contents\x86_64-win\
#     vst3-chunk-probe.vst3: The specified module could not be found.?
#     - the loader asked for the Windows package and the fixture had laid out a
#     Linux one.
#
#   linux-x86_64 (job 104895806780) passes the same test with the -linux layout,
#   which is why the Linux branch below is what it always was.
#
# The per-platform block, the Info.plist and the configure-time entry-point check
# are the same three pieces tests/data/vst3-test-effect/CMakeLists.txt uses, so
# the two fixtures cannot drift apart again.

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

if(APPLE)
	# Contents/MacOS/<stem>, plus the Info.plist whose CFBundleExecutable has to
	# name that same stem for CFBundle to find it.
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR "Contents/MacOS")
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE "${LMMS_VST3_CHUNK_PROBE_NAME}")
	set(LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
		"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist" @ONLY)
elseif(WIN32)
	# The arch directory the SDK's Windows loader looks in, spelled the way IT
	# spells it (module_win32.cpp:85-110): a name of our own invention here is a
	# bundle the loader never opens. `uname -m` answers x86_64 under Git Bash;
	# where it is absent or silent, CMAKE_SYSTEM_PROCESSOR is the generator's own
	# architecture word - AMD64 on MSVC x64, ARM64 on MSVC/LLVM arm64 - and the
	# fallback above takes it. Anything outside this mapping is reported rather
	# than guessed.
	if(LMMS_VST3_TEST_MACHINE MATCHES "^(x86_64|X86_64|amd64|AMD64)$")
		set(LMMS_VST3_CHUNK_PROBE_WIN_ARCH "x86_64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
		set(LMMS_VST3_CHUNK_PROBE_WIN_ARCH "arm64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(x86|X86|i386|i686)$")
		set(LMMS_VST3_CHUNK_PROBE_WIN_ARCH "x86-win")
	else()
		message(WARNING
			"VST3 test fixture: unrecognised machine '${LMMS_VST3_TEST_MACHINE}': "
			"laying the bundle out as Contents/${LMMS_VST3_TEST_MACHINE}-win, "
			"which the SDK's Windows loader will not open - add the architectureString "
			"this SDK uses (module_win32.cpp:85-110) to the mapping above.")
		set(LMMS_VST3_CHUNK_PROBE_WIN_ARCH "${LMMS_VST3_TEST_MACHINE}-win")
	endif()
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR "Contents/${LMMS_VST3_CHUNK_PROBE_WIN_ARCH}")
	# A Windows VST3 module is a PE DLL under the .vst3 extension, and the loader
	# takes the file name from the bundle directory's own name.
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE
		"${LMMS_VST3_CHUNK_PROBE_NAME}.vst3")
	set(LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp")
else()
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR
		"Contents/${LMMS_VST3_TEST_MACHINE}-linux")
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE "${LMMS_VST3_CHUNK_PROBE_NAME}.so")
	set(LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp")
endif()

set(LMMS_VST3_CHUNK_PROBE_BUNDLE_BIN
	"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE}")

# --- the contract, asserted where it can be asserted ------------------------
# Neither the macOS nor the Windows half of this fixture can be exercised on a
# Linux box, so the one part of the contract checkable at CONFIGURE time is
# checked here: the entry-point translation unit has to be in the pinned
# checkout. A wrong path here is otherwise a module that builds, lays out, and
# then fails on a platform only CI runs.
if(NOT EXISTS "${LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN}")
	message(FATAL_ERROR
		"The VST3 chunk probe needs the SDK's ${CMAKE_SYSTEM_NAME} entry point, "
		"and ${LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN} is not in the pinned checkout at "
		"LMMS_VST3_SDK_PATH (${LMMS_VST3_SDK_PATH}). Each loader resolves a "
		"different symbol - ModuleEntry (Linux), bundleEntry/bundleExit (macOS), "
		"GetPluginFactory/InitDll (Windows) - and the SDK selects the same three "
		"files by platform in cmake/modules/SMTG_AddSMTGLibrary.cmake:186-206.")
endif()

if(WIN32)
	get_filename_component(LMMS_VST3_CHUNK_PROBE_BUNDLE_STEM
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}" NAME)
	if(NOT LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE STREQUAL LMMS_VST3_CHUNK_PROBE_BUNDLE_STEM)
		message(FATAL_ERROR
			"The Windows VST3 loader opens <bundle>/Contents/<arch>/<bundle "
			"directory name> (module_win32.cpp:152-166), so the fixture's file "
			"name (${LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE}) has to be the bundle "
			"directory's own (${LMMS_VST3_CHUNK_PROBE_BUNDLE_STEM}).")
	endif()
endif()

# --- plug-in-side SDK sources (third-party) --------------------------------
# An OBJECT library, not a STATIC one, on purpose: a .vst3 module is loaded by
# dlopen/LoadLibrary/CFBundle, so nothing inside it references ModuleEntry /
# bundleEntry / InitDll. In a static library the linker would therefore drop the
# entry-point object entirely and the module would load with "does not export the
# required 'ModuleEntry' function" on Linux, "'bundleEntry'" on macOS, or with no
# InitDll on Windows. Object libraries are always linked whole.
add_library(${LMMS_VST3_CHUNK_PROBE_NAME}-sdkbase OBJECT
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN}"
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

# Lay the module out as the SDK's loader expects it - the Linux tree, the
# CFBundle macOS's CFBundleCreate() requires (whose Info.plist names the stem), or
# the Windows package module_win32.cpp::loadAsPackage() opens.
set(LMMS_VST3_CHUNK_PROBE_LAYOUT_STEPS
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_CHUNK_PROBE_NAME}>"
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_BIN}")
if(APPLE)
	list(APPEND LMMS_VST3_CHUNK_PROBE_LAYOUT_STEPS
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist"
			"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/Contents/Info.plist")
endif()
add_custom_command(TARGET ${LMMS_VST3_CHUNK_PROBE_NAME} POST_BUILD
	${LMMS_VST3_CHUNK_PROBE_LAYOUT_STEPS}
	COMMENT "Laying out the ${LMMS_VST3_CHUNK_PROBE_NAME}.vst3 bundle")

# The test lives in ../.. (tests/CMakeLists.txt) so that it sits with the other
# plug-in test executables; it needs this path, and variables do not travel up a
# directory level on their own.
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR "${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}" PARENT_SCOPE)
