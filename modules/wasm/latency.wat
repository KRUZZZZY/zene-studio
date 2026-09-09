;; latency.wat - demo DSP module that declares 64 frames of latency.
;;
;; ABI (frozen, see specs/SPEC-wasm-sandbox.md section 3):
;;   process(in_ptr, out_ptr, frames, sample_rate) -> i32   ;; 0 = ok
;;   host_get_param(index) -> f32                           ;; import "env"
;;
;; Mono, unity gain, pure 64-frame delay implemented as a ring buffer in the
;; module's own linear memory. The host reads the exported "latency" global and
;; compensates its dry path by the same amount (see WASM-SANDBOX.md, "Plugin
;; registration").
;;
;; DSP: out[i] = in[i - 64]   (the first 64 output frames are silence)
(module
	(import "env" "host_get_param" (func $host_get_param (param i32) (result f32)))

	(memory (export "memory") 3)

	(global (export "channels") i32 (i32.const 1))
	(global (export "latency") i32 (i32.const 64))

	;; 64 f32 slots at byte offset 65536 - past the host's in/out planes, which
	;; end at 2 * maxBlockFrames * sizeof(f32) = 65536 - plus the write cursor.
	(global $w (mut i32) (i32.const 0))

	(func (export "process")
		(param $in i32) (param $out i32) (param $frames i32) (param $sample_rate f32)
		(result i32)
		(local $i i32)
		(local $x f32)
		(local $slot i32)
		(block $done
			(loop $frame
				(br_if $done (i32.ge_s (local.get $i) (local.get $frames)))
				(local.set $x (f32.load (i32.add (local.get $in) (i32.mul (local.get $i) (i32.const 4)))))
				(local.set $slot (i32.add (i32.const 65536) (i32.mul (global.get $w) (i32.const 4))))
				(f32.store
					(i32.add (local.get $out) (i32.mul (local.get $i) (i32.const 4)))
					(f32.load (local.get $slot)))
				(f32.store (local.get $slot) (local.get $x))
				(global.set $w (i32.and (i32.add (global.get $w) (i32.const 1)) (i32.const 63)))
				(local.set $i (i32.add (local.get $i) (i32.const 1)))
				(br $frame)))
		(i32.const 0)))
