# Native CLAP hosting (task #588). CLAP is MIT licensed (github.com/free-audio/
# clap), so it needs no licence exception, no clean room and no Vestige shim.
# The headers are not vendored; see ../../cmake/modules/ClapHeaders.cmake for
# the pin (1.2.10, commit 195b42a004144fab0b3cf95e9c067187d15365b7) and how to
# obtain them.
SET(LMMS_CLAP_PATH "${CMAKE_BINARY_DIR}/clap"
	CACHE PATH "CLAP header checkout (>= 1.2.10)")
SET(WANT_CLAP "AUTO" CACHE STRING "Build native CLAP hosting: AUTO, ON or OFF")

IF(WANT_CLAP STREQUAL "OFF")
	MESSAGE(STATUS "CLAP hosting disabled (-DWANT_CLAP=OFF)")
	RETURN()
ENDIF()
IF(WANT_CLAP STREQUAL "AUTO" AND NOT EXISTS "${LMMS_CLAP_PATH}/include/clap/clap.h")
	MESSAGE(STATUS "CLAP hosting skipped: no headers at '${LMMS_CLAP_PATH}/include/clap' "
		"(clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)")
	RETURN()
ENDIF()

INCLUDE(ClapHeaders)

SET(LMMS_CLAP_EFFECT_SOURCES
	ClapBusMap.cpp
	ClapEffect.cpp
	ClapEffectControlDialog.cpp
	ClapEffectControls.cpp
	ClapHost.cpp
	ClapParameter.cpp
	ClapSubPluginFeatures.cpp
)

SET(LMMS_CLAP_EFFECT_MOCFILES
	ClapEffectControlDialog.h
	ClapEffectControls.h
	ClapParameter.h
)

BUILD_PLUGIN(clapeffect
	${LMMS_CLAP_EFFECT_SOURCES}
	MOCFILES
		${LMMS_CLAP_EFFECT_MOCFILES}
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_LINK_LIBRARIES(clapeffect lmms_clap ${CMAKE_DL_LIBS})
