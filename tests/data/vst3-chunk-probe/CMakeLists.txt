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
#
# THE LAYOUT IS PER-PLATFORM, and getting it wrong is silent until a platform
# only CI runs reaches the load. Two of the three are laid out here:
#
#   Linux    <name>.vst3/Contents/<uname -m>-linux/<name>.so
#            entry point: public.sdk/source/main/linuxmain.cpp - exports
#            ModuleEntry/ModuleExit (the module is dlopen()ed).
#   Windows  <name>.vst3/Contents/<arch>-win/<name>.vst3
#            (a PE DLL under the .vst3 extension; no Info.plist)
#            entry point: public.sdk/source/main/dllmain.cpp - provides DllMain
#            and exports InitDll/ExitDll, the symbols the SDK's Windows loader
#            resolves. The arch directory and the file name are the LOADER's own:
#            module_win32.cpp:152-166 appends Contents/<architectureString>/<the
#            bundle directory's own name> and :89-107 fixes architectureString
#            per host - "x86_64-win" on 64-bit x86 Windows, "arm64-win" on a
#            native ARM64 build, "arm64ec-win" on ARM64EC (which retries
#            "arm64x-win" then "x86_64-win"), "x86-win" on 32-bit x86.
#            Laying this fixture out in the -linux location on Windows is why
#            the msvc-x64 job reported (run 35126160372, job 104895806855):
#              LoadLibraryW failed for path ...\vst3-chunk-probe.vst3\Contents\
#              x86_64-win\vst3-chunk-probe.vst3: The specified module could not
#              be found.
#            - the loader asked for a Windows package and there was none. The
#            sibling fixture tests/data/vst3-test-effect/CMakeLists.txt carries
#            the same fix and the same reasoning; its Windows half runs green on
#            msvc-x64.
#   macOS    NOT laid out here yet: it needs Contents/MacOS/<stem>, macmain.cpp
#            (bundleEntry/bundleExit) and an Info.plist whose CFBundleExecutable
#            names the stem. tests/data/vst3-test-effect/CMakeLists.txt is the
#            worked example. This fixture still lays macOS out in the -linux
#            location, exactly as it did before, and Vst3ChunkProbeTest is red
#            on both macOS jobs for it (same run: macos-x86_64 job
#            104895806868, test #152) - that is the macOS platform lane's file
#            to change, not this one's.

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

if(WIN32)
	# The arch directory the SDK's Windows loader looks in, spelled the way IT
	# spells it (module_win32.cpp:85-110). `uname -m` answers x86_64 on the
	# Windows runners (Git Bash is on PATH); where it is absent or silent,
	# CMAKE_SYSTEM_PROCESSOR is the generator's own word - AMD64 on MSVC x64,
	# ARM64 on MSVC/LLVM arm64 - and the fallback above takes it. Anything
	# outside this mapping is reported, not guessed.
	if(LMMS_VST3_TEST_MACHINE MATCHES "^(x86_64|X86_64|amd64|AMD64)$")
		set(LMMS_VST3_TEST_WIN_ARCH "x86_64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
		set(LMMS_VST3_TEST_WIN_ARCH "arm64-win")
	elseif(LMMS_VST3_TEST_MACHINE MATCHES "^(x86|X86|i386|i686)$")
		set(LMMS_VST3_TEST_WIN_ARCH "x86-win")
	else()
		message(WARNING
			"VST3 test fixture: unrecognised machine '${LMMS_VST3_TEST_MACHINE}': "
			"laying the bundle out as Contents/${LMMS_VST3_TEST_MACHINE}-win, "
			"which the SDK's Windows loader will not open - add the "
			"architectureString this SDK uses (module_win32.cpp:85-110) to the "
			"mapping above.")
		set(LMMS_VST3_TEST_WIN_ARCH "${LMMS_VST3_TEST_MACHINE}-win")
	endif()
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR
		"Contents/${LMMS_VST3_TEST_WIN_ARCH}")
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE
		"${LMMS_VST3_CHUNK_PROBE_NAME}.vst3")
	set(LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp")
else()
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR
		"Contents/${LMMS_VST3_TEST_MACHINE}-linux")
	set(LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE
		"${LMMS_VST3_CHUNK_PROBE_NAME}.so")
	set(LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp")
endif()
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_BIN
	"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_FILE}")

# --- what can be asserted without running the loader ------------------------
# No platform but this one can be exercised on a Linux box, so the parts of the
# two loaders' contracts that are checkable at CONFIGURE time are checked here,
# and a wrong path becomes a configure error rather than a module that builds,
# lays out, and then fails on a platform only CI runs (see the note above).
if(NOT EXISTS "${LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN}")
	message(FATAL_ERROR
		"The VST3 test fixture needs the SDK's ${CMAKE_SYSTEM_NAME} entry point, "
		"and ${LMMS_VST3_CHUNK_PROBE_ENTRY_MAIN} is not in the pinned checkout at "
		"LMMS_VST3_SDK_PATH (${LMMS_VST3_SDK_PATH}). Each loader resolves a "
		"different symbol set - ModuleEntry/ModuleExit (Linux), bundleEntry/"
		"bundleExit (macOS), GetPluginFactory/InitDll (Windows) - and the SDK "
		"selects the same three files by platform in "
		"cmake/modules/SMTG_AddSMTGLibrary.cmake:186-206.")
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
# dlopen/LoadLibrary/CFBundle, so nothing inside it references the entry points,
# and in a static library the linker would drop the entry-point object entirely.
# Object libraries are always linked whole.
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

# Lay the module out as the SDK's loader expects it.
add_custom_command(TARGET ${LMMS_VST3_CHUNK_PROBE_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}/${LMMS_VST3_CHUNK_PROBE_BUNDLE_SUBDIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:${LMMS_VST3_CHUNK_PROBE_NAME}>"
		"${LMMS_VST3_CHUNK_PROBE_BUNDLE_BIN}"
	COMMENT "Laying out the ${LMMS_VST3_CHUNK_PROBE_NAME}.vst3 bundle")

# The test lives in ../.. (tests/CMakeLists.txt) so that it sits with the other
# plug-in test executables; it needs this path, and variables do not travel up a
# directory level on their own.
set(LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR "${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}" PARENT_SCOPE)
