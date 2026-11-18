# WASM test dependency restored — 2026-10-06

Pinned archive SHA256: `67683d04b416a8b91f0e607e7b4c22bd32f18f947c10b5372eb8c277ae3b883a`.

`bash scripts/fetch-wasmtime.sh > build/wasmtime-fetch-2026-10-06.log 2>&1`: EXIT=0.
`source ~/.local/share/zene-bunny-fix/bunny/env.sh` then
`/usr/bin/cmake -S . -B build -DWANT_WASM=ON -DWASMTIME_ROOT=<this worktree>/third_party/wasmtime`: EXIT=0.

Same configured build directory, owned installation; WASM DSP sandbox enabled.
No WASM target has been built or executed at this checkpoint. Source bug claims remain unverified.
Private config/API keys were not accessed or copied.

## wasmtime-fetch-2026-10-06.log
Original transcript SHA256 `4eb2f0ef49a3da120a14ac1d2525e8679a1e68c530ccacd31d0268d747eb5191`.

```text
downloading https://github.com/bytecodealliance/wasmtime/releases/download/v48.0.1/wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
  0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0  0     0    0     0    0     0      0      0 --:--:-- --:--:-- --:--:--     0
 10 15.2M   10 1612k    0     0  2473k      0  0:00:06 --:--:--  0:00:06 2473k 63 15.2M   63 9989k    0     0  6046k      0  0:00:02  0:00:01  0:00:01 8377k100 15.2M  100 15.2M    0     0  6729k      0  0:00:02  0:00:02 --:--:-- 8384k
wasmtime-v48.0.1-x86_64-linux-c-api.tar.xz: OK
wasmtime C API v48.0.1 installed at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/third_party/wasmtime
total 100320
-rw-r--r-- 1 kruzzzzy kruzzzzy 71193834 Aug 24 20:45 libwasmtime.a
-rw-r--r-- 1 kruzzzzy kruzzzzy 31528984 Aug 24 20:45 libwasmtime.so

```

## wasmtime-configure-2026-10-06.log
Original transcript SHA256 `7f1e18de0e585845862c50dd9e653abbc60d7b72a6f115283cd0db0e5b0af07a`.

