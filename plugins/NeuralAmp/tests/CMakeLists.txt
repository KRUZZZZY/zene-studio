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

include(CTest)

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
SET(NEURALAMP_ENGINE_OPTIONS $<$<CONFIG:Debug>:-O2>
	# As for the plugin target (../CMakeLists.txt): vendored Eigen's NEON kernels trip this on
	# aarch64, and -DUSE_WERROR must not turn a diagnostic in unmodified vendored code into an error.
	"$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-error=deprecated-enum-enum-conversion>")

ADD_EXECUTABLE(nam_harness EXCLUDE_FROM_ALL
	nam_harness.cpp
	${NEURALAMP_ENGINE_SOURCES}
)
# libm is a Unix convention — MSVC keeps the math routines in the CRT, so asking for m.lib
# fails the link (see ../CMakeLists.txt). Undefined variables expand to nothing in CMake, so
# this keeps the three harness targets identical on Unix and correct on MSVC.
IF(NOT MSVC)
	SET(NEURALAMP_M_LIB m)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(nam_harness PRIVATE ${NEURALAMP_TEST_INCLUDES})
TARGET_COMPILE_FEATURES(nam_harness PRIVATE cxx_std_20)
TARGET_COMPILE_OPTIONS(nam_harness PRIVATE ${NEURALAMP_ENGINE_OPTIONS})
TARGET_LINK_LIBRARIES(nam_harness PRIVATE ${NEURALAMP_M_LIB} ${CMAKE_DL_LIBS})

# Keep the cases beside their harness and mirror them into the main test tree,
# which is where the project's targeted CTest command runs.
SET(NAM_LOADER_TEST_CASES
	valid
	dilation-zero
	dilation-negative
	dilation-overflow
	receptive-total-overflow
	channels-zero
	bottleneck-zero
	head-size-zero
)
SET_PROPERTY(GLOBAL PROPERTY ZENE_NAM_LOADER_TEST_CASES "${NAM_LOADER_TEST_CASES}")
FOREACH(NAM_LOADER_TEST_CASE IN LISTS NAM_LOADER_TEST_CASES)
	ADD_TEST(NAME "NamLoaderTest_${NAM_LOADER_TEST_CASE}"
		COMMAND nam_harness --test-loader-config "${NAM_LOADER_TEST_CASE}")
ENDFOREACH()

# Stage-level CPU profile of the engine. NAM_PROFILE_LAYERS is defined for this
# target only: it turns the NAM_PROF_* hooks in NamModel.cpp into per-stage
# CLOCK_THREAD_CPUTIME_ID accumulators. The plugin and nam_harness builds do not
# define it, so their code is unchanged.
# POSIX only: the accumulators read clock_gettime(CLOCK_THREAD_CPUTIME_ID), which Windows
# lacks. tests/CMakeLists.txt builds NeuralAmpProfileArgumentsTest only if(TARGET nam_profile).
IF(NOT WIN32)
	ADD_EXECUTABLE(nam_profile EXCLUDE_FROM_ALL
		nam_profile.cpp
		${NEURALAMP_ENGINE_SOURCES}
	)
	TARGET_INCLUDE_DIRECTORIES(nam_profile PRIVATE ${NEURALAMP_TEST_INCLUDES})
	TARGET_COMPILE_FEATURES(nam_profile PRIVATE cxx_std_20)
	TARGET_COMPILE_OPTIONS(nam_profile PRIVATE ${NEURALAMP_ENGINE_OPTIONS})
	TARGET_COMPILE_DEFINITIONS(nam_profile PRIVATE NAM_PROFILE_LAYERS)
	TARGET_LINK_LIBRARIES(nam_profile PRIVATE ${NEURALAMP_M_LIB} ${CMAKE_DL_LIBS})
ENDIF()

IF(UNIX AND NOT APPLE)
	ADD_EXECUTABLE(rt_alloc_probe EXCLUDE_FROM_ALL
		rt_alloc_probe.cpp
		${NEURALAMP_ENGINE_SOURCES}
	)
	TARGET_INCLUDE_DIRECTORIES(rt_alloc_probe PRIVATE ${NEURALAMP_TEST_INCLUDES})
	TARGET_COMPILE_FEATURES(rt_alloc_probe PRIVATE cxx_std_20)
	TARGET_COMPILE_OPTIONS(rt_alloc_probe PRIVATE ${NEURALAMP_ENGINE_OPTIONS})
	TARGET_LINK_LIBRARIES(rt_alloc_probe PRIVATE ${NEURALAMP_M_LIB} ${CMAKE_DL_LIBS})
	TARGET_LINK_OPTIONS(rt_alloc_probe PRIVATE
		"-Wl,--wrap=malloc"
		"-Wl,--wrap=calloc"
		"-Wl,--wrap=realloc"
		"-Wl,--wrap=free"
	)
ENDIF()
