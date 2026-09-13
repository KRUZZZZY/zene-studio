# Native VST3 hosting (task #581). The Steinberg VST3 SDK is MIT licensed
# since 3.8; it is not vendored, see ../../cmake/modules/Vst3Sdk.cmake for the
# pin (v3.8.1_build_84) and the exact subset of SDK sources used.
SET(LMMS_VST3_SDK_PATH "${CMAKE_BINARY_DIR}/vst3sdk"
	CACHE PATH "Steinberg VST3 SDK checkout (>= 3.8)")
SET(WANT_VST3 "AUTO" CACHE STRING "Build native VST3 hosting: AUTO, ON or OFF")

IF(WANT_VST3 STREQUAL "OFF")
	MESSAGE(STATUS "VST3 hosting disabled (-DWANT_VST3=OFF)")
	RETURN()
ENDIF()
IF(WANT_VST3 STREQUAL "AUTO" AND NOT EXISTS "${LMMS_VST3_SDK_PATH}/LICENSE.txt")
	MESSAGE(STATUS "VST3 hosting skipped: no SDK at '${LMMS_VST3_SDK_PATH}' "
		"(clone instructions in cmake/modules/Vst3Sdk.cmake, then re-run cmake)")
	RETURN()
ENDIF()

INCLUDE(Vst3Sdk)

SET(LMMS_VST3_EFFECT_SOURCES
	Vst3BusMap.cpp
	Vst3Effect.cpp
	Vst3MidiEvent.cpp
	Vst3EffectControlDialog.cpp
	Vst3EffectControls.cpp
	Vst3Host.cpp
	Vst3Parameter.cpp
	Vst3SubPluginFeatures.cpp
)

SET(LMMS_VST3_EFFECT_MOCFILES
	Vst3Effect.h
	Vst3EffectControlDialog.h
	Vst3EffectControls.h
	Vst3Parameter.h
)

BUILD_PLUGIN(vst3effect
	${LMMS_VST3_EFFECT_SOURCES}
	MOCFILES
		${LMMS_VST3_EFFECT_MOCFILES}
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_LINK_LIBRARIES(vst3effect lmms_vst3_sdk)

IF(MSVC)
	# This module links the executable's import library (zene.lib), which exports std::vector<float>
	# instantiations because LMMS's own exported classes carry them in inline members - and
	# Vst3Host.cpp.obj instantiates the same members, so the link collides:
	#   zene.lib(zene.exe) : error LNK2005: "public: __cdecl std::vector<float,...>::~vector(...)"
	#                        already defined in Vst3Host.cpp.obj
	#   plugins\vst3effect.dll : fatal error LNK1169: one or more multiply defined symbols found
	# Both definitions are the same STL implementation from the same toolchain, so collapsing them
	# changes nothing at runtime, and the flag is scoped to this target so it cannot hide a duplicate
	# anywhere else. Precedent and the same honest limit: plugins/NeuralAmp/CMakeLists.txt lines 55-73
	# - this is a workaround for an MSVC export interaction that cannot be reproduced off Windows.
	# The clean fix, for a Windows CI loop: keep the STL type off the exported class surface (pimpl the
	# std::vector<float> member in Vst3Host.h, or define the class destructor out-of-line).
	TARGET_LINK_OPTIONS(vst3effect PRIVATE /FORCE:MULTIPLE)
ENDIF()