```text

Checking submodules...
-- Checking qt5-x11embed...
--   Found src/3rdparty/qt5-x11embed/CMakeLists.txt
-- Checking zynaddsubfx...
--   Found plugins/ZynAddSubFx/zynaddsubfx/CMakeLists.txt
-- Checking game-music-emu...
--   Found plugins/FreeBoy/game-music-emu/CMakeLists.txt
-- Checking adplug...
--   Found plugins/OpulenZ/adplug/Makefile.am
-- Checking veal...
--   Found plugins/LadspaEffect/calf/veal/CMakeLists.txt
-- Checking exprtk...
--   Found plugins/Xpressive/exprtk/Makefile
-- Checking ladspa...
--   Found plugins/LadspaEffect/swh/ladspa/Makefile.am
-- Checking tap-plugins...
--   Found plugins/LadspaEffect/tap/tap-plugins/Makefile
-- Checking weakjack...
--   Found src/3rdparty/weakjack/weakjack/.gitignore
-- Checking wiki...
--   Found doc/wiki/Home.md
-- Checking ringbuffer...
--   Found src/3rdparty/ringbuffer/CMakeLists.txt
-- Checking carla...
--   Found plugins/CarlaBase/carla/Makefile
-- Checking resid...
--   Found plugins/Sid/resid/resid/Makefile.am
-- Checking jack2...
--   Found src/3rdparty/jack2/.gitignore
-- Checking cmt...
--   Found plugins/LadspaEffect/cmt/cmt/.gitignore
-- Checking hiir...
--   Found src/3rdparty/hiir/hiir/license.txt
-- Checking portsmf...
--   Found plugins/MidiImport/portsmf/CMakeLists.txt
-- Done validating submodules.


Configuring ZENE
--------------------------
* Project version             : 0.4.0-alpha.350+7ba433d
*   Major version             : 0
*   Minor version             : 4
*   Release version           : 0
*   Stage version             : alpha
*   Build version             : 350+7ba433d
*

Optional Version Usage:
--------------------------
*   Override version:           -DFORCE_VERSION=x.x.x-x
*   Ignore Git information:     -DFORCE_VERSION=internal

PROCESSOR: x86_64
Machine: x86_64-linux-gnu

-- Target host is 64 bit, Intel
-- Stem separation: ONNX Runtime not found - C++ inference backend disabled, external-process backend enabled
-- Setting target microarchitecture to 'official'
-- Found Qt translations in /usr/share/qt6/translations
-- Checking for module 'suil-0'
--   Package 'suil-0', required by 'virtual:world', not found
-- Checking for module 'carla-native-plugin'
--   Package 'carla-native-plugin', required by 'virtual:world', not found
-- Checking for module 'carla-standalone>=1.9.5'
--   Package 'carla-standalone', required by 'virtual:world', not found
-- Could NOT find STK (missing: STK_RAWWAVE_ROOT) 
-- Could NOT find Portaudio (missing: Portaudio_LIBRARY Portaudio_INCLUDE_DIRS) 
-- Could NOT find SoundIo (missing: SOUNDIO_LIBRARY SOUNDIO_INCLUDE_DIR) 
-- Could NOT find Lame (missing: Lame_LIBRARY Lame_INCLUDE_DIRS) 
-- Found ALSA: /usr/lib/x86_64-linux-gnu/libasound.so
-- Could NOT find Wine (missing: WINE_CXX WINE_INCLUDE_DIRS) 
--   WINE_INCLUDE_DIR:     WINE_INCLUDE_DIR-NOTFOUND
--   WINE_CXX:             WINE_CXX-NOTFOUND
--   WINE_GCC:             WINE_GCC-NOTFOUND
--   WINE_32_FLAGS:        
--   WINE_64_FLAGS:        
--   WINE_VERSION:         
--   WINE_ASLR_ENABLED:    
-- Using Git to generate the CONTRIBUTORS file
-- Using /home/kruzzzzy/.local/bin/ccache for compiler caching
CMake Warning (dev) at CMakeLists.txt:1016 (FIND_PACKAGE):
  Policy CMP0144 is not set: find_package uses upper-case <PACKAGENAME>_ROOT
  variables.  Run "cmake --help-policy CMP0144" for policy details.  Use the
  cmake_policy command to set the policy and suppress this warning.

  CMake variable WASMTIME_ROOT is set to:

    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/third_party/wasmtime

  For compatibility, find_package is ignoring the variable, but code in a
  .cmake module might still use it.
This warning is for project developers.  Use -Wno-dev to suppress it.

-- Found Wasmtime: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/third_party/wasmtime/lib/libwasmtime.so  
-- WASM C demo module: disabled (zig not found on PATH)
-- Found CLAP 1.2.10 headers (MIT) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/build/clap/include
-- Found CLAP 1.2.10 headers (MIT) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/build/clap/include
-- Found VST3 SDK 3.8 (MIT) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/build/vst3sdk
-- Found VST3 SDK 3.8 (MIT) at /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/build/vst3sdk
CMake Warning at data/locale/CMakeLists.txt:10 (MESSAGE):
  Cannot generate locales



Installation Summary
--------------------
* Install Directory           : /usr/local

Supported audio interfaces
--------------------------
* ALSA                        : OK
* JACK                        : OK (weak linking enabled)
* OSS                         : OK
* Sndio                       : OK
* Rubber Band stretch         : OK (3.3.0)
* AAF import (LibAAF)         : OK (v0.0.0-unknown)
* FFmpeg video track          : OK (libavformat 60.16.100)
* PortAudio                   : not found, please install portaudio19-dev (or similar, version >= 1.9) ;if you require PortAudio support
* libsoundio                  : not found, please install libsoundio if you require libsoundio support
* PulseAudio                  : OK
* SDL                         : OK

Supported MIDI interfaces
-------------------------
* ALSA                        : OK
* OSS                         : OK
* Sndio                       : OK
* JACK                        : OK (weak linking enabled)
* WinMM                       : <not supported on this platform>
* AppleMidi                   : <not supported on this platform>

Supported file formats for project export
-----------------------------------------
* WAVE                        : OK
* FLAC                        : OK
* OGG/VORBIS                  : OK
* MP3/Lame                    : not found, please install libmp3lame-dev (or similar)

Optional plugins
----------------
* Lv2 plugins                 : OK
* SUIL for plugin UIs         : not found, install it or set PKG_CONFIG_PATH appropriately
* ZynAddSubFX instrument      : OK
* Carla Patchbay & Rack       : OK (weak linking enabled)
* SoundFont2 player           : OK
* Sid instrument              : OK
* Stk Mallets                 : not found, please install libstk-dev or libstk0-dev (or similar);if you require the Mallets instrument
* VST plugin host             : OK
  * 32-bit Windows host       : not found, please install (lib)wine-dev (or similar) - 64 bit systems additionally need gcc-multilib and g++-multilib
  * 64-bit Windows host       : not found, please install (lib)wine-dev (or similar) - 64 bit systems additionally need gcc-multilib and g++-multilib
* CALF LADSPA plugins         : OK
* CAPS LADSPA plugins         : OK
* CMT LADSPA plugins          : OK
* TAP LADSPA plugins          : OK
* SWH LADSPA plugins          : OK
* GIG player                  : OK

Developer options
-----------------------------------------
* Debug FP exceptions               : Disabled
* Debug using AddressSanitizer      : Disabled
* Debug using ThreadSanitizer       : Disabled
* Debug using RealtimeSanitizer     : Disabled
* Debug using MemorySanitizer       : Disabled
* Debug using UBSanitizer           : Disabled
* Debug packaging commands          : Enabled
* Profile using GNU profiler        : Disabled 
* Test coverage instrumentation     : Disabled
* Debug assertions                  : Disabled
* Experimental Qt6 support          : Enabled
* WASM DSP sandbox                  : Enabled


-----------------------------------------------------------------
IMPORTANT:
after installing missing packages, remove CMakeCache.txt before
running cmake again!
-----------------------------------------------------------------



-- Configuring done (1.9s)
-- Generating done (7.1s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-script/build

```
