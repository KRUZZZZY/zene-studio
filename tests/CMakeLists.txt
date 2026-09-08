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
	src/core/ScriptEngineTest.cpp
	src/core/TimelineTest.cpp
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
	# Lua scripting examples live in the source tree; tests run from build/tests.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LUA_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/data/scripts")

	# ScriptEngineTest loads a real instrument plugin (tripleoscillator) from the
	# build tree to prove the Lua Instrument binding (spec G4). Plugin modules
	# resolve LMMS core symbols from the host process at load time, so the test
	# binary must export its symbols exactly like the lmms executable does.
	if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
	endif()
endforeach()
