# WasmEffect - runs a sandboxed WASM DSP module (see WASM-SANDBOX.md).
#
# Only built when the WASM sandbox is enabled: without wasmtime (WANT_WASM=OFF)
# there is no runtime to host modules, so the plugin is skipped entirely and the
# configure still succeeds.
IF(NOT WANT_WASM)
	RETURN()
ENDIF()

INCLUDE(BuildPlugin)

BUILD_PLUGIN(wasm_effect
	WasmEffect.cpp
	WasmEffectControls.cpp
	WasmEffectControlDialog.cpp
	WasmEffect.h
	WasmEffectControls.h
	WasmEffectControlDialog.h
	MOCFILES
	WasmEffect.h
	WasmEffectControls.h
	WasmEffectControlDialog.h
)

# WasmWorker/WasmSandbox live in src/wasm; their implementation is part of the
# lmms library this plugin links against.
TARGET_INCLUDE_DIRECTORIES(wasm_effect PRIVATE "${CMAKE_SOURCE_DIR}/src/wasm")
