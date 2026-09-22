
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
* Project version             : 0.3.0-alpha.52+4ef3065
*   Major version             : 0
*   Minor version             : 3
*   Release version           : 0
*   Stage version             : alpha
*   Build version             : 52+4ef3065
*

Optional Version Usage:
--------------------------
*   Override version:           -DFORCE_VERSION=x.x.x-x
*   Ignore Git information:     -DFORCE_VERSION=internal

PROCESSOR: x86_64
Machine: x86_64-linux-gnu

-- Target host is 64 bit, Intel
-- Setting target microarchitecture to 'none'
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
CMake Warning (dev) at CMakeLists.txt:963 (FIND_PACKAGE):
  Policy CMP0144 is not set: find_package uses upper-case <PACKAGENAME>_ROOT
  variables.  Run "cmake --help-policy CMP0144" for policy details.  Use the
  cmake_policy command to set the policy and suppress this warning.

  CMake variable WASMTIME_ROOT is set to:

    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/third_party/wasmtime

  For compatibility, find_package is ignoring the variable, but code in a
  .cmake module might still use it.
This warning is for project developers.  Use -Wno-dev to suppress it.

-- WASM C demo module: disabled (zig not found on PATH)
-- CLAP hosting skipped: no headers at '/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build/clap/include/clap' (clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)
-- CLAP instrument hosting skipped: no headers at '/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build/clap/include/clap' (clone instructions in cmake/modules/ClapHeaders.cmake, then re-run cmake)
-- VST3 hosting skipped: no SDK at '/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build/vst3sdk' (clone instructions in cmake/modules/Vst3Sdk.cmake, then re-run cmake)
-- VST3 hosting skipped: no SDK at '/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build/vst3sdk' (clone instructions in cmake/modules/Vst3Sdk.cmake, then re-run cmake)
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
* Debug using MemorySanitizer       : Disabled
* Debug using UBSanitizer           : Disabled
* Debug packaging commands          : Disabled
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



