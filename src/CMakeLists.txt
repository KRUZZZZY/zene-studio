ADD_SUBDIRECTORY(3rdparty)

CONFIGURE_FILE("lmmsconfig.h.in"        "${CMAKE_BINARY_DIR}/lmmsconfig.h")

# Provide config flags to lmmsversion.h
get_cmake_property(_define_vars VARIABLES)
foreach (_define_var ${_define_vars})
    if(_define_var MATCHES "^WANT|LMMS_(HAVE|DEBUG)" )
        list(APPEND LMMS_BUILD_OPTIONS "${_define_var}='${${_define_var}}'")
    endif()
endforeach()

# Format for readability
string(REPLACE ";" " " LMMS_BUILD_OPTIONS "${LMMS_BUILD_OPTIONS}")

CONFIGURE_FILE("lmmsversion.h.in"       "${CMAKE_BINARY_DIR}/lmmsversion.h")

SET(LMMS_SRCS "")

SET(CMAKE_AUTOMOC ON)
SET(CMAKE_AUTOUIC ON)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Enable C++20
SET(CMAKE_CXX_STANDARD 20)

IF(LMMS_BUILD_APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
ENDIF()

ADD_SUBDIRECTORY(common)
ADD_SUBDIRECTORY(core)
ADD_SUBDIRECTORY(gui)
ADD_SUBDIRECTORY(tracks)
IF(WANT_WASM)
	ADD_SUBDIRECTORY(wasm)
ENDIF()

LIST(APPEND LMMS_SRCS ${LMMS_COMMON_SRCS})

IF(WANT_QT6)
	QT6_WRAP_UI(LMMS_UI_OUT ${LMMS_UIS})
ELSE()
	QT5_WRAP_UI(LMMS_UI_OUT ${LMMS_UIS})
ENDIF()

INCLUDE_DIRECTORIES(
	"${CMAKE_CURRENT_BINARY_DIR}"
	"${CMAKE_BINARY_DIR}"
	"${CMAKE_BINARY_DIR}/include"
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/include"
)

IF(WIN32 AND MSVC)
	SET(WINRC "${CMAKE_BINARY_DIR}/zene.rc")
ELSEIF(WIN32)
	SET(WINRC "${CMAKE_BINARY_DIR}/zenec.obj")
	ADD_CUSTOM_COMMAND(OUTPUT "${WINRC}"
				COMMAND "${CMAKE_RC_COMPILER}"
					"-I\"${CMAKE_SOURCE_DIR}\""
					"-o\"${CMAKE_BINARY_DIR}/zenec.obj\""
					"-i\"${CMAKE_BINARY_DIR}/zene.rc\""
				DEPENDS "${CMAKE_BINARY_DIR}/zene.rc")
ENDIF()

INCLUDE(GenQrc)
ADD_GEN_QRC(LMMS_RCC_OUT lmms.qrc
	"${CMAKE_SOURCE_DIR}/doc/AUTHORS"
	"${CMAKE_SOURCE_DIR}/LICENSE.txt"
	"${CONTRIBUTORS}"
)

# Paths relative to the zene executable
FILE(RELATIVE_PATH LIB_DIR_RELATIVE "/${BIN_DIR}" "/${LIB_DIR}")
FILE(RELATIVE_PATH PLUGIN_DIR_RELATIVE "/${BIN_DIR}" "/${PLUGIN_DIR}")
ADD_DEFINITIONS(-DLIB_DIR="${LIB_DIR_RELATIVE}" -DPLUGIN_DIR="${PLUGIN_DIR_RELATIVE}" ${PULSEAUDIO_DEFINITIONS})
include_directories(SYSTEM
	${JACK_INCLUDE_DIRS}
	${SNDIO_INCLUDE_DIRS}
	${FFTW3F_INCLUDE_DIRS}
)

IF(PULSEAUDIO_INCLUDE_DIR)
	include_directories(SYSTEM "${PULSEAUDIO_INCLUDE_DIR}")
ENDIF()

IF(LV2_INCLUDE_DIRS)
	include_directories(SYSTEM ${LV2_INCLUDE_DIRS})
ENDIF()

IF(Lilv_INCLUDE_DIRS)
	include_directories(SYSTEM ${Lilv_INCLUDE_DIRS})
ENDIF()

IF(SUIL_INCLUDE_DIRS)
	include_directories(SYSTEM ${SUIL_INCLUDE_DIRS})
ENDIF()

# Use libraries in non-standard directories (e.g., another version of Qt)
IF(LMMS_BUILD_LINUX)
	LINK_LIBRARIES(-Wl,--enable-new-dtags)
	SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
ENDIF()
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

ADD_LIBRARY(lmmsobjs OBJECT
	${LMMS_SRCS}
	${LMMS_INCLUDES}
	${LMMS_RCC_OUT}
)

IF(NOT LMMS_HAVE_STEM_SPLIT)
	# include/*.h is globbed into lmmsobjs for AUTOMOC. The stem separation
	# header is feature-gated: with the feature off its .cpp is not built, so
	# moc'ing it here would leave undefined references in mocs_compilation.
	SET_SOURCE_FILES_PROPERTIES("${CMAKE_SOURCE_DIR}/include/StemSplitController.h"
		PROPERTIES SKIP_AUTOMOC ON)
ENDIF()

IF(NOT LMMS_HAVE_SESSION_VIEW)
	# Same AUTOMOC trap for the gated Session View header. SessionModel has no
	# Q_OBJECT today (it is a plain data model), so this guard is a no-op now;
	# it keeps the WANT_SESSION_VIEW=OFF build clean if a Q_OBJECT is ever
	# added to the header.
	SET_SOURCE_FILES_PROPERTIES("${CMAKE_SOURCE_DIR}/include/SessionModel.h"
		PROPERTIES SKIP_AUTOMOC ON)
ENDIF()

IF(WANT_WASM)
	TARGET_INCLUDE_DIRECTORIES(lmmsobjs SYSTEM PRIVATE ${WASMTIME_INCLUDE_DIRS})
	TARGET_COMPILE_DEFINITIONS(lmmsobjs PRIVATE LMMS_HAVE_WASM=1)
	target_static_definitions(lmmsobjs LMMS_HAVE_WASM=1)
	target_static_libraries(lmmsobjs ${WASMTIME_LIBRARIES})
ENDIF()

# Lua scripting API version: the numbers come from the top-level CMakeLists.txt
# so the engine and the version tests are compiled from the same source of
# truth (include/ScriptApiVersion.h explains why that matters).
TARGET_COMPILE_DEFINITIONS(lmmsobjs PRIVATE
	ZENE_LUA_API_VERSION_MAJOR=${ZENE_LUA_API_VERSION_MAJOR}
	ZENE_LUA_API_VERSION_MINOR=${ZENE_LUA_API_VERSION_MINOR}
	ZENE_LUA_API_VERSION_PATCH=${ZENE_LUA_API_VERSION_PATCH}
)

GENERATE_EXPORT_HEADER(lmmsobjs
	BASE_NAME lmms
)

# ---------------------------------------------------------------------------
# The zene::api boundary (ARCH-2, board card #672): the registry, its contract
# tables and its undo-coalescing rule, behind a target that BUILDS HEADLESS.
# The source list and the split from LMMS_SRCS are in src/core/CMakeLists.txt;
# this is the target they compile in and the widgets axis it must not have.
#
# QtWidgets is neither LINKED here nor on this target's include path, so
# `#include <QWidget>` in a boundary translation unit fails the build in every
# CI job. Measured before the split: all boundary TUs compile with the
# QtWidgets include directory removed; docs/reports/ARCH2-BOUNDARY-REPORT.md
# carries the commands and the numbers, and tests/zene-api-boundary.py (the
# ZeneApiBoundary ctest) carries the same measurement into the test step with a
# GUI translation unit as the liveness control.
ADD_LIBRARY(zene_api STATIC ${ZENE_API_SRCS})

# The Q_OBJECT headers under include/ are AUTOMOCed by lmmsobjs (its include/*.h
# glob is unchanged) and the boundary links those moc symbols from there;
# moc'ing them here as well would define every ControlRegistry metaobject twice.
# No boundary TU declares a Q_OBJECT of its own.
SET_TARGET_PROPERTIES(zene_api PROPERTIES AUTOMOC OFF)

# The boundary is a split of the lmms object library, not a new library: its
# translation units compile with the same export macro the object library sets,
# so LMMS_EXPORT expands exactly as before (default visibility on ELF,
# dllexport on PE). The split must not move the ABI.
TARGET_COMPILE_DEFINITIONS(zene_api PRIVATE -Dlmmsobjs_EXPORTS)

# QtWidgets must not be reachable. The tree adds the Qt include directories at
# DIRECTORY scope (the top-level include_directories()), which every target
# inherits, so the widget entry is filtered out of THIS target's captured list.
# Qt6::Widgets is not linked either - that would put the directory straight
# back. Qt's own headers include each other module-qualified
# (`QtCore/qcontainertools_impl.h`), so the PARENT directory that holds
# QtCore/QtGui/QtXml stays; a module-qualified `<QtWidgets/...>` include is
# therefore still resolvable, which is what the ctest's include-closure and
# symbol checks are for.
GET_TARGET_PROPERTY(_zene_api_include_dirs zene_api INCLUDE_DIRECTORIES)
LIST(FILTER _zene_api_include_dirs EXCLUDE REGEX "[Ww]idgets")
SET_TARGET_PROPERTIES(zene_api PROPERTIES INCLUDE_DIRECTORIES "${_zene_api_include_dirs}")

# The pattern is "[Ww]idgets", not a path-shaped one, and that is measured, not
# stylistic: the link list names CMake TARGETS ("Qt6::Widgets"), whose "::" a
# pattern like "[Qq]t[0-9]*Widgets" does NOT match, so Qt6::Widgets stayed
# linked and its usage requirements put BOTH the include directory and
# -DQT_WIDGETS_LIB straight back into this target's flags.make - captured in the
# first build of this lane (zene_api.dir/flags.make showed
# `-isystem .../qt6/QtWidgets` and `-DQT_WIDGETS_LIB`). The unanchored
# "[Ww]idgets" matches the target name, the directory (.../qt6/QtWidgets) and a
# framework path (QtWidgets.framework/Headers) alike.
SET(_zene_api_qt_libs ${QT_LIBRARIES})
LIST(FILTER _zene_api_qt_libs EXCLUDE REGEX "[Ww]idgets")

# A name filter cannot see a module that drags QtWidgets in TRANSITIVELY, and one
# Qt module does exactly that: Qt5::Svg declares INTERFACE_LINK_LIBRARIES
# "Qt5::Widgets;Qt5::Gui;Qt5::Core" (measured on this machine in the Ubuntu 24.04
# kit's Qt5SvgConfig.cmake:152; Qt6 split QtSvgWidgets out, so Qt6::Svg depends on
# Qt6::Core and Qt6::Gui only). That is why the boundary was clean on the Qt6
# builds and dirty on every Qt5 CI job: run 35253612993's linux-x86_64 and both
# macOS jobs compiled all 42 boundary translation units with
# `-isystem /usr/include/x86_64-linux-gnu/qt5/QtWidgets` AND -DQT_WIDGETS_LIB -
# the widget include directory and the definition that arms
# include/zene/api/ControlApi.h's #error guard - while membership, include closure
# and object symbols all measured clean. Reproduced here before the fix: a Qt5
# configure (CMAKE_PREFIX_PATH=~/.local/opt/qt5/usr) put both on all 42 zene_api
# compile lines and the widget include on the MainWindow.cpp control line, exactly
# the CI reading. The closure is therefore walked and any REMAINING candidate that
# reaches a widget target is dropped, with the module and the target it reached
# printed at configure time - the reason belongs in the configure log, not in a
# build log nobody reads twice.
FUNCTION(_zene_api_drags_widgets library out_var)
	# ${library}'s interface closure (INTERFACE_LINK_LIBRARIES, transitively);
	# ${out_var} is the widget target it reaches, or "" when it reaches none.
	SET(_zene_api_queue "${library}")
	SET(_zene_api_seen "")
	SET(${out_var} "" PARENT_SCOPE)
	WHILE(_zene_api_queue)
		LIST(GET _zene_api_queue 0 _zene_api_head)
		LIST(REMOVE_AT _zene_api_queue 0)
		IF(_zene_api_head MATCHES "[Ww]idgets")
			SET(${out_var} "${_zene_api_head}" PARENT_SCOPE)
			RETURN()
		ENDIF()
		IF(NOT TARGET "${_zene_api_head}")
			CONTINUE()
		ENDIF()
		LIST(FIND _zene_api_seen "${_zene_api_head}" _zene_api_index)
		IF(_zene_api_index GREATER -1)
			CONTINUE()
		ENDIF()
		LIST(APPEND _zene_api_seen "${_zene_api_head}")
		GET_TARGET_PROPERTY(_zene_api_deps "${_zene_api_head}" INTERFACE_LINK_LIBRARIES)
		IF(_zene_api_deps)
			LIST(APPEND _zene_api_queue ${_zene_api_deps})
		ENDIF()
	ENDWHILE()
ENDFUNCTION()

SET(_zene_api_link_libs "")
FOREACH(_zene_api_qt_lib IN LISTS _zene_api_qt_libs)
	_zene_api_drags_widgets(${_zene_api_qt_lib} _zene_api_dragged)
	IF(_zene_api_dragged)
		MESSAGE(STATUS "zene_api: ${_zene_api_qt_lib} is NOT linked to the boundary - its interface "
			"closure reaches ${_zene_api_dragged}, which must not be reachable in this target")
	ELSE()
		LIST(APPEND _zene_api_link_libs ${_zene_api_qt_lib})
	ENDIF()
ENDFOREACH()
TARGET_LINK_LIBRARIES(zene_api ${_zene_api_link_libs})

# Every consumer of the object library - the executable, the test binaries, the
# Part C reference modules - links the boundary through the tree's own static
# dependency chain, so the link surface is unchanged.
target_static_libraries(lmmsobjs zene_api)

# The boundary compiles with the same feature DEFINITIONS the object library
# compiles with. Every LMMS_HAVE_* macro the closure uses arrives through the
# generated lmmsconfig.h - except LMMS_HAVE_WASM, which exists ONLY as this
# compile definition (lmmsobjs sets it in its IF(WANT_WASM) block) and which
# gates the registration of the wasm.* group in
# ControlRegistryRegistrations.cpp. Measured: with it missing here, the boundary
# silently compiled the group out and the live control.commands_list lost the
# eight wasm.* ids (340 -> 332) while every file still compiled - which is why
# the lane's behaviour proof is an id-set equality and not "the suite is green".
IF(WANT_WASM)
	TARGET_COMPILE_DEFINITIONS(zene_api PRIVATE LMMS_HAVE_WASM=1)
ENDIF()

# The ZENE_API_BOUNDARY definition arms include/zene/api/ControlApi.h's #error
# guard: a boundary TU compiled with QT_WIDGETS_LIB defined says why that is a
# defect rather than failing later at a link step.
TARGET_COMPILE_DEFINITIONS(zene_api PRIVATE ZENE_API_BOUNDARY)

ADD_EXECUTABLE(zene
	core/main.cpp
	"${WINRC}"
)
TARGET_INCLUDE_DIRECTORIES(zene
	PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
)
target_static_libraries(zene PUBLIC lmmsobjs)

# Enable updated C++ language standards
IF(MSVC)
	TARGET_COMPILE_OPTIONS(lmmsobjs PUBLIC "/Zc:__cplusplus" "/permissive-")
ENDIF()

# CMake doesn't define target_EXPORTS for OBJECT libraries.
# See the documentation of DEFINE_SYMBOL for details.
# Also add LMMS_STATIC_DEFINE for targets linking against it.
TARGET_COMPILE_DEFINITIONS(lmmsobjs
	PRIVATE -Dlmmsobjs_EXPORTS
)
target_static_definitions(lmmsobjs LMMS_STATIC_DEFINE)

# Set Visual Studio startup project to zene
# https://stackoverflow.com/a/37994396/8166701
IF(NOT CMAKE_VERSION VERSION_LESS 3.6)
	SET_PROPERTY(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT zene)
ENDIF()

SET_DIRECTORY_PROPERTIES(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${LMMS_RCC_OUT} lmmsconfig.h")

IF(LMMS_HAVE_WINMM)
	SET(EXTRA_LIBRARIES "winmm")
ENDIF()

IF(LMMS_BUILD_APPLE)
	SET(EXTRA_LIBRARIES "-framework CoreMIDI -framework CoreFoundation")
ENDIF()

if(LMMS_HAVE_OSS AND LMMS_BUILD_OPENBSD)
    SET(EXTRA_LIBRARIES "-lossaudio")
endif()

IF(LMMS_BUILD_HAIKU)
	SET(EXTRA_LIBRARIES "-lnetwork")
ENDIF()

if(LMMS_HAVE_LIBRT)
	list(APPEND EXTRA_LIBRARIES "rt")
endif()

if(LMMS_HAVE_PORTAUDIO)
	list(APPEND EXTRA_LIBRARIES portaudio::portaudio)
endif()

if(LMMS_HAVE_MP3LAME)
	list(APPEND EXTRA_LIBRARIES mp3lame::mp3lame)
endif()

if(LMMS_HAVE_OGGVORBIS)
	list(APPEND EXTRA_LIBRARIES Vorbis::vorbisenc Vorbis::vorbisfile)
endif()

if(LMMS_HAVE_LV2)
	list(APPEND EXTRA_LIBRARIES Lilv::lilv)
endif()

if(LMMS_HAVE_STEM_SPLIT)
	# Dev builds: default location of the reference separation CLI. Installed
	# packages must install tools/stem_split_cli.py; the runtime environment
	# variable LMMS_STEM_CLI overrides this path.
	target_compile_definitions(lmmsobjs PRIVATE
		LMMS_STEM_CLI_DEFAULT="${CMAKE_SOURCE_DIR}/tools/stem_split_cli.py")
endif()

SET(LMMS_REQUIRED_LIBS ${LMMS_REQUIRED_LIBS}
	${CMAKE_THREAD_LIBS_INIT}
	${QT_LIBRARIES}
	${ASOUND_LIBRARY}
	${SDL_LIBRARY}
	${SDL2_LIBRARY}
	${SOUNDIO_LIBRARY}
	${SNDIO_LIBRARIES}
	${PULSEAUDIO_LIBRARIES}
	${JACK_LIBRARIES}
	${SUIL_LIBRARIES}
	${FFTW3F_LIBRARIES}
	SampleRate::samplerate
	SndFile::sndfile
	${EXTRA_LIBRARIES}
)

target_link_libraries(lmmsobjs
	${LMMS_REQUIRED_LIBS}
)

# Optional ONNX Runtime dependency of the stem-separation feature. The imported
# target only exists when find_package(ONNXRuntime) succeeded at configure time.
IF(LMMS_HAVE_ONNXRUNTIME)
	TARGET_LINK_LIBRARIES(lmmsobjs ONNXRuntime::ONNXRuntime)
	TARGET_INCLUDE_DIRECTORIES(lmmsobjs SYSTEM PRIVATE ${ONNXRuntime_INCLUDE_DIRS})
ENDIF()

target_static_libraries(lmmsobjs ringbuffer lua luabridge)

set_target_properties(zene PROPERTIES
	ENABLE_EXPORTS ON
	WIN32_EXECUTABLE $<NOT:$<CONFIG:DEBUG>>
)

set_target_properties(lmmsobjs PROPERTIES AUTOUIC_SEARCH_PATHS "gui/modals")

IF(NOT WIN32 AND NOT LMMS_BUILD_APPLE)
	if(CMAKE_INSTALL_MANDIR)
		SET(INSTALL_MANDIR ${CMAKE_INSTALL_MANDIR})
	ELSE(CMAKE_INSTALL_MANDIR)
		SET(INSTALL_MANDIR share/man)
	ENDIF(CMAKE_INSTALL_MANDIR)
	INSTALL(FILES "${CMAKE_SOURCE_DIR}/doc/zene.1"
			DESTINATION "${INSTALL_MANDIR}/man1/"
			PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)
ENDIF()

INSTALL(TARGETS zene RUNTIME DESTINATION "${BIN_DIR}")
