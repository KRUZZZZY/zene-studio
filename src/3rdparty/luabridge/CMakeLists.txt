# LuaBridge 2.10 (MIT) -- header-only C++ <-> Lua binding layer, see LICENSE
#
# INTERFACE library: LuaBridge is header-only, so this target only carries the
# include directory. Because src/3rdparty is marked SYSTEM, the include dir is
# treated as a system include for consumers and LuaBridge's own warnings do
# not pollute the LMMS build.
add_library(luabridge INTERFACE)
target_include_directories(luabridge INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
