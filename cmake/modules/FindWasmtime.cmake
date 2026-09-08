# FindWasmtime.cmake - locate the wasmtime C API (optional dependency)
#
# Copyright (c) 2026 LMMS WASM DSP sandbox contributors
#
# Redistribution and use is allowed according to the terms of the New BSD
# license. For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#
# The WASM DSP sandbox links against the wasmtime C API. This module looks for
# the headers and shared library of an unpacked official prebuilt release
# (see scripts/fetch-wasmtime.sh for the pinned version). It deliberately does
# NOT fail the configure step when the runtime is absent - the caller
# (top-level CMakeLists.txt) turns WANT_WASM off with a status message, so a
# clean `cmake -B build -DWANT_QT6=ON` works on a machine without wasmtime.
#
# Search order:
#   1. -DWASMTIME_ROOT=<prefix> (cache variable)
#   2. $ENV{WASMTIME_ROOT}
#   3. <source>/third_party/wasmtime   (where fetch-wasmtime.sh installs)
#
# Provides:
#   Wasmtime_FOUND
#   WASMTIME_INCLUDE_DIRS
#   WASMTIME_LIBRARIES

set(WASMTIME_ROOT "" CACHE PATH "Root of a wasmtime C API installation (contains include/ and lib/)")

set(_wasmtime_hints "")
if(WASMTIME_ROOT)
	list(APPEND _wasmtime_hints "${WASMTIME_ROOT}")
endif()
if(DEFINED ENV{WASMTIME_ROOT})
	list(APPEND _wasmtime_hints "$ENV{WASMTIME_ROOT}")
endif()
list(APPEND _wasmtime_hints "${CMAKE_SOURCE_DIR}/third_party/wasmtime")

find_path(Wasmtime_INCLUDE_DIR
	NAMES wasmtime.h
	HINTS ${_wasmtime_hints}
	PATH_SUFFIXES include
	DOC "wasmtime C API include directory"
)

find_library(Wasmtime_LIBRARY
	NAMES wasmtime
	HINTS ${_wasmtime_hints}
	PATH_SUFFIXES lib
	DOC "wasmtime C API library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Wasmtime
	DEFAULT_MSG
	Wasmtime_LIBRARY Wasmtime_INCLUDE_DIR
)

if(Wasmtime_FOUND)
	set(WASMTIME_INCLUDE_DIRS "${Wasmtime_INCLUDE_DIR}")
	set(WASMTIME_LIBRARIES "${Wasmtime_LIBRARY}")
endif()

mark_as_advanced(Wasmtime_INCLUDE_DIR Wasmtime_LIBRARY)
