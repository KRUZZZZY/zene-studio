include(BuildPlugin)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_sc_andor")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_return_statement")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_break_continue")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_comments")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_string_capabilities")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_rtl_io_file")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dexprtk_disable_rtl_vecops")
if (NOT MSVC)
	# -fexceptions is a GNU-style flag: MSVC ignores it ("cl : Command line
	# warning D9002" in CI run 34618027143) and enables C++ exceptions by default
	# (/EHsc), so only the compilers that understand the flag get it.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
endif()

if (LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)
	if (MSVC)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wa,-mbig-obj -Dexprtk_disable_enhanced_features")
	endif()
elseif (MSVC)
	# exprtk.hpp blows past the default section limit wherever it is compiled
	# with MSVC, not only on the Windows targets (CI msvc-x64: fatal error C1128).
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
endif()

add_library(exprtk INTERFACE)
target_include_directories(exprtk INTERFACE exprtk)
set_target_properties(exprtk PROPERTIES SYSTEM TRUE)

build_plugin(xpressive
	Xpressive.cpp
	ExprSynth.cpp
	Xpressive.h
	ExprSynth.h
	MOCFILES Xpressive.h
	EMBEDDED_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.png"
)
target_link_libraries(xpressive exprtk)
