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

INCLUDE_DIRECTORIES(BEFORE "${CMAKE_CURRENT_SOURCE_DIR}/rnnoise")

BUILD_PLUGIN(rnnoisedenoiser
	RnnoiseDenoiserEffect.cpp
	RnnoiseDenoiserControls.cpp
	RnnoiseDenoiserControlDialog.cpp
	${RNNOISE_SOURCES}
	MOCFILES
		RnnoiseDenoiserEffect.h
		RnnoiseDenoiserControls.h
		RnnoiseDenoiserControlDialog.h
	EMBEDDED_RESOURCES artwork.svg
)

TARGET_LINK_LIBRARIES(rnnoisedenoiser m)

# Vendored RNNoise (rnnoise/nnet.h) emits a supported #warning when the build detects
# only SSE/SSE2 — true on the CI runners. It is informational, it lives in vendored code
# this repo does not modify, and under -DUSE_WERROR it becomes an error that fails the
# whole linux-x86_64 build. Keep the warning, drop its error status for this target's C
# sources only (gcc/clang spell it -Werror=cpp).
TARGET_COMPILE_OPTIONS(rnnoisedenoiser PRIVATE
	"$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wno-error=cpp>")