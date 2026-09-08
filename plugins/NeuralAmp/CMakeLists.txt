INCLUDE(BuildPlugin)

SET(NEURALAMP_SOURCES
	NeuralAmpEffect.cpp
	NeuralAmpControls.cpp
	NeuralAmpControlDialog.cpp
	nam/NamModel.cpp
	nam/NamModelLoader.cpp
)

# RTNeural (MIT, header-only) is vendored under rtneural/ and supplies the
# linear-algebra backend (Eigen, MPL2) and the JSON parser (nlohmann/json, MIT)
# used by the in-plugin .nam WaveNet engine.
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
	EMBEDDED_RESOURCES artwork.svg
)

TARGET_LINK_LIBRARIES(neuralamp m)

# The inference loops are the hot path; make sure they are optimised even if the
# host build type is not Release.
TARGET_COMPILE_OPTIONS(neuralamp PRIVATE $<$<CONFIG:Debug>:-O2>)
