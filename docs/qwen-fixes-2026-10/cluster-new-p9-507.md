# Cluster new-p9-507 — FIXED

## Verdict

**FIXED.** `HostedPlugin::process()` now clears every supplied output channel before checking whether the plug-in is prepared. Mapped bus channels use that clear; unmapped channels stay silent, and scratch outputs for missing caller channels are still cleared for each chunk. The audio-thread work uses a bounded loop and `memset`, with no allocation, lock, logging, or Qt call.

## File and line evidence

- `plugins/Vst3Effect/Vst3Host.cpp:78`: `clearCallerOutputs()` clears each non-null supplied channel for positive frame counts.
- `plugins/Vst3Effect/Vst3Host.cpp:615-616`: the clear runs before the unprepared early return.
- `plugins/Vst3Effect/Vst3Host.cpp:765-770`: missing caller channels still use and clear the preallocated scratch output.
- `plugins/Vst3Effect/Vst3Host.h:206-208`: documents silence on positive-frame calls while unprepared or released.
- `tests/src/plugins/Vst3InstrumentTest.cpp:252` and `:267`: regressions cover an output beyond the active bus layout and an unprepared host.

## Verification evidence

Commands below ran from the repository root unless stated. The ctest commands ran from `build/tests`. `ctest` exit codes are shown as the captured command status; the shell wrappers exited zero after printing that status.

| Evidence | Exact command | Result |
| --- | --- | --- |
| Original-code RED build | `cmake --build build --target Vst3InstrumentTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status; exit 0` | Build exit 0 |
| Original-code RED | `ctest -R '^Vst3InstrumentTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit 0` | ctest exit 8; 12 passed, 2 failed. Both new assertions reported stale samples. |
| 4b: reverse the saved product patch | `git diff -- plugins/Vst3Effect/Vst3Host.cpp plugins/Vst3Effect/Vst3Host.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/fix.patch` | Exit 0 |
| 4b: rebuild without fix | `cmake --build build --target zene vst3effect vst3instrument Vst3InstrumentTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/build-6.log 2>&1; status=$?; echo EXIT=$status; exit 0` | Build exit 0 |
| 4b: RED with fix reversed | `ctest -R '^Vst3InstrumentTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/test-6.log 2>&1; status=$?; echo EXIT=$status; exit 0` | ctest exit 8; 12 passed, 2 failed. Both new assertions failed again. |
| Restore saved product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/fix.patch` | Exit 0 |
| Final build, including `zene`, both VST3 modules, and all dependent VST3 host tests | `cmake --build build --target zene vst3effect vst3instrument Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentTest Vst3InstrumentIntegrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/build-7.log 2>&1; status=$?; echo EXIT=$status; exit 0` | Build exit 0 |
| Final GREEN, affected family | `ctest -R '^(Vst3HostTest|Vst3ChunkProbeTest|Vst3InstrumentTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/test-7.log 2>&1; status=$?; echo EXIT=$status; exit 0` | ctest exit 0; 3/3 affected-family tests passed. |
| Complexity gate, final | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status; exit 0` | Exit 0 |
| File-length gate, final | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-507.r1/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status; exit 0` | Exit 0 |

The first complexity-gate run returned exit 1 because the new branches raised `HostedPlugin::process()` to CCN 13. The clear loop was moved to the small helper at line 78; the final gate run above returned exit 0. The first file-length-gate run also returned exit 0.

## Committed files

- `plugins/Vst3Effect/Vst3Host.cpp`
- `plugins/Vst3Effect/Vst3Host.h`
- `tests/src/plugins/Vst3InstrumentTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-507.md`
