# The LV2 INSTRUMENT subject of InstrumentLatencyPdcTest (MX-157, BUGS 11.34): a real LV2 bundle built
# into the build tree - the module and its two Turtle files in one directory, which the test puts on
# LV2_PATH. Plain C against the system LV2 headers (ISC), no LMMS code, no Qt; never installed.
set(LMMS_LV2_TEST_INSTRUMENT_BUNDLE "${CMAKE_CURRENT_BINARY_DIR}/zene-test-instrument.lv2")
set(LMMS_LV2_TEST_INSTRUMENT_BUNDLE "${LMMS_LV2_TEST_INSTRUMENT_BUNDLE}" PARENT_SCOPE)

add_library(lv2-test-instrument MODULE lv2-test-instrument.c)
set_target_properties(lv2-test-instrument PROPERTIES
	PREFIX ""
	C_STANDARD 11
	C_STANDARD_REQUIRED ON
	LIBRARY_OUTPUT_DIRECTORY "${LMMS_LV2_TEST_INSTRUMENT_BUNDLE}"
)
target_include_directories(lv2-test-instrument PRIVATE ${LV2_INCLUDE_DIRS})
if(NOT MSVC)
	target_compile_options(lv2-test-instrument PRIVATE -Wall -Wextra)
endif()

set(LV2_TEST_INSTRUMENT_BINARY "lv2-test-instrument${CMAKE_SHARED_MODULE_SUFFIX}")
configure_file(manifest.ttl.in "${LMMS_LV2_TEST_INSTRUMENT_BUNDLE}/manifest.ttl" @ONLY)
configure_file(lv2-test-instrument.ttl "${LMMS_LV2_TEST_INSTRUMENT_BUNDLE}/lv2-test-instrument.ttl" COPYONLY)