-- Configuring done (1.6s)
-- Generating done (2.7s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target weakjack
[  0%] Built target ringbuffer
[  2%] Built target lua
[  2%] Built target wasm-wat2wasm_autogen_timestamp_deps
[  2%] Built target carla_native-plugin
[  2%] Built target tap_autopan
[  2%] Built target tap_chorusflanger
[  2%] Built target caps
[  3%] Built target tap_deesser
[  3%] Built target tap_doubler
[  3%] Built target tap_dynamics_m
[  3%] Built target tap_dynamics_st
[  3%] Built target tap_echo
[  3%] Built target tap_eq
[  3%] Built target tap_eqbw
[  3%] Built target tap_limiter
[  3%] Built target tap_pinknoise
[  3%] Built target tap_pitch
[  3%] Built target tap_reflector
[  3%] Built target tap_reverb
[  3%] Built target tap_rotspeak
[  3%] Built target tap_sigmoid
[  3%] Built target tap_tremolo
[  4%] Built target tap_tubewarmth
[  4%] Built target tap_vibrato
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/alias_1407.dir/ladspa/alias_1407.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/allpass_1895.dir/ladspa/allpass_1895.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/am_pitchshift_1433.dir/ladspa/am_pitchshift_1433.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/amp_1181.dir/ladspa/amp_1181.c.o
[  4%] Linking C shared module ../../ladspa/alias_1407.so
[  4%] Linking C shared module ../../ladspa/allpass_1895.so
[  4%] Linking C shared module ../../ladspa/am_pitchshift_1433.so
[  4%] Linking C shared module ../../ladspa/amp_1181.so
[  4%] Built target alias_1407
[  4%] Built target allpass_1895
[  4%] Built target am_pitchshift_1433
[  4%] Built target amp_1181
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/blo.dir/ladspa/util/blo.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bode_shifter_1431.dir/ladspa/bode_shifter_1431.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/iir.dir/ladspa/util/iir.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bode_shifter_cv_1432.dir/ladspa/bode_shifter_cv_1432.c.o
[  4%] Linking C static library libblo.a
[  4%] Linking C shared module ../../ladspa/bode_shifter_1431.so
[  4%] Linking C static library libiir.a
[  5%] Linking C shared module ../../ladspa/bode_shifter_cv_1432.so
[  5%] Built target blo
[  5%] Built target iir
[  5%] Built target bode_shifter_1431
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/chebstortion_1430.dir/ladspa/chebstortion_1430.c.o
[  5%] Built target bode_shifter_cv_1432
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_1190.dir/ladspa/comb_1190.c.o
[  5%] Linking C shared module ../../ladspa/chebstortion_1430.so
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_1887.dir/ladspa/comb_1887.c.o
[  5%] Linking C shared module ../../ladspa/comb_1190.so
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_splitter_1411.dir/ladspa/comb_splitter_1411.c.o
[  5%] Linking C shared module ../../ladspa/comb_1887.so
[  5%] Linking C shared module ../../ladspa/comb_splitter_1411.so
[  5%] Built target chebstortion_1430
[  5%] Built target comb_1190
[  6%] Built target comb_splitter_1411
[  6%] Built target comb_1887
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/const_1909.dir/ladspa/const_1909.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/crossover_dist_1404.dir/ladspa/crossover_dist_1404.c.o
[  6%] Linking C shared module ../../ladspa/const_1909.so
[  6%] Linking C shared module ../../ladspa/crossover_dist_1404.so
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/debug_1184.dir/ladspa/debug_1184.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dc_remove_1207.dir/ladspa/dc_remove_1207.c.o
[  6%] Linking C shared module ../../ladspa/debug_1184.so
[  6%] Linking C shared module ../../ladspa/dc_remove_1207.so
[  6%] Built target const_1909
[  6%] Built target crossover_dist_1404
[  6%] Built target dc_remove_1207
[  6%] Built target debug_1184
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/decay_1886.dir/ladspa/decay_1886.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/decimator_1202.dir/ladspa/decimator_1202.c.o
[  6%] Linking C shared module ../../ladspa/decay_1886.so
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/delay_1898.dir/ladspa/delay_1898.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/declip_1195.dir/ladspa/declip_1195.c.o
[  6%] Linking C shared module ../../ladspa/declip_1195.so
[  6%] Linking C shared module ../../ladspa/delay_1898.so
[  6%] Built target decay_1886
[  6%] Built target declip_1195
[  6%] Built target delay_1898
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/delayorama_1402.dir/ladspa/delayorama_1402.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/divider_1186.dir/ladspa/divider_1186.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/diode_1185.dir/ladspa/diode_1185.c.o
[  6%] Linking C shared module ../../ladspa/delayorama_1402.so
[  6%] Linking C shared module ../../ladspa/divider_1186.so
[  6%] Linking C shared module ../../ladspa/diode_1185.so
[  6%] Built target delayorama_1402
[  6%] Built target divider_1186
[  6%] Built target diode_1185
[  6%] Linking C shared module ../../ladspa/decimator_1202.so
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dj_eq_1901.dir/ladspa/dj_eq_1901.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dyson_compress_1403.dir/ladspa/dyson_compress_1403.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dj_flanger_1438.dir/ladspa/dj_flanger_1438.c.o
[  6%] Linking C shared module ../../ladspa/dj_eq_1901.so
[  6%] Linking C shared module ../../ladspa/dyson_compress_1403.so
[  6%] Linking C shared module ../../ladspa/dj_flanger_1438.so
[  6%] Built target decimator_1202
[  6%] Built target dj_eq_1901
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fad_delay_1192.dir/ladspa/fad_delay_1192.c.o
[  7%] Built target dj_flanger_1438
[  7%] Built target dyson_compress_1403
[  7%] Linking C shared module ../../ladspa/fad_delay_1192.so
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fast_lookahead_limiter_1913.dir/ladspa/fast_lookahead_limiter_1913.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/flanger_1191.dir/ladspa/flanger_1191.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fm_osc_1415.dir/ladspa/fm_osc_1415.c.o
[  7%] Linking C shared module ../../ladspa/fast_lookahead_limiter_1913.so
[  7%] Linking C shared module ../../ladspa/flanger_1191.so
[  7%] Linking C shared module ../../ladspa/fm_osc_1415.so
[  7%] Built target fad_delay_1192
[  7%] Built target fast_lookahead_limiter_1913
[  7%] Built target flanger_1191
[  7%] Built target fm_osc_1415
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/foldover_1213.dir/ladspa/foldover_1213.c.o
[  7%] Linking C shared module ../../ladspa/foldover_1213.so
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/foverdrive_1196.dir/ladspa/foverdrive_1196.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/freq_tracker_1418.dir/ladspa/freq_tracker_1418.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gate_1410.dir/ladspa/gate_1410.c.o
[  7%] Linking C shared module ../../ladspa/foverdrive_1196.so
[  7%] Linking C shared module ../../ladspa/freq_tracker_1418.so
[  7%] Linking C shared module ../../ladspa/gate_1410.so
[  7%] Built target foldover_1213
[  7%] Built target foverdrive_1196
[  7%] Built target freq_tracker_1418
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gate_1921.dir/ladspa/gate_1921.c.o
[  7%] Built target gate_1410
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/giant_flange_1437.dir/ladspa/giant_flange_1437.c.o
[  7%] Linking C shared module ../../ladspa/gate_1921.so
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gong_1424.dir/ladspa/gong_1424.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gong_beater_1439.dir/ladspa/gong_beater_1439.c.o
[  7%] Linking C shared module ../../ladspa/giant_flange_1437.so
[  7%] Linking C shared module ../../ladspa/gong_1424.so
[  7%] Linking C shared module ../../ladspa/gong_beater_1439.so
[  7%] Built target gate_1921
[  7%] Built target giant_flange_1437
[  7%] Built target gong_1424
[  7%] Built target gong_beater_1439
[  8%] Built target gsm
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb.dir/ladspa/gverb/gverb.c.o
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hard_limiter_1413.dir/ladspa/hard_limiter_1413.c.o
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/harmonic_gen_1220.dir/ladspa/harmonic_gen_1220.c.o
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb.dir/ladspa/gverb/gverbdsp.c.o
[  8%] Linking C shared module ../../ladspa/hard_limiter_1413.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hermes_filter_1200.dir/ladspa/hermes_filter_1200.c.o
[  8%] Linking C shared module ../../ladspa/harmonic_gen_1220.so
[  8%] Linking C static library libgverb.a
[  8%] Linking C shared module ../../ladspa/hermes_filter_1200.so
[  8%] Built target gverb
[  8%] Built target hard_limiter_1413
[  8%] Built target harmonic_gen_1220
[  8%] Built target hermes_filter_1200
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hilbert_1440.dir/ladspa/hilbert_1440.c.o
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/highpass_iir_1890.dir/ladspa/highpass_iir_1890.c.o
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/imp_1199.dir/ladspa/imp_1199.c.o
[  9%] Linking C shared module ../../ladspa/hilbert_1440.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/impulse_1885.dir/ladspa/impulse_1885.c.o
[  9%] Linking C shared module ../../ladspa/imp_1199.so
[  9%] Linking C shared module ../../ladspa/impulse_1885.so
[  9%] Built target hilbert_1440
[  9%] Built target imp_1199
[  9%] Built target impulse_1885
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/inv_1429.dir/ladspa/inv_1429.c.o
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/karaoke_1409.dir/ladspa/karaoke_1409.c.o
[  9%] Linking C shared module ../../ladspa/inv_1429.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/latency_1914.dir/ladspa/latency_1914.c.o
[  9%] Linking C shared module ../../ladspa/karaoke_1409.so
[  9%] Linking C shared module ../../ladspa/latency_1914.so
[  9%] Built target inv_1429
[  9%] Built target karaoke_1409
[ 10%] Built target latency_1914
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/lcr_delay_1436.dir/ladspa/lcr_delay_1436.c.o
[ 10%] Linking C shared module ../../ladspa/highpass_iir_1890.so
[ 10%] Linking C shared module ../../ladspa/lcr_delay_1436.so
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/lowpass_iir_1891.dir/ladspa/lowpass_iir_1891.c.o
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/ls_filter_1908.dir/ladspa/ls_filter_1908.c.o
[ 10%] Linking C shared module ../../ladspa/lowpass_iir_1891.so
[ 10%] Linking C shared module ../../ladspa/ls_filter_1908.so
[ 10%] Built target highpass_iir_1890
[ 10%] Built target lcr_delay_1436
[ 10%] Built target lowpass_iir_1891
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_ms_st_1421.dir/ladspa/matrix_ms_st_1421.c.o
[ 10%] Built target ls_filter_1908
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_spatialiser_1422.dir/ladspa/matrix_spatialiser_1422.c.o
[ 10%] Linking C shared module ../../ladspa/matrix_ms_st_1421.so
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_st_ms_1420.dir/ladspa/matrix_st_ms_1420.c.o
[ 10%] Linking C shared module ../../ladspa/matrix_spatialiser_1422.so
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/mbeq_1197.dir/ladspa/mbeq_1197.c.o
[ 11%] Linking C shared module ../../ladspa/matrix_st_ms_1420.so
[ 11%] Linking C shared module ../../ladspa/mbeq_1197.so
[ 11%] Built target matrix_ms_st_1421
[ 11%] Built target matrix_spatialiser_1422
[ 11%] Built target matrix_st_ms_1420
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/mod_delay_1419.dir/ladspa/mod_delay_1419.c.o
[ 11%] Built target mbeq_1197
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/multivoice_chorus_1201.dir/ladspa/multivoice_chorus_1201.c.o
[ 11%] Linking C shared module ../../ladspa/mod_delay_1419.so
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/notch_iir_1894.dir/ladspa/notch_iir_1894.c.o
[ 11%] Linking C shared module ../../ladspa/multivoice_chorus_1201.so
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/phasers_1217.dir/ladspa/phasers_1217.c.o
[ 11%] Linking C shared module ../../ladspa/notch_iir_1894.so
[ 11%] Linking C shared module ../../ladspa/phasers_1217.so
[ 11%] Built target mod_delay_1419
[ 11%] Built target multivoice_chorus_1201
[ 11%] Built target notch_iir_1894
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitchscale.dir/ladspa/util/pitchscale.c.o
[ 11%] Built target phasers_1217
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/plate_1423.dir/ladspa/plate_1423.c.o
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pointer_cast_1910.dir/ladspa/pointer_cast_1910.c.o
[ 11%] Linking C static library libpitchscale.a
[ 12%] Linking C shared module ../../ladspa/plate_1423.so
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/rate_shifter_1417.dir/ladspa/rate_shifter_1417.c.o
[ 12%] Linking C shared module ../../ladspa/pointer_cast_1910.so
[ 12%] Linking C shared module ../../ladspa/rate_shifter_1417.so
[ 12%] Built target pitchscale
[ 12%] Built target plate_1423
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/retro_flange_1208.dir/ladspa/retro_flange_1208.c.o
[ 12%] Built target pointer_cast_1910
[ 12%] Built target rate_shifter_1417
[ 12%] Linking C shared module ../../ladspa/retro_flange_1208.so
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/revdelay_1605.dir/ladspa/revdelay_1605.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/ringmod_1188.dir/ladspa/ringmod_1188.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/satan_maximiser_1408.dir/ladspa/satan_maximiser_1408.c.o
[ 12%] Linking C shared module ../../ladspa/revdelay_1605.so
[ 12%] Linking C shared module ../../ladspa/ringmod_1188.so
[ 12%] Linking C shared module ../../ladspa/satan_maximiser_1408.so
[ 12%] Built target retro_flange_1208
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/db.dir/ladspa/util/db.c.o
[ 12%] Built target revdelay_1605
[ 12%] Built target ringmod_1188
[ 12%] Linking C static library libdb.a
[ 12%] Built target satan_maximiser_1408
[ 12%] Built target rms
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/shaper_1187.dir/ladspa/shaper_1187.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sifter_1210.dir/ladspa/sifter_1210.c.o
[ 13%] Linking C shared module ../../ladspa/shaper_1187.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sin_cos_1881.dir/ladspa/sin_cos_1881.c.o
[ 13%] Built target db
[ 13%] Linking C shared module ../../ladspa/sifter_1210.so
[ 13%] Linking C shared module ../../ladspa/sin_cos_1881.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/single_para_1203.dir/ladspa/single_para_1203.c.o
[ 13%] Linking C shared module ../../ladspa/single_para_1203.so
[ 13%] Built target shaper_1187
[ 13%] Built target sifter_1210
[ 13%] Built target sin_cos_1881
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sinus_wavewrapper_1198.dir/ladspa/sinus_wavewrapper_1198.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/smooth_decimate_1414.dir/ladspa/smooth_decimate_1414.c.o
[ 13%] Linking C shared module ../../ladspa/sinus_wavewrapper_1198.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/split_1406.dir/ladspa/split_1406.c.o
[ 13%] Built target single_para_1203
[ 13%] Linking C shared module ../../ladspa/smooth_decimate_1414.so
[ 13%] Linking C shared module ../../ladspa/split_1406.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/step_muxer_1212.dir/ladspa/step_muxer_1212.c.o
[ 13%] Linking C shared module ../../ladspa/step_muxer_1212.so
[ 13%] Built target sinus_wavewrapper_1198
[ 13%] Built target smooth_decimate_1414
[ 13%] Built target split_1406
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/surround_encoder_1401.dir/ladspa/surround_encoder_1401.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/svf_1214.dir/ladspa/svf_1214.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/tape_delay_1211.dir/ladspa/tape_delay_1211.c.o
[ 13%] Linking C shared module ../../ladspa/surround_encoder_1401.so
[ 13%] Built target step_muxer_1212
[ 13%] Linking C shared module ../../ladspa/svf_1214.so
[ 13%] Linking C shared module ../../ladspa/tape_delay_1211.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/transient_1206.dir/ladspa/transient_1206.c.o
[ 13%] Linking C shared module ../../ladspa/transient_1206.so
[ 13%] Built target surround_encoder_1401
[ 13%] Built target svf_1214
[ 13%] Built target tape_delay_1211
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/triple_para_1204.dir/ladspa/triple_para_1204.c.o
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/valve_1209.dir/ladspa/valve_1209.c.o
[ 14%] Linking C shared module ../../ladspa/triple_para_1204.so
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/valve_rect_1405.dir/ladspa/valve_rect_1405.c.o
[ 14%] Built target transient_1206
[ 14%] Linking C shared module ../../ladspa/valve_1209.so
[ 14%] Linking C shared module ../../ladspa/valve_rect_1405.so
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/vocoder_1337.dir/ladspa/vocoder_1337.c.o
[ 14%] Linking C shared module ../../ladspa/vocoder_1337.so
[ 14%] Built target triple_para_1204
[ 14%] Built target valve_1209
[ 14%] Built target valve_rect_1405
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/vynil_1905.dir/ladspa/vynil_1905.c.o
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/wave_terrain_1412.dir/ladspa/wave_terrain_1412.c.o
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/xfade_1915.dir/ladspa/xfade_1915.c.o
[ 14%] Linking C shared module ../../ladspa/wave_terrain_1412.so
[ 14%] Linking C shared module ../../ladspa/vynil_1905.so
[ 14%] Built target vocoder_1337
[ 14%] Linking C shared module ../../ladspa/xfade_1915.so
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/zm1_1428.dir/ladspa/zm1_1428.c.o
[ 14%] Linking C shared module ../../ladspa/zm1_1428.so
[ 14%] Built target wave_terrain_1412
[ 14%] Built target vynil_1905
[ 14%] Built target xfade_1915
[ 14%] Built target adplug
[ 14%] Built target zm1_1428
[ 15%] Built target cmt
[ 15%] Built target veal
[ 15%] Built target gme
[ 15%] Built target resid_objects
[ 16%] Built target rnnoise_vendored
[ 16%] Performing build step for 'NativeLinuxRemoteVstPlugin64'
[ 16%] Built target zynaddsubfx_nio
[100%] Built target NativeLinuxRemoteVstPlugin64
[ 18%] Built target zynaddsubfx_gui
[ 18%] Built target FakeRemotePluginClient_autogen_timestamp_deps
[ 20%] Built target zynaddsubfx_synth
[ 20%] No install step for 'NativeLinuxRemoteVstPlugin64'
[ 20%] Built target zene_api_autogen
[ 21%] Built target wasm-wat2wasm_autogen
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/analogue_osc_1416.dir/ladspa/analogue_osc_1416.c.o
[ 21%] Completed 'NativeLinuxRemoteVstPlugin64'
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bandpass_a_iir_1893.dir/ladspa/bandpass_a_iir_1893.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bandpass_iir_1892.dir/ladspa/bandpass_iir_1892.c.o
[ 21%] Linking C shared module ../../ladspa/analogue_osc_1416.so
[ 21%] Linking C shared module ../../ladspa/bandpass_a_iir_1893.so
[ 21%] Linking C shared module ../../ladspa/bandpass_iir_1892.so
[ 21%] Built target NativeLinuxRemoteVstPlugin64
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/butterworth_1902.dir/ladspa/butterworth_1902.c.o
[ 21%] Built target analogue_osc_1416
[ 21%] Built target bandpass_a_iir_1893
[ 21%] Built target bandpass_iir_1892
[ 21%] Linking C shared module ../../ladspa/butterworth_1902.so
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gsm_1215.dir/ladspa/gsm_1215.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb_1216.dir/ladspa/gverb_1216.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitch_scale_1193.dir/ladspa/pitch_scale_1193.c.o
[ 21%] Linking C shared module ../../ladspa/gsm_1215.so
[ 21%] Linking C shared module ../../ladspa/gverb_1216.so
[ 21%] Linking C shared module ../../ladspa/pitch_scale_1193.so
[ 21%] Built target butterworth_1902
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitch_scale_1194.dir/ladspa/pitch_scale_1194.c.o
[ 21%] Built target gsm_1215
[ 21%] Built target pitch_scale_1193
[ 21%] Built target gverb_1216
[ 21%] Linking C shared module ../../ladspa/pitch_scale_1194.so
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc1_1425.dir/ladspa/sc1_1425.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc2_1426.dir/ladspa/sc2_1426.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc3_1427.dir/ladspa/sc3_1427.c.o
[ 21%] Linking C shared module ../../ladspa/sc1_1425.so
[ 21%] Linking C shared module ../../ladspa/sc3_1427.so
[ 21%] Built target pitch_scale_1194
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc4_1882.dir/ladspa/sc4_1882.c.o
[ 21%] Built target sc1_1425
[ 21%] Built target sc3_1427
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc4m_1916.dir/ladspa/sc4m_1916.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/se4_1883.dir/ladspa/se4_1883.c.o
[ 21%] Linking C shared module ../../ladspa/sc4m_1916.so
[ 21%] Linking C shared module ../../ladspa/sc4_1882.so
[ 21%] Linking C shared module ../../ladspa/se4_1883.so
[ 21%] Built target sc4m_1916
[ 21%] Built target se4_1883
[ 22%] Built target sc4_1882
[ 22%] Automatic MOC for target FakeRemotePluginClient
[ 22%] Built target ZynAddSubFxCore
[ 22%] Built target wasm-wat2wasm
[ 22%] Built target FakeRemotePluginClient_autogen
[ 22%] Built target wasm-modules
[ 22%] Linking C shared module ../../ladspa/sc2_1426.so
[ 22%] Built target RemoteZynAddSubFx
[ 22%] Built target FakeRemotePluginClient
[ 22%] Built target WasmWorkerPoolTest_autogen_timestamp_deps
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlRegistry.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlRegistryRegistrations.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibility.cpp.o
[ 22%] Built target sc2_1426
[ 22%] Automatic MOC for target WasmWorkerPoolTest
[ 22%] Built target WasmWorkerPoolTest_autogen
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlTransactions.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlUndoCoalescing.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlSchema.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlVocabulary.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTable.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableAction.cpp.o
[ 22%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableArchive.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableAutomationModes.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableAutomationRamp.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableChain.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableChord.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableClapInstrument.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableController.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableDawProject.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableDetect.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableExportPresets.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableHostChunking.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableInterchange.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableLive.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableMastering.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableMeter.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableMidiReconnect.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableMmpzGit.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableNoteScale.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableOutOfProcess.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTablePassive.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableRecording.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableRevisions.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableRouting.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableSafeStart.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableSample.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableScanAndCrash.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableSessionView.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableSnapshot.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableStems.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableStructure.cpp.o
[ 23%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableTrackFolder.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableVca.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableVerbs.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableWasmRender.cpp.o
[ 25%] Built target WasmWorkerPoolTest
[ 25%] Linking CXX static library libzene_api.a
[ 25%] Built target zene_api
[ 25%] Built target lmmsobjs_autogen_timestamp_deps
[ 25%] Automatic MOC and UIC for target lmmsobjs
[ 25%] Built target lmmsobjs_autogen
[ 25%] Generating qrc_lmms.cpp
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/lmmsobjs_autogen/mocs_compilation.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AudioEngine.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AudioEngineWorkerThread.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AutomatableModel.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AutomationClip.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AutomationNode.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/BounceInPlace.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ConfigManager.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/CrashReporter.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsArrangement.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClip.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipEdits.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipTrim.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ClipLinks.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipLink.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipLinkState.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSample.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSampleEdit.cpp.o
[ 25%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/SampleOperators.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClock.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsComp.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCompEdits.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlAutomationSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlBrowserSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomation.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomationEdit.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomationRamp.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBrowser.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBrowserTags.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsControl.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCompSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDsp.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsIdContract.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMixer.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlMixerSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMixerRoutes.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPdc.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRouting.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBus.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPorts.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPatcher.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPatcherEdit.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlMasteringSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMastering.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMasteringRun.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDetectSupport.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDetect.cpp.o
[ 26%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDetectApply.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMeter.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMeterFile.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsInterchange.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDawProject.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsModulator.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsModulatorRoutes.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteExpression.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidi.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidiReconnect.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidiReconnectEdit.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNotes.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteProbability.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlNoteShared.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteRandom.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteSlide.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteTransform.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlScaleSupport.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScale.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScaleEdit.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDevice.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPlugin.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginParams.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginPreset.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginState.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCrash.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCrashControl.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginScan.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginScanEdit.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSafeStart.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSafeStartEdit.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcess.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcessEdit.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcessSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsHostChunking.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsHostNotes.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsArrangementState.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProject.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectFiles.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRevisions.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRenderStems.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRack.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRackMacros.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRackZones.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScript.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsLivecode.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsExport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsExportPresets.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlExportPresetSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsFreeze.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsLink.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/LinkSync.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/LinkSyncWire.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSettings.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSurface.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsStructure.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTelemetry.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTransport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTrackFolder.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTrackFolderSets.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVca.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaMix.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaEdit.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaEditSet.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TrackContainerFolders.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsUndo.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTransportMap.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPunch.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecording.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRecovery.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlRecordingSupport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingInput.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRoutes.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRetro.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWarp.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWarpEdit.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlGrooveSupport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGroove.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGrooveEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGroovePool.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlWasmSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasm.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasmEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasmRender.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceCatalogue.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceHosted.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceState.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceVst3.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceClap.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlChainPresetSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChain.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChainEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlChordSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChord.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChordEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChordWrite.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlEditSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlModulationSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlRackSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectArchive.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlProjectAssets.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlProjectAssetsRelink.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectMmpzGit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsController.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsControllerTemplates.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlReversibilityTableLivecode.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControllerSurface.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServer.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServerSocket.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServerWin32.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlSession.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlStructuralSupport.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControllerConnection.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/DataFile.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Effect.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/EffectChain.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/EffectChainPatcher.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Engine.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/EnvelopeAndLfoParameters.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Mixer.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ImportFilter.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/InlineAutomation.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Instrument.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/InstrumentFunctions.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/InstrumentPlayHandle.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/InstrumentSoundShaping.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/MasteringJob.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/LfoController.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/MeterModel.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Microtuner.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/MidiLearn.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/MidiClock.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/MidiClockState.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Note.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/NotePlayHandle.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/PathUtil.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/PatternClip.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/PatternStore.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/PeakController.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Piano.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Plugin.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/PresetPreviewPlayHandle.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ProjectJournal.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ProjectIds.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ProjectRenderer.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RemotePlugin.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Rack.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RackMacros.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RenderManager.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RetroMidiClipWriter.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RoutingGraph.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/SampleClip.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/SamplePlayHandle.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/SampleRecordHandle.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptBindings.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptClock.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawBindings.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawEffects.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptEngine.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Song.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TempoMap.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/DawProjectSession.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ModulationLayer.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TempoSyncKnobModel.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TakeLane.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Track.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/TrackContainer.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/UpgradeExtendedNoteRange.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Clip.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/StepRecorder.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/midi/MidiAlsaSeq.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/midi/MidiPort.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSession.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionLaunch.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionFollow.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecord.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecordLand.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Telemetry.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/AutomatableModelView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/ControllerRackView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/FileBrowser.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/FocusDeskPane.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/GuiApplication.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MainApplication.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MainWindow.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MicrotunerConfig.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MidiCCRackView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MixerChannelView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MixerView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/PluginBrowser.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/ProjectNotes.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/SampleTrackWindow.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/clips/AutomationClipView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/clips/ClipView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/clips/MidiClipView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/clips/PatternClipView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/clips/SampleClipView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/AutomationEditor.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/Editor.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/PatternEditor.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/PianoRoll.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/PositionLine.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/SongEditor.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/TimeLineWidget.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/editors/TrackContainerView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/instrument/EnvelopeAndLfoView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/instrument/InstrumentTuningView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/instrument/InstrumentTrackWindow.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/instrument/InstrumentView.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/instrument/PianoView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/menus/RecentProjectsMenu.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/menus/TemplatesMenu.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/AboutDialog.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/ControllerConnectionDialog.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/ExportProjectDialog.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/SetupDialog.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/VersionedSaveDialog.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/AutomationTrackView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/InstrumentTrackView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/PatternTrackView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/SampleTrackView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/TrackContentWidget.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/TrackLabelButton.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/TrackOperationsWidget.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/TrackGrip.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/TrackView.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/Oscilloscope.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/TempoSyncBarModelEditor.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/TempoSyncKnob.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/TimeDisplayWidget.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/AutomationTrack.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/InstrumentTrack.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/MidiClip.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/PatternTrack.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/SampleTrack.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/TrackFolder.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/qrc_lmms.cpp.o
[ 43%] Built target lmmsobjs
[ 43%] Built target zene_autogen_timestamp_deps
[ 43%] Built target AudioBusHandleTest_autogen_timestamp_deps
[ 43%] Built target ArrayVectorTest_autogen_timestamp_deps
[ 43%] Built target AudioBufferTest_autogen_timestamp_deps
[ 43%] Built target AudioEngineTeardownTest_autogen_timestamp_deps
[ 43%] Built target AudioBusTest_autogen_timestamp_deps
[ 43%] Built target AudioPortsModelTest_autogen_timestamp_deps
[ 43%] Built target AudioPortsTest_autogen_timestamp_deps
[ 43%] Built target AudioStretcherTest_autogen_timestamp_deps
[ 43%] Built target AudioResamplerRatioTest_autogen_timestamp_deps
[ 43%] Built target AutomatableModelTest_autogen_timestamp_deps
[ 43%] Built target AutomationModesTest_autogen_timestamp_deps
[ 43%] Built target SampleAccurateAutomationTest_autogen_timestamp_deps
[ 43%] Built target ClipEditsTest_autogen_timestamp_deps
[ 43%] Built target BrowserCatalogTest_autogen_timestamp_deps
[ 43%] Built target ClipFadesRenderTest_autogen_timestamp_deps
[ 43%] Built target ClipSerialisationTest_autogen_timestamp_deps
[ 43%] Built target ClipLinkPersistenceTest_autogen_timestamp_deps
[ 43%] Built target ClipLinkTest_autogen_timestamp_deps
[ 43%] Built target WriteRefusalGateTest_autogen_timestamp_deps
[ 43%] Built target ClipWarpPersistenceTest_autogen_timestamp_deps
[ 43%] Built target ControlAutomationScriptTest_autogen_timestamp_deps
[ 43%] Built target ConfigMigrationTest_autogen_timestamp_deps
[ 43%] Built target ControlAutomationModesTest_autogen_timestamp_deps
[ 43%] Built target ControlChainPresetTest_autogen_timestamp_deps
[ 43%] Built target ControlBrowserCommandsTest_autogen_timestamp_deps
[ 43%] Built target ChordTrackTest_autogen_timestamp_deps
[ 43%] Built target ChordDetectTest_autogen_timestamp_deps
[ 43%] Built target ControlChordCommandsTest_autogen_timestamp_deps
[ 43%] Built target ChordProgressionTest_autogen_timestamp_deps
[ 43%] Built target ControlChordWriteTest_autogen_timestamp_deps
[ 43%] Built target ControlDeviceCatalogueTest_autogen_timestamp_deps
[ 43%] Built target ControlEditCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlProjectArchiveTest_autogen_timestamp_deps
[ 43%] Built target ControlLinkCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlRegistryTest_autogen_timestamp_deps
[ 43%] Built target ControlShutdownHookTest_autogen_timestamp_deps
[ 43%] Built target ControlTempoMapCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlSurfaceReferenceTest_autogen_timestamp_deps
[ 43%] Built target ControllerSurfaceTest_autogen_timestamp_deps
[ 43%] Built target ControlVerbInverseTest_autogen_timestamp_deps
[ 43%] Built target ControlSampleOperatorTest_autogen_timestamp_deps
[ 43%] Built target ControlNoteScaleVerbsTest_autogen_timestamp_deps
[ 43%] Built target ControlWarpCommandsTest_autogen_timestamp_deps
[ 43%] Built target GrooveTemplateTest_autogen_timestamp_deps
[ 43%] Built target CrashReporterArmTest_autogen_timestamp_deps
[ 43%] Built target CrashReporterTest_autogen_timestamp_deps
[ 43%] Built target ControlGrooveCommandsTest_autogen_timestamp_deps
[ 43%] Built target DocumentSectionsTest_autogen_timestamp_deps
[ 43%] Built target DataFileSaveIntegrityTest_autogen_timestamp_deps
[ 43%] Built target DataFileFormatTest_autogen_timestamp_deps
[ 43%] Built target DocumentIndexTest_autogen_timestamp_deps
[ 43%] Built target ExportDitherTest_autogen_timestamp_deps
[ 43%] Built target ExportWavDitherTest_autogen_timestamp_deps
[ 43%] Built target ProjectContainerEntriesTest_autogen_timestamp_deps
[ 43%] Built target ProjectContainerTest_autogen_timestamp_deps
[ 43%] Built target MasteringTest_autogen_timestamp_deps
[ 43%] Built target LufsMeterTest_autogen_timestamp_deps
[ 43%] Built target ImportDetectionTest_autogen_timestamp_deps
[ 43%] Built target LoudnessReportTest_autogen_timestamp_deps
[ 43%] Built target MeterTapTest_autogen_timestamp_deps
[ 43%] Built target MathTest_autogen_timestamp_deps
[ 43%] Built target MidiClockTest_autogen_timestamp_deps
[ 43%] Built target MidiLearnTest_autogen_timestamp_deps
[ 43%] Built target MidiRetroCaptureTest_autogen_timestamp_deps
[ 43%] Built target MidiProbabilityPersistenceTest_autogen_timestamp_deps
[ 43%] Built target RetroMidiCaptureCommandsTest_autogen_timestamp_deps
[ 43%] Built target MidiLearnThreadTest_autogen_timestamp_deps
[ 43%] Built target MixerConcurrencyTest_autogen_timestamp_deps
[ 43%] Built target MidiReconnectTest_autogen_timestamp_deps
[ 43%] Built target MixerRoutingBackwardCompatTest_autogen_timestamp_deps
[ 43%] Built target MixerAbRegressionTest_autogen_timestamp_deps
[ 43%] Built target MpeExpressionTest_autogen_timestamp_deps
[ 43%] Built target MpeInputPathTest_autogen_timestamp_deps
[ 43%] Built target ModulationLayerValueTest_autogen_timestamp_deps
[ 43%] Built target MpeNoteStorageTest_autogen_timestamp_deps
[ 43%] Built target ModulationLayerProjectRoundTripTest_autogen_timestamp_deps
[ 43%] Built target ModulationLayerTest_autogen_timestamp_deps
[ 43%] Built target ControlNoteExpressionCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlModulatorCommandsTest_autogen_timestamp_deps
[ 43%] Built target MultiTrackRecorderTest_autogen_timestamp_deps
[ 43%] Built target RecordingInputPathTest_autogen_timestamp_deps
[ 43%] Built target RetroAudioCaptureTest_autogen_timestamp_deps
[ 43%] Built target NamespaceRegistryTest_autogen_timestamp_deps
[ 43%] Built target NoteTransformTest_autogen_timestamp_deps
[ 43%] Built target PhaseFChannelScaleTest_autogen_timestamp_deps
[ 43%] Built target PluginLogoResourceTest_autogen_timestamp_deps
[ 43%] Built target NoteRandomTest_autogen_timestamp_deps
[ 43%] Built target PluginAudioPortsTest_autogen_timestamp_deps
[ 43%] Built target ProjectOpenIntegrityTest_autogen_timestamp_deps
[ 43%] Built target PartialLoadTest_autogen_timestamp_deps
[ 43%] Built target ProjectRecoveryTest_autogen_timestamp_deps
[ 43%] Built target ProjectVersionTest_autogen_timestamp_deps
[ 43%] Built target RackMacrosTest_autogen_timestamp_deps
[ 43%] Built target RackTest_autogen_timestamp_deps
[ 43%] Built target RackZonesTest_autogen_timestamp_deps
[ 43%] Built target RecordClipTest_autogen_timestamp_deps
[ 43%] Built target RecordRingBufferTest_autogen_timestamp_deps
[ 43%] Built target RecordingRealtimeTest_autogen_timestamp_deps
[ 43%] Built target RetroMidiRingTest_autogen_timestamp_deps
[ 43%] Built target RelativePathsTest_autogen_timestamp_deps
[ 43%] Built target ReversibilityUndoTest_autogen_timestamp_deps
[ 43%] Built target ReversibilityContractTest_autogen_timestamp_deps
[ 43%] Built target RevisionTimelineTest_autogen_timestamp_deps
[ 43%] Built target UnclaimedTrackTypeTest_autogen_timestamp_deps
[ 43%] Built target UnclaimedElementsTest_autogen_timestamp_deps
[ 43%] Built target UndoBoundsTest_autogen_timestamp_deps
[ 43%] Built target TakeLaneCompTest_autogen_timestamp_deps
[ 43%] Built target TakeLaneTest_autogen_timestamp_deps
[ 43%] Built target RenderJobQueueTest_autogen_timestamp_deps
[ 43%] Built target RemotePluginAudioPortsTest_autogen_timestamp_deps
[ 43%] Built target RemotePluginClientE2ETest_autogen_timestamp_deps
[ 43%] Built target PdcMixerTest_autogen_timestamp_deps
[ 43%] Built target RoutingGraphLiveTest_autogen_timestamp_deps
[ 43%] Built target RoutingGraphScheduleTest_autogen_timestamp_deps
[ 43%] Built target RoutingGraphTest_autogen_timestamp_deps
[ 43%] Built target VcaGroupTest_autogen_timestamp_deps
[ 43%] Built target PatcherCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlVcaCommandsTest_autogen_timestamp_deps
[ 43%] Built target ControlVcaEditGroupsTest_autogen_timestamp_deps
[ 43%] Built target ScriptClockTest_autogen_timestamp_deps
[ 43%] Built target ScriptDawBindingTest_autogen_timestamp_deps
[ 43%] Built target ScriptBindingsTest_autogen_timestamp_deps
[ 43%] Built target ScriptEngineTest_autogen_timestamp_deps
[ 43%] Built target ScriptMemoryBudgetTest_autogen_timestamp_deps
[ 43%] Built target SlideNotesTest_autogen_timestamp_deps
[ 43%] Built target ScriptStabilisationTest_autogen_timestamp_deps
[ 43%] Built target SmfInterchangeTest_autogen_timestamp_deps
[ 43%] Built target StableTrackIdsTest_autogen_timestamp_deps
[ 43%] Built target DawProjectInterchangeRoundTripTest_autogen_timestamp_deps
[ 43%] Built target SmfInterchangeRoundTripTest_autogen_timestamp_deps
[ 43%] Built target ProjectRevIdsTest_autogen_timestamp_deps
[ 43%] Built target TelemetryTest_autogen_timestamp_deps
[ 43%] Built target StemExportTest_autogen_timestamp_deps
[ 43%] Built target TelemetryTransportTest_autogen_timestamp_deps
[ 43%] Built target TempoMapPersistenceTest_autogen_timestamp_deps
[ 43%] Built target TempoMapTest_autogen_timestamp_deps
[ 43%] Built target TimelineTest_autogen_timestamp_deps
[ 43%] Built target TwoTrackRecordingHarness_autogen_timestamp_deps
[ 43%] Built target TrackFolderTest_autogen_timestamp_deps
[ 43%] Built target WarpMarkersTest_autogen_timestamp_deps
[ 43%] Built target FocusDeskTest_autogen_timestamp_deps
[ 43%] Built target FocusDeskPaneTest_autogen_timestamp_deps
[ 43%] Built target MidiLearnGuiTest_autogen_timestamp_deps
[ 43%] Built target AutomationTrackTest_autogen_timestamp_deps
[ 43%] Built target SampleClipWarpTest_autogen_timestamp_deps
[ 43%] Built target SampleClipStretchTest_autogen_timestamp_deps
[ 43%] Built target SampleClipWindowTest_autogen_timestamp_deps
[ 43%] Built target SessionSchedulerTest_autogen_timestamp_deps
[ 43%] Built target SessionModelTest_autogen_timestamp_deps
[ 43%] Built target SessionSchedulerRenderTest_autogen_timestamp_deps
[ 43%] Built target SessionFollowTest_autogen_timestamp_deps
[ 43%] Built target SessionArrangementRecordTest_autogen_timestamp_deps
[ 43%] Built target WasmSandboxTest_autogen_timestamp_deps
[ 43%] Built target WasmAbiConformanceTest_autogen_timestamp_deps
[ 43%] Built target zene_autogen
[ 43%] Automatic MOC for target ArrayVectorTest
[ 43%] Automatic MOC for target AudioBufferTest
[ 43%] Automatic MOC for target AudioBusHandleTest
[ 43%] Automatic MOC for target AudioBusTest
[ 43%] Built target ArrayVectorTest_autogen
[ 43%] Built target AudioBufferTest_autogen
[ 43%] Built target AudioBusHandleTest_autogen
[ 43%] Built target AudioBusTest_autogen
[ 43%] Automatic MOC for target AudioEngineTeardownTest
[ 43%] Automatic MOC for target AudioPortsModelTest
[ 43%] Automatic MOC for target AudioPortsTest
[ 43%] Automatic MOC for target AudioStretcherTest
[ 43%] Built target AudioEngineTeardownTest_autogen
[ 43%] Built target AudioPortsModelTest_autogen
[ 43%] Built target AudioPortsTest_autogen
[ 43%] Built target AudioStretcherTest_autogen
[ 43%] Automatic MOC for target AutomatableModelTest
[ 43%] Automatic MOC for target AudioResamplerRatioTest
[ 43%] Automatic MOC for target AutomationModesTest
[ 43%] Built target AutomatableModelTest_autogen
[ 43%] Built target AudioResamplerRatioTest_autogen
[ 43%] Automatic MOC for target SampleAccurateAutomationTest
[ 43%] Automatic MOC for target BrowserCatalogTest
[ 43%] Automatic MOC for target ClipEditsTest
[ 43%] Built target BrowserCatalogTest_autogen
[ 43%] Automatic MOC for target ClipFadesRenderTest
[ 43%] Built target AutomationModesTest_autogen
[ 43%] Built target SampleAccurateAutomationTest_autogen
[ 43%] Automatic MOC for target ClipLinkTest
[ 43%] Built target ClipEditsTest_autogen
[ 43%] Automatic MOC for target ClipLinkPersistenceTest
[ 43%] Automatic MOC for target ClipSerialisationTest
[ 43%] Built target ClipFadesRenderTest_autogen
[ 43%] Automatic MOC for target WriteRefusalGateTest
[ 43%] Built target ClipLinkTest_autogen
[ 43%] Automatic MOC for target ClipWarpPersistenceTest
[ 43%] Built target ClipLinkPersistenceTest_autogen
[ 43%] Built target ClipSerialisationTest_autogen
[ 43%] Automatic MOC for target ConfigMigrationTest
[ 43%] Built target WriteRefusalGateTest_autogen
[ 43%] Built target ConfigMigrationTest_autogen
[ 43%] Automatic MOC for target ControlAutomationScriptTest
[ 43%] Automatic MOC for target ControlBrowserCommandsTest
[ 43%] Automatic MOC for target ControlAutomationModesTest
[ 43%] Built target ClipWarpPersistenceTest_autogen
[ 43%] Automatic MOC for target ControlChainPresetTest
[ 43%] Built target ControlBrowserCommandsTest_autogen
[ 43%] Built target ControlAutomationScriptTest_autogen
[ 43%] Built target ControlAutomationModesTest_autogen
[ 43%] Automatic MOC for target ChordTrackTest
[ 43%] Automatic MOC for target ChordDetectTest
[ 43%] Automatic MOC for target ChordProgressionTest
[ 43%] Built target ChordProgressionTest_autogen
[ 43%] Automatic MOC for target ControlChordCommandsTest
[ 43%] Built target ControlChainPresetTest_autogen
[ 43%] Automatic MOC for target ControlChordWriteTest
[ 43%] Built target ChordDetectTest_autogen
[ 43%] Built target ChordTrackTest_autogen
[ 43%] Automatic MOC for target ControlEditCommandsTest
[ 43%] Automatic MOC for target ControlDeviceCatalogueTest
[ 43%] Built target ControlChordCommandsTest_autogen
[ 43%] Automatic MOC for target ControlLinkCommandsTest
[ 43%] Built target ControlChordWriteTest_autogen
[ 43%] Automatic MOC for target ControlProjectArchiveTest
[ 43%] Built target ControlEditCommandsTest_autogen
[ 43%] Automatic MOC for target ControlRegistryTest
[ 43%] Built target ControlDeviceCatalogueTest_autogen
[ 43%] Automatic MOC for target ControlSurfaceReferenceTest
[ 43%] Built target ControlLinkCommandsTest_autogen
[ 43%] Automatic MOC for target ControllerSurfaceTest
[ 43%] Built target ControlProjectArchiveTest_autogen
[ 43%] Automatic MOC for target ControlShutdownHookTest
[ 43%] Built target ControlRegistryTest_autogen
[ 43%] Built target ControlSurfaceReferenceTest_autogen
[ 43%] Automatic MOC for target ControlTempoMapCommandsTest
[ 43%] Automatic MOC for target ControlVerbInverseTest
[ 43%] Built target ControllerSurfaceTest_autogen
[ 43%] Automatic MOC for target ControlSampleOperatorTest
[ 43%] Built target ControlShutdownHookTest_autogen
[ 43%] Automatic MOC for target ControlNoteScaleVerbsTest
[ 43%] Built target ControlTempoMapCommandsTest_autogen
[ 43%] Built target ControlVerbInverseTest_autogen
[ 43%] Automatic MOC for target ControlWarpCommandsTest
[ 43%] Automatic MOC for target GrooveTemplateTest
[ 43%] Built target ControlSampleOperatorTest_autogen
[ 43%] Automatic MOC for target ControlGrooveCommandsTest
[ 43%] Built target ControlNoteScaleVerbsTest_autogen
[ 43%] Automatic MOC for target CrashReporterArmTest
[ 43%] Built target CrashReporterArmTest_autogen
[ 43%] Built target ControlWarpCommandsTest_autogen
[ 43%] Automatic MOC for target CrashReporterTest
[ 43%] Automatic MOC for target DataFileFormatTest
[ 43%] Built target GrooveTemplateTest_autogen
[ 43%] Built target DataFileFormatTest_autogen
[ 43%] Automatic MOC for target DataFileSaveIntegrityTest
[ 43%] Automatic MOC for target DocumentIndexTest
[ 43%] Built target ControlGrooveCommandsTest_autogen
[ 43%] Automatic MOC for target DocumentSectionsTest
[ 43%] Built target DocumentSectionsTest_autogen
[ 43%] Automatic MOC for target ProjectContainerTest
[ 43%] Built target ProjectContainerTest_autogen
[ 43%] Automatic MOC for target ProjectContainerEntriesTest
[ 43%] Built target ProjectContainerEntriesTest_autogen
[ 43%] Automatic MOC for target ExportDitherTest
[ 43%] Built target ExportDitherTest_autogen
[ 43%] Automatic MOC for target ExportWavDitherTest
[ 43%] Built target ExportWavDitherTest_autogen
[ 43%] Built target CrashReporterTest_autogen
[ 43%] Automatic MOC for target LoudnessReportTest
[ 43%] Automatic MOC for target LufsMeterTest
[ 43%] Built target LoudnessReportTest_autogen
[ 43%] Built target LufsMeterTest_autogen
[ 43%] Built target DataFileSaveIntegrityTest_autogen
[ 43%] Automatic MOC for target MasteringTest
[ 43%] Automatic MOC for target ImportDetectionTest
[ 43%] Automatic MOC for target MathTest
[ 43%] Built target ImportDetectionTest_autogen
[ 43%] Built target DocumentIndexTest_autogen
[ 43%] Built target MathTest_autogen
[ 43%] Automatic MOC for target MeterTapTest
[ 43%] Automatic MOC for target MidiLearnTest
[ 43%] Built target MeterTapTest_autogen
[ 43%] Automatic MOC for target MidiClockTest
[ 43%] Automatic MOC for target MidiLearnThreadTest
[ 43%] Built target MasteringTest_autogen
[ 43%] Automatic MOC for target MidiRetroCaptureTest
[ 43%] Built target MidiRetroCaptureTest_autogen
[ 43%] Built target MidiLearnTest_autogen
[ 43%] Built target MidiClockTest_autogen
[ 43%] Automatic MOC for target RetroMidiCaptureCommandsTest
[ 43%] Automatic MOC for target MidiProbabilityPersistenceTest
[ 43%] Automatic MOC for target MidiReconnectTest
[ 43%] Built target MidiLearnThreadTest_autogen
[ 43%] Built target MidiProbabilityPersistenceTest_autogen
[ 43%] Built target MidiReconnectTest_autogen
[ 43%] Automatic MOC for target MixerAbRegressionTest
[ 43%] Automatic MOC for target MixerConcurrencyTest
[ 43%] Automatic MOC for target MixerRoutingBackwardCompatTest
[ 43%] Built target MixerAbRegressionTest_autogen
[ 43%] Built target MixerConcurrencyTest_autogen
[ 43%] Built target MixerRoutingBackwardCompatTest_autogen
[ 43%] Automatic MOC for target MpeExpressionTest
[ 43%] Automatic MOC for target MpeNoteStorageTest
[ 43%] Automatic MOC for target MpeInputPathTest
[ 43%] Built target MpeNoteStorageTest_autogen
[ 43%] Automatic MOC for target ModulationLayerValueTest
[ 43%] Built target RetroMidiCaptureCommandsTest_autogen
[ 43%] Automatic MOC for target ModulationLayerTest
[ 43%] Built target MpeExpressionTest_autogen
[ 43%] Built target MpeInputPathTest_autogen
[ 43%] Automatic MOC for target ControlModulatorCommandsTest
[ 43%] Automatic MOC for target ControlNoteExpressionCommandsTest
[ 43%] Built target ModulationLayerValueTest_autogen
[ 43%] Automatic MOC for target ModulationLayerProjectRoundTripTest
[ 43%] Built target ModulationLayerTest_autogen
[ 43%] Automatic MOC for target MultiTrackRecorderTest
[ 43%] Built target MultiTrackRecorderTest_autogen
[ 43%] Automatic MOC for target RecordingInputPathTest
[ 43%] Built target ControlModulatorCommandsTest_autogen
[ 43%] Built target RecordingInputPathTest_autogen
[ 43%] Built target ControlNoteExpressionCommandsTest_autogen
[ 43%] Automatic MOC for target RetroAudioCaptureTest
[ 43%] Automatic MOC for target NamespaceRegistryTest
[ 43%] Built target RetroAudioCaptureTest_autogen
[ 43%] Built target NamespaceRegistryTest_autogen
[ 43%] Automatic MOC for target NoteRandomTest
[ 43%] Built target NoteRandomTest_autogen
[ 43%] Automatic MOC for target NoteTransformTest
[ 43%] Built target ModulationLayerProjectRoundTripTest_autogen
[ 43%] Automatic MOC for target PhaseFChannelScaleTest
[ 43%] Built target NoteTransformTest_autogen
[ 43%] Automatic MOC for target PluginLogoResourceTest
[ 43%] Built target PhaseFChannelScaleTest_autogen
[ 43%] Automatic MOC for target PluginAudioPortsTest
[ 43%] Automatic MOC for target PartialLoadTest
[ 43%] Built target PluginLogoResourceTest_autogen
[ 43%] Built target PluginAudioPortsTest_autogen
[ 43%] Automatic MOC for target ProjectOpenIntegrityTest
[ 43%] Automatic MOC for target ProjectRecoveryTest
[ 43%] Automatic MOC for target ProjectVersionTest
[ 43%] Built target ProjectRecoveryTest_autogen
[ 43%] Built target ProjectVersionTest_autogen
[ 43%] Automatic MOC for target RackMacrosTest
[ 43%] Automatic MOC for target RackTest
[ 43%] Built target PartialLoadTest_autogen
[ 43%] Built target ProjectOpenIntegrityTest_autogen
[ 43%] Automatic MOC for target RackZonesTest
[ 43%] Automatic MOC for target RecordClipTest
[ 43%] Built target RecordClipTest_autogen
[ 43%] Built target RackMacrosTest_autogen
[ 43%] Built target RackTest_autogen
[ 43%] Automatic MOC for target RecordRingBufferTest
[ 43%] Automatic MOC for target RetroMidiRingTest
[ 43%] Built target RecordRingBufferTest_autogen
[ 43%] Automatic MOC for target RecordingRealtimeTest
[ 43%] Built target RetroMidiRingTest_autogen
[ 43%] Built target RecordingRealtimeTest_autogen
[ 43%] Automatic MOC for target RelativePathsTest
[ 43%] Automatic MOC for target ReversibilityContractTest
[ 43%] Built target RelativePathsTest_autogen
[ 43%] Automatic MOC for target ReversibilityUndoTest
[ 43%] Automatic MOC for target RevisionTimelineTest
[ 43%] Built target RackZonesTest_autogen
[ 43%] Automatic MOC for target UnclaimedElementsTest
[ 43%] Built target ReversibilityContractTest_autogen
[ 43%] Built target ReversibilityUndoTest_autogen
[ 43%] Built target RevisionTimelineTest_autogen
[ 43%] Automatic MOC for target UnclaimedTrackTypeTest
[ 44%] Automatic MOC for target UndoBoundsTest
[ 44%] Automatic MOC for target TakeLaneCompTest
[ 44%] Built target UnclaimedElementsTest_autogen
[ 44%] Automatic MOC for target TakeLaneTest
[ 44%] Built target UnclaimedTrackTypeTest_autogen
[ 44%] Built target UndoBoundsTest_autogen
[ 44%] Automatic MOC for target RemotePluginAudioPortsTest
[ 44%] Built target TakeLaneCompTest_autogen
[ 44%] Built target RemotePluginAudioPortsTest_autogen
[ 44%] Automatic MOC for target RemotePluginClientE2ETest
[ 44%] Automatic MOC for target RenderJobQueueTest
[ 45%] Automatic MOC for target RoutingGraphLiveTest
[ 45%] Built target RenderJobQueueTest_autogen
[ 45%] Automatic MOC for target RoutingGraphScheduleTest
[ 45%] Built target TakeLaneTest_autogen
[ 45%] Built target RemotePluginClientE2ETest_autogen
[ 45%] Automatic MOC for target RoutingGraphTest
[ 45%] Automatic MOC for target PdcMixerTest
[ 45%] Built target PdcMixerTest_autogen
[ 45%] Automatic MOC for target PatcherCommandsTest
[ 45%] Built target RoutingGraphLiveTest_autogen
[ 45%] Built target RoutingGraphScheduleTest_autogen
[ 45%] Automatic MOC for target VcaGroupTest
[ 45%] Built target VcaGroupTest_autogen
[ 45%] Automatic MOC for target ControlVcaCommandsTest
[ 45%] Automatic MOC for target ControlVcaEditGroupsTest
[ 45%] Built target RoutingGraphTest_autogen
[ 45%] Automatic MOC for target ScriptBindingsTest
[ 45%] Built target PatcherCommandsTest_autogen
[ 45%] Automatic MOC for target ScriptDawBindingTest
[ 45%] Built target ControlVcaCommandsTest_autogen
[ 45%] Built target ControlVcaEditGroupsTest_autogen
[ 46%] Automatic MOC for target ScriptEngineTest
[ 46%] Automatic MOC for target ScriptClockTest
[ 46%] Built target ScriptBindingsTest_autogen
[ 46%] Automatic MOC for target ScriptMemoryBudgetTest
[ 46%] Built target ScriptDawBindingTest_autogen
[ 46%] Automatic MOC for target ScriptStabilisationTest
[ 46%] Built target ScriptClockTest_autogen
[ 46%] Built target ScriptEngineTest_autogen
[ 46%] Automatic MOC for target SlideNotesTest
[ 46%] Automatic MOC for target SmfInterchangeTest
[ 46%] Built target ScriptMemoryBudgetTest_autogen
[ 46%] Automatic MOC for target SmfInterchangeRoundTripTest
[ 46%] Built target ScriptStabilisationTest_autogen
[ 46%] Automatic MOC for target DawProjectInterchangeRoundTripTest
[ 46%] Built target SmfInterchangeTest_autogen
[ 46%] Built target SlideNotesTest_autogen
[ 46%] Automatic MOC for target ProjectRevIdsTest
[ 46%] Automatic MOC for target StableTrackIdsTest
[ 46%] Built target SmfInterchangeRoundTripTest_autogen
[ 47%] Automatic MOC for target StemExportTest
[ 47%] Built target DawProjectInterchangeRoundTripTest_autogen
[ 47%] Automatic MOC for target TelemetryTest
[ 47%] Built target StableTrackIdsTest_autogen
[ 47%] Built target ProjectRevIdsTest_autogen
[ 47%] Automatic MOC for target TelemetryTransportTest
[ 47%] Automatic MOC for target TempoMapPersistenceTest
[ 47%] Built target TelemetryTransportTest_autogen
[ 48%] Automatic MOC for target TempoMapTest
[ 48%] Built target StemExportTest_autogen
[ 48%] Automatic MOC for target TimelineTest
[ 48%] Built target TelemetryTest_autogen
[ 48%] Automatic MOC for target TrackFolderTest
[ 48%] Built target TempoMapPersistenceTest_autogen
[ 48%] Automatic MOC for target TwoTrackRecordingHarness
[ 48%] Built target TwoTrackRecordingHarness_autogen
[ 48%] Built target TempoMapTest_autogen
[ 48%] Automatic MOC for target WarpMarkersTest
[ 48%] Built target WarpMarkersTest_autogen
[ 48%] Automatic MOC for target FocusDeskPaneTest
[ 48%] Built target TimelineTest_autogen
[ 48%] Built target FocusDeskPaneTest_autogen
[ 48%] Automatic MOC for target FocusDeskTest
[ 48%] Automatic MOC for target MidiLearnGuiTest
[ 48%] Built target FocusDeskTest_autogen
[ 48%] Automatic MOC for target AutomationTrackTest
[ 49%] Automatic MOC for target SampleClipStretchTest
[ 49%] Built target TrackFolderTest_autogen
[ 49%] Automatic MOC for target SampleClipWarpTest
[ 49%] Built target AutomationTrackTest_autogen
[ 49%] Built target MidiLearnGuiTest_autogen
[ 49%] Built target SampleClipStretchTest_autogen
[ 49%] Automatic MOC for target SampleClipWindowTest
[ 49%] Automatic MOC for target SessionModelTest
[ 49%] Automatic MOC for target SessionSchedulerTest
[ 49%] Built target SessionSchedulerTest_autogen
[ 50%] Automatic MOC for target SessionSchedulerRenderTest
[ 50%] Built target SampleClipWarpTest_autogen
[ 50%] Automatic MOC for target SessionFollowTest
[ 50%] Built target SessionFollowTest_autogen
[ 50%] Automatic MOC for target SessionArrangementRecordTest
[ 50%] Built target SessionArrangementRecordTest_autogen
[ 50%] Automatic MOC for target WasmSandboxTest
[ 50%] Built target WasmSandboxTest_autogen
[ 50%] Automatic MOC for target WasmAbiConformanceTest
[ 50%] Built target WasmAbiConformanceTest_autogen
[ 50%] Building CXX object src/CMakeFiles/zene.dir/core/main.cpp.o
[ 50%] Built target SampleClipWindowTest_autogen
[ 50%] Built target SessionModelTest_autogen
[ 50%] Linking CXX executable ArrayVectorTest
[ 50%] Linking CXX executable AudioBufferTest
[ 50%] Built target SessionSchedulerRenderTest_autogen
[ 50%] Linking CXX executable AudioBusHandleTest
[ 50%] Built target AudioBufferTest
[ 50%] Built target ArrayVectorTest
[ 50%] Linking CXX executable AudioEngineTeardownTest
[ 50%] Linking CXX executable AudioBusTest
[ 50%] Built target AudioBusHandleTest
[ 50%] Linking CXX executable AudioPortsModelTest
[ 50%] Linking CXX executable ../zene
[ 50%] Built target AudioBusTest
[ 50%] Linking CXX executable AudioPortsTest
[ 50%] Built target AudioEngineTeardownTest
[ 51%] Built target AudioPortsModelTest
[ 51%] Linking CXX executable AudioStretcherTest
[ 51%] Linking CXX executable AudioResamplerRatioTest
[ 51%] Built target zene
[ 51%] Linking CXX executable AutomatableModelTest
[ 51%] Built target AudioPortsTest
[ 51%] Building CXX object tests/CMakeFiles/AutomationModesTest.dir/src/core/AutomationModesTest.cpp.o
[ 51%] Built target AudioStretcherTest
[ 51%] Building CXX object tests/CMakeFiles/SampleAccurateAutomationTest.dir/src/core/SampleAccurateAutomationTest.cpp.o
[ 51%] Built target AudioResamplerRatioTest
[ 51%] Linking CXX executable BrowserCatalogTest
[ 51%] Built target AutomatableModelTest
[ 51%] Building CXX object tests/CMakeFiles/ClipEditsTest.dir/src/core/ClipEditsTest.cpp.o
[ 51%] Built target BrowserCatalogTest
[ 51%] Building CXX object tests/CMakeFiles/ClipFadesRenderTest.dir/src/core/ClipFadesRenderTest.cpp.o
[ 51%] Linking CXX executable AutomationModesTest
[ 51%] Linking CXX executable SampleAccurateAutomationTest
[ 51%] Linking CXX executable ClipEditsTest
[ 51%] Linking CXX executable ClipFadesRenderTest
[ 51%] Built target AutomationModesTest
[ 51%] Building CXX object tests/CMakeFiles/ClipLinkTest.dir/src/core/ClipLinkTest.cpp.o
[ 51%] Built target SampleAccurateAutomationTest
[ 51%] Built target ClipEditsTest
[ 51%] Building CXX object tests/CMakeFiles/ClipLinkPersistenceTest.dir/src/core/ClipLinkPersistenceTest.cpp.o
[ 51%] Building CXX object tests/CMakeFiles/ClipSerialisationTest.dir/src/core/ClipSerialisationTest.cpp.o
[ 52%] Built target ClipFadesRenderTest
[ 52%] Building CXX object tests/CMakeFiles/WriteRefusalGateTest.dir/WriteRefusalGateTest_autogen/mocs_compilation.cpp.o
[ 52%] Building CXX object tests/CMakeFiles/WriteRefusalGateTest.dir/src/core/WriteRefusalGateTest.cpp.o
[ 52%] Linking CXX executable ClipLinkTest
[ 52%] Linking CXX executable ClipLinkPersistenceTest
[ 52%] Linking CXX executable ClipSerialisationTest
[ 52%] Linking CXX executable WriteRefusalGateTest
[ 52%] Built target ClipLinkTest
[ 52%] Building CXX object tests/CMakeFiles/ClipWarpPersistenceTest.dir/src/core/ClipWarpPersistenceTest.cpp.o
[ 52%] Built target ClipLinkPersistenceTest
[ 52%] Linking CXX executable ConfigMigrationTest
[ 52%] Built target ClipSerialisationTest
[ 52%] Building CXX object tests/CMakeFiles/ControlAutomationScriptTest.dir/src/core/ControlAutomationScriptTest.cpp.o
[ 52%] Built target WriteRefusalGateTest
[ 52%] Building CXX object tests/CMakeFiles/ControlAutomationModesTest.dir/src/core/ControlAutomationModesTest.cpp.o
[ 52%] Built target ConfigMigrationTest
[ 52%] Building CXX object tests/CMakeFiles/ControlBrowserCommandsTest.dir/src/core/ControlBrowserCommandsTest.cpp.o
[ 52%] Linking CXX executable ClipWarpPersistenceTest
[ 52%] Linking CXX executable ControlAutomationScriptTest
[ 52%] Built target ClipWarpPersistenceTest
[ 52%] Building CXX object tests/CMakeFiles/ControlChainPresetTest.dir/src/core/ControlChainPresetTest.cpp.o
[ 52%] Linking CXX executable ControlAutomationModesTest
[ 52%] Linking CXX executable ControlBrowserCommandsTest
[ 52%] Built target ControlAutomationScriptTest
[ 52%] Building CXX object tests/CMakeFiles/ChordTrackTest.dir/src/core/ChordTrackTest.cpp.o
[ 53%] Built target ControlAutomationModesTest
[ 53%] Building CXX object tests/CMakeFiles/ChordDetectTest.dir/src/core/ChordDetectTest.cpp.o
[ 53%] Built target ControlBrowserCommandsTest
[ 53%] Linking CXX executable ChordProgressionTest
[ 53%] Linking CXX executable ControlChainPresetTest
[ 53%] Built target ChordProgressionTest
[ 53%] Building CXX object tests/CMakeFiles/ControlChordCommandsTest.dir/src/core/ControlChordCommandsTest.cpp.o
[ 53%] Linking CXX executable ChordTrackTest
[ 53%] Linking CXX executable ChordDetectTest
[ 53%] Built target ControlChainPresetTest
[ 53%] Building CXX object tests/CMakeFiles/ControlChordWriteTest.dir/src/core/ControlChordWriteTest.cpp.o
[ 53%] Built target ChordTrackTest
[ 53%] Building CXX object tests/CMakeFiles/ControlDeviceCatalogueTest.dir/src/core/ControlDeviceCatalogueTest.cpp.o
[ 53%] Built target ChordDetectTest
[ 53%] Building CXX object tests/CMakeFiles/ControlEditCommandsTest.dir/src/core/ControlEditCommandsTest.cpp.o
[ 53%] Linking CXX executable ControlChordCommandsTest
[ 53%] Linking CXX executable ControlChordWriteTest
[ 53%] Linking CXX executable ControlDeviceCatalogueTest
[ 53%] Linking CXX executable ControlEditCommandsTest
[ 53%] Built target ControlChordCommandsTest
[ 53%] Building CXX object tests/CMakeFiles/ControlLinkCommandsTest.dir/src/core/ControlLinkCommandsTest.cpp.o
[ 53%] Built target ControlChordWriteTest
[ 53%] Building CXX object tests/CMakeFiles/ControlProjectArchiveTest.dir/src/core/ControlProjectArchiveTest.cpp.o
[ 54%] Built target ControlDeviceCatalogueTest
[ 54%] Building CXX object tests/CMakeFiles/ControlRegistryTest.dir/src/core/ControlRegistryTest.cpp.o
[ 54%] Built target ControlEditCommandsTest
[ 54%] Building CXX object tests/CMakeFiles/ControlSurfaceReferenceTest.dir/src/core/ControlSurfaceReferenceTest.cpp.o
[ 54%] Linking CXX executable ControlLinkCommandsTest
[ 54%] Linking CXX executable ControlSurfaceReferenceTest
[ 54%] Linking CXX executable ControlRegistryTest
[ 54%] Linking CXX executable ControlProjectArchiveTest
[ 54%] Built target ControlLinkCommandsTest
[ 54%] Building CXX object tests/CMakeFiles/ControllerSurfaceTest.dir/src/core/ControllerSurfaceTest.cpp.o
[ 54%] Built target ControlSurfaceReferenceTest
[ 54%] Building CXX object tests/CMakeFiles/ControlShutdownHookTest.dir/src/core/ControlShutdownHookTest.cpp.o
[ 54%] Built target ControlRegistryTest
[ 54%] Building CXX object tests/CMakeFiles/ControlTempoMapCommandsTest.dir/src/core/ControlTempoMapCommandsTest.cpp.o
[ 54%] Built target ControlProjectArchiveTest
[ 54%] Building CXX object tests/CMakeFiles/ControlVerbInverseTest.dir/src/core/ControlVerbInverseTest.cpp.o
[ 54%] Linking CXX executable ControllerSurfaceTest
[ 54%] Linking CXX executable ControlShutdownHookTest
[ 54%] Built target ControllerSurfaceTest
[ 54%] Linking CXX executable ControlVerbInverseTest
[ 54%] Building CXX object tests/CMakeFiles/ControlSampleOperatorTest.dir/ControlSampleOperatorTest_autogen/mocs_compilation.cpp.o
[ 54%] Building CXX object tests/CMakeFiles/ControlSampleOperatorTest.dir/src/core/ControlSampleOperatorTest.cpp.o
[ 54%] Linking CXX executable ControlTempoMapCommandsTest
[ 54%] Built target ControlShutdownHookTest
[ 54%] Building CXX object tests/CMakeFiles/ControlNoteScaleVerbsTest.dir/src/core/ControlNoteScaleVerbsTest.cpp.o
[ 54%] Built target ControlVerbInverseTest
[ 54%] Building CXX object tests/CMakeFiles/ControlWarpCommandsTest.dir/src/core/ControlWarpCommandsTest.cpp.o
[ 55%] Built target ControlTempoMapCommandsTest
[ 55%] Building CXX object tests/CMakeFiles/GrooveTemplateTest.dir/src/core/GrooveTemplateTest.cpp.o
[ 55%] Linking CXX executable ControlSampleOperatorTest
[ 55%] Linking CXX executable ControlNoteScaleVerbsTest
[ 55%] Linking CXX executable GrooveTemplateTest
[ 55%] Linking CXX executable ControlWarpCommandsTest
[ 55%] Built target ControlSampleOperatorTest
[ 55%] Building CXX object tests/CMakeFiles/ControlGrooveCommandsTest.dir/src/core/ControlGrooveCommandsTest.cpp.o
[ 56%] Built target ControlNoteScaleVerbsTest
[ 56%] Linking CXX executable CrashReporterArmTest
[ 57%] Built target GrooveTemplateTest
[ 57%] Building CXX object tests/CMakeFiles/CrashReporterTest.dir/src/core/CrashReporterTest.cpp.o
[ 57%] Built target ControlWarpCommandsTest
[ 57%] Linking CXX executable DataFileFormatTest
[ 58%] Built target CrashReporterArmTest
[ 58%] Building CXX object tests/CMakeFiles/DataFileSaveIntegrityTest.dir/src/core/DataFileSaveIntegrityTest.cpp.o
[ 58%] Built target DataFileFormatTest
[ 58%] Building CXX object tests/CMakeFiles/DocumentIndexTest.dir/src/core/DocumentIndexTest.cpp.o
[ 58%] Linking CXX executable CrashReporterTest
[ 58%] Linking CXX executable ControlGrooveCommandsTest
[ 58%] Built target CrashReporterTest
[ 58%] Linking CXX executable DocumentSectionsTest
[ 58%] Built target ControlGrooveCommandsTest
[ 58%] Linking CXX executable ProjectContainerTest
[ 58%] Linking CXX executable DataFileSaveIntegrityTest
[ 58%] Linking CXX executable DocumentIndexTest
[ 59%] Built target DocumentSectionsTest
[ 60%] Linking CXX executable ProjectContainerEntriesTest
[ 60%] Built target ProjectContainerTest
[ 60%] Built target DataFileSaveIntegrityTest
[ 60%] Linking CXX executable ExportDitherTest
[ 60%] Linking CXX executable ExportWavDitherTest
[ 60%] Built target DocumentIndexTest
[ 60%] Linking CXX executable LoudnessReportTest
[ 60%] Built target ProjectContainerEntriesTest
[ 60%] Linking CXX executable LufsMeterTest
[ 60%] Built target ExportWavDitherTest
[ 60%] Built target ExportDitherTest
[ 60%] Building CXX object tests/CMakeFiles/MasteringTest.dir/src/core/MasteringTest.cpp.o
[ 60%] Linking CXX executable ImportDetectionTest
[ 60%] Built target LoudnessReportTest
[ 60%] Linking CXX executable MathTest
[ 60%] Built target LufsMeterTest
[ 60%] Linking CXX executable MeterTapTest
[ 60%] Built target ImportDetectionTest
[ 60%] Building CXX object tests/CMakeFiles/MidiLearnTest.dir/src/core/MidiLearnTest.cpp.o
[ 60%] Built target MathTest
[ 60%] Building CXX object tests/CMakeFiles/MidiClockTest.dir/src/core/MidiClockTest.cpp.o
[ 61%] Built target MeterTapTest
[ 61%] Building CXX object tests/CMakeFiles/MidiLearnThreadTest.dir/src/core/MidiLearnThreadTest.cpp.o
[ 61%] Linking CXX executable MasteringTest
[ 61%] Linking CXX executable MidiLearnTest
[ 61%] Linking CXX executable MidiClockTest
[ 61%] Built target MasteringTest
[ 61%] Linking CXX executable MidiRetroCaptureTest
[ 61%] Linking CXX executable MidiLearnThreadTest
[ 61%] Built target MidiLearnTest
[ 61%] Building CXX object tests/CMakeFiles/RetroMidiCaptureCommandsTest.dir/src/core/RetroMidiCaptureCommandsTest.cpp.o
[ 61%] Built target MidiClockTest
[ 61%] Linking CXX executable MidiProbabilityPersistenceTest
[ 61%] Built target MidiRetroCaptureTest
[ 61%] Linking CXX executable MidiReconnectTest
[ 61%] Built target MidiLearnThreadTest
[ 61%] Linking CXX executable MixerAbRegressionTest
[ 61%] Built target MidiProbabilityPersistenceTest
[ 61%] Linking CXX executable MixerConcurrencyTest
[ 62%] Built target MidiReconnectTest
[ 62%] Linking CXX executable MixerRoutingBackwardCompatTest
[ 62%] Built target MixerAbRegressionTest
[ 62%] Building CXX object tests/CMakeFiles/MpeExpressionTest.dir/src/core/MpeExpressionTest.cpp.o
[ 62%] Linking CXX executable RetroMidiCaptureCommandsTest
[ 62%] Built target MixerConcurrencyTest
[ 62%] Building CXX object tests/CMakeFiles/MpeInputPathTest.dir/src/core/MpeInputPathTest.cpp.o
[ 62%] Built target MixerRoutingBackwardCompatTest
[ 62%] Linking CXX executable MpeNoteStorageTest
[ 62%] Built target RetroMidiCaptureCommandsTest
[ 62%] Built target mpe_test_consumer_autogen_timestamp_deps
[ 62%] Building CXX object tests/CMakeFiles/ModulationLayerValueTest.dir/src/core/ModulationLayerValueTest.cpp.o
[ 62%] Linking CXX executable MpeExpressionTest
[ 62%] Built target MpeNoteStorageTest
[ 62%] Building CXX object tests/CMakeFiles/ModulationLayerTest.dir/src/core/ModulationLayerTest.cpp.o
[ 62%] Linking CXX executable MpeInputPathTest
[ 62%] Built target MpeExpressionTest
[ 62%] Building CXX object tests/CMakeFiles/ControlModulatorCommandsTest.dir/src/core/ControlModulatorCommandsTest.cpp.o
[ 62%] Built target MpeInputPathTest
[ 62%] Linking CXX executable ModulationLayerValueTest
[ 62%] Building CXX object tests/CMakeFiles/ControlNoteExpressionCommandsTest.dir/src/core/ControlNoteExpressionCommandsTest.cpp.o
[ 62%] Linking CXX executable ModulationLayerTest
[ 62%] Built target ModulationLayerValueTest
[ 62%] Building CXX object tests/CMakeFiles/ModulationLayerProjectRoundTripTest.dir/src/core/ModulationLayerProjectRoundTripTest.cpp.o
[ 63%] Built target ModulationLayerTest
[ 63%] Linking CXX executable MultiTrackRecorderTest
[ 63%] Linking CXX executable ControlModulatorCommandsTest
[ 63%] Linking CXX executable ControlNoteExpressionCommandsTest
[ 64%] Built target MultiTrackRecorderTest
[ 64%] Linking CXX executable RecordingInputPathTest
[ 64%] Built target ControlModulatorCommandsTest
[ 65%] Linking CXX executable RetroAudioCaptureTest
[ 65%] Built target ControlNoteExpressionCommandsTest
[ 65%] Linking CXX executable NamespaceRegistryTest
[ 65%] Linking CXX executable ModulationLayerProjectRoundTripTest
[ 65%] Built target RecordingInputPathTest
[ 65%] Linking CXX executable NoteRandomTest
[ 65%] Built target RetroAudioCaptureTest
[ 65%] Linking CXX executable NoteTransformTest
[ 65%] Built target NamespaceRegistryTest
[ 65%] Built target ModulationLayerProjectRoundTripTest
[ 66%] Linking CXX executable PluginLogoResourceTest
[ 66%] Linking CXX executable PhaseFChannelScaleTest
[ 66%] Built target NoteRandomTest
[ 66%] Linking CXX executable PluginAudioPortsTest
[ 66%] Built target NoteTransformTest
[ 66%] Building CXX object tests/CMakeFiles/PartialLoadTest.dir/src/core/PartialLoadTest.cpp.o
[ 66%] Built target PhaseFChannelScaleTest
[ 66%] Built target PluginLogoResourceTest
[ 66%] Building CXX object tests/CMakeFiles/ProjectOpenIntegrityTest.dir/src/core/ProjectOpenIntegrityTest.cpp.o
[ 66%] Linking CXX executable ProjectRecoveryTest
[ 66%] Built target PluginAudioPortsTest
[ 66%] Linking CXX executable ProjectVersionTest
[ 66%] Built target ProjectRecoveryTest
[ 66%] Building CXX object tests/CMakeFiles/RackMacrosTest.dir/src/core/RackMacrosTest.cpp.o
[ 66%] Built target ProjectVersionTest
[ 66%] Building CXX object tests/CMakeFiles/RackTest.dir/src/core/RackTest.cpp.o
[ 66%] Linking CXX executable PartialLoadTest
[ 66%] Linking CXX executable ProjectOpenIntegrityTest
[ 66%] Built target PartialLoadTest
[ 66%] Building CXX object tests/CMakeFiles/RackZonesTest.dir/src/core/RackZonesTest.cpp.o
[ 66%] Built target ProjectOpenIntegrityTest
[ 66%] Linking CXX executable RecordClipTest
[ 67%] Linking CXX executable RackMacrosTest
[ 67%] Linking CXX executable RackTest
[ 67%] Built target RecordClipTest
[ 67%] Linking CXX executable RecordRingBufferTest
[ 67%] Built target RackMacrosTest
[ 67%] Linking CXX executable RetroMidiRingTest
[ 67%] Built target RackTest
[ 68%] Linking CXX executable RecordingRealtimeTest
[ 68%] Linking CXX executable RackZonesTest
[ 68%] Built target RecordRingBufferTest
[ 68%] Linking CXX executable RelativePathsTest
[ 68%] Built target RetroMidiRingTest
[ 68%] Building CXX object tests/CMakeFiles/ReversibilityContractTest.dir/src/core/ReversibilityContractTest.cpp.o
[ 68%] Built target RecordingRealtimeTest
[ 68%] Building CXX object tests/CMakeFiles/ReversibilityUndoTest.dir/src/core/ReversibilityUndoTest.cpp.o
[ 68%] Built target RackZonesTest
[ 68%] Building CXX object tests/CMakeFiles/RevisionTimelineTest.dir/src/core/RevisionTimelineTest.cpp.o
[ 68%] Built target RelativePathsTest
[ 68%] Building CXX object tests/CMakeFiles/UnclaimedElementsTest.dir/src/core/UnclaimedElementsTest.cpp.o
[ 68%] Linking CXX executable ReversibilityContractTest
[ 68%] Linking CXX executable RevisionTimelineTest
[ 68%] Linking CXX executable UnclaimedElementsTest
[ 68%] Linking CXX executable ReversibilityUndoTest
[ 68%] Built target ReversibilityContractTest
[ 68%] Building CXX object tests/CMakeFiles/UnclaimedTrackTypeTest.dir/src/core/UnclaimedTrackTypeTest.cpp.o
[ 68%] Built target RevisionTimelineTest
[ 68%] Building CXX object tests/CMakeFiles/UndoBoundsTest.dir/src/core/UndoBoundsTest.cpp.o
[ 68%] Built target UnclaimedElementsTest
[ 68%] Building CXX object tests/CMakeFiles/TakeLaneCompTest.dir/src/core/TakeLaneCompTest.cpp.o
[ 68%] Built target ReversibilityUndoTest
[ 68%] Building CXX object tests/CMakeFiles/TakeLaneTest.dir/src/core/TakeLaneTest.cpp.o
[ 68%] Linking CXX executable UnclaimedTrackTypeTest
[ 68%] Linking CXX executable UndoBoundsTest
[ 68%] Linking CXX executable TakeLaneCompTest
[ 68%] Built target UnclaimedTrackTypeTest
[ 68%] Linking CXX executable RemotePluginAudioPortsTest
[ 68%] Linking CXX executable TakeLaneTest
[ 68%] Built target UndoBoundsTest
[ 68%] Building CXX object tests/CMakeFiles/RemotePluginClientE2ETest.dir/src/core/RemotePluginClientE2ETest.cpp.o
[ 68%] Built target TakeLaneCompTest
[ 68%] Linking CXX executable RenderJobQueueTest
[ 68%] Built target RemotePluginAudioPortsTest
[ 68%] Built target TakeLaneTest
[ 68%] Building CXX object tests/CMakeFiles/RoutingGraphScheduleTest.dir/RoutingGraphScheduleTest_autogen/mocs_compilation.cpp.o
[ 68%] Building CXX object tests/CMakeFiles/RoutingGraphLiveTest.dir/src/core/RoutingGraphLiveTest.cpp.o
[ 68%] Building CXX object tests/CMakeFiles/RoutingGraphScheduleTest.dir/src/core/RoutingGraphScheduleTest.cpp.o
[ 68%] Built target RenderJobQueueTest
[ 68%] Building CXX object tests/CMakeFiles/RoutingGraphTest.dir/src/core/RoutingGraphTest.cpp.o
[ 68%] Linking CXX executable RemotePluginClientE2ETest
[ 68%] Linking CXX executable RoutingGraphLiveTest
[ 68%] Linking CXX executable RoutingGraphScheduleTest
[ 68%] Built target RemotePluginClientE2ETest
[ 68%] Linking CXX executable PdcMixerTest
[ 68%] Built target RoutingGraphLiveTest
[ 68%] Building CXX object tests/CMakeFiles/PatcherCommandsTest.dir/src/core/PatcherCommandsTest.cpp.o
[ 68%] Linking CXX executable RoutingGraphTest
[ 68%] Built target RoutingGraphScheduleTest
[ 68%] Linking CXX executable VcaGroupTest
[ 68%] Built target PdcMixerTest
[ 68%] Building CXX object tests/CMakeFiles/ControlVcaCommandsTest.dir/src/core/ControlVcaCommandsTest.cpp.o
[ 68%] Built target RoutingGraphTest
[ 68%] Building CXX object tests/CMakeFiles/ControlVcaEditGroupsTest.dir/src/core/ControlVcaEditGroupsTest.cpp.o
[ 68%] Built target VcaGroupTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptBindingsTest.dir/src/core/ScriptBindingsTest.cpp.o
[ 68%] Linking CXX executable PatcherCommandsTest
[ 68%] Linking CXX executable ControlVcaCommandsTest
[ 68%] Linking CXX executable ControlVcaEditGroupsTest
[ 68%] Built target PatcherCommandsTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptDawBindingTest.dir/src/core/ScriptDawBindingTest.cpp.o
[ 68%] Linking CXX executable ScriptBindingsTest
[ 68%] Built target ControlVcaCommandsTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptEngineTest.dir/src/core/ScriptEngineTest.cpp.o
[ 68%] Built target ControlVcaEditGroupsTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptClockTest.dir/ScriptClockTest_autogen/mocs_compilation.cpp.o
[ 68%] Building CXX object tests/CMakeFiles/ScriptClockTest.dir/src/core/ScriptClockTest.cpp.o
[ 68%] Built target ScriptBindingsTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptMemoryBudgetTest.dir/src/core/ScriptMemoryBudgetTest.cpp.o
[ 68%] Linking CXX executable ScriptDawBindingTest
[ 68%] Linking CXX executable ScriptClockTest
[ 68%] Linking CXX executable ScriptMemoryBudgetTest
[ 68%] Built target ScriptDawBindingTest
[ 68%] Building CXX object tests/CMakeFiles/ScriptStabilisationTest.dir/src/core/ScriptStabilisationTest.cpp.o
[ 68%] Linking CXX executable ScriptEngineTest
[ 68%] Built target ScriptClockTest
[ 68%] Building CXX object tests/CMakeFiles/SlideNotesTest.dir/src/core/SlideNotesTest.cpp.o
[ 68%] Built target ScriptMemoryBudgetTest
[ 68%] Building CXX object tests/CMakeFiles/SmfInterchangeTest.dir/src/core/SmfInterchangeTest.cpp.o
[ 68%] Built target ScriptEngineTest
[ 68%] Building CXX object tests/CMakeFiles/SmfInterchangeRoundTripTest.dir/src/core/SmfInterchangeRoundTripTest.cpp.o
[ 68%] Linking CXX executable ScriptStabilisationTest
[ 68%] Linking CXX executable SlideNotesTest
[ 68%] Linking CXX executable SmfInterchangeTest
[ 68%] Built target ScriptStabilisationTest
[ 68%] Building CXX object tests/CMakeFiles/DawProjectInterchangeRoundTripTest.dir/src/core/DawProjectInterchangeRoundTripTest.cpp.o
[ 68%] Linking CXX executable SmfInterchangeRoundTripTest
[ 68%] Built target SlideNotesTest
[ 68%] Building CXX object tests/CMakeFiles/StableTrackIdsTest.dir/src/core/StableTrackIdsTest.cpp.o
[ 68%] Built target SmfInterchangeTest
[ 68%] Building CXX object tests/CMakeFiles/ProjectRevIdsTest.dir/ProjectRevIdsTest_autogen/mocs_compilation.cpp.o
[ 68%] Building CXX object tests/CMakeFiles/ProjectRevIdsTest.dir/src/core/ProjectRevIdsTest.cpp.o
[ 68%] Built target SmfInterchangeRoundTripTest
[ 68%] Building CXX object tests/CMakeFiles/StemExportTest.dir/src/core/StemExportTest.cpp.o
[ 68%] Linking CXX executable DawProjectInterchangeRoundTripTest
[ 68%] Linking CXX executable StableTrackIdsTest
[ 68%] Linking CXX executable ProjectRevIdsTest
[ 68%] Built target DawProjectInterchangeRoundTripTest
[ 68%] Building CXX object tests/CMakeFiles/TelemetryTest.dir/src/core/TelemetryTest.cpp.o
[ 68%] Linking CXX executable StemExportTest
[ 68%] Built target StableTrackIdsTest
[ 68%] Linking CXX executable TelemetryTransportTest
[ 68%] Built target ProjectRevIdsTest
[ 68%] Building CXX object tests/CMakeFiles/TempoMapPersistenceTest.dir/src/core/TempoMapPersistenceTest.cpp.o
[ 68%] Built target StemExportTest
[ 68%] Building CXX object tests/CMakeFiles/TempoMapTest.dir/src/core/TempoMapTest.cpp.o
[ 68%] Built target TelemetryTransportTest
[ 68%] Building CXX object tests/CMakeFiles/TimelineTest.dir/src/core/TimelineTest.cpp.o
[ 68%] Linking CXX executable TelemetryTest
[ 68%] Linking CXX executable TempoMapPersistenceTest
[ 68%] Built target TelemetryTest
[ 68%] Building CXX object tests/CMakeFiles/TrackFolderTest.dir/src/core/TrackFolderTest.cpp.o
[ 68%] Linking CXX executable TempoMapTest
[ 68%] Linking CXX executable TimelineTest
[ 68%] Built target TempoMapPersistenceTest
[ 68%] Linking CXX executable TwoTrackRecordingHarness
[ 68%] Built target TempoMapTest
[ 68%] Linking CXX executable WarpMarkersTest
[ 68%] Built target TimelineTest
[ 68%] Linking CXX executable FocusDeskPaneTest
[ 68%] Built target TwoTrackRecordingHarness
[ 68%] Linking CXX executable FocusDeskTest
[ 68%] Linking CXX executable TrackFolderTest
[ 68%] Built target WarpMarkersTest
[ 68%] Building CXX object tests/CMakeFiles/MidiLearnGuiTest.dir/src/gui/MidiLearnGuiTest.cpp.o
[ 68%] Built target FocusDeskPaneTest
[ 68%] Building CXX object tests/CMakeFiles/AutomationTrackTest.dir/src/tracks/AutomationTrackTest.cpp.o
[ 68%] Built target FocusDeskTest
[ 68%] Building CXX object tests/CMakeFiles/SampleClipStretchTest.dir/src/tracks/SampleClipStretchTest.cpp.o
[ 68%] Built target TrackFolderTest
[ 68%] Building CXX object tests/CMakeFiles/SampleClipWarpTest.dir/src/tracks/SampleClipWarpTest.cpp.o
[ 68%] Linking CXX executable MidiLearnGuiTest
[ 68%] Linking CXX executable AutomationTrackTest
[ 68%] Linking CXX executable SampleClipWarpTest
[ 68%] Linking CXX executable SampleClipStretchTest
[ 68%] Built target MidiLearnGuiTest
[ 68%] Building CXX object tests/CMakeFiles/SampleClipWindowTest.dir/src/tracks/SampleClipWindowTest.cpp.o
[ 69%] Built target AutomationTrackTest
[ 69%] Building CXX object tests/CMakeFiles/SessionModelTest.dir/src/core/SessionModelTest.cpp.o
[ 69%] Built target SampleClipWarpTest
[ 69%] Linking CXX executable SessionSchedulerTest
[ 69%] Built target SampleClipStretchTest
[ 69%] Building CXX object tests/CMakeFiles/SessionSchedulerRenderTest.dir/src/core/SessionSchedulerRenderTest.cpp.o
[ 69%] Built target SessionSchedulerTest
[ 69%] Linking CXX executable SessionFollowTest
[ 69%] Linking CXX executable SampleClipWindowTest
[ 69%] Linking CXX executable SessionModelTest
[ 69%] Linking CXX executable SessionSchedulerRenderTest
[ 69%] Built target SessionFollowTest
[ 69%] Built target SampleClipWindowTest
[ 69%] Built target partc_ref_amplifier_autogen_timestamp_deps
[ 69%] Linking CXX executable SessionArrangementRecordTest
[ 69%] Built target partc_ref_bassbooster_autogen_timestamp_deps
[ 69%] Built target partc_ref_bitcrush_autogen_timestamp_deps
[ 69%] Built target partc_ref_dualfilter_autogen_timestamp_deps
[ 69%] Built target partc_ref_waveshaper_autogen_timestamp_deps
[ 69%] Built target partc_ref_flanger_autogen_timestamp_deps
[ 69%] Built target partc_ref_delay_autogen_timestamp_deps
[ 69%] Built target partc_ref_compressor_autogen_timestamp_deps
[ 69%] Built target partc_ref_crossovereq_autogen_timestamp_deps
[ 69%] Built target partc_ref_dynamicsprocessor_autogen_timestamp_deps
[ 69%] Built target partc_ref_lomm_autogen_timestamp_deps
[ 69%] Built target partc_ref_multitapecho_autogen_timestamp_deps
[ 69%] Built target partc_ref_reverbsc_autogen_timestamp_deps
[ 69%] Built target partc_ref_stereoenhancer_autogen_timestamp_deps
[ 69%] Built target partc_ref_stereomatrix_autogen_timestamp_deps
[ 69%] Built target partc_ref_dispersion_autogen_timestamp_deps
[ 69%] Built target partc_ref_vectorscope_autogen_timestamp_deps
[ 69%] Built target partc_ref_analyzer_autogen_timestamp_deps
[ 69%] Built target partc_ref_granularpitchshifter_autogen_timestamp_deps
[ 69%] Built target partc_ref_eq_autogen_timestamp_deps
[ 69%] Built target partc_ref_freeboy_autogen_timestamp_deps
[ 69%] Built target partc_ref_nes_autogen_timestamp_deps
[ 69%] Built target partc_ref_sid_autogen_timestamp_deps
[ 69%] Built target partc_ref_opulenz_autogen_timestamp_deps
[ 69%] Built target partc_ref_sfxr_autogen_timestamp_deps
[ 69%] Built target partc_ref_bitinvader_autogen_timestamp_deps
[ 69%] Built target partc_ref_watsyn_autogen_timestamp_deps
[ 69%] Built target partc_ref_xpressive_autogen_timestamp_deps
[ 69%] Built target SessionModelTest
[ 69%] Built target partc_ref_vibedstrings_autogen_timestamp_deps
[ 69%] Built target partc_ref_kicker_autogen_timestamp_deps
[ 69%] Built target partc_ref_tripleoscillator_autogen_timestamp_deps
[ 69%] Built target partc_ref_monstro_autogen_timestamp_deps
[ 69%] Built target partc_ref_organic_autogen_timestamp_deps
[ 69%] Built target partc_ref_audiofileprocessor_autogen_timestamp_deps
[ 69%] Built target partc_ref_lb302_autogen_timestamp_deps
[ 69%] Built target partc_ref_ladspaeffect_autogen_timestamp_deps
[ 69%] Built target partc_ref_frequencyshifter_autogen_timestamp_deps
[ 69%] Built target partc_ref_oscilloscope_autogen_timestamp_deps
[ 69%] Built target partc_ref_slewdistortion_autogen_timestamp_deps
[ 69%] Built target partc_ref_lv2effect_autogen_timestamp_deps
[ 69%] Built target partc_ref_lv2instrument_autogen_timestamp_deps
[ 69%] Built target partc_ref_sf2player_autogen_timestamp_deps
[ 69%] Built target partc_ref_gigplayer_autogen_timestamp_deps
[ 69%] Linking CXX executable WasmSandboxTest
[ 69%] Linking CXX executable WasmAbiConformanceTest
[ 69%] Built target SessionSchedulerRenderTest
[ 69%] Built target synthetic_audio_plugin_autogen_timestamp_deps
[ 69%] Building CXX object plugins/AudioFileProcessor/CMakeFiles/audiofileprocessor.dir/AudioFileProcessor.cpp.o
[ 69%] Built target SessionArrangementRecordTest
[ 69%] Building CXX object plugins/Kicker/CMakeFiles/kicker.dir/Kicker.cpp.o
[ 69%] Built target WasmAbiConformanceTest
[ 69%] Building CXX object plugins/TripleOscillator/CMakeFiles/tripleoscillator.dir/TripleOscillator.cpp.o
[ 69%] Built target WasmSandboxTest
[ 69%] Building CXX object plugins/Amplifier/CMakeFiles/amplifier.dir/Amplifier.cpp.o
[ 69%] Building CXX object plugins/AudioFileProcessor/CMakeFiles/audiofileprocessor.dir/AudioFileProcessorView.cpp.o
[ 69%] Linking CXX shared module ../libkicker.so
[ 69%] Built target kicker
[ 69%] Building CXX object plugins/BassBooster/CMakeFiles/bassbooster.dir/BassBooster.cpp.o
[ 69%] Building CXX object plugins/Amplifier/CMakeFiles/amplifier.dir/AmplifierControls.cpp.o
[ 69%] Linking CXX shared module ../libtripleoscillator.so
[ 69%] Built target tripleoscillator
[ 69%] Building CXX object plugins/BitInvader/CMakeFiles/bitinvader.dir/BitInvader.cpp.o
[ 69%] Linking CXX shared module ../libaudiofileprocessor.so
[ 69%] Linking CXX shared module ../libamplifier.so
[ 69%] Built target audiofileprocessor
[ 69%] Building CXX object plugins/Bitcrush/CMakeFiles/bitcrush.dir/Bitcrush.cpp.o
[ 70%] Built target amplifier
[ 70%] Building CXX object plugins/CarlaBase/CMakeFiles/carlabase.dir/Carla.cpp.o
[ 70%] Building CXX object plugins/BassBooster/CMakeFiles/bassbooster.dir/BassBoosterControls.cpp.o
[ 70%] Linking CXX shared module ../libbitinvader.so
[ 70%] Built target bitinvader
[ 70%] Building CXX object plugins/Compressor/CMakeFiles/compressor.dir/Compressor.cpp.o
[ 70%] Linking CXX shared module ../libbassbooster.so
[ 70%] Building CXX object plugins/Bitcrush/CMakeFiles/bitcrush.dir/BitcrushControls.cpp.o
[ 71%] Built target bassbooster
[ 71%] Building CXX object plugins/CrossoverEQ/CMakeFiles/crossovereq.dir/CrossoverEQ.cpp.o
[ 71%] Linking CXX shared library ../libcarlabase.so
[ 72%] Built target carlabase
[ 72%] Building CXX object plugins/Delay/CMakeFiles/delay.dir/DelayEffect.cpp.o
[ 72%] Linking CXX shared module ../libbitcrush.so
[ 72%] Building CXX object plugins/Compressor/CMakeFiles/compressor.dir/CompressorControls.cpp.o
[ 72%] Built target bitcrush
[ 72%] Building CXX object plugins/Dispersion/CMakeFiles/dispersion.dir/Dispersion.cpp.o
[ 72%] Building CXX object plugins/CrossoverEQ/CMakeFiles/crossovereq.dir/CrossoverEQControls.cpp.o
[ 72%] Building CXX object plugins/Delay/CMakeFiles/delay.dir/DelayControls.cpp.o
[ 72%] Building CXX object plugins/Compressor/CMakeFiles/compressor.dir/CompressorControlDialog.cpp.o
[ 72%] Linking CXX shared module ../libcrossovereq.so
[ 72%] Building CXX object plugins/Dispersion/CMakeFiles/dispersion.dir/DispersionControls.cpp.o
[ 73%] Built target crossovereq
[ 73%] Building CXX object plugins/DualFilter/CMakeFiles/dualfilter.dir/DualFilter.cpp.o
[ 73%] Linking CXX shared module ../libdelay.so
[ 74%] Built target delay
[ 74%] Building CXX object plugins/DynamicsProcessor/CMakeFiles/dynamicsprocessor.dir/DynamicsProcessor.cpp.o
[ 74%] Linking CXX shared module ../libdispersion.so
[ 74%] Built target dispersion
[ 74%] Building CXX object plugins/Eq/CMakeFiles/eq.dir/EqEffect.cpp.o
[ 74%] Building CXX object plugins/Compressor/CMakeFiles/compressor.dir/moc_Compressor.cpp.o
[ 74%] Building CXX object plugins/DualFilter/CMakeFiles/dualfilter.dir/DualFilterControls.cpp.o
[ 74%] Building CXX object plugins/DynamicsProcessor/CMakeFiles/dynamicsprocessor.dir/DynamicsProcessorControls.cpp.o
[ 74%] Linking CXX shared module ../libcompressor.so
[ 74%] Built target compressor
[ 74%] Building CXX object plugins/Eq/CMakeFiles/eq.dir/EqControls.cpp.o
[ 75%] Building CXX object plugins/Flanger/CMakeFiles/flanger.dir/FlangerEffect.cpp.o
[ 75%] Linking CXX shared module ../libdualfilter.so
[ 75%] Built target dualfilter
[ 75%] Building CXX object plugins/FrequencyShifter/CMakeFiles/frequencyshifter.dir/FrequencyShifterEffect.cpp.o
[ 75%] Linking CXX shared module ../libdynamicsprocessor.so
[ 75%] Built target dynamicsprocessor
[ 75%] Building CXX object plugins/GranularPitchShifter/CMakeFiles/granularpitchshifter.dir/GranularPitchShifterEffect.cpp.o
[ 75%] Linking CXX shared module ../libeq.so
[ 75%] Building CXX object plugins/Flanger/CMakeFiles/flanger.dir/FlangerControls.cpp.o
[ 76%] Built target eq
[ 76%] Building CXX object plugins/HydrogenImport/CMakeFiles/hydrogenimport.dir/HydrogenImport.cpp.o
[ 76%] Building CXX object plugins/FrequencyShifter/CMakeFiles/frequencyshifter.dir/FrequencyShifterControls.cpp.o
[ 76%] Building CXX object plugins/GranularPitchShifter/CMakeFiles/granularpitchshifter.dir/GranularPitchShifterControls.cpp.o
[ 76%] Linking CXX shared module ../libflanger.so
[ 76%] Built target flanger
[ 76%] Linking CXX shared module ../libladspabrowser.so
[ 77%] Built target ladspabrowser
[ 77%] Building CXX object plugins/LadspaEffect/CMakeFiles/ladspaeffect.dir/LadspaEffect.cpp.o
[ 77%] Building CXX object plugins/FrequencyShifter/CMakeFiles/frequencyshifter.dir/moc_FrequencyShifterEffect.cpp.o
[ 77%] Linking CXX shared module ../libhydrogenimport.so
[ 77%] Built target hydrogenimport
[ 77%] Building CXX object plugins/LOMM/CMakeFiles/lomm.dir/LOMM.cpp.o
[ 77%] Linking CXX shared module ../libgranularpitchshifter.so
[ 77%] Built target granularpitchshifter
[ 77%] Building CXX object plugins/Lv2Effect/CMakeFiles/lv2effect.dir/Lv2Effect.cpp.o
[ 77%] Linking CXX shared module ../libfrequencyshifter.so
[ 78%] Built target frequencyshifter
[ 78%] Building CXX object plugins/Lv2Instrument/CMakeFiles/lv2instrument.dir/Lv2Instrument.cpp.o
[ 78%] Building CXX object plugins/LadspaEffect/CMakeFiles/ladspaeffect.dir/LadspaControls.cpp.o
[ 78%] Building CXX object plugins/LOMM/CMakeFiles/lomm.dir/LOMMControls.cpp.o
[ 78%] Building CXX object plugins/Lv2Effect/CMakeFiles/lv2effect.dir/Lv2FxControls.cpp.o
[ 78%] Linking CXX shared module ../liblv2instrument.so
[ 78%] Built target lv2instrument
[ 78%] Building CXX object plugins/Lb302/CMakeFiles/lb302.dir/Lb302.cpp.o
[ 78%] Building CXX object plugins/LadspaEffect/CMakeFiles/ladspaeffect.dir/moc_LadspaEffect.cpp.o
[ 78%] Building CXX object plugins/LOMM/CMakeFiles/lomm.dir/LOMMControlDialog.cpp.o
[ 78%] Building CXX object plugins/Lv2Effect/CMakeFiles/lv2effect.dir/moc_Lv2Effect.cpp.o
[ 78%] Linking CXX shared module ../libladspaeffect.so
[ 78%] Built target ladspaeffect
[ 78%] Building CXX object plugins/MidiImport/CMakeFiles/midiimport.dir/MidiImport.cpp.o
[ 78%] Linking CXX shared module ../liblv2effect.so
[ 78%] Built target lv2effect
[ 78%] Building CXX object plugins/MidiExport/CMakeFiles/midiexport.dir/MidiExport.cpp.o
[ 78%] Building CXX object plugins/Lb302/CMakeFiles/lb302.dir/moc_Lb302.cpp.o
[ 78%] Building CXX object plugins/LOMM/CMakeFiles/lomm.dir/moc_LOMM.cpp.o
[ 78%] Linking CXX shared module ../liblb302.so
[ 78%] Linking CXX shared module ../libmidiimport.so
[ 78%] Built target lb302
[ 78%] Building CXX object plugins/MultitapEcho/CMakeFiles/multitapecho.dir/MultitapEcho.cpp.o
[ 78%] Linking CXX shared module ../liblomm.so
[ 78%] Built target midiimport
[ 78%] Building CXX object plugins/Monstro/CMakeFiles/monstro.dir/Monstro.cpp.o
[ 78%] Built target lomm
[ 78%] Building CXX object plugins/Nes/CMakeFiles/nes.dir/Nes.cpp.o
[ 78%] Linking CXX shared module ../libmidiexport.so
[ 78%] Built target midiexport
[ 78%] Linking CXX shared module ../libneuralamp.so
[ 78%] Built target neuralamp
[ 78%] Building CXX object plugins/OpulenZ/CMakeFiles/opulenz.dir/OpulenZ.cpp.o
[ 78%] Building CXX object plugins/MultitapEcho/CMakeFiles/multitapecho.dir/MultitapEchoControls.cpp.o
[ 79%] Linking CXX shared module ../libnes.so
[ 79%] Built target nes
[ 79%] Building CXX object plugins/Organic/CMakeFiles/organic.dir/Organic.cpp.o
[ 79%] Linking CXX shared module ../libopulenz.so
[ 79%] Linking CXX shared module ../libmonstro.so
[ 79%] Built target opulenz
[ 79%] Built target monstro
[ 79%] Building CXX object plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/Oscilloscope.cpp.o
[ 79%] Building CXX object plugins/FreeBoy/CMakeFiles/freeboy.dir/FreeBoy.cpp.o
[ 79%] Linking CXX shared module ../libmultitapecho.so
[ 79%] Built target multitapecho
[ 79%] Building CXX object plugins/Patman/CMakeFiles/patman.dir/Patman.cpp.o
[ 79%] Linking CXX shared module ../liborganic.so
[ 79%] Building CXX object plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/OscilloscopeControls.cpp.o
[ 79%] Built target organic
[ 79%] Building CXX object plugins/PeakControllerEffect/CMakeFiles/peakcontrollereffect.dir/PeakControllerEffect.cpp.o
[ 79%] Linking CXX shared module ../libfreeboy.so
[ 79%] Built target freeboy
[ 79%] Built target gigplayer_autogen_timestamp_deps
[ 79%] Building CXX object plugins/ReverbSC/CMakeFiles/reverbsc.dir/ReverbSC.cpp.o
[ 79%] Linking CXX shared module ../libpatman.so
[ 80%] Built target patman
[ 80%] Linking CXX shared module ../librnnoisedenoiser.so
[ 80%] Built target rnnoisedenoiser
[ 80%] Built target sf2player_autogen_timestamp_deps
[ 80%] Building CXX object plugins/Sfxr/CMakeFiles/sfxr.dir/Sfxr.cpp.o
[ 80%] Building CXX object plugins/Oscilloscope/CMakeFiles/oscilloscope.dir/OscilloscopeGraph.cpp.o
[ 80%] Building CXX object plugins/PeakControllerEffect/CMakeFiles/peakcontrollereffect.dir/PeakControllerEffectControls.cpp.o
[ 80%] Building CXX object plugins/ReverbSC/CMakeFiles/reverbsc.dir/ReverbSCControls.cpp.o
[ 80%] Linking CXX shared module ../liboscilloscope.so
[ 81%] Built target oscilloscope
[ 81%] Building CXX object plugins/Sid/CMakeFiles/sid.dir/SidInstrument.cpp.o
[ 81%] Linking CXX shared module ../libsfxr.so
[ 81%] Linking CXX shared module ../libpeakcontrollereffect.so
[ 81%] Built target sfxr
[ 81%] Building CXX object plugins/SlewDistortion/CMakeFiles/slewdistortion.dir/SlewDistortion.cpp.o
[ 81%] Built target peakcontrollereffect
[ 81%] Building CXX object plugins/SlicerT/CMakeFiles/slicert.dir/SlicerT.cpp.o
[ 81%] Linking CXX shared module ../libreverbsc.so
[ 82%] Built target reverbsc
[ 82%] Building CXX object plugins/SpectrumAnalyzer/CMakeFiles/analyzer.dir/Analyzer.cpp.o
[ 82%] Building CXX object plugins/Sid/CMakeFiles/sid.dir/moc_SidInstrument.cpp.o
[ 82%] Building CXX object plugins/SlicerT/CMakeFiles/slicert.dir/SlicerTView.cpp.o
[ 82%] Building CXX object plugins/SpectrumAnalyzer/CMakeFiles/analyzer.dir/SaControls.cpp.o
[ 82%] Building CXX object plugins/SlewDistortion/CMakeFiles/slewdistortion.dir/SlewDistortionControls.cpp.o
[ 82%] Linking CXX shared module ../libsid.so
[ 82%] Built target sid
[ 82%] Building CXX object plugins/StereoEnhancer/CMakeFiles/stereoenhancer.dir/StereoEnhancer.cpp.o
[ 82%] Linking CXX shared module ../libanalyzer.so
[ 82%] Linking CXX shared module ../libslicert.so
[ 82%] Building CXX object plugins/SlewDistortion/CMakeFiles/slewdistortion.dir/SlewDistortionControlDialog.cpp.o
[ 83%] Built target analyzer
[ 83%] Building CXX object plugins/StereoMatrix/CMakeFiles/stereomatrix.dir/StereoMatrix.cpp.o
[ 83%] Built target slicert
[ 83%] Building CXX object plugins/TapTempo/CMakeFiles/taptempo.dir/TapTempo.cpp.o
[ 83%] Building CXX object plugins/StereoEnhancer/CMakeFiles/stereoenhancer.dir/StereoEnhancerControls.cpp.o
[ 83%] Linking CXX shared module ../libtaptempo.so
[ 83%] Building CXX object plugins/StereoMatrix/CMakeFiles/stereomatrix.dir/StereoMatrixControls.cpp.o
[ 83%] Built target taptempo
[ 84%] Building CXX object plugins/VstBase/vstbase/CMakeFiles/vstbase.dir/__/VstPlugin.cpp.o
[ 84%] Building CXX object plugins/SlewDistortion/CMakeFiles/slewdistortion.dir/moc_SlewDistortion.cpp.o
[ 84%] Linking CXX shared module ../libstereoenhancer.so
[ 85%] Built target stereoenhancer
[ 85%] Linking CXX shared module ../libwasm_effect.so
[ 85%] Built target wasm_effect
[ 85%] Building CXX object plugins/Watsyn/CMakeFiles/watsyn.dir/Watsyn.cpp.o
[ 85%] Linking CXX shared module ../libstereomatrix.so
[ 85%] Built target stereomatrix
[ 85%] Building CXX object plugins/WaveShaper/CMakeFiles/waveshaper.dir/WaveShaper.cpp.o
[ 85%] Linking CXX shared module ../libslewdistortion.so
[ 86%] Built target slewdistortion
[ 86%] Building CXX object plugins/Vectorscope/CMakeFiles/vectorscope.dir/Vectorscope.cpp.o
[ 86%] Linking CXX shared library ../../libvstbase.so
[ 86%] Built target vstbase
[ 86%] Building CXX object plugins/Vibed/CMakeFiles/vibedstrings.dir/Vibed.cpp.o
[ 86%] Building CXX object plugins/WaveShaper/CMakeFiles/waveshaper.dir/WaveShaperControls.cpp.o
[ 86%] Linking CXX shared module ../libwatsyn.so
[ 86%] Built target watsyn
[ 87%] Building CXX object plugins/Vectorscope/CMakeFiles/vectorscope.dir/VecControls.cpp.o
[ 87%] Building CXX object plugins/Xpressive/CMakeFiles/xpressive.dir/Xpressive.cpp.o
[ 87%] Linking CXX shared module ../libwaveshaper.so
[ 87%] Linking CXX shared module ../libvibedstrings.so
[ 88%] Built target waveshaper
[ 88%] Building CXX object plugins/Vectorscope/CMakeFiles/vectorscope.dir/VecControlsDialog.cpp.o
[ 88%] Building CXX object plugins/ZynAddSubFx/CMakeFiles/zynaddsubfx.dir/ZynAddSubFx.cpp.o
[ 88%] Built target vibedstrings
[ 88%] Built target SafeStartTest_autogen_timestamp_deps
[ 88%] Built target SafeStartLoadPathTest_autogen_timestamp_deps
[ 88%] Building CXX object plugins/Xpressive/CMakeFiles/xpressive.dir/ExprSynth.cpp.o
[ 88%] Building CXX object plugins/ZynAddSubFx/CMakeFiles/zynaddsubfx.dir/moc_ZynAddSubFx.cpp.o
[ 88%] Linking CXX shared module ../libvectorscope.so
[ 88%] Built target vectorscope
[ 88%] Automatic MOC for target mpe_test_consumer
[ 88%] Built target mpe_test_consumer_autogen
[ 88%] Built target PhaseDSidechainTest_autogen_timestamp_deps
[ 88%] Built target PluginScanCacheTest_autogen_timestamp_deps
[ 88%] Automatic MOC for target partc_ref_amplifier
[ 88%] Built target partc_ref_amplifier_autogen
[ 88%] Automatic MOC for target partc_ref_bassbooster
[ 88%] Built target partc_ref_bassbooster_autogen
[ 88%] Automatic MOC for target partc_ref_bitcrush
[ 88%] Built target partc_ref_bitcrush_autogen
[ 88%] Automatic MOC for target partc_ref_dualfilter
[ 88%] Built target partc_ref_dualfilter_autogen
[ 88%] Automatic MOC for target partc_ref_waveshaper
[ 88%] Built target partc_ref_waveshaper_autogen
[ 88%] Automatic MOC for target partc_ref_flanger
[ 88%] Built target partc_ref_flanger_autogen
[ 88%] Automatic MOC for target partc_ref_delay
[ 88%] Built target partc_ref_delay_autogen
[ 88%] Automatic MOC for target partc_ref_compressor
[ 88%] Built target partc_ref_compressor_autogen
[ 88%] Automatic MOC for target partc_ref_crossovereq
[ 88%] Built target partc_ref_crossovereq_autogen
[ 88%] Automatic MOC for target partc_ref_dynamicsprocessor
[ 88%] Built target partc_ref_dynamicsprocessor_autogen
[ 88%] Automatic MOC for target partc_ref_lomm
[ 88%] Built target partc_ref_lomm_autogen
[ 88%] Automatic MOC for target partc_ref_multitapecho
[ 88%] Built target partc_ref_multitapecho_autogen
[ 88%] Automatic MOC for target partc_ref_reverbsc
[ 88%] Built target partc_ref_reverbsc_autogen
[ 88%] Automatic MOC for target partc_ref_stereoenhancer
[ 88%] Built target partc_ref_stereoenhancer_autogen
[ 88%] Automatic MOC for target partc_ref_stereomatrix
[ 88%] Built target partc_ref_stereomatrix_autogen
[ 88%] Automatic MOC for target partc_ref_dispersion
[ 88%] Built target partc_ref_dispersion_autogen
[ 88%] Automatic MOC for target partc_ref_vectorscope
[ 88%] Built target partc_ref_vectorscope_autogen
[ 88%] Automatic MOC for target partc_ref_analyzer
[ 88%] Built target partc_ref_analyzer_autogen
[ 88%] Automatic MOC for target partc_ref_granularpitchshifter
[ 88%] Built target partc_ref_granularpitchshifter_autogen
[ 88%] Automatic MOC for target partc_ref_eq
[ 89%] Automatic MOC for target partc_ref_freeboy
[ 89%] Built target partc_ref_eq_autogen
[ 89%] Built target partc_ref_freeboy_autogen
[ 89%] Automatic MOC for target partc_ref_nes
[ 90%] Automatic MOC for target partc_ref_sid
[ 90%] Built target partc_ref_nes_autogen
[ 90%] Built target partc_ref_sid_autogen
[ 90%] Automatic MOC for target partc_ref_opulenz
[ 90%] Automatic MOC for target partc_ref_sfxr
[ 90%] Built target partc_ref_opulenz_autogen
[ 90%] Built target partc_ref_sfxr_autogen
[ 90%] Automatic MOC for target partc_ref_bitinvader
[ 90%] Automatic MOC for target partc_ref_watsyn
[ 90%] Built target partc_ref_bitinvader_autogen
[ 90%] Built target partc_ref_watsyn_autogen
[ 90%] Automatic MOC for target partc_ref_xpressive
[ 90%] Automatic MOC for target partc_ref_vibedstrings
[ 90%] Built target partc_ref_xpressive_autogen
[ 90%] Built target partc_ref_vibedstrings_autogen
[ 90%] Automatic MOC for target partc_ref_kicker
[ 90%] Automatic MOC for target partc_ref_tripleoscillator
[ 90%] Built target partc_ref_kicker_autogen
[ 90%] Built target partc_ref_tripleoscillator_autogen
[ 90%] Automatic MOC for target partc_ref_monstro
[ 90%] Automatic MOC for target partc_ref_organic
[ 90%] Built target partc_ref_monstro_autogen
[ 90%] Built target partc_ref_organic_autogen
[ 90%] Automatic MOC for target partc_ref_audiofileprocessor
[ 90%] Automatic MOC for target partc_ref_lb302
[ 90%] Built target partc_ref_audiofileprocessor_autogen
[ 90%] Automatic MOC for target partc_ref_ladspaeffect
[ 90%] Built target partc_ref_ladspaeffect_autogen
[ 90%] Automatic MOC for target partc_ref_frequencyshifter
[ 90%] Built target partc_ref_frequencyshifter_autogen
[ 90%] Automatic MOC for target partc_ref_oscilloscope
[ 90%] Built target partc_ref_oscilloscope_autogen
[ 90%] Linking CXX shared module ../libzynaddsubfx.so
[ 90%] Automatic MOC for target partc_ref_slewdistortion
[ 90%] Built target partc_ref_slewdistortion_autogen
[ 90%] Built target partc_ref_lb302_autogen
[ 90%] Automatic MOC for target partc_ref_lv2effect
[ 90%] Automatic MOC for target partc_ref_lv2instrument
[ 90%] Built target partc_ref_lv2effect_autogen
[ 90%] Built target partc_ref_lv2instrument_autogen
[ 90%] Automatic MOC and UIC for target partc_ref_sf2player
[ 90%] Automatic MOC and UIC for target partc_ref_gigplayer
[ 90%] Built target partc_ref_sf2player_autogen
[ 90%] Automatic MOC for target synthetic_audio_plugin
[ 90%] Built target partc_ref_gigplayer_autogen
[ 90%] Building CXX object plugins/CarlaPatchbay/CMakeFiles/carlapatchbay.dir/CarlaPatchbay.cpp.o
[ 90%] Built target synthetic_audio_plugin_autogen
[ 90%] Building CXX object plugins/CarlaRack/CMakeFiles/carlarack.dir/CarlaRack.cpp.o
[ 90%] Built target zynaddsubfx
[ 90%] Built target gigplayer_autogen
[ 90%] Built target sf2player_autogen
[ 90%] Building CXX object plugins/Vestige/CMakeFiles/vestige.dir/Vestige.cpp.o
[ 90%] Linking CXX shared module ../libcarlapatchbay.so
[ 90%] Linking CXX shared module ../libcarlarack.so
[ 90%] Built target carlapatchbay
[ 90%] Built target carlarack
[ 90%] Building CXX object plugins/VstEffect/CMakeFiles/vsteffect.dir/VstEffect.cpp.o
[ 90%] Built target OutOfProcessHostTest_autogen_timestamp_deps
[ 90%] Built target OutOfProcessHostClientLoopTest_autogen_timestamp_deps
[ 90%] Automatic MOC for target SafeStartTest
[ 90%] Built target SafeStartTest_autogen
[ 90%] Automatic MOC for target SafeStartLoadPathTest
[ 90%] Built target SafeStartLoadPathTest_autogen
[ 90%] Building CXX object tests/CMakeFiles/mpe_test_consumer.dir/src/plugins/MpeTestConsumer.cpp.o
[ 90%] Building CXX object plugins/Vestige/CMakeFiles/vestige.dir/moc_Vestige.cpp.o
[ 91%] Linking CXX shared module mpe-test-consumer/libmpe_test_consumer.so
[ 91%] Built target mpe_test_consumer
[ 91%] Automatic MOC for target PhaseDSidechainTest
[ 91%] Built target PhaseDSidechainTest_autogen
[ 91%] Automatic MOC for target PluginScanCacheTest
[ 91%] Built target PluginScanCacheTest_autogen
[ 91%] Linking CXX shared module partc_ref_amplifier.so
[ 91%] Built target partc_ref_amplifier
[ 91%] Linking CXX shared module partc_ref_bassbooster.so
[ 91%] Built target partc_ref_bassbooster
[ 91%] Building CXX object plugins/VstEffect/CMakeFiles/vsteffect.dir/VstEffectControls.cpp.o
[ 91%] Linking CXX shared module partc_ref_bitcrush.so
[ 91%] Built target partc_ref_bitcrush
[ 91%] Linking CXX shared module partc_ref_dualfilter.so
[ 91%] Built target partc_ref_dualfilter
[ 91%] Building CXX object tests/CMakeFiles/partc_ref_waveshaper.dir/reference/WaveShaper/WaveShaperControls.cpp.o
[ 91%] Linking CXX shared module ../libvestige.so
[ 91%] Built target vestige
[ 91%] Building CXX object tests/CMakeFiles/partc_ref_flanger.dir/reference/Flanger/FlangerControls.cpp.o
[ 91%] Linking CXX shared module partc_ref_waveshaper.so
[ 91%] Building CXX object plugins/VstEffect/CMakeFiles/vsteffect.dir/VstEffectControlDialog.cpp.o
[ 91%] Built target partc_ref_waveshaper
[ 91%] Linking CXX shared module partc_ref_delay.so
[ 91%] Built target partc_ref_delay
[ 91%] Linking CXX shared module partc_ref_compressor.so
[ 91%] Linking CXX shared module partc_ref_flanger.so
[ 91%] Built target partc_ref_compressor
[ 91%] Linking CXX shared module partc_ref_crossovereq.so
[ 91%] Built target partc_ref_flanger
[ 91%] Building CXX object tests/CMakeFiles/partc_ref_dynamicsprocessor.dir/reference/DynamicsProcessor/DynamicsProcessorControls.cpp.o
[ 92%] Built target partc_ref_crossovereq
[ 92%] Linking CXX shared module partc_ref_lomm.so
[ 92%] Built target partc_ref_lomm
[ 92%] Linking CXX shared module partc_ref_multitapecho.so
[ 92%] Built target partc_ref_multitapecho
[ 92%] Linking CXX shared module partc_ref_reverbsc.so
[ 92%] Built target partc_ref_reverbsc
[ 92%] Linking CXX shared module partc_ref_stereoenhancer.so
[ 92%] Built target partc_ref_stereoenhancer
[ 92%] Linking CXX shared module partc_ref_stereomatrix.so
[ 92%] Linking CXX shared module ../libvsteffect.so
[ 92%] Built target partc_ref_stereomatrix
[ 92%] Built target vsteffect
[ 92%] Linking CXX shared module partc_ref_dispersion.so
[ 92%] Linking CXX shared module partc_ref_vectorscope.so
[ 92%] Built target partc_ref_dispersion
[ 92%] Linking CXX shared module partc_ref_analyzer.so
[ 93%] Built target partc_ref_vectorscope
[ 93%] Linking CXX shared module partc_ref_granularpitchshifter.so
[ 93%] Linking CXX shared module partc_ref_dynamicsprocessor.so
[ 93%] Built target partc_ref_analyzer
[ 93%] Linking CXX shared module partc_ref_eq.so
[ 93%] Built target partc_ref_granularpitchshifter
[ 93%] Building CXX object tests/CMakeFiles/partc_ref_freeboy.dir/reference/FreeBoy/FreeBoy.cpp.o
[ 94%] Built target partc_ref_dynamicsprocessor
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_nes.dir/reference/Nes/Nes.cpp.o
[ 94%] Built target partc_ref_eq
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_sid.dir/reference/Sid/SidInstrument.cpp.o
[ 94%] Linking CXX shared module partc_ref_freeboy.so
[ 94%] Linking CXX shared module partc_ref_nes.so
[ 94%] Built target partc_ref_freeboy
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_opulenz.dir/reference/OpulenZ/OpulenZ.cpp.o
[ 94%] Linking CXX shared module partc_ref_sid.so
[ 94%] Built target partc_ref_nes
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_sfxr.dir/reference/Sfxr/Sfxr.cpp.o
[ 94%] Built target partc_ref_sid
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_bitinvader.dir/reference/BitInvader/BitInvader.cpp.o
[ 94%] Linking CXX shared module partc_ref_bitinvader.so
[ 94%] Linking CXX shared module partc_ref_opulenz.so
[ 94%] Built target partc_ref_bitinvader
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_watsyn.dir/reference/Watsyn/Watsyn.cpp.o
[ 94%] Built target partc_ref_opulenz
[ 94%] Linking CXX shared module partc_ref_sfxr.so
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_xpressive.dir/reference/Xpressive/Xpressive.cpp.o
[ 94%] Built target partc_ref_sfxr
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_vibedstrings.dir/reference/Vibed/Vibed.cpp.o
[ 94%] Linking CXX shared module partc_ref_watsyn.so
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_xpressive.dir/reference/Xpressive/ExprSynth.cpp.o
[ 94%] Built target partc_ref_watsyn
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_kicker.dir/reference/Kicker/Kicker.cpp.o
[ 94%] Linking CXX shared module partc_ref_vibedstrings.so
[ 94%] Built target partc_ref_vibedstrings
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_tripleoscillator.dir/reference/TripleOscillator/TripleOscillator.cpp.o
[ 94%] Linking CXX shared module partc_ref_kicker.so
[ 94%] Built target partc_ref_kicker
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_monstro.dir/reference/Monstro/Monstro.cpp.o
[ 94%] Linking CXX shared module partc_ref_tripleoscillator.so
[ 94%] Built target partc_ref_tripleoscillator
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_organic.dir/reference/Organic/Organic.cpp.o
[ 94%] Linking CXX shared module partc_ref_organic.so
[ 94%] Built target partc_ref_organic
[ 94%] Building CXX object tests/CMakeFiles/partc_ref_audiofileprocessor.dir/reference/AudioFileProcessor/AudioFileProcessor.cpp.o
[ 94%] Linking CXX shared module partc_ref_monstro.so
[ 95%] Built target partc_ref_monstro
[ 95%] Building CXX object tests/CMakeFiles/partc_ref_lb302.dir/partc_ref_lb302_autogen/mocs_compilation.cpp.o
[ 95%] Building CXX object tests/CMakeFiles/partc_ref_lb302.dir/reference/Lb302/Lb302.cpp.o
[ 96%] Building CXX object tests/CMakeFiles/partc_ref_audiofileprocessor.dir/reference/AudioFileProcessor/AudioFileProcessorView.cpp.o
[ 96%] Linking CXX shared module partc_ref_audiofileprocessor.so
[ 96%] Built target partc_ref_audiofileprocessor
[ 96%] Linking CXX shared module partc_ref_lb302.so
[ 96%] Building CXX object tests/CMakeFiles/partc_ref_ladspaeffect.dir/reference/LadspaEffect/LadspaEffect.cpp.o
[ 96%] Built target partc_ref_lb302
[ 96%] Linking CXX shared module partc_ref_frequencyshifter.so
[ 96%] Built target partc_ref_frequencyshifter
[ 96%] Linking CXX shared module partc_ref_oscilloscope.so
[ 97%] Built target partc_ref_oscilloscope
[ 97%] Linking CXX shared module partc_ref_slewdistortion.so
[ 97%] Built target partc_ref_slewdistortion
[ 97%] Linking CXX shared module partc_ref_lv2effect.so
[ 97%] Built target partc_ref_lv2effect
[ 97%] Building CXX object tests/CMakeFiles/partc_ref_lv2instrument.dir/reference/Lv2Instrument/Lv2Instrument.cpp.o
[ 97%] Linking CXX shared module partc_ref_ladspaeffect.so
[ 98%] Built target partc_ref_ladspaeffect
[ 98%] Building CXX object tests/CMakeFiles/partc_ref_sf2player.dir/reference/Sf2Player/Sf2Player.cpp.o
[ 98%] Linking CXX shared module partc_ref_lv2instrument.so
[ 98%] Built target partc_ref_lv2instrument
[ 98%] Building CXX object tests/CMakeFiles/partc_ref_gigplayer.dir/reference/GigPlayer/GigPlayer.cpp.o
[ 98%] Linking CXX shared module partc_ref_sf2player.so
[ 98%] Built target partc_ref_sf2player
[ 98%] Built target ZynSeparateProcessTest_autogen_timestamp_deps
[ 98%] Building CXX object tests/CMakeFiles/synthetic_audio_plugin.dir/src/plugins/SyntheticAudioPlugin.cpp.o
[ 98%] Linking CXX shared module partc_ref_gigplayer.so
[ 98%] Built target partc_ref_gigplayer
[ 98%] Building CXX object plugins/GigPlayer/CMakeFiles/gigplayer.dir/GigPlayer.cpp.o
[ 98%] Linking CXX shared module synthetic_audio_plugin.so
[ 98%] Built target synthetic_audio_plugin
[ 98%] Building CXX object plugins/Sf2Player/CMakeFiles/sf2player.dir/Sf2Player.cpp.o
[ 98%] Linking CXX shared module ../libgigplayer.so
[ 99%] Built target gigplayer
[ 99%] Automatic MOC for target OutOfProcessHostTest
[ 99%] Linking CXX shared module ../libxpressive.so
[ 99%] Built target OutOfProcessHostTest_autogen
[ 99%] Automatic MOC for target OutOfProcessHostClientLoopTest
[ 99%] Built target OutOfProcessHostClientLoopTest_autogen
[ 99%] Linking CXX executable SafeStartTest
[ 99%] Linking CXX shared module ../libsf2player.so
[ 99%] Built target xpressive
[ 99%] Linking CXX executable SafeStartLoadPathTest
[ 99%] Built target sf2player
[ 99%] Built target MpePlaybackTest_autogen_timestamp_deps
[ 99%] Linking CXX executable PhaseDSidechainTest
[ 99%] Built target SafeStartTest
[ 99%] Linking CXX executable PluginScanCacheTest
[ 99%] Built target SafeStartLoadPathTest
[ 99%] Automatic MOC for target ZynSeparateProcessTest
[ 99%] Built target PhaseDSidechainTest
[ 99%] Built target AudioPluginTest_autogen_timestamp_deps
[ 99%] Building CXX object tests/CMakeFiles/OutOfProcessHostTest.dir/src/core/OutOfProcessHostTest.cpp.o
[ 99%] Built target ZynSeparateProcessTest_autogen
[ 99%] Building CXX object tests/CMakeFiles/OutOfProcessHostClientLoopTest.dir/src/core/OutOfProcessHostClientLoopTest.cpp.o
[ 99%] Built target PluginScanCacheTest
[ 99%] Automatic MOC for target MpePlaybackTest
[ 99%] Built target MpePlaybackTest_autogen
[ 99%] Building CXX object tests/CMakeFiles/ZynSeparateProcessTest.dir/src/plugins/ZynSeparateProcessTest.cpp.o
[100%] Linking CXX executable OutOfProcessHostClientLoopTest
[100%] Linking CXX executable OutOfProcessHostTest
[100%] Linking CXX executable ZynSeparateProcessTest
[100%] Built target OutOfProcessHostClientLoopTest
[100%] Automatic MOC for target AudioPluginTest
[100%] Built target OutOfProcessHostTest
[100%] Built target AudioPluginTest_autogen
[100%] Building CXX object tests/CMakeFiles/MpePlaybackTest.dir/src/core/MpePlaybackTest.cpp.o
[100%] Linking CXX executable AudioPluginTest
[100%] Built target ZynSeparateProcessTest
[100%] Built target AudioPluginTest
[100%] Linking CXX executable MpePlaybackTest
[100%] Built target MpePlaybackTest
[100%] Linking CXX shared module partc_ref_xpressive.so
[100%] Built target partc_ref_xpressive
[100%] Built target PluginPortsMigrationReference_autogen_timestamp_deps
[100%] Automatic MOC for target PluginPortsMigrationReference
[100%] Built target PluginPortsMigrationReference_autogen
[100%] Building CXX object tests/CMakeFiles/PluginPortsMigrationReference.dir/src/plugins/PluginPortsMigrationReference.cpp.o
[100%] Linking CXX executable PluginPortsMigrationReference
[100%] Built target PluginPortsMigrationReference
[100%] Built target PluginPortsMigrationTest_autogen_timestamp_deps
[100%] Automatic MOC for target PluginPortsMigrationTest
[100%] Built target PluginPortsMigrationTest_autogen
[100%] Building CXX object tests/CMakeFiles/PluginPortsMigrationTest.dir/src/plugins/PluginPortsMigrationTest.cpp.o
[100%] Linking CXX executable PluginPortsMigrationTest
[100%] Built target PluginPortsMigrationTest
