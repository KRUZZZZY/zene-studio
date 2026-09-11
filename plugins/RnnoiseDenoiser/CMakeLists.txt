INCLUDE(BuildPlugin)

SET(RNNOISE_SOURCES
	rnnoise/denoise.c
	rnnoise/rnn.c
	rnnoise/pitch.c
	rnnoise/kiss_fft.c
	rnnoise/celt_lpc.c
	rnnoise/nnet.c
	rnnoise/nnet_default.c
	rnnoise/parse_lpcnet_weights.c
	rnnoise/rnnoise_data.c
	rnnoise/rnnoise_tables.c
)

# Vendored RNNoise (MIT, unmodified upstream C) builds as its OWN target carrying the
# project's third-party flags — the treatment gme, zynaddsubfx_* and src/3rdparty already
# get (cmake/modules/ErrorFlags.cmake switches on the target's SYSTEM property).
#
# Compiling it inside the plugin target made every RNNoise diagnostic fatal under
# -DUSE_WERROR / /WX, and they arrived one file at a time: -Werror=cpp on the supported
# #warning in nnet.h (linux-x86_64), then C4305 in denoise.c, then C4244 in vec.h via
# nnet.c and nnet_default.c (msvc-x64). Suppressing diagnostics one at a time is a losing
# game against code this repo does not maintain, and it silently widens the suppression to
# the plugin's own sources. Isolating the vendored code keeps that plugin's C++ under the
# normal -Wall/-Werror (/WX) policy while the vendored C compiles warning-quiet.
ADD_LIBRARY(rnnoise_vendored STATIC ${RNNOISE_SOURCES})
SET_TARGET_PROPERTIES(rnnoise_vendored PROPERTIES
	SYSTEM TRUE
	POSITION_INDEPENDENT_CODE ON
)
TARGET_INCLUDE_DIRECTORIES(rnnoise_vendored PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/rnnoise")

BUILD_PLUGIN(rnnoisedenoiser
	RnnoiseDenoiserEffect.cpp
	RnnoiseDenoiserControls.cpp
	RnnoiseDenoiserControlDialog.cpp
	MOCFILES
		RnnoiseDenoiserEffect.h
		RnnoiseDenoiserControls.h
		RnnoiseDenoiserControlDialog.h
	EMBEDDED_RESOURCES artwork.svg
)

# The plugin includes "rnnoise.h" directly, so the vendored include directory is marked
# SYSTEM for it too — otherwise vendored headers leak warnings into the fork's own TU
# (the same reason the libgig includes are SYSTEM in the GigPlayer targets).
TARGET_INCLUDE_DIRECTORIES(rnnoisedenoiser SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/rnnoise")

TARGET_LINK_LIBRARIES(rnnoisedenoiser rnnoise_vendored ${CMAKE_DL_LIBS})
# Same libm guard as the other plugins: Unix-only, and MSVC keeps the math routines in
# the CRT (asking for m.lib fails the link).
IF(NOT MSVC)
	TARGET_LINK_LIBRARIES(rnnoisedenoiser m)
ENDIF()
