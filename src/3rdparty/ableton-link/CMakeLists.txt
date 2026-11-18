# Ableton Link 4.1 (GPL-2.0-or-later) and the standalone asio 1.38.2 it bundles
# (BSL-1.0) - see README.lmms beside this file for the pin, the licences and the
# exact files taken (MANIFEST.tsv records every file's upstream blob identity).
#
# INTERFACE library: Link is header-only. This target carries what Link's own
# cmake_include/ConfigureAbletonLink.cmake and ConfigureAsioStandalone.cmake set
# for a consumer - the two include directories, the LINK_PLATFORM_* macro, the
# asio configuration (standalone, no RTTI type ids, and an inline version
# namespace so this copy of asio cannot clash with another one in the process) -
# and nothing else. Only ONE translation unit links it (src/core/AbletonLinkSession.cpp,
# built as its own static library in src/CMakeLists.txt), so none of these
# definitions reach the rest of the core. Because src/3rdparty is marked SYSTEM
# the include directories are system includes, so Link's and asio's own warnings
# do not trip -Werror in the consumer.
add_library(ableton_link INTERFACE)
target_include_directories(ableton_link SYSTEM INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/link/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/asio/include"
)

# The inline namespace is derived from the bundled asio version the same way
# upstream's ConfigureAsioStandalone.cmake derives it (ASIO_VERSION 103802 ->
# 1_38_2), with this tree's own prefix.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/asio/include/asio/version.hpp" _zene_asio_version_line
	REGEX "^#define ASIO_VERSION ")
string(REGEX MATCH "[0-9]+" _zene_asio_version "${_zene_asio_version_line}")
if(NOT _zene_asio_version MATCHES "^[0-9]+$")
	message(FATAL_ERROR "Could not read ASIO_VERSION from the vendored asio/version.hpp")
endif()
math(EXPR _zene_asio_major "${_zene_asio_version} / 100000")
math(EXPR _zene_asio_minor "${_zene_asio_version} / 100 % 1000")
math(EXPR _zene_asio_sub "${_zene_asio_version} % 100")

target_compile_definitions(ableton_link INTERFACE
	ASIO_STANDALONE=1
	ASIO_NO_TYPEID=1
	ASIO_VERSION_NAMESPACE=zene_link_asio_${_zene_asio_major}_${_zene_asio_minor}_${_zene_asio_sub}
)

if(UNIX)
	target_compile_definitions(ableton_link INTERFACE LINK_PLATFORM_UNIX=1)
endif()
if(APPLE)
	target_compile_definitions(ableton_link INTERFACE LINK_PLATFORM_MACOSX=1)
elseif(WIN32)
	# _WIN32_WINNT is what upstream pins on its own targets (link_pin_win32_winnt);
	# asio and Winsock must see it before their first header, so it is a definition.
	target_compile_definitions(ableton_link INTERFACE
		LINK_PLATFORM_WINDOWS=1
		INCL_EXTRA_HTON_FUNCTIONS=1
		_WIN32_WINNT=0x0601
	)
	# Upstream names these with #pragma comment(lib, ...), which only MSVC honours.
	target_link_libraries(ableton_link INTERFACE ws2_32 iphlpapi avrt)
else()
	target_compile_definitions(ableton_link INTERFACE LINK_PLATFORM_LINUX=1)
endif()

find_package(Threads REQUIRED)
target_link_libraries(ableton_link INTERFACE Threads::Threads)
