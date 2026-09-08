# Headless verification binaries for the NeuralAmp .nam engine.
#
# These are EXCLUDE_FROM_ALL: a normal LMMS build does not compile them. Build
# them explicitly, e.g.
#
#   cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON
#   cmake --build build-rel -j4 --target nam_harness
#   cmake --build build-rel -j4 --target rt_alloc_probe
#
# nam_harness    loads a real .nam model, processes a deterministic signal,
#                reports CPU per block, and can compare sample-exactly against a
#                reference render produced by the upstream NeuralAmpModelerCore
#                `render` tool (--compare-ref).
# rt_alloc_probe asserts that NamModel::process() performs no heap allocation
#                (Linux only: interposes malloc/calloc/realloc/free with ld
#                --wrap and replaces global operator new/delete).

SET(NEURALAMP_ENGINE_SOURCES
	"${CMAKE_CURRENT_SOURCE_DIR}/../nam/NamModel.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/../nam/NamModelLoader.cpp"
)

SET(NEURALAMP_TEST_INCLUDES
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
	"${CMAKE_CURRENT_SOURCE_DIR}/../rtneural/modules/Eigen"
	"${CMAKE_CURRENT_SOURCE_DIR}/../rtneural/modules/json"
)

# The same engine options as the plugin target so measurements describe the
# code that actually ships in libneuralamp.so.
SET(NEURALAMP_ENGINE_OPTIONS $<$<CONFIG:Debug>:-O2>)

ADD_EXECUTABLE(nam_harness EXCLUDE_FROM_ALL
	nam_harness.cpp
	${NEURALAMP_ENGINE_SOURCES}
)
TARGET_INCLUDE_DIRECTORIES(nam_harness PRIVATE ${NEURALAMP_TEST_INCLUDES})
TARGET_COMPILE_FEATURES(nam_harness PRIVATE cxx_std_20)
TARGET_COMPILE_OPTIONS(nam_harness PRIVATE ${NEURALAMP_ENGINE_OPTIONS})
TARGET_LINK_LIBRARIES(nam_harness PRIVATE m)

IF(UNIX AND NOT APPLE)
	ADD_EXECUTABLE(rt_alloc_probe EXCLUDE_FROM_ALL
		rt_alloc_probe.cpp
		${NEURALAMP_ENGINE_SOURCES}
	)
	TARGET_INCLUDE_DIRECTORIES(rt_alloc_probe PRIVATE ${NEURALAMP_TEST_INCLUDES})
	TARGET_COMPILE_FEATURES(rt_alloc_probe PRIVATE cxx_std_20)
	TARGET_COMPILE_OPTIONS(rt_alloc_probe PRIVATE ${NEURALAMP_ENGINE_OPTIONS})
	TARGET_LINK_LIBRARIES(rt_alloc_probe PRIVATE m)
	TARGET_LINK_OPTIONS(rt_alloc_probe PRIVATE
		"-Wl,--wrap=malloc"
		"-Wl,--wrap=calloc"
		"-Wl,--wrap=realloc"
		"-Wl,--wrap=free"
	)
ENDIF()
