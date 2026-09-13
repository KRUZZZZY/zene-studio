;; mutable-channels.wat - docs/WASM-EFFECT-ABI.md section 12, UNKNOWN 4, made
;; observable.
;;
;; The document could not settle whether `channels` and `latency` may be declared
;; as MUTABLE globals and changed at run time. They may - wasm lets a module export
;; a mutable global - but the host reads them ONCE, at instantiation
;; (WasmSandbox.cpp:403-406; WasmWorker.cpp re-reads only on a re-instantiation),
;; so a change afterwards is ineffective rather than forbidden.
;;
;; This module proves both halves in one call: it sets its own `channels` global to
;; 2 inside process(), and writes that new value into the output plane, so a reader
;; can see the module really did change it. The host must still report channels = 1
;; and must still enter process() exactly once (planes_run = 1): plane 0 reads back
;; [2.0], the module's own evidence that its global moved.
(module
	(memory (export "memory") 3)

	(global (export "channels") (mut i32)
		(i32.const 1))

	(func (export "process") (param $in_offset i32) (param $out_offset i32)
		(param $frames i32) (param $sample_rate f32) (result i32)
		(global.set 0 (i32.const 2))
		(f32.store (local.get $out_offset) (f32.convert_i32_s (global.get 0)))
		(i32.const 0)))
