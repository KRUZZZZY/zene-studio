;; channels-clamped.wat - ABI doc s4.1 and s4.2: the host's CLAMPS.
;;
;; `channels` is a real i32 global here, declared 6. abi::maxChannels is 2, so the
;; host must clamp it silently - 2 is what has to come back, not 6 and not an
;; error. `latency` is declared -5, which the host clamps to 0.
;;
;; Both clamps are silent in the host (WasmSandbox.cpp:407-418), which is exactly
;; why they are worth a fixture: a module author who declared 6 would otherwise
;; believe it.
(module
	(memory (export "memory") 3)
	(global (export "channels") i32
		(i32.const 6))
	(global (export "latency") i32
		(i32.const -5)))
