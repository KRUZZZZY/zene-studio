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