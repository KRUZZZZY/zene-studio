;; softclip.wat - the #614 example effect, written from docs/WASM-EFFECT-ABI.md and nothing else.
;;
;; It is the reference module for the documented v0 WASM effect ABI. Every export and import below is
;; one the ABI document defines, and nothing here was copied from an existing fixture:
;;
;;   process  (i32 in_offset, i32 out_offset, i32 frames, f32 sample_rate) -> i32   [ABI doc s3]
;;   memory   linear memory named exactly "memory"                                  [ABI doc s2]
;;   channels i32 GLOBAL = 2 (stereo)                                               [ABI doc s4.1]
;;   env.host_get_param(index: i32) -> f32                                          [ABI doc s6]
;;   env.host_log(ptr: i32, len: i32) -> ()                                         [ABI doc s10]
;;
;; The ABI properties this module deliberately demonstrates, each one a place a module is easy to get
;; wrong:
;;
;;   * `channels` is a GLOBAL, not a function (ABI doc s4.1) - declared with (global ...) and exported.
;;   * process() is called ONCE PER CHANNEL PLANE, with the same frames/sample_rate and a different
;;     offset pair each call (ABI doc s3). This body therefore treats in_offset/out_offset as the base
;;     of ONE plane and never assumes it is handed a whole interleaved block.
;;   * the input plane and the output plane are separate regions of this module's own memory
;;     (ABI doc s5); the body reads from `in` and writes to `out`, never in place through a host pointer.
;;   * the sample rate arrives as the 4th argument (ABI doc s7). This effect does not use it, so the
;;     argument is declared and left alone rather than read from a global that does not exist.
;;   * parameter 0 is a gain read through host_get_param (ABI doc s6). Out-of-range indices return
;;     0.0f, so the module keeps to index 0.
;;   * the i32 result is returned as 0 by convention. The host records the value and does not act on it
;;     (ABI doc s8), so 0 is a convention here, not an error signal.
;;   * memory is three pages so this module can carry a maximum-size stereo block: the planes need
;;     2 * channels * maxBlockFrames * 4 = 131072 bytes at frames = 8192 (ABI doc s5), which is larger
;;     than one page.
;;
;; DSP: out[i] = clamp(in[i] * host_get_param(0), -1.0, 1.0)
(module
	(import "env" "host_get_param" (func $host_get_param (param i32) (result f32)))
	(import "env" "host_log" (func $host_log (param i32 i32)))

	(memory (export "memory") 3)

	;; The declared channel count and latency are i32 globals (ABI doc s4.1, s4.2). This effect is
	;; stereo and introduces no delay, so latency is 0.
	(global (export "channels") i32 (i32.const 2))
	(global (export "latency") i32 (i32.const 0))

	;; A short banner logged once per call, so a host-side conformance run can observe host_log wiring
	;; (ABI doc s10). It sits ABOVE the planes' maximum extent, 2 * channels * maxBlockFrames * 4 =
	;; 131072 bytes (ABI doc s5), because the host writes the input plane from offset 0 on every
	;; block and would otherwise overwrite it.
	(data (i32.const 131072) "softclip: v0 ABI")

	(func (export "process")
		(param $in_offset i32)
		(param $out_offset i32)
		(param $frames i32)
		(param $sample_rate f32)
		(result i32)
		(local $i i32)
		(local $gain f32)
		(local $y f32)

		;; Parameter 0 is the gain. host_get_param returns 0.0f for any index outside [0, 16).
		(local.set $gain (call $host_get_param (i32.const 0)))

		;; Log the banner once per call - the host keeps only the most recent text (ABI doc s10).
		(call $host_log (i32.const 4) (i32.const 16))

		(block $done
			(loop $frame
				;; for (i = 0; i < frames; ++i)
				(br_if $done (i32.ge_s (local.get $i) (local.get $frames)))

				;; y = in[i] * gain
				(local.set $y
					(f32.mul
						(f32.load (i32.add (local.get $in_offset)
							(i32.mul (local.get $i) (i32.const 4))))
						(local.get $gain)))

				;; y = min(y, 1.0); y = max(y, -1.0)
				(local.set $y (f32.min (local.get $y) (f32.const 1.0)))
				(local.set $y (f32.max (local.get $y) (f32.const -1.0)))

				;; out[i] = y
				(f32.store
					(i32.add (local.get $out_offset)
						(i32.mul (local.get $i) (i32.const 4)))
					(local.get $y))

				(local.set $i (i32.add (local.get $i) (i32.const 1)))
				(br $frame)))

		;; 0 = ok by convention; the host does not read this value (ABI doc s8).
		(i32.const 0)))
