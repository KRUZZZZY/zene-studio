SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
SET(CMAKE_DEBUG_POSTFIX "")

# Enable C++20
SET(CMAKE_CXX_STANDARD 20)

IF(LMMS_BUILD_APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
ENDIF()

include_directories("${CMAKE_BINARY_DIR}/src")

# See cmake/modules/PluginList.cmake
FOREACH(PLUGIN ${PLUGIN_LIST})
	ADD_SUBDIRECTORY(${PLUGIN})
ENDFOREACH()

# The Zene Rack product plug-in is a VST3 bundle for OTHER hosts, not one of
# Zene's own modules, so it is not in PLUGIN_LIST. It needs the pinned VST3
# SDK, which Vst3Effect creates as lmms_vst3_sdk when that module is configured.
IF(TARGET lmms_vst3_sdk)
	ADD_SUBDIRECTORY(ZeneRack)
ENDIF()
