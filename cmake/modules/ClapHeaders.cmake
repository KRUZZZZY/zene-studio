# ClapHeaders.cmake - pinned CLAP headers for native CLAP hosting
#
# CLAP (https://github.com/free-audio/clap) is MIT licensed, which makes
# in-process hosting compatible with LMMS' GPL-2.0-or-later licence without any
# exception (unlike VST2, which stays Vestige-only).
#
# PIN: tag 1.2.10, commit 195b42a004144fab0b3cf95e9c067187d15365b7
#      https://github.com/free-audio/clap.git
#
# The headers are NOT vendored into the repository (68 headers). Obtain them
# once into the build tree:
#
#   git clone --depth 1 --branch 1.2.10 \
#       https://github.com/free-audio/clap.git <build>/clap
#
# or select an existing checkout with -DLMMS_CLAP_PATH=<dir>. Setting
# -DLMMS_CLAP_FETCH=ON lets CMake fetch the pinned commit itself (needs network
# access at configure time; off by default so packaging stays offline-safe).

SET(LMMS_CLAP_TAG "1.2.10")
SET(LMMS_CLAP_COMMIT "195b42a004144fab0b3cf95e9c067187d15365b7")
SET(LMMS_CLAP_URL "https://github.com/free-audio/clap.git")
SET(LMMS_CLAP_PATH "${CMAKE_BINARY_DIR}/clap"
	CACHE PATH "CLAP header checkout (>= 1.2.10)")
SET(LMMS_CLAP_FETCH "OFF"
	CACHE BOOL "Fetch the pinned CLAP headers at configure time")

# NO_CMAKE_FIND_ROOT_PATH is required for the cross-compile jobs. The CLAP
# headers are third-party headers with no target-architecture content, and they
# are provisioned INSIDE the build tree (as the note above says), but the MinGW
# toolchain sets CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY, so a plain FIND_PATH
# re-roots the HINT below under the target sysroot and cannot see
# "${LMMS_CLAP_PATH}/include" at all. That is how the mingw64 job failed to
# configure while the native jobs found the very same checkout:
#   CMake Error at cmake/modules/ClapHeaders.cmake:45 (MESSAGE):
#     No CLAP headers at '<src>/build/clap/include/clap'.
# Un-roots the one HINT that points into the build tree; the ordinary default
# search paths stay rooted, so a host-installed clap/clap.h cannot be picked up
# in place of the pinned checkout.
FIND_PATH(LMMS_CLAP_INCLUDE_DIR clap/clap.h
	HINTS "${LMMS_CLAP_PATH}/include"
	NO_CMAKE_FIND_ROOT_PATH
	DOC "Directory containing clap/clap.h")

IF(NOT LMMS_CLAP_INCLUDE_DIR AND LMMS_CLAP_FETCH)
	INCLUDE(FetchContent)
	MESSAGE(STATUS "Fetching pinned CLAP ${LMMS_CLAP_TAG} (${LMMS_CLAP_COMMIT})")
	FETCHCONTENT_DECLARE(clap
		GIT_REPOSITORY "${LMMS_CLAP_URL}"
		GIT_TAG "${LMMS_CLAP_COMMIT}"
		GIT_SHALLOW TRUE
		SOURCE_DIR "${LMMS_CLAP_PATH}")
	FETCHCONTENT_MAKEAVAILABLE(clap)
	SET(LMMS_CLAP_INCLUDE_DIR "${LMMS_CLAP_PATH}/include")
ENDIF()

IF(NOT LMMS_CLAP_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR
		"No CLAP headers at '${LMMS_CLAP_PATH}/include/clap'. Obtain them with:\n"
		"  git clone --depth 1 --branch ${LMMS_CLAP_TAG} ${LMMS_CLAP_URL} ${LMMS_CLAP_PATH}\n"
		"or pass -DLMMS_CLAP_PATH=<existing checkout>, or -DLMMS_CLAP_FETCH=ON.")
ENDIF()

# --- version gate ---------------------------------------------------------
FILE(READ "${LMMS_CLAP_INCLUDE_DIR}/clap/version.h" LMMS_CLAP_VERSION_H)
IF(LMMS_CLAP_VERSION_H MATCHES "#define CLAP_VERSION_MAJOR +([0-9]+)")
	SET(LMMS_CLAP_VERSION_MAJOR "${CMAKE_MATCH_1}")
ENDIF()
IF(LMMS_CLAP_VERSION_H MATCHES "#define CLAP_VERSION_MINOR +([0-9]+)")
	SET(LMMS_CLAP_VERSION_MINOR "${CMAKE_MATCH_1}")
ENDIF()
IF(LMMS_CLAP_VERSION_H MATCHES "#define CLAP_VERSION_REVISION +([0-9]+)")
	SET(LMMS_CLAP_VERSION_REVISION "${CMAKE_MATCH_1}")
ENDIF()

IF(NOT DEFINED LMMS_CLAP_VERSION_MAJOR OR NOT DEFINED LMMS_CLAP_VERSION_MINOR)
	MESSAGE(FATAL_ERROR "Could not determine the CLAP version from "
		"${LMMS_CLAP_INCLUDE_DIR}/clap/version.h")
ENDIF()
IF(NOT LMMS_CLAP_VERSION_MAJOR EQUAL 1)
	MESSAGE(FATAL_ERROR "CLAP ${LMMS_CLAP_VERSION_MAJOR}.x is not supported; "
		"pin ${LMMS_CLAP_TAG} (${LMMS_CLAP_COMMIT}).")
ENDIF()
IF(LMMS_CLAP_VERSION_MINOR LESS 2)
	MESSAGE(FATAL_ERROR "CLAP 1.${LMMS_CLAP_VERSION_MINOR} is older than the "
		"pinned ${LMMS_CLAP_TAG}; pin the pinned commit (${LMMS_CLAP_COMMIT}).")
ENDIF()
MESSAGE(STATUS "Found CLAP ${LMMS_CLAP_VERSION_MAJOR}.${LMMS_CLAP_VERSION_MINOR}."
	"${LMMS_CLAP_VERSION_REVISION} headers (MIT) at ${LMMS_CLAP_INCLUDE_DIR}")

# --- licence gate: MIT, and no proprietary header drop-ins -----------------
GET_FILENAME_COMPONENT(LMMS_CLAP_ROOT "${LMMS_CLAP_INCLUDE_DIR}" DIRECTORY)
IF(EXISTS "${LMMS_CLAP_ROOT}/LICENSE")
	FILE(READ "${LMMS_CLAP_ROOT}/LICENSE" LMMS_CLAP_LICENSE_TEXT)
	IF(NOT LMMS_CLAP_LICENSE_TEXT MATCHES "MIT License")
		MESSAGE(FATAL_ERROR
			"${LMMS_CLAP_ROOT}/LICENSE is not the MIT licence. Only the MIT "
			"licensed CLAP headers may be used by LMMS.")
	ENDIF()
ELSE()
	MESSAGE(WARNING "No LICENSE file next to ${LMMS_CLAP_INCLUDE_DIR}; cannot "
		"verify that the CLAP headers are the MIT licensed upstream ones.")
ENDIF()

IF(NOT TARGET lmms_clap)
	ADD_LIBRARY(lmms_clap INTERFACE)
	TARGET_INCLUDE_DIRECTORIES(lmms_clap INTERFACE "${LMMS_CLAP_INCLUDE_DIR}")
ENDIF()
