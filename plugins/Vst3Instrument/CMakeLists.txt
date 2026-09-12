# Native VST3 INSTRUMENT hosting (post-alpha instrument-hosting lane).
#
# The Steinberg VST3 SDK is MIT licensed since 3.8; it is not vendored. See
# ../../cmake/modules/Vst3Sdk.cmake for the pin (v3.8.1_build_84) and the
# exact subset of SDK sources compiled into lmms_vst3_sdk, and
# ../../docs/VST3-INSTRUMENT-HOSTING.md for what this plug-in does, what it
# does not do yet, and the evidence for both.
#
# Why this is a separate module rather than a second class in vst3effect.so:
# one shared library exposes exactly one descriptor, named after the file
# (src/core/PluginFactory.cpp:177-185), and a sub-plugin browser needs its own
# descriptor of type Instrument. The host sources are therefore shared the way
# the one-descriptor rule allows - by compiling the same files into both
# targets. The test harnesses in this repository already do exactly that for
# other plug-ins (tests/CMakeLists.txt:526-535, the CLAP lane), so this is the
# established shape rather than a new one.
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

SET(LMMS_VST3_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Vst3Effect")

SET(LMMS_VST3_INSTRUMENT_SOURCES
	Vst3Instrument.cpp
	Vst3InstrumentView.cpp
	${LMMS_VST3_HOST_DIR}/Vst3BusMap.cpp
	${LMMS_VST3_HOST_DIR}/Vst3Host.cpp
	${LMMS_VST3_HOST_DIR}/Vst3Parameter.cpp
	${LMMS_VST3_HOST_DIR}/Vst3SubPluginFeatures.cpp
)

# Vst3Parameter.h is moc'ed by both VST3 plug-in targets; each module carries
# its own copy of the generated code, which is why this works at all.
SET(LMMS_VST3_INSTRUMENT_MOCFILES
	Vst3Instrument.h
	Vst3InstrumentView.h
	${LMMS_VST3_HOST_DIR}/Vst3Parameter.h
)

INCLUDE_DIRECTORIES("${LMMS_VST3_HOST_DIR}")

BUILD_PLUGIN(vst3instrument
	${LMMS_VST3_INSTRUMENT_SOURCES}
	MOCFILES
		${LMMS_VST3_INSTRUMENT_MOCFILES}
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_LINK_LIBRARIES(vst3instrument lmms_vst3_sdk)
