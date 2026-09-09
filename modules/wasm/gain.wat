;; gain.wat - demo DSP module for the WASM DSP sandbox (v0 audio ABI).
;;
;; ABI (frozen, see specs/SPEC-wasm-sandbox.md section 3):
;;   process(in_ptr, out_ptr, frames, sample_rate) -> i32   ;; 0 = ok
;;   host_get_param(index) -> f32                           ;; import "env"
;;
;; in_ptr/out_ptr are OFFSETS into this module's linear memory. The host copies
;; one PLANAR channel in and out per call; the module declares its channel count
;; and latency through exported globals. This module is stereo: the host calls
;; process() once per output channel with a different plane offset.
;;
;; DSP: out[i] = in[i] * host_get_param(0)   (host default gain = 0.5)
(module
	(import "env" "host_get_param" (func $host_get_param (param i32) (result f32)))

	(memory (export "memory") 1)

	(global (export "channels") i32 (i32.const 2))
	(global (export "latency") i32 (i32.const 0))

	(func (export "process")
		(param $in i32) (param $out i32) (param $frames i32) (param $sample_rate f32)
		(result i32)
		(local $i i32)
		(local $gain f32)
		(local.set $gain (call $host_get_param (i32.const 0)))
		(block $done
			(loop $frame
				(br_if $done (i32.ge_s (local.get $i) (local.get $frames)))
				(f32.store
					(i32.add (local.get $out) (i32.mul (local.get $i) (i32.const 4)))
					(f32.mul
						(f32.load (i32.add (local.get $in) (i32.mul (local.get $i) (i32.const 4))))
						(local.get $gain)))
				(local.set $i (i32.add (local.get $i) (i32.const 1)))
				(br $frame)))
		(i32.const 0)))
