;; latency_trap.wat - latency.wat that traps on its fourth process() call.
;;
;; Same 64-frame delay as latency.wat, but the fourth call executes
;; `unreachable` before touching memory. The host quarantines the module and
;; falls back to the (latency-compensated) dry path; this module exists to
;; prove that the effect's reported latency does not jump when a module dies.
(module
	(import "env" "host_get_param" (func $host_get_param (param i32) (result f32)))

	(memory (export "memory") 3)

	(global (export "channels") i32 (i32.const 1))
	(global (export "latency") i32 (i32.const 64))

	(global $w (mut i32) (i32.const 0))
	(global $calls (mut i32) (i32.const 0))

	(func (export "process")
		(param $in i32) (param $out i32) (param $frames i32) (param $sample_rate f32)
		(result i32)
		(local $i i32)
		(local $x f32)
		(local $slot i32)
		(global.set $calls (i32.add (global.get $calls) (i32.const 1)))
		(if (i32.ge_s (global.get $calls) (i32.const 4)) (then (unreachable)))
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
