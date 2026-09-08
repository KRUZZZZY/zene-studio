include(CTest)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

set(LMMS_TESTS
	src/core/ArrayVectorTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AudioBusHandleTest.cpp
	src/core/AudioPortsTest.cpp
	src/core/AutomatableModelTest.cpp
	src/core/MathTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RecordRingBufferTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/RoutingGraphTest.cpp
	src/core/TimelineTest.cpp
	src/core/TwoTrackRecordingHarness.cpp
	src/tracks/AutomationTrackTest.cpp
)

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
endforeach()

# Real-hardware capture probe (task #556). Deliberately NOT registered with
# CTest: it needs a capture device and records live audio, so it is run
# manually (see RECORDING-PROTOTYPE.md).
add_executable(TwoTrackAlsaCaptureProbe src/core/TwoTrackAlsaCaptureProbe.cpp)
target_include_directories(TwoTrackAlsaCaptureProbe PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
target_static_libraries(TwoTrackAlsaCaptureProbe PRIVATE lmmsobjs)
target_link_libraries(TwoTrackAlsaCaptureProbe PRIVATE
	${QT_LIBRARIES}
	${ASOUND_LIBRARY}
)
target_compile_features(TwoTrackAlsaCaptureProbe PRIVATE cxx_std_20)
target_compile_definitions(TwoTrackAlsaCaptureProbe PRIVATE LMMS_TESTING)
