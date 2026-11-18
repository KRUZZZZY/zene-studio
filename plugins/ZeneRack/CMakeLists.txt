# ZeneRack - the installable Zene Rack VST3 product plug-in (Q050-267.1).
#
# This is a VST3 BUNDLE, not one of Zene's own plug-in modules: it is loaded by
# another DAW's VST3 host, so it must not use BUILD_PLUGIN (that produces a
# Zene module with a Zene descriptor and links the zene executable). Following
# tests/data/vst3-chunk-probe/CMakeLists.txt and tests/data/vst3-test-effect/,
# the plug-in-side SDK sources are an OBJECT library so the per-platform entry
# point (ModuleEntry / bundleEntry / InitDll) is linked whole.
#
# The chain the module carries is documented in plugins/ZeneRack/ZeneRack.cpp,
# docs/050-features/Q050-267.1.md and the user manual. It processes:
#   Amplifier > BassBooster > WaveShaper
#
# The bundle is laid out where the SDK's loaders look:
#   Linux   ZeneRack.vst3/Contents/<uname -m>-linux/ZeneRack.so
#   macOS   ZeneRack.vst3/Contents/MacOS/ZeneRack + Contents/Info.plist
#   Windows ZeneRack.vst3/Contents/<arch>-win/ZeneRack.vst3

execute_process(COMMAND uname -m
	OUTPUT_VARIABLE LMMS_ZENERACK_MACHINE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET)
if(NOT LMMS_ZENERACK_MACHINE)
	set(LMMS_ZENERACK_MACHINE "${CMAKE_SYSTEM_PROCESSOR}")
endif()

