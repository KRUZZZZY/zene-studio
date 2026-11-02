#!/usr/bin/env bash
# build-carla-runtime.sh - build a Carla runtime from the bundled submodule for
# the VST2-through-Carla tests (Q050-481.1).
#
# Usage: tools/build-carla-runtime.sh <out-dir> [<llvm-mingw-or-mingw-w64 bin dir>]
#
# The product links Carla weakly: a build without a system Carla ships a stand-in
# libcarla_native-plugin that hosts nothing. CarlaVst2HostingTest needs a REAL
# runtime, so this builds one out of plugins/CarlaBase/carla (the pinned
# submodule) into <out-dir>/lib:
#   libcarla_native-plugin.so  the Carla Rack the product instantiates
#   libcarla_host-plugin.so    the CarlaHost.h API the VST2 host drives it with
#   carla-bridge-native, carla-discovery-native
# and, when a Windows cross toolchain is given (llvm-mingw or mingw-w64; Carla's
# Makefile needs a C++ PE compiler, which winegcc alone is not):
#   carla-bridge-win64.exe, carla-discovery-win64.exe, jackbridge-wine64.dll
# which is what runs a Windows VST2 under Wine.
#
# The test finds the runtime at <build>/carla-runtime/lib (the CMake cache
# variable ZENE_CARLA_RUNTIME_DIR), so the usual call is
#   tools/build-carla-runtime.sh build/carla-runtime [path/to/llvm-mingw/bin]
# Sources are copied to <out-dir>/src-native and <out-dir>/src-win (the submodule
# is never built in place), so a second run is incremental.
#
# -fno-strict-aliasing -fno-lifetime-dse: measured on GCC 16.2 (2026-11-02) - the
# optimised build of this Carla snapshot corrupts the heap inside
# carla_get_library_folder() (water's String/File code), which the product calls
# whenever it creates a Carla Rack; glibc's malloc checker aborts on it at once,
# and the product test crashed in 2 of 3 runs. The DEBUG build and the optimised
# build with these two flags are clean.
set -euo pipefail

out="${1:?usage: $0 <out-dir> [<windows-toolchain-bin-dir>]}"
toolchain="${2:-}"
here="$(cd "$(dirname "$0")/.." && pwd)"
carla="$here/plugins/CarlaBase/carla"
jobs="${JOBS:-4}"
[ -f "$carla/Makefile" ] || { echo "no Carla submodule at $carla (git submodule update --init)" >&2; exit 2; }
mkdir -p "$out/lib"
out="$(cd "$out" && pwd)"

# Everything optional off: no frontend, no Qt, no JUCE - only the engine, the
# rack plug-in, the bridges and discovery.
features=(HAVE_FRONTEND=false HAVE_PYQT=false HAVE_QT4=false HAVE_QT5=false HAVE_QT6=false
	HAVE_JUCE=false USING_JUCE=false HAVE_FLUIDSYNTH=false HAVE_YSFX=false HAVE_HYLIA=false
	HAVE_DGL=false HAVE_LIBLO=false HAVE_SNDFILE=false HAVE_LIBMAGIC=false)

native_flags="-fno-strict-aliasing -fno-lifetime-dse"
rsync -a --exclude .git "$carla/" "$out/src-native/"
CFLAGS="${CFLAGS:-} $native_flags" CXXFLAGS="${CXXFLAGS:-} $native_flags" \
	make -C "$out/src-native" -j"$jobs" "${features[@]}" backend plugin bridges-plugin discovery
cp -a "$out/src-native/bin/libcarla_native-plugin.so" "$out/src-native/bin/libcarla_host-plugin.so" \
	"$out/src-native/bin/carla-bridge-native" "$out/src-native/bin/carla-discovery-native" "$out/lib/"

if [ -n "$toolchain" ]; then
	# Carla's Wine bridge: a PE build of the bridge and discovery tools with
	# BUILDING_FOR_WINE, plus jackbridge-wine64.dll (a winelib DLL built with
	# winegcc) that lets the bridge talk to the Linux side.
	rsync -a --exclude .git "$carla/" "$out/src-win/"
	# clang (llvm-mingw) has no -posix; mingw-w64 gcc ignores its absence.
	sed -i 's/^BASE_FLAGS += -posix$/# BASE_FLAGS += -posix/' "$out/src-win/source/Makefile.mk"
	PATH="$toolchain:$PATH" make -C "$out/src-win" -j"$jobs" WINDOWS=true LINUX=false \
		CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ AR=x86_64-w64-mingw32-ar "${features[@]}" win64
	make -C "$out/src-native" wine64
	cp -a "$out/src-win/bin/carla-bridge-win64.exe" "$out/src-win/bin/carla-discovery-win64.exe" \
		"$out/src-native/bin/jackbridge-wine64.dll" "$out/lib/"
fi
echo "Carla runtime in $out/lib:"
ls "$out/lib"
