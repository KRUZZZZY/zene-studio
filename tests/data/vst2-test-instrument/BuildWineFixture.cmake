# Q050-481.1: builds the Windows (PE) VST2 test instrument with winegcc, best
# effort: a failure leaves no .dll behind and does not fail the build (see
# CMakeLists.txt beside this file). Inputs: WINEGCC, SOURCE, OUTPUT, and (Q050-481.2, for the test
# effect in ../vst2-test-effect) an optional INCLUDE_DIR holding zene_vst2_abi.h.
get_filename_component(dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${dir}")
set(include_flags)
if(INCLUDE_DIR)
	set(include_flags "-I${INCLUDE_DIR}")
endif()
execute_process(
	COMMAND "${WINEGCC}" -b x86_64-w64-mingw32 -shared -O2 ${include_flags} -o "${OUTPUT}" "${SOURCE}"
	RESULT_VARIABLE result
	OUTPUT_VARIABLE out
	ERROR_VARIABLE out)
if(NOT result EQUAL 0)
	file(REMOVE "${OUTPUT}")
	message(STATUS "${OUTPUT}: winegcc could not build a PE module (${result}); the bridged fixture is absent")
endif()
