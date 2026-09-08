;; abort.wat - module that aborts (traps) for the G3 crash-isolation gate.
;;
;; `unreachable` is what compilers emit for abort()/panic=abort. The trap must
;; be caught at the embedder boundary: the host keeps running, the effect is
;; marked corrupted and passes audio through dry.
(module
	(memory (export "memory") 1)

	(global (export "channels") i32 (i32.const 1))

	(func $abort (export "abort_now") (result i32)
		unreachable)

	(func (export "process")
		(param i32) (param i32) (param i32) (param f32)
		(result i32)
		(call $abort)))
