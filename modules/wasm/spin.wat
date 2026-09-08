;; spin.wat - runaway module for the G3 fuel gate.
;;
;; process() enters an unconditional loop that never branches out. Without fuel
;; metering this would hang the host forever; with it, wasmtime traps with
;; WASMTIME_TRAP_CODE_OUT_OF_FUEL (11) once the per-call budget is exhausted.
(module
	(memory (export "memory") 1)

	(global (export "channels") i32 (i32.const 1))

	(func (export "process")
		(param i32) (param i32) (param i32) (param f32)
		(result i32)
		(loop $forever
			(br $forever))
		(i32.const 0)))
