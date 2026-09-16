# Native CLAP INSTRUMENT hosting (feature row 79, board task #669).
#
# CLAP is MIT licensed (github.com/free-audio/clap), so it needs no licence
# exception, no clean room and no Vestige shim. The headers are not vendored;
# see ../../cmake/modules/ClapHeaders.cmake for the pin (1.2.10, commit
# 195b42a004144fab0b3cf95e9c067187d15365b7) and how to obtain them.
#
# Why this is a separate module rather than a second class in clapeffect.so:
# one shared library exposes exactly one descriptor, named after the file
# (src/core/PluginFactory.cpp), and the instrument browser needs its own
# descriptor of type Instrument. The host sources are therefore shared the way
# the one-descriptor rule allows - by compiling the same files into both
# targets, exactly as plugins/Vst3Instrument/CMakeLists.txt does for the VST3
# pair and the CLAP test harnesses already do in tests/CMakeLists.txt.
#
# The guard is the same shape as ClapEffect's: WANT_CLAP=OFF disables it, and
# AUTO skips it when the pinned headers are not in the build tree.
SET(LMMS_CLAP_PATH "${CMAKE_BINARY_DIR}/clap"
	CACHE PATH "CLAP header checkout (>= 1.2.10)")
SET(WANT_CLAP "AUTO" CACHE STRING "Build native CLAP hosting: AUTO, ON or OFF")

IF(WANT_CLAP STREQUAL "OFF")
	MESSAGE(STATUS "CLAP hosting disabled (-DWANT_CLAP=OFF)")
	RETURN()
ENDIF()
IF(WANT_CLAP STREQUAL "AUTO" AND NOT EXISTS "${LMMS_CLAP_PATH}/include/clap/clap.h")
	MESSAGE(STATUS "CLAP instrument hosting skipped: no headers at '${LMMS_CLAP_PATH}/include/clap' "
		"(clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)")
	RETURN()
ENDIF()

INCLUDE(ClapHeaders)

SET(LMMS_CLAP_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ClapEffect")

SET(LMMS_CLAP_INSTRUMENT_SOURCES
	ClapInstrument.cpp
	ClapInstrumentView.cpp
	${LMMS_CLAP_HOST_DIR}/ClapBusMap.cpp
	${LMMS_CLAP_HOST_DIR}/ClapHost.cpp
	${LMMS_CLAP_HOST_DIR}/ClapHostLifecycle.cpp
	${LMMS_CLAP_HOST_DIR}/ClapHostNotes.cpp
	${LMMS_CLAP_HOST_DIR}/ClapHostParams.cpp
	${LMMS_CLAP_HOST_DIR}/ClapLoader.cpp
	${LMMS_CLAP_HOST_DIR}/ClapParameter.cpp
	${LMMS_CLAP_HOST_DIR}/ClapSubPluginFeatures.cpp
)

# ClapParameter.h is moc'ed by both CLAP plug-in targets; each module carries
# its own copy of the generated code, which is why this works at all (the same
# note as plugins/Vst3Instrument/CMakeLists.txt).
SET(LMMS_CLAP_INSTRUMENT_MOCFILES
	ClapInstrument.h
	ClapInstrumentView.h
	${LMMS_CLAP_HOST_DIR}/ClapParameter.h
)

INCLUDE_DIRECTORIES("${LMMS_CLAP_HOST_DIR}")

BUILD_PLUGIN(clapinstrument
	${LMMS_CLAP_INSTRUMENT_SOURCES}
	MOCFILES
		${LMMS_CLAP_INSTRUMENT_MOCFILES}
	EMBEDDED_RESOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/logo.png"
)

TARGET_LINK_LIBRARIES(clapinstrument lmms_clap ${CMAKE_DL_LIBS})

IF(MSVC)
	# The same collision as clapeffect's, and the same reasoning: this module links the executable's
	# import library, whose export list CMake generates from every external symbol in zene.exe's
	# objects (ENABLE_EXPORTS + Source/bindexplib.cxx) without filtering template or inline members,
	# while the host TU instantiates the same members. The full note, with the measured LNK2005 list,
	# is in plugins/ClapEffect/CMakeLists.txt; the VST3 pair carries the same scoped flag for the
	# same reason.
	TARGET_LINK_OPTIONS(clapinstrument PRIVATE /FORCE:MULTIPLE)
ENDIF()
