;; grow.wat - docs/WASM-EFFECT-ABI.md section 12, UNKNOWN 2, made observable.
;;
;; The document could not determine, from the host source alone, whether the host
;; still writes/reads the module's linear memory correctly when the module GROWS
;; that memory inside process(). The host captures the `memory` handle at load and
;; re-resolves its data pointer and size on each use
;; (WasmSandbox::memoryData/memorySize, WasmWorker.cpp calls them per block), so a
;; growth mid-call must not lose the write that follows it. This module writes to
;; the output plane, grows a page, and writes again:
;;
;;     out[0] = 42.0,  memory.grow 1,  out[1] = 7.0
;;
;; Plane 0 must read back exactly [42.0, 7.0]. A stale pointer would show it.
;;
;; Three pages to start (the ABI's own example layout), so the block fits before
;; the growth as well as after it.
(module
	(memory (export "memory") 3)

	(func (export "process") (param $in_offset i32) (param $out_offset i32)
		(param $frames i32) (param $sample_rate f32) (result i32)
		(f32.store (local.get $out_offset) (f32.const 42))
		;; The old page count is returned and deliberately dropped.
		(drop (memory.grow (i32.const 1)))
		(f32.store (i32.add (local.get $out_offset) (i32.const 4)) (f32.const 7))
		(i32.const 0)))
