# Zene: Threads::Threads must exist on every platform, not only the pthread ones.
#
# The top-level pthread check (CMakeLists.txt) is gated to LMMS_BUILD_LINUX /
# APPLE / OPENBSD / FREEBSD / HAIKU, and FindSDL2.cmake only looks for Threads in
# its non-config fallback - which MSYS2 clangarm64 skips, because the SDL2 package
# ships its own CMake config package (find_package(SDL2 CONFIG QUIET) succeeds and
# the module returns before that fallback). On Windows, then, nothing above the
# consumers defined Threads::Threads, and tests/CMakeLists.txt's SfzFilePoolTest
# (which links it for sfizz/abseil) failed at the generate step:
#   Target "SfzFilePoolTest" links to: Threads::Threads but the target was not found.
#
# The top-level includes this so the target exists everywhere the pthread gate used
# to provide it, for every consumer (tests, plugins, SDL2).
if(NOT TARGET Threads::Threads)
	find_package(Threads REQUIRED)
endif()
