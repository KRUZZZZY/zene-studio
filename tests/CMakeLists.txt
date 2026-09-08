include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/TimelineTest.cpp
	src/tracks/AutomationTrackTest.cpp
)

# Offline stem separation tests (opt-in, see doc/STEM-SPLIT.md).
IF(LMMS_HAVE_STEM_SPLIT)
	LIST(APPEND LMMS_TESTS
		src/core/StemJobManagerTest.cpp
		src/core/StemModelStoreTest.cpp
		src/core/StemSplitPipelineTest.cpp
	)
	IF(LMMS_HAVE_ONNXRUNTIME)
		LIST(APPEND LMMS_TESTS src/core/OnnxRuntimeStemSeparatorTest.cpp)
	ENDIF()
ENDIF()

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME})

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
	# Test fixtures (stub ONNX model) and the reference CLI path.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LMMS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
	IF(LMMS_HAVE_STEM_SPLIT)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_STEM_CLI_DEFAULT="${CMAKE_SOURCE_DIR}/tools/stem_split_cli.py")
	ENDIF()
endforeach()
