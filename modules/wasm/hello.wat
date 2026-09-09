;; hello.wat - G1 smoke module: proves the runtime is embedded and a module can
;; be instantiated and called. No memory, no imports, one exported function.
;;
;; Assembled to hello.wasm by the build's wasm-wat2wasm tool (wasmtime_wat2wasm).
(module
	(func (export "hello") (result i32)
		i32.const 42))
