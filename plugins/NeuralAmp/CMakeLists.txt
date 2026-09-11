INCLUDE(BuildPlugin)

SET(NEURALAMP_SOURCES
	NeuralAmpEffect.cpp
	NeuralAmpControls.cpp
	NeuralAmpControlDialog.cpp
	nam/NamModel.cpp
	nam/NamModelLoader.cpp
)

# RTNeural (BSD-3-Clause, header-only) is vendored under rtneural/. This plugin
# uses only two of its vendored submodules: Eigen (MPL-2.0, linear algebra) and
# nlohmann/json (MIT, .nam parsing). See LICENSE-NOTICE.md for the inventory.
# RTNeural's own layer code is not used (it has no .nam loader).
INCLUDE_DIRECTORIES(BEFORE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/rtneural/modules/Eigen"
	"${CMAKE_CURRENT_SOURCE_DIR}/rtneural/modules/json"
)

BUILD_PLUGIN(neuralamp
	${NEURALAMP_SOURCES}
	MOCFILES
		NeuralAmpEffect.h
		NeuralAmpControls.h
		NeuralAmpControlDialog.h
)

# libm is a Unix convention: on MSVC the math routines live in the CRT, and asking for
# m.lib fails the link outright (CI msvc-x64: "LNK1104: cannot open file 'm.lib'").
# Upstream guards its own 'm' links the same way — plugins/LadspaEffect/{swh,tap}.
TARGET_LINK_LIBRARIES(neuralamp ${CMAKE_DL_LIBS})
IF(NOT MSVC)
	TARGET_LINK_LIBRARIES(neuralamp m)
ENDIF()

# The inference loops are the hot path; make sure they are optimised even if the
# host build type is not Release.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE $<$<CONFIG:Debug>:-O2>)

# Vendored Eigen (rtneural/modules/Eigen) trips -Wdeprecated-enum-enum-conversion in its
# NEON kernels on aarch64 — the linux-arm64 CI job — and with -DUSE_WERROR that vendored
# diagnostic becomes an error in code this repo does not modify. Keep the warning, drop
# its error status for this target only.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE
	"$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-error=deprecated-enum-enum-conversion>")

# Headless verification/benchmark binaries (EXCLUDE_FROM_ALL, see tests/).
ADD_SUBDIRECTORY(tests)
