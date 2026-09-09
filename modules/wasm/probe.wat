;; probe.wat - exercises the module -> host import surface.
;;
;; Imports (all pure / no allocation, see specs/SPEC-wasm-sandbox.md section 3):
;;   host_log(ptr, len)
;;   host_get_transport_state() -> i32   (0 = stopped, 1 = playing)
;;
;; The exported functions let a host test observe that the imports are wired to
;; the real callbacks.
(module
	(import "env" "host_log" (func $host_log (param i32 i32)))
	(import "env" "host_get_transport_state" (func $host_get_transport_state (result i32)))

	(memory (export "memory") 1)

	(data (i32.const 0) "hello from wasm")

	(func (export "log_it")
		(call $host_log (i32.const 0) (i32.const 15)))

	(func (export "transport") (result i32)
		(call $host_get_transport_state)))
