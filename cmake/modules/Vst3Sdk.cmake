# Vst3Sdk.cmake - pinned Steinberg VST3 SDK for native VST3 hosting
#
# The VST3 SDK is MIT licensed since 3.8, which makes in-process hosting
# compatible with LMMS' GPL-2.0-or-later licence without any exception.
#
# PIN: tag v3.8.1_build_84, commit 3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96
#      https://github.com/steinbergmedia/vst3sdk.git
#
# The checkout is NOT vendored into the repository (it is ~150 MB). It is
# obtained once into the build tree:
#
#   git clone --depth 1 --branch v3.8.1_build_84 \
#       https://github.com/steinbergmedia/vst3sdk.git <build>/vst3sdk
#   cd <build>/vst3sdk
#   git submodule update --init --depth 1 base cmake pluginterfaces public.sdk
#
# or an existing checkout is selected with -DLMMS_VST3_SDK_PATH=<dir>.
#
# Only the subset of SDK sources needed by an in-process host is compiled.
# The list below is taken verbatim from the SDK's own
# cmake/modules/SMTG_VST3_SDK.cmake (smtg_create_lib_base_target,
# smtg_create_pluginterfaces_target, smtg_create_public_sdk_common_target,
# smtg_create_public_sdk_hosting_target) plus public.sdk/source/common/
# memorystream.cpp, public.sdk/source/vst/hosting/plugprovider.cpp and the
# platform module loader module_linux.cpp, which the SDK only adds to its own
# sample host applications.

SET(LMMS_VST3_SDK_TAG "v3.8.1_build_84")
SET(LMMS_VST3_SDK_COMMIT "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96")
SET(LMMS_VST3_SDK_URL "https://github.com/steinbergmedia/vst3sdk.git")
SET(LMMS_VST3_SDK_PATH "${CMAKE_BINARY_DIR}/vst3sdk"
	CACHE PATH "Steinberg VST3 SDK checkout (>= 3.8)")

IF(NOT EXISTS "${LMMS_VST3_SDK_PATH}/LICENSE.txt"
	OR NOT EXISTS "${LMMS_VST3_SDK_PATH}/pluginterfaces/vst/ivstcomponent.h")
	MESSAGE(FATAL_ERROR
		"No VST3 SDK at '${LMMS_VST3_SDK_PATH}'. Obtain it with:\n"
		"  git clone --depth 1 --branch ${LMMS_VST3_SDK_TAG} ${LMMS_VST3_SDK_URL} ${LMMS_VST3_SDK_PATH}\n"
		"  cd ${LMMS_VST3_SDK_PATH} && git submodule update --init --depth 1 base cmake pluginterfaces public.sdk\n"
		"or pass -DLMMS_VST3_SDK_PATH=<existing checkout>.")
ENDIF()

# --- licence gate: MIT since 3.8, and no VST2 headers ever ---------------
FILE(READ "${LMMS_VST3_SDK_PATH}/LICENSE.txt" LMMS_VST3_LICENSE_TEXT)
IF(NOT LMMS_VST3_LICENSE_TEXT MATCHES "MIT License")
	MESSAGE(FATAL_ERROR
		"${LMMS_VST3_SDK_PATH}/LICENSE.txt is not the MIT licence. Only "
		"VST3 SDK >= 3.8 may be used by LMMS (see plugin-hosting/VST3-LICENSING.md).")
ENDIF()
IF(EXISTS "${LMMS_VST3_SDK_PATH}/pluginterfaces/vst2.x")
	MESSAGE(FATAL_ERROR
		"${LMMS_VST3_SDK_PATH}/pluginterfaces/vst2.x exists: VST2 headers must "
		"never be vendored into LMMS. Use a VST3-only SDK checkout.")
ENDIF()
FILE(READ "${LMMS_VST3_SDK_PATH}/CMakeLists.txt" LMMS_VST3_ROOT_CMAKE)
IF(LMMS_VST3_ROOT_CMAKE MATCHES "project\\(vstsdk[ \t\r\n]+VERSION[ \t]+([0-9]+\\.[0-9]+)")
	SET(LMMS_VST3_SDK_VERSION "${CMAKE_MATCH_1}")
	IF(LMMS_VST3_SDK_VERSION VERSION_LESS "3.8")
		MESSAGE(FATAL_ERROR
			"VST3 SDK ${LMMS_VST3_SDK_VERSION} is older than 3.8 and not MIT "
			"licensed. Pin a >= 3.8 checkout.")
	ENDIF()
	MESSAGE(STATUS "Found VST3 SDK ${LMMS_VST3_SDK_VERSION} (MIT) at ${LMMS_VST3_SDK_PATH}")
ELSE()
	MESSAGE(WARNING "Could not determine the VST3 SDK version from "
		"${LMMS_VST3_SDK_PATH}/CMakeLists.txt")
ENDIF()

# --- static host library -------------------------------------------------
#
# Two plug-in targets host VST3 (plugins/Vst3Effect and plugins/Vst3Instrument),
# each including this module from its own CMakeLists.txt, because one shared
# library exposes exactly one descriptor (src/core/PluginFactory.cpp:177-185).
# The target is therefore created once, by whichever directory configures
# first, and this module is a no-op for the second.
IF(NOT TARGET lmms_vst3_sdk)

