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

TARGET_LINK_LIBRARIES(neuralamp m)

# The inference loops are the hot path; make sure they are optimised even if the
# host build type is not Release.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE $<$<CONFIG:Debug>:-O2>)

# Headless verification/benchmark binaries (EXCLUDE_FROM_ALL, see tests/).
ADD_SUBDIRECTORY(tests)
