# Audio Unit (AUv2) instrument hosting, Q050-491: macOS only, like ../AuEffect. A separate module because
# one library exposes one descriptor and the instrument browser needs one of type Instrument; the host
# sources are compiled into both, as the CLAP and VST3 pairs do.
IF(NOT LMMS_BUILD_APPLE)
	RETURN()
ENDIF()
SET(WANT_AU ON CACHE BOOL "Build Audio Unit (AUv2) hosting on macOS")
IF(NOT WANT_AU)
	RETURN()
ENDIF()

SET(LMMS_AU_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../AuEffect")
# A source property is seen by the targets of the directory that sets it, so it is set here again.
SET_SOURCE_FILES_PROPERTIES("${LMMS_AU_HOST_DIR}/AuHost.cpp" "${LMMS_AU_HOST_DIR}/AuCocoaView.cpp"
	PROPERTIES COMPILE_OPTIONS "-xobjective-c++")
INCLUDE_DIRECTORIES("${LMMS_AU_HOST_DIR}")

BUILD_PLUGIN(auinstrument
	AuInstrument.cpp
	AuInstrumentView.cpp
	"${LMMS_AU_HOST_DIR}/AuCocoaView.cpp"
	"${LMMS_AU_HOST_DIR}/AuHost.cpp"
	"${LMMS_AU_HOST_DIR}/AuParameter.cpp"
	"${LMMS_AU_HOST_DIR}/AuState.cpp"
	"${LMMS_AU_HOST_DIR}/AuSubPluginFeatures.cpp"
	MOCFILES
		AuInstrument.h
		AuInstrumentView.h
		"${LMMS_AU_HOST_DIR}/AuParameter.h"
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_INCLUDE_DIRECTORIES(auinstrument PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../Vst3Effect")
TARGET_LINK_LIBRARIES(auinstrument
	"-framework AudioToolbox" "-framework AudioUnit" "-framework CoreAudio" "-framework CoreAudioKit"
	"-framework CoreFoundation" "-framework AppKit")
