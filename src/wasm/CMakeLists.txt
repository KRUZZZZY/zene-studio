set(LMMS_SRCS
	${LMMS_SRCS}

	wasm/WasmSandbox.cpp
	wasm/WasmWorker.cpp

	PARENT_SCOPE
)

# wat2wasm front end: assembles the demo .wat modules with the runtime's own
# parser so the build needs no wabt/cargo/emscripten toolchain.
ADD_EXECUTABLE(wasm-wat2wasm "${CMAKE_SOURCE_DIR}/tools/wasm/wat2wasm.cpp")
TARGET_INCLUDE_DIRECTORIES(wasm-wat2wasm SYSTEM PRIVATE ${WASMTIME_INCLUDE_DIRS})
TARGET_LINK_LIBRARIES(wasm-wat2wasm PRIVATE ${WASMTIME_LIBRARIES})

# Assemble the demo modules into <build>/wasm-modules for the tests and for
# manual experimentation with the sandbox.
SET(WASM_MODULE_DIR "${CMAKE_BINARY_DIR}/wasm-modules")
SET(WASM_MODULE_NAMES hello gain spin abort oob probe latency latency_trap)
UNSET(WASM_MODULE_OUTPUTS)
FOREACH(module IN LISTS WASM_MODULE_NAMES)
	SET(wat "${CMAKE_SOURCE_DIR}/modules/wasm/${module}.wat")
	SET(wasm "${WASM_MODULE_DIR}/${module}.wasm")
	ADD_CUSTOM_COMMAND(OUTPUT "${wasm}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${WASM_MODULE_DIR}"
		COMMAND "$<TARGET_FILE:wasm-wat2wasm>" "${wat}" "${wasm}"
		DEPENDS "$<TARGET_FILE:wasm-wat2wasm>" "${wat}"
		COMMENT "Assembling ${module}.wat -> ${module}.wasm"
		VERBATIM
	)
	LIST(APPEND WASM_MODULE_OUTPUTS "${wasm}")
ENDFOREACH()
ADD_CUSTOM_TARGET(wasm-modules ALL DEPENDS ${WASM_MODULE_OUTPUTS})

# G4: reference DSP module compiled from C. Only built when a wasm-capable C
# toolchain is present (zig cc bundles clang + wasm-ld); the gate test skips
# when the module is absent, so a plain LMMS checkout still configures/builds.
FIND_PROGRAM(ZIG_EXECUTABLE NAMES zig)
IF(ZIG_EXECUTABLE)
	SET(WASM_C_DEMO "${WASM_MODULE_DIR}/gain_clip.wasm")
	ADD_CUSTOM_COMMAND(OUTPUT "${WASM_C_DEMO}"
		COMMAND "${ZIG_EXECUTABLE}" cc -target wasm32-freestanding -O2 -nostdlib
			-Wl,--no-entry
			-o "${WASM_C_DEMO}"
			"${CMAKE_SOURCE_DIR}/modules/wasm/demo/gain_clip.c"
		DEPENDS "${CMAKE_SOURCE_DIR}/modules/wasm/demo/gain_clip.c"
		COMMENT "Compiling C demo module gain_clip.c -> gain_clip.wasm (zig cc)"
		VERBATIM
	)
	ADD_CUSTOM_TARGET(wasm-c-demo ALL DEPENDS "${WASM_C_DEMO}")
	ADD_DEPENDENCIES(wasm-modules wasm-c-demo)
	MESSAGE(STATUS "WASM C demo module: enabled (zig: ${ZIG_EXECUTABLE})")
ELSE()
	MESSAGE(STATUS "WASM C demo module: disabled (zig not found on PATH)")
ENDIF()
