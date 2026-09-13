;; channels-function.wat - ABI doc s4.1: `channels` must be an i32 GLOBAL.
;;
;; This module exports a FUNCTION named `channels` instead, which the host's
;; Impl::readGlobalI32 ignores (it requires kind == WASMTIME_EXTERN_GLOBAL), so
;; the default of 1 must apply with no error and no warning. Same file settles
;; s4.2's positive case: `latency` here IS a global, declared 64, and 64 must come
;; back. It exports no process(), so it also shows that a module without one LOADS
;; and is not an effect (ABI doc s1).
;;
;; Committed rather than generated so the probe is reviewable, and loaded by
;; tests/control-wasm-sandbox.py through the wasm.* control-surface commands.
(module
	(memory (export "memory") 3)
	(func (export "channels") (result i32)
		(i32.const 2))
	(global (export "latency") i32
		(i32.const 64)))
