INCLUDE(BuildPlugin)
INCLUDE(CheckCXXSourceCompiles)
INCLUDE(CheckCXXCompilerFlag)

# SFZ Player (Q050-484): the sfizz SFZ sampler engine (BSD-2-Clause, sfizz 1.2.3) built from the sources vendored
# in sfizz/ - on every platform Zene ships, with no system sfizz, abseil or other dependency beyond libsndfile
# (which Zene already requires). See LICENSE-NOTICE.md for the inventory and what was left out.

FIND_PACKAGE(Threads REQUIRED)

SET(SFIZZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sfizz")

# --- Abseil (Apache-2.0) ----------------------------------------------------------------------------------------
# Built as sfizz's own build does it: static, position independent, hidden symbols (so the copy inside this
# module can never be bound to another copy in the process), C++17 (what sfizz is written and tested against;
# the player's own sources are C++20 and only ever see sfizz.hpp, which exposes no abseil type).
FUNCTION(SFZPLAYER_ADD_ABSEIL)
	SET(CMAKE_CXX_STANDARD 17)
	SET(BUILD_SHARED_LIBS OFF)
	SET(CMAKE_POSITION_INDEPENDENT_CODE ON)
	SET(CMAKE_CXX_VISIBILITY_PRESET hidden)
	SET(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
	SET(ABSL_PROPAGATE_CXX_STD ON)
	SET(ABSL_ENABLE_INSTALL OFF)
	SET(ABSL_BUILD_TESTING OFF)
	SET(ABSL_USE_GOOGLETEST_HEAD OFF)
	ADD_SUBDIRECTORY("${SFIZZ_DIR}/external/abseil-cpp" "${CMAKE_CURRENT_BINARY_DIR}/abseil" EXCLUDE_FROM_ALL)
ENDFUNCTION()
SFZPLAYER_ADD_ABSEIL()

# --- sfizz ------------------------------------------------------------------------------------------------------
SET(SFIZZ_SRC "${SFIZZ_DIR}/src")
SET(SFIZZ_EXT "${SFIZZ_DIR}/external")

SET(SFIZZ_SOURCES
	sfizz/Synth.cpp
	sfizz/FileId.cpp
	sfizz/FilePool.cpp
	sfizz/FileMetadata.cpp
	sfizz/AudioReader.cpp
	sfizz/FilterPool.cpp
	sfizz/EQPool.cpp
	sfizz/RegionStateful.cpp
	sfizz/Region.cpp
	sfizz/Voice.cpp
	sfizz/ScopedFTZ.cpp
	sfizz/MidiState.cpp
	sfizz/Oversampler.cpp
	sfizz/ADSREnvelope.cpp
	sfizz/SfzFilter.cpp
	sfizz/Curve.cpp
	sfizz/Smoothers.cpp
	sfizz/Wavetables.cpp
	sfizz/Tuning.cpp
	sfizz/RegionSet.cpp
	sfizz/PolyphonyGroup.cpp
	sfizz/VoiceManager.cpp
	sfizz/VoiceStealing.cpp
	sfizz/RTSemaphore.cpp
	sfizz/Panning.cpp
	sfizz/Effects.cpp
	sfizz/LFO.cpp
	sfizz/LFODescription.cpp
	sfizz/PowerFollower.cpp
	sfizz/FlexEGDescription.cpp
	sfizz/FlexEnvelope.cpp
	sfizz/BeatClock.cpp
	sfizz/Metronome.cpp
	sfizz/SynthMessaging.cpp
	sfizz/WindowedSinc.cpp
	sfizz/Interpolators.cpp
	sfizz/Layer.cpp
	sfizz/Resources.cpp
	sfizz/modulations/ModId.cpp
	sfizz/modulations/ModKey.cpp
	sfizz/modulations/ModKeyHash.cpp
	sfizz/modulations/ModMatrix.cpp
	sfizz/modulations/sources/ADSREnvelope.cpp
	sfizz/modulations/sources/ChannelAftertouch.cpp
	sfizz/modulations/sources/PolyAftertouch.cpp
	sfizz/modulations/sources/Controller.cpp
	sfizz/modulations/sources/FlexEnvelope.cpp
	sfizz/modulations/sources/LFO.cpp
	sfizz/effects/Nothing.cpp
	sfizz/effects/Filter.cpp
	sfizz/effects/Eq.cpp
	sfizz/effects/Apan.cpp
	sfizz/effects/Lofi.cpp
	sfizz/effects/Limiter.cpp
	sfizz/effects/Compressor.cpp
	sfizz/effects/Gate.cpp
	sfizz/effects/Disto.cpp
	sfizz/effects/Strings.cpp
	sfizz/effects/Fverb.cpp
	sfizz/effects/Rectify.cpp
	sfizz/effects/Gain.cpp
	sfizz/effects/Width.cpp
	sfizz/effects/impl/ResonantString.cpp
	sfizz/effects/impl/ResonantStringSSE.cpp
	sfizz/effects/impl/ResonantStringAVX.cpp
	sfizz/effects/impl/ResonantArray.cpp
	sfizz/effects/impl/ResonantArraySSE.cpp
	sfizz/effects/impl/ResonantArrayAVX.cpp
	sfizz/utility/c++17/AlignedMemorySupport.cpp
	sfizz/SIMDHelpers.cpp
	sfizz/simd/HelpersNEON.cpp
	sfizz/simd/HelpersSSE.cpp
	sfizz/simd/HelpersAVX.cpp
	# the SFZ parser
	sfizz/Opcode.cpp
	sfizz/Defaults.cpp
	sfizz/OpcodeCleanup.cpp
	sfizz/parser/Parser.cpp
	sfizz/parser/ParserPrivate.cpp
	# OSC-style messaging
	sfizz/Messaging.cpp
	# the lock-free spin mutex sfizz guards its render callback with
	sfizz/utility/spin_mutex/spin_mutex.cpp
	sfizz/utility/spin_mutex/SpinMutex.cpp
	# the public API
	sfizz/sfizz_wrapper.cpp
	sfizz/sfizz.cpp)
LIST(TRANSFORM SFIZZ_SOURCES PREPEND "${SFIZZ_SRC}/")

SET(SFIZZ_C_SOURCES
	"${SFIZZ_SRC}/external/kiss_fft/kiss_fft.c"
	"${SFIZZ_SRC}/external/kiss_fft/kiss_fftr.c"
	"${SFIZZ_EXT}/cephes/src/chbevl.c"
	"${SFIZZ_EXT}/cephes/src/i0.c"
	# st_audiofile in its libsndfile configuration: Zene already links libsndfile for every platform's sample decoding
	"${SFIZZ_EXT}/st_audiofile/src/st_audiofile.c"
	"${SFIZZ_EXT}/st_audiofile/src/st_audiofile_common.c"
	"${SFIZZ_EXT}/st_audiofile/src/st_audiofile_sndfile.c")

SET(SFIZZ_THIRDPARTY_CXX_SOURCES
	"${SFIZZ_SRC}/external/pugixml/src/pugixml.cpp"
	"${SFIZZ_SRC}/external/spline/spline/spline.cpp"
	"${SFIZZ_SRC}/external/tunings/src/Tunings.cpp"
	"${SFIZZ_SRC}/external/cpuid/src/cpuid/cpuinfo.cpp"
	"${SFIZZ_SRC}/external/cpuid/src/cpuid/version.cpp"
	# ghc::filesystem's implementation, compiled once for the forward-declared header sfizz includes
	"${CMAKE_CURRENT_BINARY_DIR}/fs_std_impl.cpp")
FILE(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fs_std_impl.cpp" "#include <ghc/fs_std_impl.hpp>\n")

ADD_LIBRARY(sfzplayer_sfizz STATIC ${SFIZZ_SOURCES} ${SFIZZ_C_SOURCES} ${SFIZZ_THIRDPARTY_CXX_SOURCES})
SET_TARGET_PROPERTIES(sfzplayer_sfizz PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	C_STANDARD 99
	POSITION_INDEPENDENT_CODE ON
	CXX_VISIBILITY_PRESET hidden
	C_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	# Vendored code builds warning-quiet, like gme, rnnoise and the other third-party trees
	# (cmake/modules/ErrorFlags.cmake switches on this property).
	SYSTEM TRUE
)
TARGET_INCLUDE_DIRECTORIES(sfzplayer_sfizz
	PUBLIC "${SFIZZ_SRC}"
	PRIVATE
		"${SFIZZ_SRC}/sfizz"
		"${SFIZZ_SRC}/sfizz/utility/bit_array"
		"${SFIZZ_SRC}/sfizz/utility/spin_mutex"
		"${SFIZZ_SRC}/external/hiir"
		"${SFIZZ_SRC}/external/kiss_fft"
		"${SFIZZ_SRC}/external/pugixml/src"
		"${SFIZZ_SRC}/external/spline"
		"${SFIZZ_SRC}/external/tunings/include"
		"${SFIZZ_SRC}/external/cpuid/src"
		"${SFIZZ_SRC}/external/cpuid/platform/src"
		"${SFIZZ_EXT}/atomic_queue/include"
		"${SFIZZ_EXT}/cephes/src"
		"${SFIZZ_EXT}/filesystem/include"
		"${SFIZZ_EXT}/jsl/include"
		"${SFIZZ_EXT}/simde"
		"${SFIZZ_EXT}/st_audiofile/src"
		"${SFIZZ_EXT}/threadpool")
TARGET_COMPILE_DEFINITIONS(sfzplayer_sfizz PRIVATE
	SFIZZ_USE_SNDFILE=1
	# the one change to sfizz's own sources: no waiting and no garbage collection inside the render callback
	ZENE_SFIZZ_NO_RENDER_THREAD_WAITS=1
	ST_AUDIO_FILE_USE_SNDFILE=1
	GHC_FILESYSTEM_FWD
	# pugixml and kiss_fft are compiled with the settings sfizz gives them upstream
	$<$<BOOL:${WIN32}>:NOMINMAX>
	$<$<BOOL:${WIN32}>:_USE_MATH_DEFINES>)
TARGET_LINK_LIBRARIES(sfzplayer_sfizz PRIVATE
	absl::strings absl::span absl::flat_hash_map absl::optional absl::memory
	Threads::Threads SndFile::sndfile)
IF(NOT MSVC)
	TARGET_LINK_LIBRARIES(sfzplayer_sfizz PRIVATE m)
ENDIF()

# sfizz's per-file instruction-set flags: its AVX kernels are compiled for AVX and picked at run time by cpuid.
IF(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64|AMD64|x64|X64|i.86|x86|X86)" OR CMAKE_GENERATOR_PLATFORM MATCHES "(x64|Win32)")
	IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		SET_SOURCE_FILES_PROPERTIES(
			"${SFIZZ_SRC}/sfizz/effects/impl/ResonantStringAVX.cpp"
			"${SFIZZ_SRC}/sfizz/effects/impl/ResonantArrayAVX.cpp"
			"${SFIZZ_SRC}/sfizz/simd/HelpersAVX.cpp"
			PROPERTIES COMPILE_FLAGS "-mavx")
	ENDIF()
ENDIF()

# Fast math for the DSP, as sfizz builds its core.
IF(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	TARGET_COMPILE_OPTIONS(sfzplayer_sfizz PRIVATE -ffast-math -Wno-error)
ELSEIF(MSVC)
	TARGET_COMPILE_OPTIONS(sfzplayer_sfizz PRIVATE /fp:fast /utf-8 /Zc:__cplusplus /WX-)
ENDIF()

# Newer Clang makes -Wmissing-template-arg-list-after-template-kw an error by default, and the vendored
# atomic_queue (sfizz/external) trips it. Test the positive form so compilers without the flag are left alone.
CHECK_CXX_COMPILER_FLAG("-Wmissing-template-arg-list-after-template-kw" ZENE_HAS_WMISSING_TEMPLATE_ARG_LIST)
IF(ZENE_HAS_WMISSING_TEMPLATE_ARG_LIST)
	TARGET_COMPILE_OPTIONS(sfzplayer_sfizz PRIVATE -Wno-missing-template-arg-list-after-template-kw)
ENDIF()

# C++17 aligned new/delete: absent below macOS 10.14, where sfizz supplies its own.
CHECK_CXX_SOURCE_COMPILES("
struct Test { alignas(1024) int z; };
int main() { new Test; return 0; }
" SFZPLAYER_HAVE_CXX17_ALIGNED_NEW)
IF(NOT SFZPLAYER_HAVE_CXX17_ALIGNED_NEW)
	TARGET_COMPILE_DEFINITIONS(sfzplayer_sfizz PRIVATE SFIZZ_IMPLEMENT_CXX17_ALIGNED_NEW_SUPPORT=1)
	CHECK_CXX_COMPILER_FLAG("-faligned-allocation" SFZPLAYER_HAVE_FALIGNED_ALLOCATION)
	IF(SFZPLAYER_HAVE_FALIGNED_ALLOCATION)
		TARGET_COMPILE_OPTIONS(sfzplayer_sfizz PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-faligned-allocation>")
	ENDIF()
ENDIF()

# The vendored abseil targets are third-party too: mark every one of them, so the project's warning flags (and
# -Werror / /WX) stay on the Zene-owned code in this directory alone.
FUNCTION(SFZPLAYER_MARK_THIRD_PARTY DIR)
	GET_PROPERTY(_targets DIRECTORY "${DIR}" PROPERTY BUILDSYSTEM_TARGETS)
	FOREACH(_target IN LISTS _targets)
		SET_TARGET_PROPERTIES(${_target} PROPERTIES SYSTEM TRUE)
		GET_TARGET_PROPERTY(_type ${_target} TYPE)
		IF(MSVC AND NOT _type STREQUAL "INTERFACE_LIBRARY")
			TARGET_COMPILE_OPTIONS(${_target} PRIVATE /WX-)
		ELSEIF(NOT _type STREQUAL "INTERFACE_LIBRARY")
			TARGET_COMPILE_OPTIONS(${_target} PRIVATE -Wno-error)
		ENDIF()
	ENDFOREACH()
	GET_PROPERTY(_subdirs DIRECTORY "${DIR}" PROPERTY SUBDIRECTORIES)
	FOREACH(_subdir IN LISTS _subdirs)
		SFZPLAYER_MARK_THIRD_PARTY("${_subdir}")
	ENDFOREACH()
ENDFUNCTION()
SFZPLAYER_MARK_THIRD_PARTY("${CMAKE_CURRENT_BINARY_DIR}/abseil")

# --- the module --------------------------------------------------------------------------------------------------
BUILD_PLUGIN(sfzplayer
	SfzPlayer.cpp
	SfzPlayerView.cpp
	SfzSynth.cpp
	MOCFILES
		SfzPlayer.h
		SfzPlayerView.h
	EMBEDDED_RESOURCES logo.png
)
TARGET_LINK_LIBRARIES(sfzplayer sfzplayer_sfizz)
# sfizz.hpp is the only sfizz header the player includes; mark it third-party so its inline code stays quiet.
TARGET_INCLUDE_DIRECTORIES(sfzplayer SYSTEM PRIVATE "${SFIZZ_SRC}")