SET(LMMS_VST3_SDK_SOURCES
	pluginterfaces/base/conststringtable.cpp
	pluginterfaces/base/coreiids.cpp
	pluginterfaces/base/funknown.cpp
	pluginterfaces/base/ustring.cpp
	base/source/baseiids.cpp
	base/source/fbuffer.cpp
	base/source/fdebug.cpp
	base/source/fdynlib.cpp
	base/source/fobject.cpp
	base/source/fstreamer.cpp
	base/source/fstring.cpp
	base/source/timer.cpp
	base/source/updatehandler.cpp
	base/thread/source/fcondition.cpp
	base/thread/source/flock.cpp
	public.sdk/source/common/commoniids.cpp
	public.sdk/source/common/commonstringconvert.cpp
	public.sdk/source/common/memorystream.cpp
	public.sdk/source/vst/utility/stringconvert.cpp
	public.sdk/source/vst/vstinitiids.cpp
	public.sdk/source/vst/hosting/connectionproxy.cpp
	public.sdk/source/vst/hosting/eventlist.cpp
	public.sdk/source/vst/hosting/hostclasses.cpp
	public.sdk/source/vst/hosting/module.cpp
	public.sdk/source/vst/hosting/parameterchanges.cpp
	public.sdk/source/vst/hosting/pluginterfacesupport.cpp
	public.sdk/source/vst/hosting/plugprovider.cpp
	public.sdk/source/vst/hosting/processdata.cpp
)

IF(WIN32)
	LIST(APPEND LMMS_VST3_SDK_SOURCES
		public.sdk/source/vst/hosting/module_win32.cpp
		public.sdk/source/common/threadchecker_win32.cpp
		public.sdk/source/common/systemclipboard_win32.cpp
	)
ELSEIF(APPLE)
	LIST(APPEND LMMS_VST3_SDK_SOURCES
		public.sdk/source/vst/hosting/module_mac.mm
		public.sdk/source/common/threadchecker_mac.mm
		public.sdk/source/common/systemclipboard_mac.mm
	)
ELSE()
	LIST(APPEND LMMS_VST3_SDK_SOURCES
		public.sdk/source/vst/hosting/module_linux.cpp
		public.sdk/source/common/threadchecker_linux.cpp
		public.sdk/source/common/systemclipboard_linux.cpp
	)
ENDIF()

LIST(TRANSFORM LMMS_VST3_SDK_SOURCES PREPEND "${LMMS_VST3_SDK_PATH}/")

IF(APPLE)
	# module_mac.mm, systemclipboard_mac.mm and threadchecker_mac.mm are
	# Objective-C++ and this project enables no other .mm source anywhere, so
	# CMake cannot pick a compiler (or a link language) for the target without
	# this line. plugins/Vst3Effect is the highest directory common to every
	# target that compiles them (the plugin itself and, via lmms_vst3_sdk, the
	# VST3 test fixture), which is where enable_language belongs.
	IF(NOT CMAKE_OBJCXX_COMPILER)
		ENABLE_LANGUAGE(OBJCXX)
	ENDIF()
ENDIF()

ADD_LIBRARY(lmms_vst3_sdk STATIC ${LMMS_VST3_SDK_SOURCES})
TARGET_INCLUDE_DIRECTORIES(lmms_vst3_sdk PUBLIC "${LMMS_VST3_SDK_PATH}")
TARGET_COMPILE_FEATURES(lmms_vst3_sdk PUBLIC cxx_std_17)
# The SDK is third-party source compiled into this tree, exactly like gme,
# adplug, exprtk and zynaddsubfx. Marking it SYSTEM is what tells
# cmake/modules/ErrorFlags.cmake to compile it with the third-party flag set
# (-w) instead of -Wall -Werror. Without this, -DUSE_WERROR=ON (the CI's
# linux-x86_64 job) cannot build this target at all:
#   pluginterfaces/base/ustring.cpp:228:41: error: format '%lld' expects
#   argument of type 'long long int*', but argument 3 has type
#   'Steinberg::int64*' {aka 'long int*'} [-Werror=format=]
# That diagnostic is a real LP64 int64-vs-long-long mismatch in the SDK,
# which the SDK's own build does not treat as an error. We do not patch
# non-vendored SDK sources; we compile them the way the other third-party
# trees in this repository are compiled.
SET_TARGET_PROPERTIES(lmms_vst3_sdk PROPERTIES
	POSITION_INDEPENDENT_CODE ON
	SYSTEM ON)
TARGET_COMPILE_DEFINITIONS(lmms_vst3_sdk PUBLIC
	"$<$<CONFIG:Debug>:DEVELOPMENT=1>"
	"$<$<NOT:$<CONFIG:Debug>>:RELEASE=1>")
IF(UNIX AND NOT APPLE)
	TARGET_LINK_LIBRARIES(lmms_vst3_sdk PUBLIC dl)
ELSEIF(APPLE)
	# The three Objective-C++ translation units above call CoreFoundation
	# (CFBundle*, in module_mac.mm) and, from systemclipboard_mac.mm,
	# NSPasteboard through Cocoa/Foundation. Windows needs nothing added here:
	# ole32 (OleInitialize, CoCreateInstance) and shell32
	# (SHGetKnownFolderPath) used by module_win32.cpp are already in
	# CMAKE_CXX_STANDARD_LIBRARIES for both MSVC and MinGW.
	# UNVERIFIED ON macOS: this box cannot build Darwin targets, so CI is the
	# verifier for these three frameworks (see docs/PLUGIN-HOSTING-IN-RELEASE.md).
	TARGET_LINK_LIBRARIES(lmms_vst3_sdk PUBLIC
		"-framework CoreFoundation"
		"-framework Foundation"
		"-framework Cocoa")
ENDIF()

ENDIF() # NOT TARGET lmms_vst3_sdk
