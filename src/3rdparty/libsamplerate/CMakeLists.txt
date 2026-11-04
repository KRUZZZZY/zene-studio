# Private, pinned backend: no symbol interposition on the system libsamplerate
# used by plug-ins, SampleBuffer and the bit-exact reference tests.
add_library(zene_samplerate STATIC samplerate.c src_sinc.c src_linear.c src_zoh.c)
set_target_properties(zene_samplerate PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(zene_samplerate PRIVATE c_std_99)
target_compile_definitions(zene_samplerate PRIVATE
	HAVE_STDBOOL_H ENABLE_SINC_FAST_CONVERTER ENABLE_SINC_MEDIUM_CONVERTER
	ENABLE_SINC_BEST_CONVERTER PACKAGE="libsamplerate" VERSION="0.2.2-zene")
if(MSVC)
	target_compile_options(zene_samplerate PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/prefix.h)
else()
	target_compile_options(zene_samplerate PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/prefix.h)
endif()
if(UNIX)
	target_link_libraries(zene_samplerate PUBLIC m)
endif()
install(FILES COPYING DESTINATION "${LMMS_DATA_DIR}/licenses/libsamplerate")
