;; oob.wat - negative test: reads outside its linear memory.
;;
;; The module's memory is one page (65536 bytes). The load targets offset
;; 1048576, far outside the sandbox, and must trap with
;; WASMTIME_TRAP_CODE_MEMORY_OUT_OF_BOUNDS (1) instead of touching host memory.
(module
	(memory (export "memory") 1)

	(global (export "channels") i32 (i32.const 1))

	(func (export "process")
		(param $in i32) (param $out i32) (param $frames i32) (param $sample_rate f32)
		(result i32)
		(drop (i32.load offset=1048576 (i32.const 0)))
		(i32.const 0)))
