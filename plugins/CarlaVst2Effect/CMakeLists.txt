# Q050-481.2: VST2 effects hosted through Carla (a Carla Rack holding one VST2
# plug-in), beside plugins/CarlaVst2's instruments. Built wherever Carla Rack is:
# against the system Carla, or the weak stand-in library when there is none, in
# which case it lists and loads nothing at run time (lmms_plugin_main declines
# without a Carla runtime). Discovery is carlabase's, shared with the instrument
# host; the render worker is plugins/CarlaVst2's.
if(LMMS_HAVE_CARLA OR LMMS_HAVE_WEAKCARLA)
	INCLUDE(BuildPlugin)
	include_directories(SYSTEM ${CARLA_INCLUDE_DIRS})
	include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../CarlaBase" "${CMAKE_CURRENT_SOURCE_DIR}/../CarlaVst2")
	LINK_DIRECTORIES("${CMAKE_CURRENT_BINARY_DIR}/../CarlaBase"
                          ${CARLA_LIBRARY_DIRS})
	LINK_LIBRARIES(carlabase ${CARLA_LIBRARIES})
	BUILD_PLUGIN(carlavst2effect CarlaVst2Effect.cpp CarlaVst2Rack.cpp ../CarlaVst2/CarlaVst2Renderer.cpp
		CarlaVst2Effect.h CarlaVst2Rack.h ../CarlaVst2/CarlaVst2Renderer.h ../CarlaVst2/CarlaVst2SubPluginFeatures.h
		MOCFILES CarlaVst2Effect.h
		EMBEDDED_RESOURCES logo.png)
endif()