# On Windows the arch directory is the TARGET's, which is what the SDK's loader
# compiles in (_M_ARM64 -> "arm64-win"): hosted windows-arm64 runs MSYS2, whose
# `uname -m` is an emulated x86_64 program. Ask the compiler instead.
if(WIN32)
	include(CheckCXXSourceCompiles)
	check_cxx_source_compiles("
#if !(defined(_M_ARM64) || defined(__aarch64__)) || defined(_M_ARM64EC)
#error not a native ARM64 target
#endif
int main() { return 0; }" LMMS_ZENERACK_TARGET_ARM64)
	if(LMMS_ZENERACK_TARGET_ARM64)
		set(LMMS_ZENERACK_MACHINE "ARM64")
	endif()
endif()

set(LMMS_ZENERACK_NAME "ZeneRack")
set(LMMS_ZENERACK_BUNDLE_DIR
	"${CMAKE_CURRENT_BINARY_DIR}/${LMMS_ZENERACK_NAME}.vst3")

if(APPLE)
	set(LMMS_ZENERACK_BUNDLE_SUBDIR "Contents/MacOS")
	set(LMMS_ZENERACK_BUNDLE_FILE "${LMMS_ZENERACK_NAME}")
	set(LMMS_ZENERACK_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/macmain.cpp")
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
		"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist" @ONLY)
elseif(WIN32)
	if(LMMS_ZENERACK_MACHINE MATCHES "^(x86_64|X86_64|amd64|AMD64)$")
		set(LMMS_ZENERACK_WIN_ARCH "x86_64-win")
	elseif(LMMS_ZENERACK_MACHINE MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
		set(LMMS_ZENERACK_WIN_ARCH "arm64-win")
	elseif(LMMS_ZENERACK_MACHINE MATCHES "^(x86|X86|i386|i686)$")
		set(LMMS_ZENERACK_WIN_ARCH "x86-win")
	else()
		message(WARNING
			"Zene Rack: unrecognised machine '${LMMS_ZENERACK_MACHINE}': laying the "
			"bundle out as Contents/${LMMS_ZENERACK_MACHINE}-win, which the SDK's "
			"Windows loader will not open - add the architectureString this SDK "
			"uses (module_win32.cpp:85-110) to the mapping above.")
		set(LMMS_ZENERACK_WIN_ARCH "${LMMS_ZENERACK_MACHINE}-win")
	endif()
	set(LMMS_ZENERACK_BUNDLE_SUBDIR "Contents/${LMMS_ZENERACK_WIN_ARCH}")
	set(LMMS_ZENERACK_BUNDLE_FILE "${LMMS_ZENERACK_NAME}.vst3")
	set(LMMS_ZENERACK_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/dllmain.cpp")
else()
	set(LMMS_ZENERACK_BUNDLE_SUBDIR
		"Contents/${LMMS_ZENERACK_MACHINE}-linux")
	set(LMMS_ZENERACK_BUNDLE_FILE "${LMMS_ZENERACK_NAME}.so")
	set(LMMS_ZENERACK_ENTRY_MAIN
		"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/linuxmain.cpp")
endif()

if(NOT EXISTS "${LMMS_ZENERACK_ENTRY_MAIN}")
	message(FATAL_ERROR
		"Zene Rack needs the VST3 SDK's ${CMAKE_SYSTEM_NAME} entry point, and "
		"${LMMS_ZENERACK_ENTRY_MAIN} is not in the pinned checkout at "
		"LMMS_VST3_SDK_PATH (${LMMS_VST3_SDK_PATH}).")
endif()

if(WIN32)
	get_filename_component(LMMS_ZENERACK_BUNDLE_STEM
		"${LMMS_ZENERACK_BUNDLE_DIR}" NAME)
	if(NOT LMMS_ZENERACK_BUNDLE_FILE STREQUAL LMMS_ZENERACK_BUNDLE_STEM)
		message(FATAL_ERROR
			"The Windows VST3 loader opens <bundle>/Contents/<arch>/<bundle "
			"directory name>, so the bundle's file name "
			"(${LMMS_ZENERACK_BUNDLE_FILE}) has to be the bundle directory's own "
			"(${LMMS_ZENERACK_BUNDLE_STEM}).")
	endif()
endif()

set(LMMS_ZENERACK_BUNDLE_BIN
	"${LMMS_ZENERACK_BUNDLE_DIR}/${LMMS_ZENERACK_BUNDLE_SUBDIR}/${LMMS_ZENERACK_BUNDLE_FILE}")

# --- the plug-in side of the SDK (third-party, compiled SYSTEM) --------------
add_library(zenerack-sdkbase OBJECT
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/common/pluginview.cpp"
	"${LMMS_ZENERACK_ENTRY_MAIN}"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/moduleinit.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/main/pluginfactory.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstbus.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstcomponentbase.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstparameters.cpp"
	"${LMMS_VST3_SDK_PATH}/public.sdk/source/vst/vstsinglecomponenteffect.cpp"
)
target_link_libraries(zenerack-sdkbase PUBLIC lmms_vst3_sdk)
target_compile_features(zenerack-sdkbase PUBLIC cxx_std_17)
set_target_properties(zenerack-sdkbase PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	SYSTEM ON)

# --- the product module ----------------------------------------------------
add_library(zenerack MODULE
	ZeneRack.cpp
)
set_target_properties(zenerack PROPERTIES
	PREFIX ""
	OUTPUT_NAME "${LMMS_ZENERACK_NAME}")
target_compile_features(zenerack PRIVATE cxx_std_20)
target_link_libraries(zenerack PRIVATE zenerack-sdkbase)

set(LMMS_ZENERACK_LAYOUT_STEPS
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"${LMMS_ZENERACK_BUNDLE_DIR}/${LMMS_ZENERACK_BUNDLE_SUBDIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:zenerack>"
		"${LMMS_ZENERACK_BUNDLE_BIN}")
if(APPLE)
	list(APPEND LMMS_ZENERACK_LAYOUT_STEPS
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_BINARY_DIR}/bundle-Info.plist"
			"${LMMS_ZENERACK_BUNDLE_DIR}/Contents/Info.plist")
endif()
add_custom_command(TARGET zenerack POST_BUILD
	${LMMS_ZENERACK_LAYOUT_STEPS}
	COMMENT "Laying out the ZeneRack.vst3 bundle")

# Installable beside the other VST3 bundles a host scans (on Linux/macOS the
# conventional user/system location is <libdir>/vst3).
install(DIRECTORY "${LMMS_ZENERACK_BUNDLE_DIR}/"
	DESTINATION "${LIB_DIR}/vst3/${LMMS_ZENERACK_NAME}.vst3")

# tests/ is configured after plugins/, so the bundle path travels as a cache
# variable; the test target is registered only when this target exists.
set(ZENERACK_BUNDLE_DIR "${LMMS_ZENERACK_BUNDLE_DIR}" CACHE INTERNAL
	"Zene Rack VST3 bundle directory")
