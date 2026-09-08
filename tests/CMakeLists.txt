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
	src/core/RelativePathsTest.cpp
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
endforeach()

# Native VST3 hosting tests. They only exist when the MIT licensed VST3 SDK
# (>= 3.8) is available; see cmake/modules/Vst3Sdk.cmake.
if(TARGET vst3effect)
	set(LMMS_VST3_EFFECT_DIR ${CMAKE_SOURCE_DIR}/plugins/Vst3Effect)
	set(LMMS_VST3_TEST_PLUGIN "${CMAKE_BINARY_DIR}/vst3sdk/build/VST3/Release/again.vst3"
		CACHE PATH "VST3 plug-in used by Vst3HostTest")

	add_executable(Vst3BusMapTest
		src/plugins/Vst3BusMapTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
	)
	add_test(NAME Vst3BusMapTest COMMAND Vst3BusMapTest)
	target_include_directories(Vst3BusMapTest PRIVATE ${LMMS_VST3_EFFECT_DIR})
	target_compile_features(Vst3BusMapTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3BusMapTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})

	add_executable(Vst3HostTest
		src/plugins/Vst3HostTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
	)
	add_test(NAME Vst3HostTest COMMAND Vst3HostTest)
	target_include_directories(Vst3HostTest PRIVATE ${LMMS_VST3_EFFECT_DIR})
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3HostTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY} lmms_vst3_sdk)
	target_compile_definitions(Vst3HostTest PRIVATE VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_PLUGIN}")

	add_executable(Vst3EffectIntegrationTest
		src/plugins/Vst3EffectIntegrationTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Effect.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControlDialog.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControls.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Parameter.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3SubPluginFeatures.cpp
	)
	add_test(NAME Vst3EffectIntegrationTest COMMAND Vst3EffectIntegrationTest)
	target_include_directories(Vst3EffectIntegrationTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_VST3_EFFECT_DIR}
	)
	target_static_libraries(Vst3EffectIntegrationTest PRIVATE lmmsobjs)
	target_compile_features(Vst3EffectIntegrationTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3EffectIntegrationTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
	)
	target_compile_definitions(Vst3EffectIntegrationTest PRIVATE
		LMMS_TESTING
		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_PLUGIN}"
	)
endif()
