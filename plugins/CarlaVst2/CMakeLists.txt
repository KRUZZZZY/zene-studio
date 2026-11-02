# Q050-481.1: VST2 instruments hosted through Carla (a Carla Rack holding one
# VST2 plug-in). Built wherever Carla Rack is: against the system Carla, or the
# weak stand-in library when there is none, in which case it lists and loads
# nothing at run time (lmms_plugin_main declines without a Carla runtime).
if(LMMS_HAVE_CARLA OR LMMS_HAVE_WEAKCARLA)
	INCLUDE(BuildPlugin)
	include_directories(SYSTEM ${CARLA_INCLUDE_DIRS})
	include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../CarlaBase")
	LINK_DIRECTORIES("${CMAKE_CURRENT_BINARY_DIR}/../CarlaBase"
                          ${CARLA_LIBRARY_DIRS})
	LINK_LIBRARIES(carlabase ${CARLA_LIBRARIES})
	BUILD_PLUGIN(carlavst2 CarlaVst2.cpp CarlaVst2Discovery.cpp CarlaVst2Instrument.h CarlaVst2Discovery.h
		MOCFILES CarlaVst2Instrument.h
		EMBEDDED_RESOURCES logo.png)
endif()
