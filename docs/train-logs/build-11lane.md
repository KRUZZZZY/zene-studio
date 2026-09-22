
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
* Project version             : 0.3.0-alpha.63+9d71f47
*   Major version             : 0
*   Minor version             : 3
*   Release version           : 0
*   Stage version             : alpha
*   Build version             : 63+9d71f47
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



-- Configuring done (1.4s)
-- Generating done (2.8s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-040/build
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target weakjack
[  0%] Built target ringbuffer
[  1%] Built target lua
[  1%] Built target wasm-wat2wasm_autogen_timestamp_deps
[  1%] Built target carla_native-plugin
[  1%] Built target tap_autopan
[  2%] Built target caps
[  3%] Built target tap_chorusflanger
[  3%] Built target tap_deesser
[  3%] Built target tap_doubler
[  3%] Built target tap_dynamics_m
[  3%] Built target tap_dynamics_st
[  3%] Built target tap_echo
[  3%] Built target tap_eqbw
[  3%] Built target tap_eq
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
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/amp_1181.dir/ladspa/amp_1181.c.o
[  4%] Building C object plugins/LadspaEffect/swh/CMakeFiles/am_pitchshift_1433.dir/ladspa/am_pitchshift_1433.c.o
[  5%] Linking C shared module ../../ladspa/alias_1407.so
[  5%] Linking C shared module ../../ladspa/allpass_1895.so
[  5%] Linking C shared module ../../ladspa/am_pitchshift_1433.so
[  5%] Linking C shared module ../../ladspa/amp_1181.so
[  5%] Built target alias_1407
[  5%] Built target allpass_1895
[  5%] Built target am_pitchshift_1433
[  5%] Built target amp_1181
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/blo.dir/ladspa/util/blo.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/iir.dir/ladspa/util/iir.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bode_shifter_cv_1432.dir/ladspa/bode_shifter_cv_1432.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bode_shifter_1431.dir/ladspa/bode_shifter_1431.c.o
[  5%] Linking C static library libblo.a
[  5%] Linking C static library libiir.a
[  5%] Linking C shared module ../../ladspa/bode_shifter_cv_1432.so
[  5%] Linking C shared module ../../ladspa/bode_shifter_1431.so
[  5%] Built target iir
[  5%] Built target blo
[  5%] Built target bode_shifter_cv_1432
[  5%] Built target bode_shifter_1431
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_1190.dir/ladspa/comb_1190.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/chebstortion_1430.dir/ladspa/chebstortion_1430.c.o
[  5%] Linking C shared module ../../ladspa/chebstortion_1430.so
[  5%] Linking C shared module ../../ladspa/comb_1190.so
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_1887.dir/ladspa/comb_1887.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/comb_splitter_1411.dir/ladspa/comb_splitter_1411.c.o
[  5%] Linking C shared module ../../ladspa/comb_splitter_1411.so
[  5%] Linking C shared module ../../ladspa/comb_1887.so
[  5%] Built target comb_1190
[  5%] Built target chebstortion_1430
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/crossover_dist_1404.dir/ladspa/crossover_dist_1404.c.o
[  5%] Built target comb_splitter_1411
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/const_1909.dir/ladspa/const_1909.c.o
[  5%] Built target comb_1887
[  5%] Linking C shared module ../../ladspa/crossover_dist_1404.so
[  5%] Linking C shared module ../../ladspa/const_1909.so
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dc_remove_1207.dir/ladspa/dc_remove_1207.c.o
[  5%] Building C object plugins/LadspaEffect/swh/CMakeFiles/debug_1184.dir/ladspa/debug_1184.c.o
[  5%] Linking C shared module ../../ladspa/dc_remove_1207.so
[  5%] Linking C shared module ../../ladspa/debug_1184.so
[  5%] Built target const_1909
[  6%] Built target crossover_dist_1404
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/decimator_1202.dir/ladspa/decimator_1202.c.o
[  6%] Built target dc_remove_1207
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/decay_1886.dir/ladspa/decay_1886.c.o
[  6%] Built target debug_1184
[  6%] Linking C shared module ../../ladspa/decay_1886.so
[  6%] Linking C shared module ../../ladspa/decimator_1202.so
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/declip_1195.dir/ladspa/declip_1195.c.o
[  6%] Building C object plugins/LadspaEffect/swh/CMakeFiles/delay_1898.dir/ladspa/delay_1898.c.o
[  7%] Linking C shared module ../../ladspa/declip_1195.so
[  7%] Linking C shared module ../../ladspa/delay_1898.so
[  7%] Built target decay_1886
[  7%] Built target decimator_1202
[  7%] Built target declip_1195
[  7%] Built target delay_1898
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/delayorama_1402.dir/ladspa/delayorama_1402.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/diode_1185.dir/ladspa/diode_1185.c.o
[  7%] Linking C shared module ../../ladspa/diode_1185.so
[  7%] Linking C shared module ../../ladspa/delayorama_1402.so
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/divider_1186.dir/ladspa/divider_1186.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dj_eq_1901.dir/ladspa/dj_eq_1901.c.o
[  7%] Linking C shared module ../../ladspa/divider_1186.so
[  7%] Linking C shared module ../../ladspa/dj_eq_1901.so
[  7%] Built target diode_1185
[  7%] Built target delayorama_1402
[  7%] Built target divider_1186
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dj_flanger_1438.dir/ladspa/dj_flanger_1438.c.o
[  7%] Built target dj_eq_1901
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/dyson_compress_1403.dir/ladspa/dyson_compress_1403.c.o
[  7%] Linking C shared module ../../ladspa/dj_flanger_1438.so
[  7%] Linking C shared module ../../ladspa/dyson_compress_1403.so
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fad_delay_1192.dir/ladspa/fad_delay_1192.c.o
[  7%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fast_lookahead_limiter_1913.dir/ladspa/fast_lookahead_limiter_1913.c.o
[  8%] Linking C shared module ../../ladspa/fad_delay_1192.so
[  8%] Linking C shared module ../../ladspa/fast_lookahead_limiter_1913.so
[  8%] Built target dj_flanger_1438
[  8%] Built target dyson_compress_1403
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/flanger_1191.dir/ladspa/flanger_1191.c.o
[  8%] Built target fad_delay_1192
[  8%] Built target fast_lookahead_limiter_1913
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/fm_osc_1415.dir/ladspa/fm_osc_1415.c.o
[  8%] Linking C shared module ../../ladspa/flanger_1191.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/foldover_1213.dir/ladspa/foldover_1213.c.o
[  8%] Linking C shared module ../../ladspa/fm_osc_1415.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/foverdrive_1196.dir/ladspa/foverdrive_1196.c.o
[  8%] Linking C shared module ../../ladspa/foldover_1213.so
[  8%] Linking C shared module ../../ladspa/foverdrive_1196.so
[  8%] Built target flanger_1191
[  8%] Built target fm_osc_1415
[  8%] Built target foldover_1213
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/freq_tracker_1418.dir/ladspa/freq_tracker_1418.c.o
[  8%] Built target foverdrive_1196
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gate_1410.dir/ladspa/gate_1410.c.o
[  8%] Linking C shared module ../../ladspa/freq_tracker_1418.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gate_1921.dir/ladspa/gate_1921.c.o
[  8%] Linking C shared module ../../ladspa/gate_1410.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/giant_flange_1437.dir/ladspa/giant_flange_1437.c.o
[  8%] Linking C shared module ../../ladspa/gate_1921.so
[  8%] Linking C shared module ../../ladspa/giant_flange_1437.so
[  8%] Built target freq_tracker_1418
[  8%] Built target gate_1410
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gong_1424.dir/ladspa/gong_1424.c.o
[  8%] Built target giant_flange_1437
[  8%] Built target gate_1921
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gong_beater_1439.dir/ladspa/gong_beater_1439.c.o
[  8%] Linking C shared module ../../ladspa/gong_1424.so
[  8%] Linking C shared module ../../ladspa/gong_beater_1439.so
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb.dir/ladspa/gverb/gverb.c.o
[  8%] Built target gsm
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb.dir/ladspa/gverb/gverbdsp.c.o
[  8%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hard_limiter_1413.dir/ladspa/hard_limiter_1413.c.o
[  8%] Linking C static library libgverb.a
[  8%] Built target gong_1424
[  9%] Linking C shared module ../../ladspa/hard_limiter_1413.so
[  9%] Built target gong_beater_1439
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/harmonic_gen_1220.dir/ladspa/harmonic_gen_1220.c.o
[  9%] Built target gverb
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hermes_filter_1200.dir/ladspa/hermes_filter_1200.c.o
[  9%] Linking C shared module ../../ladspa/harmonic_gen_1220.so
[  9%] Linking C shared module ../../ladspa/hermes_filter_1200.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/highpass_iir_1890.dir/ladspa/highpass_iir_1890.c.o
[  9%] Built target hard_limiter_1413
[  9%] Linking C shared module ../../ladspa/highpass_iir_1890.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/hilbert_1440.dir/ladspa/hilbert_1440.c.o
[  9%] Built target harmonic_gen_1220
[  9%] Linking C shared module ../../ladspa/hilbert_1440.so
[  9%] Built target hermes_filter_1200
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/imp_1199.dir/ladspa/imp_1199.c.o
[  9%] Built target highpass_iir_1890
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/impulse_1885.dir/ladspa/impulse_1885.c.o
[  9%] Linking C shared module ../../ladspa/imp_1199.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/inv_1429.dir/ladspa/inv_1429.c.o
[  9%] Built target hilbert_1440
[  9%] Linking C shared module ../../ladspa/impulse_1885.so
[  9%] Linking C shared module ../../ladspa/inv_1429.so
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/karaoke_1409.dir/ladspa/karaoke_1409.c.o
[  9%] Linking C shared module ../../ladspa/karaoke_1409.so
[  9%] Built target imp_1199
[  9%] Built target impulse_1885
[  9%] Built target inv_1429
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/latency_1914.dir/ladspa/latency_1914.c.o
[  9%] Building C object plugins/LadspaEffect/swh/CMakeFiles/lcr_delay_1436.dir/ladspa/lcr_delay_1436.c.o
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/lowpass_iir_1891.dir/ladspa/lowpass_iir_1891.c.o
[ 10%] Linking C shared module ../../ladspa/latency_1914.so
[ 10%] Linking C shared module ../../ladspa/lcr_delay_1436.so
[ 10%] Built target karaoke_1409
[ 10%] Linking C shared module ../../ladspa/lowpass_iir_1891.so
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/ls_filter_1908.dir/ladspa/ls_filter_1908.c.o
[ 10%] Linking C shared module ../../ladspa/ls_filter_1908.so
[ 10%] Built target latency_1914
[ 10%] Built target lcr_delay_1436
[ 10%] Built target lowpass_iir_1891
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_ms_st_1421.dir/ladspa/matrix_ms_st_1421.c.o
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_spatialiser_1422.dir/ladspa/matrix_spatialiser_1422.c.o
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/matrix_st_ms_1420.dir/ladspa/matrix_st_ms_1420.c.o
[ 10%] Linking C shared module ../../ladspa/matrix_ms_st_1421.so
[ 10%] Linking C shared module ../../ladspa/matrix_spatialiser_1422.so
[ 10%] Built target ls_filter_1908
[ 10%] Linking C shared module ../../ladspa/matrix_st_ms_1420.so
[ 10%] Building C object plugins/LadspaEffect/swh/CMakeFiles/mbeq_1197.dir/ladspa/mbeq_1197.c.o
[ 10%] Built target matrix_ms_st_1421
[ 10%] Linking C shared module ../../ladspa/mbeq_1197.so
[ 11%] Built target matrix_spatialiser_1422
[ 11%] Built target matrix_st_ms_1420
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/mod_delay_1419.dir/ladspa/mod_delay_1419.c.o
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/multivoice_chorus_1201.dir/ladspa/multivoice_chorus_1201.c.o
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/notch_iir_1894.dir/ladspa/notch_iir_1894.c.o
[ 11%] Linking C shared module ../../ladspa/mod_delay_1419.so
[ 11%] Linking C shared module ../../ladspa/multivoice_chorus_1201.so
[ 11%] Linking C shared module ../../ladspa/notch_iir_1894.so
[ 11%] Built target mbeq_1197
[ 11%] Building C object plugins/LadspaEffect/swh/CMakeFiles/phasers_1217.dir/ladspa/phasers_1217.c.o
[ 11%] Built target mod_delay_1419
[ 11%] Linking C shared module ../../ladspa/phasers_1217.so
[ 11%] Built target multivoice_chorus_1201
[ 11%] Built target notch_iir_1894
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitchscale.dir/ladspa/util/pitchscale.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/plate_1423.dir/ladspa/plate_1423.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pointer_cast_1910.dir/ladspa/pointer_cast_1910.c.o
[ 12%] Linking C static library libpitchscale.a
[ 12%] Linking C shared module ../../ladspa/plate_1423.so
[ 12%] Linking C shared module ../../ladspa/pointer_cast_1910.so
[ 12%] Built target phasers_1217
[ 12%] Built target pitchscale
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/rate_shifter_1417.dir/ladspa/rate_shifter_1417.c.o
[ 12%] Linking C shared module ../../ladspa/rate_shifter_1417.so
[ 12%] Built target pointer_cast_1910
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/retro_flange_1208.dir/ladspa/retro_flange_1208.c.o
[ 12%] Built target plate_1423
[ 12%] Linking C shared module ../../ladspa/retro_flange_1208.so
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/revdelay_1605.dir/ladspa/revdelay_1605.c.o
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/ringmod_1188.dir/ladspa/ringmod_1188.c.o
[ 12%] Linking C shared module ../../ladspa/revdelay_1605.so
[ 12%] Linking C shared module ../../ladspa/ringmod_1188.so
[ 12%] Built target rate_shifter_1417
[ 12%] Built target retro_flange_1208
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/satan_maximiser_1408.dir/ladspa/satan_maximiser_1408.c.o
[ 12%] Linking C shared module ../../ladspa/satan_maximiser_1408.so
[ 12%] Building C object plugins/LadspaEffect/swh/CMakeFiles/db.dir/ladspa/util/db.c.o
[ 13%] Built target revdelay_1605
[ 13%] Built target ringmod_1188
[ 13%] Linking C static library libdb.a
[ 13%] Built target rms
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/shaper_1187.dir/ladspa/shaper_1187.c.o
[ 13%] Linking C shared module ../../ladspa/shaper_1187.so
[ 13%] Built target satan_maximiser_1408
[ 13%] Built target db
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sifter_1210.dir/ladspa/sifter_1210.c.o
[ 13%] Linking C shared module ../../ladspa/sifter_1210.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sin_cos_1881.dir/ladspa/sin_cos_1881.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/single_para_1203.dir/ladspa/single_para_1203.c.o
[ 13%] Linking C shared module ../../ladspa/sin_cos_1881.so
[ 13%] Linking C shared module ../../ladspa/single_para_1203.so
[ 13%] Built target shaper_1187
[ 13%] Built target sifter_1210
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sinus_wavewrapper_1198.dir/ladspa/sinus_wavewrapper_1198.c.o
[ 13%] Linking C shared module ../../ladspa/sinus_wavewrapper_1198.so
[ 13%] Built target sin_cos_1881
[ 13%] Built target single_para_1203
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/smooth_decimate_1414.dir/ladspa/smooth_decimate_1414.c.o
[ 13%] Linking C shared module ../../ladspa/smooth_decimate_1414.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/split_1406.dir/ladspa/split_1406.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/step_muxer_1212.dir/ladspa/step_muxer_1212.c.o
[ 13%] Linking C shared module ../../ladspa/split_1406.so
[ 13%] Linking C shared module ../../ladspa/step_muxer_1212.so
[ 13%] Built target sinus_wavewrapper_1198
[ 13%] Built target smooth_decimate_1414
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/surround_encoder_1401.dir/ladspa/surround_encoder_1401.c.o
[ 13%] Built target step_muxer_1212
[ 13%] Built target split_1406
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/svf_1214.dir/ladspa/svf_1214.c.o
[ 13%] Linking C shared module ../../ladspa/surround_encoder_1401.so
[ 13%] Linking C shared module ../../ladspa/svf_1214.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/tape_delay_1211.dir/ladspa/tape_delay_1211.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/transient_1206.dir/ladspa/transient_1206.c.o
[ 13%] Linking C shared module ../../ladspa/tape_delay_1211.so
[ 13%] Linking C shared module ../../ladspa/transient_1206.so
[ 13%] Built target surround_encoder_1401
[ 13%] Built target svf_1214
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/triple_para_1204.dir/ladspa/triple_para_1204.c.o
[ 13%] Built target transient_1206
[ 13%] Built target tape_delay_1211
[ 13%] Linking C shared module ../../ladspa/triple_para_1204.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/valve_1209.dir/ladspa/valve_1209.c.o
[ 13%] Linking C shared module ../../ladspa/valve_1209.so
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/valve_rect_1405.dir/ladspa/valve_rect_1405.c.o
[ 13%] Building C object plugins/LadspaEffect/swh/CMakeFiles/vocoder_1337.dir/ladspa/vocoder_1337.c.o
[ 13%] Linking C shared module ../../ladspa/valve_rect_1405.so
[ 13%] Linking C shared module ../../ladspa/vocoder_1337.so
[ 13%] Built target triple_para_1204
[ 14%] Built target valve_1209
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/vynil_1905.dir/ladspa/vynil_1905.c.o
[ 14%] Built target valve_rect_1405
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/wave_terrain_1412.dir/ladspa/wave_terrain_1412.c.o
[ 14%] Built target vocoder_1337
[ 14%] Linking C shared module ../../ladspa/vynil_1905.so
[ 14%] Linking C shared module ../../ladspa/wave_terrain_1412.so
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/xfade_1915.dir/ladspa/xfade_1915.c.o
[ 14%] Building C object plugins/LadspaEffect/swh/CMakeFiles/zm1_1428.dir/ladspa/zm1_1428.c.o
[ 14%] Linking C shared module ../../ladspa/xfade_1915.so
[ 14%] Linking C shared module ../../ladspa/zm1_1428.so
[ 14%] Built target vynil_1905
[ 14%] Built target wave_terrain_1412
[ 14%] Built target xfade_1915
[ 14%] Built target zm1_1428
[ 16%] Built target cmt
[ 16%] Built target adplug
[ 16%] Built target gme
[ 16%] Built target veal
[ 16%] Built target resid_objects
[ 17%] Built target rnnoise_vendored
[ 17%] Performing build step for 'NativeLinuxRemoteVstPlugin64'
[ 17%] Built target zynaddsubfx_nio
[100%] Built target NativeLinuxRemoteVstPlugin64
[ 19%] Built target zynaddsubfx_gui
[ 19%] Built target FakeRemotePluginClient_autogen_timestamp_deps
[ 21%] Built target zynaddsubfx_synth
[ 21%] No install step for 'NativeLinuxRemoteVstPlugin64'
[ 21%] Built target zene_api_autogen
[ 21%] Built target wasm-wat2wasm_autogen
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/analogue_osc_1416.dir/ladspa/analogue_osc_1416.c.o
[ 21%] Completed 'NativeLinuxRemoteVstPlugin64'
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bandpass_a_iir_1893.dir/ladspa/bandpass_a_iir_1893.c.o
[ 21%] Building C object plugins/LadspaEffect/swh/CMakeFiles/bandpass_iir_1892.dir/ladspa/bandpass_iir_1892.c.o
[ 21%] Linking C shared module ../../ladspa/analogue_osc_1416.so
[ 22%] Linking C shared module ../../ladspa/bandpass_a_iir_1893.so
[ 22%] Linking C shared module ../../ladspa/bandpass_iir_1892.so
[ 22%] Built target NativeLinuxRemoteVstPlugin64
[ 22%] Built target analogue_osc_1416
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/butterworth_1902.dir/ladspa/butterworth_1902.c.o
[ 22%] Built target bandpass_iir_1892
[ 22%] Built target bandpass_a_iir_1893
[ 22%] Linking C shared module ../../ladspa/butterworth_1902.so
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gsm_1215.dir/ladspa/gsm_1215.c.o
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/gverb_1216.dir/ladspa/gverb_1216.c.o
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitch_scale_1193.dir/ladspa/pitch_scale_1193.c.o
[ 22%] Linking C shared module ../../ladspa/gsm_1215.so
[ 22%] Linking C shared module ../../ladspa/gverb_1216.so
[ 22%] Linking C shared module ../../ladspa/pitch_scale_1193.so
[ 22%] Built target butterworth_1902
[ 22%] Built target gsm_1215
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/pitch_scale_1194.dir/ladspa/pitch_scale_1194.c.o
[ 22%] Built target pitch_scale_1193
[ 22%] Built target gverb_1216
[ 22%] Linking C shared module ../../ladspa/pitch_scale_1194.so
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc1_1425.dir/ladspa/sc1_1425.c.o
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc2_1426.dir/ladspa/sc2_1426.c.o
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc3_1427.dir/ladspa/sc3_1427.c.o
[ 22%] Linking C shared module ../../ladspa/sc1_1425.so
[ 22%] Linking C shared module ../../ladspa/sc2_1426.so
[ 22%] Linking C shared module ../../ladspa/sc3_1427.so
[ 22%] Built target pitch_scale_1194
[ 22%] Built target sc1_1425
[ 22%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc4_1882.dir/ladspa/sc4_1882.c.o
[ 22%] Built target sc2_1426
[ 23%] Built target sc3_1427
[ 23%] Linking C shared module ../../ladspa/sc4_1882.so
[ 23%] Building C object plugins/LadspaEffect/swh/CMakeFiles/sc4m_1916.dir/ladspa/sc4m_1916.c.o
[ 23%] Building C object plugins/LadspaEffect/swh/CMakeFiles/se4_1883.dir/ladspa/se4_1883.c.o
[ 23%] Built target ZynAddSubFxCore
[ 23%] Linking C shared module ../../ladspa/sc4m_1916.so
[ 23%] Linking C shared module ../../ladspa/se4_1883.so
[ 23%] Automatic MOC for target FakeRemotePluginClient
[ 23%] Built target sc4_1882
[ 23%] Built target sc4m_1916
[ 23%] Built target FakeRemotePluginClient_autogen
[ 23%] Built target se4_1883
[ 24%] Built target wasm-wat2wasm
[ 24%] Built target FakeRemotePluginClient
[ 24%] Built target RemoteZynAddSubFx
[ 24%] Built target wasm-modules
[ 24%] Built target WasmWorkerPoolTest_autogen_timestamp_deps
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlRegistry.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlRegistryRegistrations.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlTransactions.cpp.o
[ 24%] Automatic MOC for target WasmWorkerPoolTest
[ 24%] Built target WasmWorkerPoolTest_autogen
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlUndoCoalescing.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlSchema.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlVocabulary.cpp.o
[ 24%] Building CXX object src/CMakeFiles/zene_api.dir/core/ControlReversibilityTableRouting.cpp.o
[ 25%] Built target WasmWorkerPoolTest
[ 25%] Linking CXX static library libzene_api.a
[ 27%] Built target zene_api
[ 27%] Built target lmmsobjs_autogen_timestamp_deps
[ 27%] Automatic MOC and UIC for target lmmsobjs
[ 27%] Built target lmmsobjs_autogen
[ 27%] Generating qrc_lmms.cpp
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/lmmsobjs_autogen/mocs_compilation.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AudioBusHandle.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/AudioEngine.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ConfigManager.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/CrashReporter.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsArrangement.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClip.cpp.o
[ 27%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipEdits.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipTrim.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipLink.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClipLinkState.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSample.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSampleEdit.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsClock.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsComp.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCompEdits.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlAutomationSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlBrowserSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomation.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomationEdit.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsAutomationRamp.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBrowser.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBrowserTags.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsControl.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCompSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDsp.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsIdContract.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMixer.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlMixerSupport.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMixerRoutes.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPdc.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRouting.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsBus.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPorts.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPatcher.cpp.o
[ 28%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPatcherEdit.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlMasteringSupport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMastering.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMasteringRun.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDetectSupport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDetect.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDetectApply.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMeter.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMeterFile.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsInterchange.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDawProject.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsModulator.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsModulatorRoutes.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteExpression.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidi.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidiReconnect.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsMidiReconnectEdit.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNotes.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteProbability.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlNoteShared.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteRandom.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteSlide.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsNoteTransform.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlScaleSupport.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScale.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScaleEdit.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsDevice.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPlugin.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginParams.cpp.o
[ 29%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginPreset.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginState.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCrash.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsCrashControl.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginScan.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPluginScanEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSafeStart.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSafeStartEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcess.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcessEdit.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsOutOfProcessSupport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsHostChunking.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsHostNotes.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsArrangementState.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProject.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectFiles.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRevisions.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRenderStems.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRack.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRackMacros.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRackZones.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsScript.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsLivecode.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsExport.cpp.o
[ 30%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsExportPresets.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlExportPresetSupport.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsFeedback.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsFreeze.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsLink.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSettings.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSurface.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsStructure.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTelemetry.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTransport.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTrackFolder.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTrackFolderSets.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVca.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaMix.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaEdit.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsVcaEditSet.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsUndo.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsTransportMap.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsPunch.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecording.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRecovery.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlRecordingSupport.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingInput.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRoutes.cpp.o
[ 31%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsRecordingRetro.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWarp.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWarpEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlGrooveSupport.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGroove.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGrooveEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsGroovePool.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlWasmSupport.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasm.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasmEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsWasmRender.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceCatalogue.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceHosted.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceState.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceSupport.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceVst3.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlDeviceClap.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlChainPresetSupport.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChain.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChainEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlChordSupport.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChord.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChordEdit.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsChordWrite.cpp.o
[ 32%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlEditSupport.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlModulationSupport.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlRackSupport.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectArchive.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlProjectAssets.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlProjectAssetsRelink.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsProjectMmpzGit.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsController.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsControllerTemplates.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServer.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServerSocket.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlServerWin32.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlSession.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlStructuralSupport.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/DataFile.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Engine.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Mixer.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/VcaGroup.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/RackMacros.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawBindings.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawEdit.cpp.o
[ 33%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ScriptDawEffects.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Song.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/DawProjectSession.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ModulationLayer.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ProvenanceSection.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSession.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionLaunch.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionFollow.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecord.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecordLand.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/Telemetry.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/FocusDesk.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/FocusDeskPane.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/FocusDeskPlacement.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/GuiApplication.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/LmmsStyle.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MainWindow.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MicrotunerConfig.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MixerChannelView.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/MixerView.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/SendButtonIndicator.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/modals/AboutDialog.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/InstrumentTrackView.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/tracks/SampleTrackView.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/TabWidget.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/gui/widgets/ToolButton.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/InstrumentTrack.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/SampleTrack.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/tracks/TrackFolder.cpp.o
[ 34%] Building CXX object src/CMakeFiles/lmmsobjs.dir/qrc_lmms.cpp.o
[ 45%] Built target lmmsobjs
[ 45%] Built target ArrayVectorTest_autogen_timestamp_deps
[ 45%] Built target zene_autogen_timestamp_deps
[ 45%] Built target AudioBufferTest_autogen_timestamp_deps
[ 45%] Built target AudioBusHandleTest_autogen_timestamp_deps
[ 45%] Built target AudioEngineTeardownTest_autogen_timestamp_deps
[ 45%] Built target AudioBusTest_autogen_timestamp_deps
[ 45%] Built target AudioPortsModelTest_autogen_timestamp_deps
[ 45%] Built target AudioPortsTest_autogen_timestamp_deps
[ 45%] Built target AudioStretcherTest_autogen_timestamp_deps
[ 45%] Built target AutomatableModelTest_autogen_timestamp_deps
[ 45%] Built target AudioResamplerRatioTest_autogen_timestamp_deps
[ 45%] Built target AutomationModesTest_autogen_timestamp_deps
[ 45%] Built target BrowserCatalogTest_autogen_timestamp_deps
[ 45%] Built target SampleAccurateAutomationTest_autogen_timestamp_deps
[ 45%] Built target ClipFadesRenderTest_autogen_timestamp_deps
[ 45%] Built target ClipEditsTest_autogen_timestamp_deps
[ 45%] Built target ClipLinkTest_autogen_timestamp_deps
[ 45%] Built target ClipSerialisationTest_autogen_timestamp_deps
[ 45%] Built target ClipLinkPersistenceTest_autogen_timestamp_deps
[ 45%] Built target WriteRefusalGateTest_autogen_timestamp_deps
[ 45%] Built target ConfigMigrationTest_autogen_timestamp_deps
[ 45%] Built target ClipWarpPersistenceTest_autogen_timestamp_deps
[ 45%] Built target ControlAutomationScriptTest_autogen_timestamp_deps
[ 45%] Built target ControlAutomationModesTest_autogen_timestamp_deps
[ 45%] Built target ControlBrowserCommandsTest_autogen_timestamp_deps
[ 45%] Built target ChordTrackTest_autogen_timestamp_deps
[ 45%] Built target ControlChainPresetTest_autogen_timestamp_deps
[ 45%] Built target ChordDetectTest_autogen_timestamp_deps
[ 45%] Built target ChordProgressionTest_autogen_timestamp_deps
[ 45%] Built target ControlChordCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlChordWriteTest_autogen_timestamp_deps
[ 45%] Built target ControlDeviceCatalogueTest_autogen_timestamp_deps
[ 45%] Built target ControlEditCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlLinkCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlProjectArchiveTest_autogen_timestamp_deps
[ 45%] Built target ControlRegistryTest_autogen_timestamp_deps
[ 45%] Built target ControlSurfaceReferenceTest_autogen_timestamp_deps
[ 45%] Built target ControllerSurfaceTest_autogen_timestamp_deps
[ 45%] Built target ControlTempoMapCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlShutdownHookTest_autogen_timestamp_deps
[ 45%] Built target ControlVerbInverseTest_autogen_timestamp_deps
[ 45%] Built target ControlNoteScaleVerbsTest_autogen_timestamp_deps
[ 45%] Built target ControlSampleOperatorTest_autogen_timestamp_deps
[ 45%] Built target ControlWarpCommandsTest_autogen_timestamp_deps
[ 45%] Built target GrooveTemplateTest_autogen_timestamp_deps
[ 45%] Built target ControlGrooveCommandsTest_autogen_timestamp_deps
[ 45%] Built target CrashReporterArmTest_autogen_timestamp_deps
[ 45%] Built target CrashReporterTest_autogen_timestamp_deps
[ 45%] Built target DataFileFormatTest_autogen_timestamp_deps
[ 45%] Built target DataFileSaveIntegrityTest_autogen_timestamp_deps
[ 45%] Built target DocumentSectionsTest_autogen_timestamp_deps
[ 45%] Built target DocumentIndexTest_autogen_timestamp_deps
[ 45%] Built target ProjectContainerTest_autogen_timestamp_deps
[ 45%] Built target ExportDitherTest_autogen_timestamp_deps
[ 45%] Built target ProjectContainerEntriesTest_autogen_timestamp_deps
[ 45%] Built target ExportWavDitherTest_autogen_timestamp_deps
[ 45%] Built target LoudnessReportTest_autogen_timestamp_deps
[ 45%] Built target MasteringTest_autogen_timestamp_deps
[ 45%] Built target LufsMeterTest_autogen_timestamp_deps
[ 45%] Built target ImportDetectionTest_autogen_timestamp_deps
[ 45%] Built target MathTest_autogen_timestamp_deps
[ 45%] Built target MidiClockTest_autogen_timestamp_deps
[ 45%] Built target MidiLearnTest_autogen_timestamp_deps
[ 45%] Built target MeterTapTest_autogen_timestamp_deps
[ 45%] Built target MidiLearnThreadTest_autogen_timestamp_deps
[ 45%] Built target MidiProbabilityPersistenceTest_autogen_timestamp_deps
[ 45%] Built target MidiRetroCaptureTest_autogen_timestamp_deps
[ 45%] Built target RetroMidiCaptureCommandsTest_autogen_timestamp_deps
[ 45%] Built target MidiReconnectTest_autogen_timestamp_deps
[ 45%] Built target MixerRoutingBackwardCompatTest_autogen_timestamp_deps
[ 45%] Built target MixerAbRegressionTest_autogen_timestamp_deps
[ 45%] Built target MixerConcurrencyTest_autogen_timestamp_deps
[ 45%] Built target MpeExpressionTest_autogen_timestamp_deps
[ 45%] Built target MpeInputPathTest_autogen_timestamp_deps
[ 45%] Built target MpeNoteStorageTest_autogen_timestamp_deps
[ 45%] Built target ModulationLayerValueTest_autogen_timestamp_deps
[ 45%] Built target ControlNoteExpressionCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlModulatorCommandsTest_autogen_timestamp_deps
[ 45%] Built target ModulationLayerProjectRoundTripTest_autogen_timestamp_deps
[ 45%] Built target ModulationLayerTest_autogen_timestamp_deps
[ 45%] Built target MultiTrackRecorderTest_autogen_timestamp_deps
[ 45%] Built target RetroAudioCaptureTest_autogen_timestamp_deps
[ 45%] Built target RecordingInputPathTest_autogen_timestamp_deps
[ 45%] Built target NamespaceRegistryTest_autogen_timestamp_deps
[ 45%] Built target NoteRandomTest_autogen_timestamp_deps
[ 45%] Built target PhaseFChannelScaleTest_autogen_timestamp_deps
[ 45%] Built target NoteTransformTest_autogen_timestamp_deps
[ 45%] Built target PluginLogoResourceTest_autogen_timestamp_deps
[ 45%] Built target ProjectOpenIntegrityTest_autogen_timestamp_deps
[ 45%] Built target PluginAudioPortsTest_autogen_timestamp_deps
[ 45%] Built target ProjectRecoveryTest_autogen_timestamp_deps
[ 45%] Built target PartialLoadTest_autogen_timestamp_deps
[ 45%] Built target ProjectVersionTest_autogen_timestamp_deps
[ 45%] Built target RackMacrosTest_autogen_timestamp_deps
[ 45%] Built target RackZonesTest_autogen_timestamp_deps
[ 45%] Built target RackTest_autogen_timestamp_deps
[ 45%] Built target RetroMidiRingTest_autogen_timestamp_deps
[ 45%] Built target RecordClipTest_autogen_timestamp_deps
[ 45%] Built target RecordRingBufferTest_autogen_timestamp_deps
[ 45%] Built target RecordingRealtimeTest_autogen_timestamp_deps
[ 45%] Built target RelativePathsTest_autogen_timestamp_deps
[ 45%] Built target ReversibilityUndoTest_autogen_timestamp_deps
[ 45%] Built target ReversibilityContractTest_autogen_timestamp_deps
[ 45%] Built target RevisionTimelineTest_autogen_timestamp_deps
[ 45%] Built target TakeLaneCompTest_autogen_timestamp_deps
[ 45%] Built target UnclaimedElementsTest_autogen_timestamp_deps
[ 45%] Built target UndoBoundsTest_autogen_timestamp_deps
[ 45%] Built target UnclaimedTrackTypeTest_autogen_timestamp_deps
[ 45%] Built target TakeLaneTest_autogen_timestamp_deps
[ 45%] Built target RemotePluginAudioPortsTest_autogen_timestamp_deps
[ 45%] Built target RemotePluginClientE2ETest_autogen_timestamp_deps
[ 45%] Built target RenderJobQueueTest_autogen_timestamp_deps
[ 45%] Built target RoutingGraphLiveTest_autogen_timestamp_deps
[ 45%] Built target RoutingGraphTest_autogen_timestamp_deps
[ 45%] Built target PdcMixerTest_autogen_timestamp_deps
[ 45%] Built target RoutingGraphScheduleTest_autogen_timestamp_deps
[ 45%] Built target VcaGroupTest_autogen_timestamp_deps
[ 45%] Built target PatcherCommandsTest_autogen_timestamp_deps
[ 45%] Built target ControlVcaEditGroupsTest_autogen_timestamp_deps
[ 45%] Built target ControlVcaCommandsTest_autogen_timestamp_deps
[ 45%] Built target ScriptBindingsTest_autogen_timestamp_deps
[ 45%] Built target ScriptEngineTest_autogen_timestamp_deps
[ 45%] Built target ScriptDawBindingTest_autogen_timestamp_deps
[ 45%] Built target ScriptClockTest_autogen_timestamp_deps
[ 45%] Built target ScriptMemoryBudgetTest_autogen_timestamp_deps
[ 45%] Built target SlideNotesTest_autogen_timestamp_deps
[ 45%] Built target ScriptStabilisationTest_autogen_timestamp_deps
[ 45%] Built target SmfInterchangeTest_autogen_timestamp_deps
[ 45%] Built target SmfInterchangeRoundTripTest_autogen_timestamp_deps
[ 45%] Built target DawProjectInterchangeRoundTripTest_autogen_timestamp_deps
[ 45%] Built target StableTrackIdsTest_autogen_timestamp_deps
[ 45%] Built target ProjectRevIdsTest_autogen_timestamp_deps
[ 45%] Built target ProvenanceSectionTest_autogen_timestamp_deps
[ 45%] Built target StemExportTest_autogen_timestamp_deps
[ 45%] Built target TelemetryTransportTest_autogen_timestamp_deps
[ 45%] Built target TelemetryTest_autogen_timestamp_deps
[ 45%] Built target TempoMapPersistenceTest_autogen_timestamp_deps
[ 45%] Built target TrackFolderTest_autogen_timestamp_deps
[ 45%] Built target TempoMapTest_autogen_timestamp_deps
[ 45%] Built target TimelineTest_autogen_timestamp_deps
[ 45%] Built target WarpMarkersTest_autogen_timestamp_deps
[ 45%] Built target TwoTrackRecordingHarness_autogen_timestamp_deps
[ 45%] Built target FocusDeskPaneTest_autogen_timestamp_deps
[ 45%] Built target WcagContrastTest_autogen_timestamp_deps
[ 45%] Built target AutomationTrackTest_autogen_timestamp_deps
[ 45%] Built target AccessibilityHelperTest_autogen_timestamp_deps
[ 45%] Built target FocusDeskTest_autogen_timestamp_deps
[ 45%] Built target MidiLearnGuiTest_autogen_timestamp_deps
[ 45%] Built target SampleClipWarpTest_autogen_timestamp_deps
[ 45%] Built target SessionModelTest_autogen_timestamp_deps
[ 45%] Built target SampleClipStretchTest_autogen_timestamp_deps
[ 45%] Built target SampleClipWindowTest_autogen_timestamp_deps
[ 45%] Built target SessionFollowTest_autogen_timestamp_deps
[ 45%] Built target SessionSchedulerRenderTest_autogen_timestamp_deps
[ 45%] Built target SessionSchedulerTest_autogen_timestamp_deps
[ 45%] Built target SessionArrangementRecordTest_autogen_timestamp_deps
[ 45%] Built target WasmSandboxTest_autogen_timestamp_deps
[ 45%] Built target WasmAbiConformanceTest_autogen_timestamp_deps
[ 45%] Built target zene_autogen
[ 45%] Automatic MOC for target ArrayVectorTest
[ 45%] Automatic MOC for target AudioBusTest
[ 45%] Automatic MOC for target AudioBufferTest
[ 45%] Automatic MOC for target AudioBusHandleTest
[ 45%] Built target ArrayVectorTest_autogen
[ 45%] Built target AudioBufferTest_autogen
[ 45%] Built target AudioBusTest_autogen
[ 45%] Automatic MOC for target AudioEngineTeardownTest
[ 45%] Automatic MOC for target AudioPortsModelTest
[ 45%] Automatic MOC for target AudioPortsTest
[ 45%] Built target AudioEngineTeardownTest_autogen
[ 45%] Built target AudioPortsModelTest_autogen
[ 45%] Built target AudioPortsTest_autogen
[ 45%] Automatic MOC for target AudioStretcherTest
[ 45%] Automatic MOC for target AutomatableModelTest
[ 45%] Automatic MOC for target AudioResamplerRatioTest
[ 45%] Built target AudioStretcherTest_autogen
[ 45%] Built target AutomatableModelTest_autogen
[ 45%] Built target AudioResamplerRatioTest_autogen
[ 45%] Automatic MOC for target AutomationModesTest
[ 45%] Automatic MOC for target BrowserCatalogTest
[ 45%] Automatic MOC for target SampleAccurateAutomationTest
[ 45%] Built target AutomationModesTest_autogen
[ 45%] Built target BrowserCatalogTest_autogen
[ 45%] Automatic MOC for target ClipEditsTest
[ 45%] Automatic MOC for target ClipFadesRenderTest
[ 45%] Built target ClipEditsTest_autogen
[ 45%] Built target ClipFadesRenderTest_autogen
[ 45%] Automatic MOC for target ClipLinkTest
[ 45%] Automatic MOC for target ClipLinkPersistenceTest
[ 45%] Built target AudioBusHandleTest_autogen
[ 45%] Automatic MOC for target ClipSerialisationTest
[ 45%] Built target ClipSerialisationTest_autogen
[ 45%] Automatic MOC for target WriteRefusalGateTest
[ 45%] Built target WriteRefusalGateTest_autogen
[ 45%] Automatic MOC for target ClipWarpPersistenceTest
[ 45%] Built target ClipWarpPersistenceTest_autogen
[ 45%] Built target SampleAccurateAutomationTest_autogen
[ 45%] Automatic MOC for target ConfigMigrationTest
[ 45%] Built target ConfigMigrationTest_autogen
[ 45%] Automatic MOC for target ControlAutomationScriptTest
[ 45%] Built target ClipLinkTest_autogen
[ 45%] Automatic MOC for target ControlAutomationModesTest
[ 45%] Built target ClipLinkPersistenceTest_autogen
[ 45%] Automatic MOC for target ControlBrowserCommandsTest
[ 45%] Automatic MOC for target ControlChainPresetTest
[ 45%] Built target ControlAutomationScriptTest_autogen
[ 45%] Built target ControlAutomationModesTest_autogen
[ 45%] Automatic MOC for target ChordTrackTest
[ 45%] Built target ControlBrowserCommandsTest_autogen
[ 45%] Built target ChordTrackTest_autogen
[ 45%] Automatic MOC for target ChordDetectTest
[ 45%] Built target ControlChainPresetTest_autogen
[ 45%] Automatic MOC for target ChordProgressionTest
[ 45%] Built target ChordProgressionTest_autogen
[ 45%] Automatic MOC for target ControlChordCommandsTest
[ 45%] Automatic MOC for target ControlChordWriteTest
[ 45%] Automatic MOC for target ControlDeviceCatalogueTest
[ 45%] Built target ChordDetectTest_autogen
[ 45%] Automatic MOC for target ControlEditCommandsTest
[ 45%] Built target ControlChordCommandsTest_autogen
[ 45%] Built target ControlChordWriteTest_autogen
[ 45%] Automatic MOC for target ControlLinkCommandsTest
[ 45%] Built target ControlDeviceCatalogueTest_autogen
[ 45%] Automatic MOC for target ControlProjectArchiveTest
[ 45%] Automatic MOC for target ControlRegistryTest
[ 45%] Built target ControlEditCommandsTest_autogen
[ 45%] Automatic MOC for target ControlSurfaceReferenceTest
[ 45%] Built target ControlLinkCommandsTest_autogen
[ 45%] Built target ControlProjectArchiveTest_autogen
[ 45%] Built target ControlRegistryTest_autogen
[ 45%] Automatic MOC for target ControllerSurfaceTest
[ 45%] Automatic MOC for target ControlShutdownHookTest
[ 45%] Built target ControllerSurfaceTest_autogen
[ 45%] Automatic MOC for target ControlTempoMapCommandsTest
[ 45%] Automatic MOC for target ControlVerbInverseTest
[ 45%] Built target ControlSurfaceReferenceTest_autogen
[ 45%] Automatic MOC for target ControlSampleOperatorTest
[ 45%] Built target ControlShutdownHookTest_autogen
[ 45%] Automatic MOC for target ControlNoteScaleVerbsTest
[ 45%] Built target ControlVerbInverseTest_autogen
[ 45%] Built target ControlTempoMapCommandsTest_autogen
[ 45%] Automatic MOC for target ControlWarpCommandsTest
[ 45%] Automatic MOC for target GrooveTemplateTest
[ 45%] Built target ControlSampleOperatorTest_autogen
[ 45%] Automatic MOC for target ControlGrooveCommandsTest
[ 45%] Built target ControlNoteScaleVerbsTest_autogen
[ 45%] Automatic MOC for target CrashReporterArmTest
[ 45%] Built target ControlWarpCommandsTest_autogen
[ 45%] Built target CrashReporterArmTest_autogen
[ 45%] Built target GrooveTemplateTest_autogen
[ 45%] Automatic MOC for target CrashReporterTest
[ 45%] Automatic MOC for target DataFileFormatTest
[ 45%] Automatic MOC for target DataFileSaveIntegrityTest
[ 45%] Built target DataFileFormatTest_autogen
[ 45%] Built target DataFileSaveIntegrityTest_autogen
[ 45%] Automatic MOC for target DocumentSectionsTest
[ 45%] Automatic MOC for target DocumentIndexTest
[ 45%] Built target DocumentSectionsTest_autogen
[ 45%] Built target DocumentIndexTest_autogen
[ 45%] Automatic MOC for target ProjectContainerEntriesTest
[ 45%] Automatic MOC for target ProjectContainerTest
[ 45%] Built target ProjectContainerTest_autogen
[ 45%] Built target ProjectContainerEntriesTest_autogen
[ 45%] Automatic MOC for target ExportDitherTest
[ 45%] Automatic MOC for target ExportWavDitherTest
[ 45%] Built target ExportDitherTest_autogen
[ 45%] Built target ExportWavDitherTest_autogen
[ 45%] Built target ControlGrooveCommandsTest_autogen
[ 45%] Automatic MOC for target LufsMeterTest
[ 45%] Automatic MOC for target LoudnessReportTest
[ 45%] Built target LufsMeterTest_autogen
[ 45%] Built target LoudnessReportTest_autogen
[ 45%] Automatic MOC for target MasteringTest
[ 45%] Built target MasteringTest_autogen
[ 45%] Automatic MOC for target MathTest
[ 45%] Automatic MOC for target ImportDetectionTest
[ 45%] Built target ImportDetectionTest_autogen
[ 45%] Built target MathTest_autogen
[ 45%] Built target CrashReporterTest_autogen
[ 45%] Automatic MOC for target MeterTapTest
[ 45%] Built target MeterTapTest_autogen
[ 45%] Automatic MOC for target MidiLearnTest
[ 45%] Automatic MOC for target MidiClockTest
[ 45%] Built target MidiClockTest_autogen
[ 45%] Built target MidiLearnTest_autogen
[ 45%] Automatic MOC for target MidiLearnThreadTest
[ 45%] Automatic MOC for target MidiRetroCaptureTest
[ 45%] Built target MidiLearnThreadTest_autogen
[ 45%] Built target MidiRetroCaptureTest_autogen
[ 45%] Automatic MOC for target RetroMidiCaptureCommandsTest
[ 45%] Automatic MOC for target MidiProbabilityPersistenceTest
[ 45%] Automatic MOC for target MidiReconnectTest
[ 45%] Built target MidiProbabilityPersistenceTest_autogen
[ 45%] Automatic MOC for target MixerAbRegressionTest
[ 45%] Built target MidiReconnectTest_autogen
[ 45%] Automatic MOC for target MixerConcurrencyTest
[ 45%] Automatic MOC for target MixerRoutingBackwardCompatTest
[ 45%] Built target RetroMidiCaptureCommandsTest_autogen
[ 45%] Built target MixerAbRegressionTest_autogen
[ 45%] Automatic MOC for target MpeExpressionTest
[ 45%] Built target MpeExpressionTest_autogen
[ 45%] Built target MixerConcurrencyTest_autogen
[ 45%] Automatic MOC for target MpeInputPathTest
[ 45%] Built target MixerRoutingBackwardCompatTest_autogen
[ 45%] Built target MpeInputPathTest_autogen
[ 45%] Automatic MOC for target MpeNoteStorageTest
[ 45%] Automatic MOC for target ModulationLayerValueTest
[ 45%] Built target MpeNoteStorageTest_autogen
[ 45%] Automatic MOC for target ModulationLayerTest
[ 45%] Automatic MOC for target ControlModulatorCommandsTest
[ 45%] Automatic MOC for target ControlNoteExpressionCommandsTest
[ 45%] Built target ModulationLayerValueTest_autogen
[ 45%] Built target ModulationLayerTest_autogen
[ 45%] Built target ControlModulatorCommandsTest_autogen
[ 45%] Automatic MOC for target MultiTrackRecorderTest
[ 45%] Built target ControlNoteExpressionCommandsTest_autogen
[ 45%] Automatic MOC for target ModulationLayerProjectRoundTripTest
[ 45%] Built target MultiTrackRecorderTest_autogen
[ 45%] Automatic MOC for target RecordingInputPathTest
[ 45%] Built target RecordingInputPathTest_autogen
[ 45%] Automatic MOC for target RetroAudioCaptureTest
[ 45%] Automatic MOC for target NamespaceRegistryTest
[ 45%] Built target RetroAudioCaptureTest_autogen
[ 45%] Built target NamespaceRegistryTest_autogen
[ 45%] Automatic MOC for target NoteRandomTest
[ 45%] Built target NoteRandomTest_autogen
[ 45%] Automatic MOC for target NoteTransformTest
[ 45%] Automatic MOC for target PhaseFChannelScaleTest
[ 45%] Built target NoteTransformTest_autogen
[ 45%] Automatic MOC for target PluginLogoResourceTest
[ 45%] Built target PluginLogoResourceTest_autogen
[ 45%] Automatic MOC for target PluginAudioPortsTest
[ 45%] Built target PluginAudioPortsTest_autogen
[ 45%] Automatic MOC for target PartialLoadTest
[ 45%] Built target PartialLoadTest_autogen
[ 45%] Automatic MOC for target ProjectOpenIntegrityTest
[ 45%] Automatic MOC for target ProjectRecoveryTest
[ 45%] Built target ProjectRecoveryTest_autogen
[ 45%] Automatic MOC for target ProjectVersionTest
[ 45%] Built target ProjectVersionTest_autogen
[ 45%] Built target ModulationLayerProjectRoundTripTest_autogen
[ 45%] Automatic MOC for target RackMacrosTest
[ 45%] Automatic MOC for target RackTest
[ 45%] Built target PhaseFChannelScaleTest_autogen
[ 45%] Automatic MOC for target RackZonesTest
[ 45%] Built target ProjectOpenIntegrityTest_autogen
[ 45%] Automatic MOC for target RecordClipTest
[ 45%] Built target RecordClipTest_autogen
[ 45%] Automatic MOC for target RecordRingBufferTest
[ 45%] Built target RecordRingBufferTest_autogen
[ 45%] Built target RackMacrosTest_autogen
[ 45%] Automatic MOC for target RetroMidiRingTest
[ 45%] Built target RackTest_autogen
[ 45%] Built target RetroMidiRingTest_autogen
[ 45%] Automatic MOC for target RecordingRealtimeTest
[ 45%] Built target RecordingRealtimeTest_autogen
[ 45%] Automatic MOC for target RelativePathsTest
[ 45%] Automatic MOC for target ReversibilityContractTest
[ 45%] Built target RelativePathsTest_autogen
[ 45%] Automatic MOC for target ReversibilityUndoTest
[ 45%] Automatic MOC for target RevisionTimelineTest
[ 45%] Built target RackZonesTest_autogen
[ 45%] Automatic MOC for target UnclaimedElementsTest
[ 45%] Built target UnclaimedElementsTest_autogen
[ 46%] Automatic MOC for target UnclaimedTrackTypeTest
[ 46%] Built target ReversibilityContractTest_autogen
[ 46%] Built target ReversibilityUndoTest_autogen
[ 46%] Automatic MOC for target UndoBoundsTest
[ 46%] Built target RevisionTimelineTest_autogen
[ 46%] Automatic MOC for target TakeLaneCompTest
[ 46%] Automatic MOC for target TakeLaneTest
[ 46%] Built target UnclaimedTrackTypeTest_autogen
[ 46%] Automatic MOC for target RemotePluginAudioPortsTest
[ 46%] Built target RemotePluginAudioPortsTest_autogen
[ 46%] Automatic MOC for target RemotePluginClientE2ETest
[ 46%] Built target RemotePluginClientE2ETest_autogen
[ 46%] Automatic MOC for target RenderJobQueueTest
[ 46%] Built target RenderJobQueueTest_autogen
[ 46%] Built target UndoBoundsTest_autogen
[ 46%] Automatic MOC for target RoutingGraphScheduleTest
[ 46%] Built target TakeLaneCompTest_autogen
[ 46%] Automatic MOC for target RoutingGraphLiveTest
[ 46%] Built target TakeLaneTest_autogen
[ 46%] Automatic MOC for target RoutingGraphTest
[ 46%] Built target RoutingGraphTest_autogen
[ 46%] Automatic MOC for target PdcMixerTest
[ 46%] Automatic MOC for target PatcherCommandsTest
[ 46%] Built target RoutingGraphScheduleTest_autogen
[ 46%] Built target RoutingGraphLiveTest_autogen
[ 46%] Automatic MOC for target VcaGroupTest
[ 46%] Automatic MOC for target ControlVcaCommandsTest
[ 46%] Built target PdcMixerTest_autogen
[ 46%] Built target PatcherCommandsTest_autogen
[ 46%] Automatic MOC for target ControlVcaEditGroupsTest
[ 46%] Automatic MOC for target ScriptBindingsTest
[ 46%] Built target ScriptBindingsTest_autogen
[ 46%] Automatic MOC for target ScriptDawBindingTest
[ 46%] Built target VcaGroupTest_autogen
[ 46%] Built target ControlVcaCommandsTest_autogen
[ 46%] Automatic MOC for target ScriptClockTest
[ 46%] Automatic MOC for target ScriptEngineTest
[ 46%] Built target ScriptClockTest_autogen
[ 46%] Built target ScriptEngineTest_autogen
[ 46%] Built target ControlVcaEditGroupsTest_autogen
[ 46%] Automatic MOC for target ScriptMemoryBudgetTest
[ 46%] Automatic MOC for target ScriptStabilisationTest
[ 46%] Automatic MOC for target SlideNotesTest
[ 46%] Built target ScriptStabilisationTest_autogen
[ 46%] Built target SlideNotesTest_autogen
[ 46%] Automatic MOC for target SmfInterchangeTest
[ 46%] Built target ScriptDawBindingTest_autogen
[ 46%] Automatic MOC for target SmfInterchangeRoundTripTest
[ 46%] Automatic MOC for target DawProjectInterchangeRoundTripTest
[ 46%] Built target ScriptMemoryBudgetTest_autogen
[ 46%] Automatic MOC for target StableTrackIdsTest
[ 46%] Built target SmfInterchangeTest_autogen
[ 46%] Built target SmfInterchangeRoundTripTest_autogen
[ 46%] Automatic MOC for target ProjectRevIdsTest
[ 46%] Automatic MOC for target ProvenanceSectionTest
[ 46%] Built target DawProjectInterchangeRoundTripTest_autogen
[ 46%] Automatic MOC for target StemExportTest
[ 46%] Built target StemExportTest_autogen
[ 46%] Automatic MOC for target TelemetryTest
[ 46%] Built target StableTrackIdsTest_autogen
[ 46%] Automatic MOC for target TelemetryTransportTest
[ 46%] Built target TelemetryTransportTest_autogen
[ 46%] Built target ProjectRevIdsTest_autogen
[ 46%] Built target ProvenanceSectionTest_autogen
[ 47%] Automatic MOC for target TempoMapPersistenceTest
[ 47%] Built target TempoMapPersistenceTest_autogen
[ 47%] Automatic MOC for target TempoMapTest
[ 47%] Automatic MOC for target TimelineTest
[ 47%] Built target TempoMapTest_autogen
[ 47%] Automatic MOC for target TwoTrackRecordingHarness
[ 47%] Built target TimelineTest_autogen
[ 47%] Automatic MOC for target TrackFolderTest
[ 47%] Built target TwoTrackRecordingHarness_autogen
[ 47%] Automatic MOC for target WarpMarkersTest
[ 47%] Built target WarpMarkersTest_autogen
[ 47%] Built target TelemetryTest_autogen
[ 47%] Automatic MOC for target AccessibilityHelperTest
[ 47%] Automatic MOC for target WcagContrastTest
[ 47%] Automatic MOC for target FocusDeskPaneTest
[ 47%] Built target FocusDeskPaneTest_autogen
[ 47%] Automatic MOC for target FocusDeskTest
[ 47%] Built target FocusDeskTest_autogen
[ 47%] Automatic MOC for target MidiLearnGuiTest
[ 47%] Built target MidiLearnGuiTest_autogen
[ 47%] Automatic MOC for target AutomationTrackTest
[ 47%] Built target AutomationTrackTest_autogen
[ 47%] Built target TrackFolderTest_autogen
[ 47%] Automatic MOC for target SampleClipStretchTest
[ 47%] Built target WcagContrastTest_autogen
[ 47%] Automatic MOC for target SampleClipWarpTest
[ 47%] Built target SampleClipWarpTest_autogen
[ 47%] Automatic MOC for target SampleClipWindowTest
[ 47%] Built target AccessibilityHelperTest_autogen
[ 47%] Built target SampleClipWindowTest_autogen
[ 47%] Automatic MOC for target SessionModelTest
[ 47%] Built target SessionModelTest_autogen
[ 47%] Automatic MOC for target SessionSchedulerTest
[ 47%] Automatic MOC for target SessionSchedulerRenderTest
[ 47%] Built target SessionSchedulerTest_autogen
[ 47%] Automatic MOC for target SessionFollowTest
[ 47%] Built target SessionSchedulerRenderTest_autogen
[ 47%] Built target SessionFollowTest_autogen
[ 47%] Automatic MOC for target SessionArrangementRecordTest
[ 47%] Automatic MOC for target WasmSandboxTest
[ 47%] Automatic MOC for target WasmAbiConformanceTest
[ 47%] Built target SessionArrangementRecordTest_autogen
[ 47%] Built target WasmSandboxTest_autogen
[ 47%] Built target WasmAbiConformanceTest_autogen
[ 47%] Building CXX object src/CMakeFiles/zene.dir/core/main.cpp.o
[ 47%] Linking CXX executable ArrayVectorTest
[ 47%] Linking CXX executable AudioBufferTest
[ 47%] Built target SampleClipStretchTest_autogen
[ 47%] Building CXX object tests/CMakeFiles/AudioBusHandleTest.dir/src/core/AudioBusHandleTest.cpp.o
[ 47%] Built target ArrayVectorTest
[ 47%] Linking CXX executable AudioBusTest
[ 47%] Built target AudioBufferTest
[ 47%] Linking CXX executable AudioEngineTeardownTest
[ 47%] Linking CXX executable ../zene
[ 47%] Linking CXX executable AudioBusHandleTest
[ 47%] Built target AudioBusTest
[ 47%] Linking CXX executable AudioPortsModelTest
[ 47%] Built target AudioEngineTeardownTest
[ 47%] Linking CXX executable AudioPortsTest
[ 47%] Built target zene
[ 47%] Linking CXX executable AudioStretcherTest
[ 47%] Built target AudioBusHandleTest
[ 47%] Linking CXX executable AudioResamplerRatioTest
[ 47%] Built target AudioPortsModelTest
[ 47%] Linking CXX executable AutomatableModelTest
[ 47%] Built target AudioPortsTest
[ 47%] Linking CXX executable AutomationModesTest
[ 47%] Built target AudioStretcherTest
[ 48%] Building CXX object tests/CMakeFiles/SampleAccurateAutomationTest.dir/src/core/SampleAccurateAutomationTest.cpp.o
[ 48%] Built target AudioResamplerRatioTest
[ 48%] Linking CXX executable BrowserCatalogTest
[ 48%] Built target AutomatableModelTest
[ 49%] Built target AutomationModesTest
[ 49%] Linking CXX executable ClipEditsTest
[ 49%] Linking CXX executable ClipFadesRenderTest
[ 49%] Built target BrowserCatalogTest
[ 49%] Building CXX object tests/CMakeFiles/ClipLinkTest.dir/src/core/ClipLinkTest.cpp.o
[ 49%] Built target ClipFadesRenderTest
[ 50%] Built target ClipEditsTest
[ 50%] Building CXX object tests/CMakeFiles/ClipLinkPersistenceTest.dir/src/core/ClipLinkPersistenceTest.cpp.o
[ 50%] Linking CXX executable ClipSerialisationTest
[ 50%] Linking CXX executable SampleAccurateAutomationTest
[ 50%] Built target ClipSerialisationTest
[ 50%] Linking CXX executable WriteRefusalGateTest
[ 50%] Linking CXX executable ClipLinkTest
[ 50%] Built target SampleAccurateAutomationTest
[ 50%] Linking CXX executable ClipLinkPersistenceTest
[ 50%] Linking CXX executable ClipWarpPersistenceTest
[ 50%] Built target WriteRefusalGateTest
[ 50%] Linking CXX executable ConfigMigrationTest
[ 50%] Built target ClipLinkTest
[ 50%] Building CXX object tests/CMakeFiles/ControlAutomationScriptTest.dir/src/core/ControlAutomationScriptTest.cpp.o
[ 50%] Built target ClipWarpPersistenceTest
[ 50%] Built target ClipLinkPersistenceTest
[ 50%] Building CXX object tests/CMakeFiles/ControlAutomationModesTest.dir/src/core/ControlAutomationModesTest.cpp.o
[ 50%] Building CXX object tests/CMakeFiles/ControlBrowserCommandsTest.dir/src/core/ControlBrowserCommandsTest.cpp.o
[ 51%] Built target ConfigMigrationTest
[ 51%] Building CXX object tests/CMakeFiles/ControlChainPresetTest.dir/src/core/ControlChainPresetTest.cpp.o
[ 51%] Linking CXX executable ControlAutomationModesTest
[ 51%] Linking CXX executable ControlBrowserCommandsTest
[ 51%] Linking CXX executable ControlAutomationScriptTest
[ 51%] Linking CXX executable ControlChainPresetTest
[ 51%] Built target ControlAutomationModesTest
[ 51%] Built target ControlBrowserCommandsTest
[ 51%] Linking CXX executable ChordTrackTest
[ 51%] Building CXX object tests/CMakeFiles/ChordDetectTest.dir/src/core/ChordDetectTest.cpp.o
[ 51%] Built target ControlAutomationScriptTest
[ 51%] Linking CXX executable ChordProgressionTest
[ 51%] Built target ControlChainPresetTest
[ 51%] Building CXX object tests/CMakeFiles/ControlChordCommandsTest.dir/src/core/ControlChordCommandsTest.cpp.o
[ 51%] Built target ChordTrackTest
[ 51%] Building CXX object tests/CMakeFiles/ControlChordWriteTest.dir/src/core/ControlChordWriteTest.cpp.o
[ 51%] Built target ChordProgressionTest
[ 51%] Building CXX object tests/CMakeFiles/ControlDeviceCatalogueTest.dir/src/core/ControlDeviceCatalogueTest.cpp.o
[ 51%] Linking CXX executable ChordDetectTest
[ 51%] Linking CXX executable ControlChordCommandsTest
[ 51%] Built target ChordDetectTest
[ 51%] Building CXX object tests/CMakeFiles/ControlEditCommandsTest.dir/src/core/ControlEditCommandsTest.cpp.o
[ 51%] Linking CXX executable ControlDeviceCatalogueTest
[ 51%] Linking CXX executable ControlChordWriteTest
[ 51%] Built target ControlChordCommandsTest
[ 51%] Building CXX object tests/CMakeFiles/ControlLinkCommandsTest.dir/src/core/ControlLinkCommandsTest.cpp.o
[ 51%] Built target ControlDeviceCatalogueTest
[ 51%] Building CXX object tests/CMakeFiles/ControlProjectArchiveTest.dir/src/core/ControlProjectArchiveTest.cpp.o
[ 52%] Built target ControlChordWriteTest
[ 52%] Building CXX object tests/CMakeFiles/ControlRegistryTest.dir/src/core/ControlRegistryTest.cpp.o
[ 52%] Linking CXX executable ControlEditCommandsTest
[ 52%] Linking CXX executable ControlLinkCommandsTest
[ 52%] Built target ControlEditCommandsTest
[ 52%] Building CXX object tests/CMakeFiles/ControlSurfaceReferenceTest.dir/src/core/ControlSurfaceReferenceTest.cpp.o
[ 52%] Linking CXX executable ControlProjectArchiveTest
[ 52%] Linking CXX executable ControlRegistryTest
[ 52%] Built target ControlLinkCommandsTest
[ 52%] Linking CXX executable ControllerSurfaceTest
[ 52%] Built target ControlProjectArchiveTest
[ 52%] Building CXX object tests/CMakeFiles/ControlShutdownHookTest.dir/src/core/ControlShutdownHookTest.cpp.o
[ 52%] Built target ControlRegistryTest
[ 52%] Building CXX object tests/CMakeFiles/ControlTempoMapCommandsTest.dir/src/core/ControlTempoMapCommandsTest.cpp.o
[ 52%] Linking CXX executable ControlSurfaceReferenceTest
[ 53%] Built target ControllerSurfaceTest
[ 53%] Building CXX object tests/CMakeFiles/ControlVerbInverseTest.dir/src/core/ControlVerbInverseTest.cpp.o
[ 54%] Built target ControlSurfaceReferenceTest
[ 54%] Building CXX object tests/CMakeFiles/ControlSampleOperatorTest.dir/src/core/ControlSampleOperatorTest.cpp.o
[ 54%] Linking CXX executable ControlShutdownHookTest
[ 54%] Linking CXX executable ControlVerbInverseTest
[ 54%] Linking CXX executable ControlTempoMapCommandsTest
[ 54%] Built target ControlShutdownHookTest
[ 54%] Building CXX object tests/CMakeFiles/ControlNoteScaleVerbsTest.dir/src/core/ControlNoteScaleVerbsTest.cpp.o
[ 54%] Built target ControlTempoMapCommandsTest
[ 54%] Built target ControlVerbInverseTest
[ 54%] Building CXX object tests/CMakeFiles/ControlWarpCommandsTest.dir/src/core/ControlWarpCommandsTest.cpp.o
[ 54%] Building CXX object tests/CMakeFiles/GrooveTemplateTest.dir/src/core/GrooveTemplateTest.cpp.o
[ 54%] Linking CXX executable ControlSampleOperatorTest
[ 54%] Built target ControlSampleOperatorTest
[ 54%] Building CXX object tests/CMakeFiles/ControlGrooveCommandsTest.dir/src/core/ControlGrooveCommandsTest.cpp.o
[ 54%] Linking CXX executable GrooveTemplateTest
[ 54%] Linking CXX executable ControlNoteScaleVerbsTest
[ 54%] Linking CXX executable ControlWarpCommandsTest
[ 54%] Built target GrooveTemplateTest
[ 54%] Linking CXX executable CrashReporterArmTest
[ 54%] Built target ControlNoteScaleVerbsTest
[ 54%] Building CXX object tests/CMakeFiles/CrashReporterTest.dir/src/core/CrashReporterTest.cpp.o
[ 54%] Built target ControlWarpCommandsTest
[ 54%] Linking CXX executable DataFileFormatTest
[ 54%] Linking CXX executable ControlGrooveCommandsTest
[ 54%] Built target CrashReporterArmTest
[ 54%] Linking CXX executable DataFileSaveIntegrityTest
[ 54%] Built target DataFileFormatTest
[ 54%] Linking CXX executable DocumentIndexTest
[ 54%] Linking CXX executable CrashReporterTest
[ 54%] Built target ControlGrooveCommandsTest
[ 54%] Linking CXX executable DocumentSectionsTest
[ 54%] Built target DataFileSaveIntegrityTest
[ 54%] Linking CXX executable ProjectContainerTest
[ 55%] Built target DocumentIndexTest
[ 55%] Linking CXX executable ProjectContainerEntriesTest
[ 55%] Built target CrashReporterTest
[ 55%] Linking CXX executable ExportDitherTest
[ 55%] Built target DocumentSectionsTest
[ 55%] Linking CXX executable ExportWavDitherTest
[ 55%] Built target ProjectContainerTest
[ 55%] Linking CXX executable LoudnessReportTest
[ 56%] Built target ProjectContainerEntriesTest
[ 56%] Linking CXX executable LufsMeterTest
[ 56%] Built target ExportDitherTest
[ 56%] Linking CXX executable MasteringTest
[ 56%] Built target ExportWavDitherTest
[ 56%] Linking CXX executable ImportDetectionTest
[ 56%] Built target LoudnessReportTest
[ 56%] Linking CXX executable MathTest
[ 56%] Built target LufsMeterTest
[ 56%] Linking CXX executable MeterTapTest
[ 56%] Built target MasteringTest
[ 56%] Linking CXX executable MidiLearnTest
[ 56%] Built target ImportDetectionTest
[ 56%] Linking CXX executable MidiClockTest
[ 57%] Built target MathTest
[ 57%] Linking CXX executable MidiLearnThreadTest
[ 57%] Built target MeterTapTest
[ 57%] Linking CXX executable MidiRetroCaptureTest
[ 57%] Built target MidiLearnTest
[ 57%] Building CXX object tests/CMakeFiles/RetroMidiCaptureCommandsTest.dir/src/core/RetroMidiCaptureCommandsTest.cpp.o
[ 57%] Built target MidiClockTest
[ 58%] Linking CXX executable MidiProbabilityPersistenceTest
[ 58%] Built target MidiLearnThreadTest
[ 58%] Linking CXX executable MidiReconnectTest
[ 58%] Built target MidiRetroCaptureTest
[ 58%] Building CXX object tests/CMakeFiles/MixerAbRegressionTest.dir/src/core/MixerAbRegressionTest.cpp.o
[ 58%] Built target MidiProbabilityPersistenceTest
[ 58%] Building CXX object tests/CMakeFiles/MixerConcurrencyTest.dir/src/core/MixerConcurrencyTest.cpp.o
[ 58%] Built target MidiReconnectTest
[ 58%] Building CXX object tests/CMakeFiles/MixerRoutingBackwardCompatTest.dir/src/core/MixerRoutingBackwardCompatTest.cpp.o
[ 58%] Linking CXX executable RetroMidiCaptureCommandsTest
[ 58%] Linking CXX executable MixerAbRegressionTest
[ 58%] Built target RetroMidiCaptureCommandsTest
[ 58%] Linking CXX executable MpeExpressionTest
[ 58%] Linking CXX executable MixerConcurrencyTest
[ 58%] Built target MixerAbRegressionTest
[ 58%] Linking CXX executable MpeInputPathTest
[ 58%] Linking CXX executable MixerRoutingBackwardCompatTest
[ 58%] Built target MpeExpressionTest
[ 58%] Linking CXX executable MpeNoteStorageTest
[ 58%] Built target MixerConcurrencyTest
[ 58%] Built target mpe_test_consumer_autogen_timestamp_deps
[ 58%] Building CXX object tests/CMakeFiles/ModulationLayerValueTest.dir/src/core/ModulationLayerValueTest.cpp.o
[ 58%] Built target MpeInputPathTest
[ 58%] Building CXX object tests/CMakeFiles/ModulationLayerTest.dir/src/core/ModulationLayerTest.cpp.o
[ 58%] Built target MixerRoutingBackwardCompatTest
[ 58%] Building CXX object tests/CMakeFiles/ControlModulatorCommandsTest.dir/src/core/ControlModulatorCommandsTest.cpp.o
[ 58%] Built target MpeNoteStorageTest
[ 58%] Building CXX object tests/CMakeFiles/ControlNoteExpressionCommandsTest.dir/src/core/ControlNoteExpressionCommandsTest.cpp.o
[ 58%] Linking CXX executable ModulationLayerValueTest
[ 58%] Linking CXX executable ModulationLayerTest
[ 58%] Linking CXX executable ControlModulatorCommandsTest
[ 58%] Built target ModulationLayerValueTest
[ 58%] Building CXX object tests/CMakeFiles/ModulationLayerProjectRoundTripTest.dir/src/core/ModulationLayerProjectRoundTripTest.cpp.o
[ 58%] Linking CXX executable ControlNoteExpressionCommandsTest
[ 58%] Built target ModulationLayerTest
[ 58%] Linking CXX executable MultiTrackRecorderTest
[ 58%] Built target ControlModulatorCommandsTest
[ 58%] Linking CXX executable RecordingInputPathTest
[ 59%] Built target ControlNoteExpressionCommandsTest
[ 59%] Linking CXX executable RetroAudioCaptureTest
[ 59%] Built target MultiTrackRecorderTest
[ 59%] Linking CXX executable NamespaceRegistryTest
[ 60%] Built target RecordingInputPathTest
[ 60%] Linking CXX executable NoteRandomTest
[ 60%] Built target RetroAudioCaptureTest
[ 60%] Linking CXX executable NoteTransformTest
[ 61%] Linking CXX executable ModulationLayerProjectRoundTripTest
[ 61%] Built target NamespaceRegistryTest
[ 61%] Building CXX object tests/CMakeFiles/PhaseFChannelScaleTest.dir/src/core/PhaseFChannelScaleTest.cpp.o
[ 61%] Built target NoteRandomTest
[ 61%] Linking CXX executable PluginLogoResourceTest
[ 61%] Built target NoteTransformTest
[ 61%] Linking CXX executable PluginAudioPortsTest
[ 61%] Built target ModulationLayerProjectRoundTripTest
[ 61%] Linking CXX executable PartialLoadTest
[ 61%] Built target PluginLogoResourceTest
[ 61%] Building CXX object tests/CMakeFiles/ProjectOpenIntegrityTest.dir/src/core/ProjectOpenIntegrityTest.cpp.o
[ 61%] Built target PluginAudioPortsTest
[ 61%] Linking CXX executable ProjectRecoveryTest
[ 61%] Built target PartialLoadTest
[ 61%] Linking CXX executable ProjectVersionTest
[ 61%] Linking CXX executable PhaseFChannelScaleTest
[ 61%] Built target ProjectVersionTest
[ 61%] Built target ProjectRecoveryTest
[ 61%] Building CXX object tests/CMakeFiles/RackTest.dir/src/core/RackTest.cpp.o
[ 61%] Building CXX object tests/CMakeFiles/RackMacrosTest.dir/src/core/RackMacrosTest.cpp.o
[ 62%] Built target PhaseFChannelScaleTest
[ 62%] Building CXX object tests/CMakeFiles/RackZonesTest.dir/src/core/RackZonesTest.cpp.o
[ 62%] Linking CXX executable ProjectOpenIntegrityTest
[ 62%] Built target ProjectOpenIntegrityTest
[ 62%] Linking CXX executable RecordClipTest
[ 62%] Linking CXX executable RackTest
[ 62%] Linking CXX executable RackMacrosTest
[ 62%] Linking CXX executable RackZonesTest
[ 62%] Built target RecordClipTest
[ 62%] Linking CXX executable RecordRingBufferTest
[ 62%] Built target RackTest
[ 62%] Linking CXX executable RetroMidiRingTest
[ 62%] Built target RackMacrosTest
[ 62%] Built target RackZonesTest
[ 62%] Linking CXX executable RecordingRealtimeTest
[ 62%] Linking CXX executable RelativePathsTest
[ 62%] Built target RecordRingBufferTest
[ 62%] Building CXX object tests/CMakeFiles/ReversibilityContractTest.dir/src/core/ReversibilityContractTest.cpp.o
[ 62%] Built target RetroMidiRingTest
[ 62%] Building CXX object tests/CMakeFiles/ReversibilityUndoTest.dir/src/core/ReversibilityUndoTest.cpp.o
[ 62%] Built target RecordingRealtimeTest
[ 62%] Built target RelativePathsTest
[ 63%] Building CXX object tests/CMakeFiles/RevisionTimelineTest.dir/src/core/RevisionTimelineTest.cpp.o
[ 63%] Linking CXX executable UnclaimedElementsTest
[ 63%] Built target UnclaimedElementsTest
[ 63%] Building CXX object tests/CMakeFiles/UnclaimedTrackTypeTest.dir/src/core/UnclaimedTrackTypeTest.cpp.o
[ 63%] Linking CXX executable ReversibilityContractTest
[ 63%] Linking CXX executable RevisionTimelineTest
[ 63%] Built target ReversibilityContractTest
[ 63%] Building CXX object tests/CMakeFiles/UndoBoundsTest.dir/src/core/UndoBoundsTest.cpp.o
[ 63%] Linking CXX executable ReversibilityUndoTest
[ 63%] Linking CXX executable UnclaimedTrackTypeTest
[ 63%] Built target RevisionTimelineTest
[ 63%] Building CXX object tests/CMakeFiles/TakeLaneCompTest.dir/src/core/TakeLaneCompTest.cpp.o
[ 63%] Built target ReversibilityUndoTest
[ 63%] Building CXX object tests/CMakeFiles/TakeLaneTest.dir/src/core/TakeLaneTest.cpp.o
[ 63%] Built target UnclaimedTrackTypeTest
[ 63%] Linking CXX executable RemotePluginAudioPortsTest
[ 63%] Linking CXX executable UndoBoundsTest
[ 63%] Built target RemotePluginAudioPortsTest
[ 63%] Linking CXX executable RemotePluginClientE2ETest
[ 63%] Linking CXX executable TakeLaneCompTest
[ 63%] Linking CXX executable TakeLaneTest
[ 63%] Built target UndoBoundsTest
[ 63%] Linking CXX executable RenderJobQueueTest
[ 63%] Built target RemotePluginClientE2ETest
[ 63%] Building CXX object tests/CMakeFiles/RoutingGraphLiveTest.dir/src/core/RoutingGraphLiveTest.cpp.o
[ 63%] Built target TakeLaneCompTest
[ 63%] Building CXX object tests/CMakeFiles/RoutingGraphScheduleTest.dir/src/core/RoutingGraphScheduleTest.cpp.o
[ 63%] Built target TakeLaneTest
[ 63%] Linking CXX executable RoutingGraphTest
[ 64%] Built target RenderJobQueueTest
[ 64%] Building CXX object tests/CMakeFiles/PdcMixerTest.dir/src/core/PdcMixerTest.cpp.o
[ 64%] Built target RoutingGraphTest
[ 64%] Building CXX object tests/CMakeFiles/PatcherCommandsTest.dir/src/core/PatcherCommandsTest.cpp.o
[ 64%] Linking CXX executable RoutingGraphLiveTest
[ 64%] Linking CXX executable RoutingGraphScheduleTest
[ 64%] Linking CXX executable PdcMixerTest
[ 64%] Built target RoutingGraphLiveTest
[ 64%] Building CXX object tests/CMakeFiles/VcaGroupTest.dir/src/core/VcaGroupTest.cpp.o
[ 64%] Built target RoutingGraphScheduleTest
[ 64%] Building CXX object tests/CMakeFiles/ControlVcaCommandsTest.dir/src/core/ControlVcaCommandsTest.cpp.o
[ 64%] Linking CXX executable PatcherCommandsTest
[ 64%] Built target PdcMixerTest
[ 64%] Building CXX object tests/CMakeFiles/ControlVcaEditGroupsTest.dir/src/core/ControlVcaEditGroupsTest.cpp.o
[ 64%] Built target PatcherCommandsTest
[ 64%] Linking CXX executable ScriptBindingsTest
[ 64%] Linking CXX executable VcaGroupTest
[ 64%] Linking CXX executable ControlVcaCommandsTest
[ 64%] Built target ScriptBindingsTest
[ 64%] Building CXX object tests/CMakeFiles/ScriptDawBindingTest.dir/src/core/ScriptDawBindingTest.cpp.o
[ 64%] Linking CXX executable ControlVcaEditGroupsTest
[ 64%] Built target VcaGroupTest
[ 64%] Linking CXX executable ScriptEngineTest
[ 64%] Built target ControlVcaCommandsTest
[ 64%] Linking CXX executable ScriptClockTest
[ 64%] Built target ControlVcaEditGroupsTest
[ 64%] Building CXX object tests/CMakeFiles/ScriptMemoryBudgetTest.dir/src/core/ScriptMemoryBudgetTest.cpp.o
[ 64%] Built target ScriptEngineTest
[ 64%] Linking CXX executable ScriptStabilisationTest
[ 64%] Built target ScriptClockTest
[ 64%] Linking CXX executable SlideNotesTest
[ 65%] Linking CXX executable ScriptDawBindingTest
[ 65%] Built target ScriptStabilisationTest
[ 65%] Building CXX object tests/CMakeFiles/SmfInterchangeTest.dir/src/core/SmfInterchangeTest.cpp.o
[ 65%] Built target SlideNotesTest
[ 65%] Linking CXX executable ScriptMemoryBudgetTest
[ 65%] Building CXX object tests/CMakeFiles/SmfInterchangeRoundTripTest.dir/src/core/SmfInterchangeRoundTripTest.cpp.o
[ 65%] Built target ScriptDawBindingTest
[ 65%] Building CXX object tests/CMakeFiles/DawProjectInterchangeRoundTripTest.dir/src/core/DawProjectInterchangeRoundTripTest.cpp.o
[ 65%] Built target ScriptMemoryBudgetTest
[ 65%] Building CXX object tests/CMakeFiles/StableTrackIdsTest.dir/src/core/StableTrackIdsTest.cpp.o
[ 65%] Linking CXX executable SmfInterchangeTest
[ 65%] Linking CXX executable SmfInterchangeRoundTripTest
[ 65%] Linking CXX executable DawProjectInterchangeRoundTripTest
[ 65%] Built target SmfInterchangeTest
[ 65%] Building CXX object tests/CMakeFiles/ProjectRevIdsTest.dir/src/core/ProjectRevIdsTest.cpp.o
[ 65%] Built target SmfInterchangeRoundTripTest
[ 66%] Building CXX object tests/CMakeFiles/ProvenanceSectionTest.dir/ProvenanceSectionTest_autogen/mocs_compilation.cpp.o
[ 66%] Building CXX object tests/CMakeFiles/ProvenanceSectionTest.dir/src/core/ProvenanceSectionTest.cpp.o
[ 67%] Linking CXX executable StableTrackIdsTest
[ 67%] Built target DawProjectInterchangeRoundTripTest
[ 67%] Linking CXX executable StemExportTest
[ 67%] Built target StableTrackIdsTest
[ 67%] Building CXX object tests/CMakeFiles/TelemetryTest.dir/src/core/TelemetryTest.cpp.o
[ 67%] Built target StemExportTest
[ 67%] Linking CXX executable TelemetryTransportTest
[ 67%] Linking CXX executable ProjectRevIdsTest
[ 67%] Linking CXX executable ProvenanceSectionTest
[ 67%] Built target TelemetryTransportTest
[ 67%] Linking CXX executable TempoMapPersistenceTest
[ 67%] Linking CXX executable TelemetryTest
[ 67%] Built target ProjectRevIdsTest
[ 67%] Linking CXX executable TempoMapTest
[ 67%] Built target ProvenanceSectionTest
[ 67%] Linking CXX executable TimelineTest
[ 67%] Built target TempoMapPersistenceTest
[ 67%] Building CXX object tests/CMakeFiles/TrackFolderTest.dir/src/core/TrackFolderTest.cpp.o
[ 67%] Built target TelemetryTest
[ 67%] Linking CXX executable TwoTrackRecordingHarness
[ 67%] Built target TempoMapTest
[ 67%] Linking CXX executable WarpMarkersTest
[ 67%] Built target TimelineTest
[ 67%] Linking CXX executable FocusDeskPaneTest
[ 67%] Built target TwoTrackRecordingHarness
[ 67%] Building CXX object tests/CMakeFiles/WcagContrastTest.dir/WcagContrastTest_autogen/mocs_compilation.cpp.o
[ 67%] Building CXX object tests/CMakeFiles/WcagContrastTest.dir/src/gui/WcagContrastTest.cpp.o
[ 67%] Built target WarpMarkersTest
[ 67%] Building CXX object tests/CMakeFiles/AccessibilityHelperTest.dir/AccessibilityHelperTest_autogen/mocs_compilation.cpp.o
[ 67%] Building CXX object tests/CMakeFiles/AccessibilityHelperTest.dir/src/gui/AccessibilityHelperTest.cpp.o
[ 67%] Linking CXX executable TrackFolderTest
[ 67%] Built target FocusDeskPaneTest
[ 67%] Linking CXX executable FocusDeskTest
[ 67%] Built target TrackFolderTest
[ 67%] Linking CXX executable MidiLearnGuiTest
[ 68%] Built target FocusDeskTest
[ 68%] Linking CXX executable AutomationTrackTest
[ 68%] Linking CXX executable WcagContrastTest
[ 68%] Linking CXX executable AccessibilityHelperTest
[ 68%] Built target MidiLearnGuiTest
[ 68%] Building CXX object tests/CMakeFiles/SampleClipStretchTest.dir/src/tracks/SampleClipStretchTest.cpp.o
[ 68%] Built target AutomationTrackTest
[ 68%] Linking CXX executable SampleClipWarpTest
[ 68%] Built target WcagContrastTest
[ 68%] Linking CXX executable SampleClipWindowTest
[ 68%] Built target AccessibilityHelperTest
[ 69%] Linking CXX executable SessionModelTest
[ 69%] Built target SampleClipWarpTest
[ 69%] Linking CXX executable SessionSchedulerTest
[ 69%] Built target SampleClipWindowTest
[ 69%] Linking CXX executable SessionSchedulerRenderTest
[ 69%] Built target SessionModelTest
[ 69%] Linking CXX executable SessionFollowTest
[ 69%] Linking CXX executable SampleClipStretchTest
[ 69%] Built target SessionSchedulerTest
[ 69%] Linking CXX executable SessionArrangementRecordTest
[ 69%] Built target SessionSchedulerRenderTest
[ 69%] Built target partc_ref_amplifier_autogen_timestamp_deps
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
[ 69%] Built target partc_ref_vibedstrings_autogen_timestamp_deps
[ 69%] Built target partc_ref_kicker_autogen_timestamp_deps
[ 69%] Built target partc_ref_tripleoscillator_autogen_timestamp_deps
[ 69%] Built target partc_ref_monstro_autogen_timestamp_deps
[ 69%] Built target SessionFollowTest
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
[ 69%] Built target SampleClipStretchTest
[ 69%] Built target synthetic_audio_plugin_autogen_timestamp_deps
[ 69%] Linking CXX shared module ../libaudiofileprocessor.so
[ 69%] Built target SessionArrangementRecordTest
[ 69%] Linking CXX shared module ../libkicker.so
[ 69%] Built target audiofileprocessor
[ 69%] Linking CXX shared module ../libtripleoscillator.so
[ 70%] Built target kicker
[ 70%] Linking CXX shared module ../libamplifier.so
[ 70%] Built target tripleoscillator
[ 70%] Linking CXX shared module ../libbassbooster.so
[ 70%] Built target amplifier
[ 70%] Linking CXX shared module ../libbitinvader.so
[ 70%] Built target WasmAbiConformanceTest
[ 70%] Linking CXX shared module ../libbitcrush.so
[ 70%] Built target WasmSandboxTest
[ 70%] Linking CXX shared library ../libcarlabase.so
[ 70%] Built target bassbooster
[ 70%] Linking CXX shared module ../libcompressor.so
[ 71%] Built target bitinvader
[ 71%] Linking CXX shared module ../libcrossovereq.so
[ 71%] Built target bitcrush
[ 71%] Linking CXX shared module ../libdelay.so
[ 71%] Built target carlabase
[ 71%] Linking CXX shared module ../libdispersion.so
[ 71%] Built target compressor
[ 71%] Linking CXX shared module ../libdualfilter.so
[ 72%] Built target dispersion
[ 72%] Built target crossovereq
[ 72%] Linking CXX shared module ../libdynamicsprocessor.so
[ 72%] Built target delay
[ 72%] Linking CXX shared module ../libeq.so
[ 72%] Linking CXX shared module ../libflanger.so
[ 72%] Built target dualfilter
[ 72%] Linking CXX shared module ../libfrequencyshifter.so
[ 73%] Built target dynamicsprocessor
[ 73%] Linking CXX shared module ../libgranularpitchshifter.so
[ 73%] Built target flanger
[ 73%] Built target eq
[ 73%] Linking CXX shared module ../libhydrogenimport.so
[ 73%] Linking CXX shared module ../libladspabrowser.so
[ 73%] Built target hydrogenimport
[ 73%] Linking CXX shared module ../libladspaeffect.so
[ 73%] Built target frequencyshifter
[ 73%] Linking CXX shared module ../liblomm.so
[ 74%] Built target granularpitchshifter
[ 74%] Linking CXX shared module ../liblv2effect.so
[ 74%] Built target ladspabrowser
[ 74%] Linking CXX shared module ../liblv2instrument.so
[ 75%] Built target ladspaeffect
[ 75%] Linking CXX shared module ../liblb302.so
[ 75%] Built target lv2effect
[ 75%] Built target lomm
[ 75%] Built target lv2instrument
[ 75%] Linking CXX shared module ../libmidiimport.so
[ 75%] Linking CXX shared module ../libmidiexport.so
[ 75%] Linking CXX shared module ../libmultitapecho.so
[ 75%] Built target midiexport
[ 75%] Linking CXX shared module ../libmonstro.so
[ 75%] Built target lb302
[ 75%] Linking CXX shared module ../libnes.so
[ 75%] Built target midiimport
[ 75%] Linking CXX shared module ../libneuralamp.so
[ 75%] Built target multitapecho
[ 75%] Linking CXX shared module ../libopulenz.so
[ 76%] Built target monstro
[ 76%] Linking CXX shared module ../liborganic.so
[ 77%] Built target nes
[ 77%] Linking CXX shared module ../liboscilloscope.so
[ 77%] Built target neuralamp
[ 77%] Linking CXX shared module ../libfreeboy.so
[ 77%] Built target opulenz
[ 77%] Linking CXX shared module ../libpatman.so
[ 78%] Built target organic
[ 78%] Linking CXX shared module ../libpeakcontrollereffect.so
[ 78%] Built target oscilloscope
[ 78%] Built target gigplayer_autogen_timestamp_deps
[ 79%] Built target freeboy
[ 79%] Linking CXX shared module ../libreverbsc.so
[ 79%] Linking CXX shared module ../librnnoisedenoiser.so
[ 79%] Built target patman
[ 79%] Built target sf2player_autogen_timestamp_deps
[ 80%] Linking CXX shared module ../libsfxr.so
[ 80%] Built target peakcontrollereffect
[ 80%] Linking CXX shared module ../libsid.so
[ 80%] Built target rnnoisedenoiser
[ 80%] Built target reverbsc
[ 80%] Linking CXX shared module ../libslewdistortion.so
[ 80%] Linking CXX shared module ../libslicert.so
[ 80%] Built target sfxr
[ 80%] Linking CXX shared module ../libanalyzer.so
[ 80%] Built target sid
[ 80%] Linking CXX shared module ../libstereoenhancer.so
[ 81%] Built target slewdistortion
[ 81%] Linking CXX shared module ../libstereomatrix.so
[ 81%] Built target slicert
[ 81%] Linking CXX shared module ../libtaptempo.so
[ 82%] Built target analyzer
[ 83%] Built target stereoenhancer
[ 83%] Linking CXX shared library ../../libvstbase.so
[ 83%] Linking CXX shared module ../libwasm_effect.so
[ 83%] Built target stereomatrix
[ 83%] Linking CXX shared module ../libwatsyn.so
[ 83%] Built target taptempo
[ 83%] Linking CXX shared module ../libwaveshaper.so
[ 84%] Built target vstbase
[ 84%] Built target wasm_effect
[ 84%] Linking CXX shared module ../libvectorscope.so
[ 84%] Linking CXX shared module ../libvibedstrings.so
[ 84%] Built target watsyn
[ 84%] Linking CXX shared module ../libxpressive.so
[ 85%] Built target waveshaper
[ 85%] Linking CXX shared module ../libzynaddsubfx.so
[ 85%] Built target vibedstrings
[ 86%] Built target vectorscope
[ 86%] Built target SafeStartTest_autogen_timestamp_deps
[ 86%] Built target SafeStartLoadPathTest_autogen_timestamp_deps
[ 86%] Built target PhaseDSidechainTest_autogen_timestamp_deps
[ 86%] Automatic MOC for target mpe_test_consumer
[ 86%] Built target PluginScanCacheTest_autogen_timestamp_deps
[ 86%] Built target mpe_test_consumer_autogen
[ 86%] Automatic MOC for target partc_ref_amplifier
[ 86%] Automatic MOC for target partc_ref_bassbooster
[ 86%] Built target partc_ref_amplifier_autogen
[ 86%] Built target partc_ref_bassbooster_autogen
[ 86%] Automatic MOC for target partc_ref_bitcrush
[ 86%] Automatic MOC for target partc_ref_dualfilter
[ 86%] Built target partc_ref_bitcrush_autogen
[ 86%] Built target partc_ref_dualfilter_autogen
[ 86%] Automatic MOC for target partc_ref_waveshaper
[ 86%] Automatic MOC for target partc_ref_flanger
[ 86%] Built target partc_ref_waveshaper_autogen
[ 86%] Built target partc_ref_flanger_autogen
[ 86%] Built target zynaddsubfx
[ 86%] Automatic MOC for target partc_ref_delay
[ 86%] Automatic MOC for target partc_ref_compressor
[ 86%] Automatic MOC for target partc_ref_crossovereq
[ 86%] Built target partc_ref_delay_autogen
[ 86%] Built target partc_ref_compressor_autogen
[ 86%] Built target partc_ref_crossovereq_autogen
[ 86%] Automatic MOC for target partc_ref_dynamicsprocessor
[ 86%] Automatic MOC for target partc_ref_lomm
[ 86%] Automatic MOC for target partc_ref_multitapecho
[ 86%] Built target partc_ref_dynamicsprocessor_autogen
[ 86%] Built target partc_ref_lomm_autogen
[ 86%] Built target partc_ref_multitapecho_autogen
[ 86%] Automatic MOC for target partc_ref_reverbsc
[ 86%] Automatic MOC for target partc_ref_stereoenhancer
[ 86%] Automatic MOC for target partc_ref_stereomatrix
[ 86%] Built target partc_ref_reverbsc_autogen
[ 86%] Built target partc_ref_stereoenhancer_autogen
[ 86%] Built target partc_ref_stereomatrix_autogen
[ 86%] Automatic MOC for target partc_ref_dispersion
[ 86%] Automatic MOC for target partc_ref_vectorscope
[ 86%] Automatic MOC for target partc_ref_analyzer
[ 86%] Built target partc_ref_dispersion_autogen
[ 86%] Built target partc_ref_vectorscope_autogen
[ 86%] Built target partc_ref_analyzer_autogen
[ 86%] Automatic MOC for target partc_ref_granularpitchshifter
[ 86%] Automatic MOC for target partc_ref_eq
[ 86%] Automatic MOC for target partc_ref_freeboy
[ 86%] Built target partc_ref_granularpitchshifter_autogen
[ 86%] Built target partc_ref_freeboy_autogen
[ 86%] Built target partc_ref_eq_autogen
[ 86%] Automatic MOC for target partc_ref_nes
[ 86%] Automatic MOC for target partc_ref_sid
[ 86%] Automatic MOC for target partc_ref_opulenz
[ 86%] Built target partc_ref_nes_autogen
[ 86%] Built target partc_ref_sid_autogen
[ 86%] Built target partc_ref_opulenz_autogen
[ 86%] Automatic MOC for target partc_ref_sfxr
[ 86%] Automatic MOC for target partc_ref_bitinvader
[ 86%] Automatic MOC for target partc_ref_watsyn
[ 86%] Built target partc_ref_sfxr_autogen
[ 86%] Built target xpressive
[ 86%] Built target partc_ref_bitinvader_autogen
[ 86%] Built target partc_ref_watsyn_autogen
[ 86%] Automatic MOC for target partc_ref_xpressive
[ 86%] Automatic MOC for target partc_ref_vibedstrings
[ 86%] Automatic MOC for target partc_ref_kicker
[ 86%] Automatic MOC for target partc_ref_tripleoscillator
[ 86%] Built target partc_ref_xpressive_autogen
[ 86%] Built target partc_ref_vibedstrings_autogen
[ 86%] Built target partc_ref_kicker_autogen
[ 86%] Built target partc_ref_tripleoscillator_autogen
[ 87%] Automatic MOC for target partc_ref_organic
[ 87%] Automatic MOC for target partc_ref_monstro
[ 87%] Automatic MOC for target partc_ref_audiofileprocessor
[ 87%] Built target partc_ref_organic_autogen
[ 87%] Automatic MOC for target partc_ref_lb302
[ 87%] Built target partc_ref_monstro_autogen
[ 87%] Built target partc_ref_audiofileprocessor_autogen
[ 87%] Built target partc_ref_lb302_autogen
[ 87%] Automatic MOC for target partc_ref_ladspaeffect
[ 87%] Automatic MOC for target partc_ref_frequencyshifter
[ 87%] Automatic MOC for target partc_ref_oscilloscope
[ 87%] Built target partc_ref_ladspaeffect_autogen
[ 87%] Automatic MOC for target partc_ref_slewdistortion
[ 87%] Built target partc_ref_frequencyshifter_autogen
[ 87%] Built target partc_ref_oscilloscope_autogen
[ 87%] Built target partc_ref_slewdistortion_autogen
[ 87%] Automatic MOC for target partc_ref_lv2effect
[ 87%] Automatic MOC for target partc_ref_lv2instrument
[ 87%] Automatic MOC and UIC for target partc_ref_sf2player
[ 87%] Built target partc_ref_lv2instrument_autogen
[ 87%] Built target partc_ref_lv2effect_autogen
[ 87%] Automatic MOC and UIC for target partc_ref_gigplayer
[ 87%] Built target partc_ref_sf2player_autogen
[ 87%] Built target ZynSeparateProcessTest_autogen_timestamp_deps
[ 87%] Automatic MOC for target synthetic_audio_plugin
[ 87%] Built target partc_ref_gigplayer_autogen
[ 87%] Linking CXX shared module ../libcarlapatchbay.so
[ 87%] Linking CXX shared module ../libcarlarack.so
[ 87%] Built target synthetic_audio_plugin_autogen
[ 87%] Built target gigplayer_autogen
[ 87%] Built target sf2player_autogen
[ 87%] Linking CXX shared module ../libvestige.so
[ 87%] Linking CXX shared module ../libvsteffect.so
[ 87%] Built target carlapatchbay
[ 87%] Built target carlarack
[ 87%] Built target OutOfProcessHostTest_autogen_timestamp_deps
[ 87%] Built target OutOfProcessHostClientLoopTest_autogen_timestamp_deps
[ 87%] Automatic MOC for target SafeStartTest
[ 87%] Automatic MOC for target SafeStartLoadPathTest
[ 87%] Built target SafeStartLoadPathTest_autogen
[ 87%] Built target SafeStartTest_autogen
[ 87%] Linking CXX shared module mpe-test-consumer/libmpe_test_consumer.so
[ 87%] Built target vestige
[ 87%] Automatic MOC for target PhaseDSidechainTest
[ 87%] Built target vsteffect
[ 87%] Automatic MOC for target PluginScanCacheTest
[ 87%] Linking CXX shared module partc_ref_amplifier.so
[ 87%] Built target PluginScanCacheTest_autogen
[ 87%] Built target mpe_test_consumer
[ 87%] Linking CXX shared module partc_ref_bassbooster.so
[ 87%] Linking CXX shared module partc_ref_bitcrush.so
[ 87%] Built target partc_ref_amplifier
[ 87%] Linking CXX shared module partc_ref_dualfilter.so
[ 87%] Built target partc_ref_bassbooster
[ 87%] Built target partc_ref_bitcrush
[ 87%] Linking CXX shared module partc_ref_waveshaper.so
[ 87%] Linking CXX shared module partc_ref_flanger.so
[ 87%] Built target PhaseDSidechainTest_autogen
[ 87%] Linking CXX shared module partc_ref_delay.so
[ 88%] Built target partc_ref_dualfilter
[ 88%] Built target partc_ref_waveshaper
[ 88%] Linking CXX shared module partc_ref_compressor.so
[ 88%] Built target partc_ref_flanger
[ 88%] Linking CXX shared module partc_ref_crossovereq.so
[ 88%] Linking CXX shared module partc_ref_dynamicsprocessor.so
[ 88%] Built target partc_ref_delay
[ 88%] Linking CXX shared module partc_ref_lomm.so
[ 89%] Built target partc_ref_compressor
[ 89%] Built target partc_ref_crossovereq
[ 89%] Built target partc_ref_dynamicsprocessor
[ 89%] Linking CXX shared module partc_ref_multitapecho.so
[ 89%] Linking CXX shared module partc_ref_reverbsc.so
[ 89%] Linking CXX shared module partc_ref_stereoenhancer.so
[ 89%] Built target partc_ref_lomm
[ 89%] Linking CXX shared module partc_ref_stereomatrix.so
[ 89%] Built target partc_ref_stereoenhancer
[ 89%] Linking CXX shared module partc_ref_dispersion.so
[ 89%] Built target partc_ref_multitapecho
[ 89%] Built target partc_ref_reverbsc
[ 89%] Linking CXX shared module partc_ref_vectorscope.so
[ 90%] Linking CXX shared module partc_ref_analyzer.so
[ 90%] Built target partc_ref_stereomatrix
[ 90%] Linking CXX shared module partc_ref_granularpitchshifter.so
[ 90%] Built target partc_ref_dispersion
[ 90%] Linking CXX shared module partc_ref_eq.so
[ 91%] Built target partc_ref_vectorscope
[ 91%] Linking CXX shared module partc_ref_freeboy.so
[ 91%] Built target partc_ref_analyzer
[ 91%] Built target partc_ref_granularpitchshifter
[ 91%] Linking CXX shared module partc_ref_nes.so
[ 91%] Linking CXX shared module partc_ref_sid.so
[ 91%] Built target partc_ref_eq
[ 91%] Linking CXX shared module partc_ref_opulenz.so
[ 92%] Built target partc_ref_freeboy
[ 92%] Built target partc_ref_nes
[ 92%] Linking CXX shared module partc_ref_sfxr.so
[ 93%] Built target partc_ref_sid
[ 93%] Linking CXX shared module partc_ref_bitinvader.so
[ 93%] Linking CXX shared module partc_ref_watsyn.so
[ 93%] Built target partc_ref_opulenz
[ 93%] Linking CXX shared module partc_ref_xpressive.so
[ 93%] Built target partc_ref_sfxr
[ 93%] Built target partc_ref_bitinvader
[ 93%] Built target partc_ref_watsyn
[ 93%] Linking CXX shared module partc_ref_kicker.so
[ 93%] Linking CXX shared module partc_ref_vibedstrings.so
[ 93%] Linking CXX shared module partc_ref_tripleoscillator.so
[ 93%] Built target partc_ref_vibedstrings
[ 93%] Built target partc_ref_kicker
[ 93%] Linking CXX shared module partc_ref_monstro.so
[ 93%] Linking CXX shared module partc_ref_organic.so
[ 93%] Built target partc_ref_tripleoscillator
[ 93%] Linking CXX shared module partc_ref_audiofileprocessor.so
[ 93%] Built target partc_ref_monstro
[ 93%] Built target partc_ref_organic
[ 93%] Linking CXX shared module partc_ref_lb302.so
[ 93%] Linking CXX shared module partc_ref_ladspaeffect.so
[ 93%] Built target partc_ref_audiofileprocessor
[ 94%] Built target partc_ref_xpressive
[ 94%] Linking CXX shared module partc_ref_frequencyshifter.so
[ 94%] Linking CXX shared module partc_ref_oscilloscope.so
[ 95%] Built target partc_ref_ladspaeffect
[ 95%] Built target partc_ref_lb302
[ 95%] Linking CXX shared module partc_ref_slewdistortion.so
[ 95%] Linking CXX shared module partc_ref_lv2effect.so
[ 95%] Built target partc_ref_frequencyshifter
[ 95%] Built target partc_ref_oscilloscope
[ 95%] Linking CXX shared module partc_ref_lv2instrument.so
[ 95%] Linking CXX shared module partc_ref_sf2player.so
[ 95%] Built target partc_ref_lv2effect
[ 95%] Linking CXX shared module partc_ref_gigplayer.so
[ 95%] Built target partc_ref_slewdistortion
[ 96%] Built target partc_ref_lv2instrument
[ 96%] Linking CXX shared module synthetic_audio_plugin.so
[ 96%] Automatic MOC for target ZynSeparateProcessTest
[ 96%] Built target ZynSeparateProcessTest_autogen
[ 96%] Linking CXX shared module ../libgigplayer.so
[ 96%] Built target partc_ref_sf2player
[ 96%] Linking CXX shared module ../libsf2player.so
[ 96%] Built target synthetic_audio_plugin
[ 96%] Automatic MOC for target OutOfProcessHostTest
[ 96%] Built target partc_ref_gigplayer
[ 96%] Automatic MOC for target OutOfProcessHostClientLoopTest
[ 97%] Built target gigplayer
[ 97%] Linking CXX executable SafeStartTest
[ 97%] Built target sf2player
[ 97%] Linking CXX executable SafeStartLoadPathTest
[ 97%] Built target OutOfProcessHostTest_autogen
[ 97%] Built target MpePlaybackTest_autogen_timestamp_deps
[ 97%] Building CXX object tests/CMakeFiles/PhaseDSidechainTest.dir/src/core/PhaseDSidechainTest.cpp.o
[ 97%] Built target OutOfProcessHostClientLoopTest_autogen
[ 97%] Linking CXX executable PluginScanCacheTest
[ 97%] Built target SafeStartLoadPathTest
[ 97%] Built target SafeStartTest
[ 97%] Built target PluginPortsMigrationReference_autogen_timestamp_deps
[ 97%] Built target AudioPluginTest_autogen_timestamp_deps
[ 97%] Linking CXX executable ZynSeparateProcessTest
[ 97%] Building CXX object tests/CMakeFiles/OutOfProcessHostTest.dir/src/core/OutOfProcessHostTest.cpp.o
[ 97%] Built target PluginScanCacheTest
[ 97%] Building CXX object tests/CMakeFiles/OutOfProcessHostClientLoopTest.dir/src/core/OutOfProcessHostClientLoopTest.cpp.o
[ 97%] Built target ZynSeparateProcessTest
[ 97%] Automatic MOC for target MpePlaybackTest
[ 97%] Built target MpePlaybackTest_autogen
[ 97%] Automatic MOC for target PluginPortsMigrationReference
[ 97%] Built target PluginPortsMigrationReference_autogen
[ 97%] Automatic MOC for target AudioPluginTest
[ 97%] Built target AudioPluginTest_autogen
[ 98%] Linking CXX executable MpePlaybackTest
[ 98%] Linking CXX executable PhaseDSidechainTest
[ 98%] Built target PhaseDSidechainTest
[ 98%] Built target MpePlaybackTest
[ 98%] Linking CXX executable PluginPortsMigrationReference
[ 98%] Linking CXX executable AudioPluginTest
[ 98%] Linking CXX executable OutOfProcessHostTest
[ 98%] Linking CXX executable OutOfProcessHostClientLoopTest
[ 98%] Built target PluginPortsMigrationReference
[ 99%] Built target AudioPluginTest
[ 99%] Built target PluginPortsMigrationTest_autogen_timestamp_deps
[ 99%] Built target OutOfProcessHostTest
[ 99%] Automatic MOC for target PluginPortsMigrationTest
[ 99%] Built target PluginPortsMigrationTest_autogen
[100%] Built target OutOfProcessHostClientLoopTest
[100%] Linking CXX executable PluginPortsMigrationTest
[100%] Built target PluginPortsMigrationTest
BUILD=0
