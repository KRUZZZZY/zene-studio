FIXED

# Cluster 3205: null MTS MIDI buffer dereferenced during parse

## Evidence

- Base `thirdparty/mts-esp/libMTSClient.cpp:615` read `unsigned char b = buffer[i];` inside `parseMIDIData` with no null check on `buffer` at the top of the function.
- The exports `MTS_ParseMIDIDataU` and `MTS_ParseMIDIData` (base lines 923-924) guarded only the client pointer `c`, so a null `buffer` with `len > 0` reached the dereference.
- Fix: the guard is now at `thirdparty/mts-esp/libMTSClient.cpp:605`: `if (buffer == nullptr || len <= 0) return;`. The header and the export signatures are unchanged.

## Test matrix

| Case | Where it is covered |
|---|---|
| Normal valid-dump path (positive length, real buffer) | Both green slot runs (signed and unsigned) |
| Null buffer failure for both exports | `localMtsParseIgnoresNullUnsignedBuffer` (`MTS_ParseMIDIDataU`, null with len 0, -5, 1); `localMtsParseIgnoresNullSignedBuffer` (`MTS_ParseMIDIData`, null with len 3) |
| Zero and negative length | Already no-ops before the fix, because the `for (i < len)` loop never runs; the test asserts they stay no-ops |
| Seeded-client reconfiguration | `seedLocalMtsScale` runs before and after the null parse; the scale name stays `Test scale` and the sysex flag is correct |
| Not on the audio thread | Tests call the exports directly on the test thread; no audio thread or threads are involved |
| Plain portable guard | One `if` with a pointer and length check; no platform-specific code |

## Commands and exit codes

Log paths are under `/home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/`. Each EXIT value is the last line of that log.
| Command | Log | EXIT |
|---|---|---|
| `{ cmake --build build --target zene SessionTuningTest; echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-build-green.log 2>&1` | 3205-build-green.log | 0 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullUnsignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-green-unsigned.log 2>&1` | 3205-test-green-unsigned.log | 0 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullSignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-green-signed.log 2>&1` | 3205-test-green-signed.log | 0 |
| `{ (cd build/tests && ctest -R SessionTuningTest --output-on-failure); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-green-ctest.log 2>&1` | 3205-test-green-ctest.log | 0 |
| `{ git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/fix.patch; echo EXIT=$?; } >> /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-patch-ops.log 2>&1` (first EXIT line) | 3205-patch-ops.log | 0 |
| `{ git apply /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/fix.patch; echo EXIT=$?; } >> /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-patch-ops.log 2>&1` (second EXIT line) | 3205-patch-ops.log | 0 |
| `{ cmake --build build --target zene SessionTuningTest; echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-build-reversed.log 2>&1` | 3205-build-reversed.log | 0 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullUnsignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-reversed-unsigned.log 2>&1` | 3205-test-reversed-unsigned.log | 139 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullSignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-reversed-signed.log 2>&1` | 3205-test-reversed-signed.log | 139 |
| `{ cmake --build build --target zene SessionTuningTest; echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-build-restored.log 2>&1` | 3205-build-restored.log | 0 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullUnsignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-restored-unsigned.log 2>&1` | 3205-test-restored-unsigned.log | 0 |
| `{ (cd build/tests && ./SessionTuningTest localMtsParseIgnoresNullSignedBuffer); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-restored-signed.log 2>&1` | 3205-test-restored-signed.log | 0 |
| `{ (cd build/tests && ctest -R SessionTuningTest --output-on-failure); echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-test-3.log 2>&1` | 3205-test-3.log | 0 |
| `{ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3205; echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-gates-1.log 2>&1` | 3205-gates-1.log | 0 (RESULT: PASS, no FAIL rows) |
| `{ python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-3205; echo EXIT=$?; } > /home/kruzzzzy/.config/kads/zene/state/runs/3205.r1/logs/3205-gates-2.log 2>&1` | 3205-gates-2.log | 0 (RESULT: PASS, no FAIL rows) |

`3205-test-3.log` is the restored-guard CTest run (`100% tests passed out of 1`, EXIT=0), taken after the re-apply.

## Self-check

1. **Regression fails and passes both ways.** With the guard reversed, both slots crash with exit 139 (signed and unsigned). With the guard restored, both slots exit 0.
2. **Deterministic.** The null pointer is passed directly to the export. There are no sleeps, threads, or clocks in the test.
3. **Cause and completeness.** One guard in `parseMIDIData` covers both exported siblings, because both call it. The only in-tree caller of these exports is `SessionTuningTest`.
4. **No collateral damage.** Old project data is untouched. Valid positive-length parsing is still proven by both green slots. The header and ABI are unchanged.
5. **Not ALREADY-FIXED or FALSE-POSITIVE.** The base code at line 615 dereferences `buffer[i]` with no null check, and the exports at base 923-924 do not check `buffer`. The reversed-guard run crashes, so the defect is real.
