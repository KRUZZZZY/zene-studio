# Cluster new-p11-35

Verdict: **FIXED**.

`modulator.voice_set` now treats an explicit JSON `null` trigger as “clear the trigger,” and records that value in the inverse when the previous source was free running. The regression replays the serialized inverse and checks the reported source has no trigger.

## File evidence

- [ControlCommandsModulatorVoices.cpp](../../src/core/ControlCommandsModulatorVoices.cpp):92-100 clears `triggerTrack` for null; lines 124-126 include a null trigger in the inverse; line 149 accepts null in the command schema.
- [InstrumentLayersCommandsTest.cpp](../../tests/src/core/InstrumentLayersCommandsTest.cpp):300-328 assigns a trigger to a free-running modulator, replays the reported inverse, and asserts the trigger is gone.

## Regression evidence

The affected CTest family is `InstrumentLayersCommandsTest`: **1/1 CTest target**. The RED run reported 8 QtTest outcomes: 2 passed, 1 failed, 5 skipped. The failure was the new inverse replay assertion: `!source.contains("trigger")` returned false because the assigned track remained.

| Stage | Exact command | Exit |
|---|---|---:|
| Initial test scaffold build | `cmake --build build --target InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-1.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-1.log; exit "$result"` | 0 |
| Initial test scaffold run (the plugin-dependent fixture skipped this case) | `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-1.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-1.log; exit "$result"` | 0 |
| Build plugin-independent regression, before fix | `cmake --build build --target InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-2.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-2.log; exit "$result"` | 0 |
| **RED** | `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-2.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-2.log; exit "$result"` | **8** |
| Build with fix | `cmake --build build --target zene InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-3.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-3.log; exit "$result"` | 0 |
| **GREEN** | `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-3.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-3.log; exit "$result"` | **0** |

## Reversal proof

| Stage | Exact command | Exit |
|---|---|---:|
| Remove product fix | `git diff -- src/core/ControlCommandsModulatorVoices.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/fix.patch; result=$?; echo "EXIT=$result" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/revert-1.log; exit "$result"` | 0 |
| Rebuild without fix | `cmake --build build --target zene InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-4.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-4.log; exit "$result"` | 0 |
| **RED without fix** | `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-4.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-4.log; exit "$result"` | **8** |
| Reapply product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/fix.patch; result=$?; echo "EXIT=$result" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/restore-1.log; exit "$result"` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-5.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-5.log; exit "$result"` | 0 |
| **GREEN after restore** | `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-5.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-5.log; exit "$result"` | **0** |

After the final test comment edit, the affected target was rebuilt and rerun:

- `cmake --build build --target zene InstrumentLayersCommandsTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-6.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/build-6.log; exit "$result"` — EXIT=0.
- `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-6.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/test-6.log; exit "$result"` — EXIT=0.

## Ratchet gates

The final runs after the last edit both passed:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-3.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-3.log; exit "$result"` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-4.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-4.log; exit "$result"` — EXIT=0.

Earlier gate runs also passed:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-1.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-1.log; exit "$result"` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-2.log 2>&1; result=$?; echo "EXIT=$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-35.r1/logs/gate-2.log; exit "$result"` — EXIT=0.

## Files in the single commit

- `src/core/ControlCommandsModulatorVoices.cpp`
- `tests/src/core/InstrumentLayersCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p11-35.md`
