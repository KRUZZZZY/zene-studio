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
	# This module links the executable's import library (zene.lib), which offers definitions of the
	# same std::vector<float> members Vst3Host.cpp.obj instantiates itself, so the link collides:
	#   zene.lib(zene.exe) : error LNK2005: "public: __cdecl std::vector<float,...>::~vector(...)"
	#                        already defined in Vst3Host.cpp.obj
	#   plugins\vst3effect.dll : fatal error LNK1169: one or more multiply defined symbols found
	#
	# WHY zene.lib carries them, checked against the source rather than inferred from the error: the
	# export side is CMake's, not an LMMS export macro. src/CMakeLists.txt:255 sets ENABLE_EXPORTS ON on
	# the executable, and on Windows CMake generates that import library's export list by walking the
	# COFF symbol tables of every object that goes into zene.exe and keeping every external symbol with
	# a non-zero section number (CMake Source/bindexplib.cxx, DumpExternalsObjects(); the only filters
	# are ??_G/??_E deleting destructors, dotted names, managed-code symbols and C++ operators declared
	# extern "C"). Inline and class-template members are NOT filtered, so every std::vector<float>
	# member any of the executable's own TUs instantiate (src/core/MasteringJob.cpp:270,307,
	# src/core/fft_helpers.cpp, src/core/SampleDecoder.cpp:160, ...) is exported and offered as a
	# definition. cmake/modules/BuildPlugin.cmake:54 links 'zene' into every plug-in module.
	#
	# Both definitions are the same STL implementation from the same toolchain, so collapsing them
	# changes nothing at runtime, and the flag is scoped to this target so it cannot hide a duplicate
	# anywhere else. Precedent and the same honest limit: plugins/NeuralAmp/CMakeLists.txt lines 51-73,
	# and that module links successfully in the very job that fails here -- [2103/2190] Linking CXX
	# shared module plugins\neuralamp.dll, about a second before this failure -- so the flag's effect
	# in this tree is observed, not assumed.
	#
	# WHAT DOES NOT FIX IT, and why the export-macro/pimpl route recorded beside the NeuralAmp
	# collision is not an option: the tree contains no "extern template" and no explicit instantiation
	# of a std container, and the only LMMS_EXPORT'ed class deriving from one is
	# include/ValueBuffer.h:37 -- but the three colliding symbols are instantiated by ordinary STL use
	# in the executable's own TUs, which CMake exports unfiltered (see above). Pimpl-ing a
	# std::vector<float> member of Vst3Host.h, or defining a destructor out of line, therefore leaves
	# every one of them in zene.exe. The durable fixes are a filtered export list for the executable or
	# moving the plug-in-facing host API into a DLL; neither is testable without a Windows CI loop,
	# which is why this module keeps the scoped flag. Each collapsed symbol still appears as LNK4006 in
	# the build log, so what the flag hides stays auditable.
	TARGET_LINK_OPTIONS(vst3effect PRIVATE /FORCE:MULTIPLE)
ENDIF()
