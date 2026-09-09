# FindONNXRuntime.cmake - locate the ONNX Runtime C/C++ SDK
#
# ONNX Runtime is an *optional* dependency of the offline stem-separation
# feature (WANT_STEM_SPLIT). It is MIT licensed, so it is GPLv2-compatible.
# This module never hard-fails: if the runtime is not present the feature
# degrades to the external-process inference backend and configuration still
# succeeds (see doc/STEM-SPLIT.md, "Integration policy").
#
# Usage:
#   find_package(ONNXRuntime QUIET)
#   target_link_libraries(foo PRIVATE ONNXRuntime::ONNXRuntime)
#
# Hints (checked in order):
#   ONNXRUNTIME_ROOT  - CMake cache variable or environment variable pointing
#                       at an extracted onnxruntime-linux-x64-<ver> tree or an
#                       install prefix
#   ONNXRUNTIME_DIR   - same, alternative spelling
#   pkg-config "onnxruntime" (for distro packages that ship a .pc file)
#
# Result variables:
#   ONNXRuntime_FOUND        - TRUE if headers and library were found
#   ONNXRuntime_INCLUDE_DIRS - include directories (SDK layout or repo layout)
#   ONNXRuntime_LIBRARIES    - libraries to link
#   ONNXRuntime_VERSION      - version string when detectable, else "unknown"
#
# Imported target:
#   ONNXRuntime::ONNXRuntime

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(PC_ONNXRUNTIME QUIET onnxruntime)
endif()

# Collect user-provided hints. Both the cache variable and the environment
# variable are honoured; an empty entry is ignored.
set(_onnxruntime_hints "")
foreach(_hint IN ITEMS
	"${ONNXRUNTIME_ROOT}"
	"$ENV{ONNXRUNTIME_ROOT}"
	"${ONNXRUNTIME_DIR}"
	"$ENV{ONNXRUNTIME_DIR}")
	if(_hint)
		list(APPEND _onnxruntime_hints "${_hint}")
	endif()
endforeach()

# The official prebuilt archives flatten the headers into <root>/include/,
# while a from-source tree keeps them under
# include/onnxruntime/core/session/. Accept both. Both the C++ wrapper header
# and the C header must live in the same directory for the wrapper to compile.
find_path(ONNXRuntime_INCLUDE_DIR
	NAMES onnxruntime_cxx_api.h onnxruntime_c_api.h
	HINTS ${_onnxruntime_hints} ${PC_ONNXRUNTIME_INCLUDE_DIRS}
	PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session
)

find_library(ONNXRuntime_LIBRARY
	NAMES onnxruntime
	HINTS ${_onnxruntime_hints} ${PC_ONNXRUNTIME_LIBRARY_DIRS}
	PATH_SUFFIXES lib lib64 lib/x64
)

# Version detection: the prebuilt archives ship a VERSION_NUMBER file at the
# archive root; distro packages may set it via pkg-config. Fall back to
# "unknown" rather than guessing from ORT_API_VERSION (not a 1:1 mapping).
set(ONNXRuntime_VERSION "unknown")
foreach(_root IN LISTS _onnxruntime_hints)
	if(EXISTS "${_root}/VERSION_NUMBER")
		file(STRINGS "${_root}/VERSION_NUMBER" ONNXRuntime_VERSION LIMIT_COUNT 1)
		break()
	endif()
endforeach()
if(ONNXRuntime_VERSION STREQUAL "unknown" AND PC_ONNXRUNTIME_VERSION)
	set(ONNXRuntime_VERSION "${PC_ONNXRUNTIME_VERSION}")
endif()

find_package_handle_standard_args(ONNXRuntime
	REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR
	VERSION_VAR ONNXRuntime_VERSION
	FAIL_MESSAGE "ONNX Runtime not found - the C++ inference backend will be disabled"
)

if(ONNXRuntime_FOUND)
	set(ONNXRuntime_INCLUDE_DIRS "${ONNXRuntime_INCLUDE_DIR}")
	set(ONNXRuntime_LIBRARIES ONNXRuntime::ONNXRuntime)
	if(NOT TARGET ONNXRuntime::ONNXRuntime)
		add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
		set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
			IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
		)
	endif()
endif()

mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY)
