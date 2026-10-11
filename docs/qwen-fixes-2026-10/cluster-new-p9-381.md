# Cluster new-p9-381

**Verdict: FIXED**

`QuadratureLfo::setSampleRate()` recalculated the phase increment using the new sample rate while retaining a frequency that could now exceed Nyquist. It now clamps the stored frequency to `samplerate / 2.0` before calculating the increment ([include/QuadratureLfo.h:65](../../include/QuadratureLfo.h#L65)).

The regression ([tests/src/core/DeviceDspTest.cpp:208](../../tests/src/core/DeviceDspTest.cpp#L208)) sets 20 kHz at 48 kHz, lowers the rate to 32 kHz, and checks that the second sample matches the 16 kHz Nyquist limit. On the original implementation it failed with `left == -0.707107`. Removing only the production patch reproduced the same failure; restoring it passed.

The suggested `FlangerEffect` ctest selector matches no registered test in this checkout. The focused test is in the existing `DeviceDspTest` DSP family instead. The affected family executed **1 CTest target**. The original RED output had 2 QtTest passes, 1 failure, and 8 skips for optional plugin modules not present in this build; GREEN CTest runs reported 1/1 passed.

## Evidence

| Phase | Command | Exit | Log |
|---|---|---:|---|
| Build before original RED | `cmake --build build --target zene DeviceDspTest -- -j12` | 0 | `build-1.log` |
| Original RED | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure` | 8 | `test-1-red.log` |
| Build with fix | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger -- -j12` | 0 | `build-2.log` |
| Initial GREEN | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure` | 0 | `test-2-green.log` |
| Create product-only patch | `git diff -- include/QuadratureLfo.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-381.r1/logs/fix.patch` | 0 | — |
| Remove product fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-381.r1/logs/fix.patch` | 0 | — |
| Rebuild without fix | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger -- -j12` | 0 | `build-3-red-proof.log` |
| RED with product fix removed | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure` | 8 | `test-3-red-proof.log` |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-381.r1/logs/fix.patch` | 0 | — |
| Final rebuild | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger -- -j12` | 0 | `build-4-green-proof.log` |
| Final GREEN | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^DeviceDspTest$' --output-on-failure` | 0 | `test-4-green-proof.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log` |

The inherited-header change is declared in `tests/upstream-modifications.txt:1054` with its regression reference.

## Files committed

- `include/QuadratureLfo.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-381.md`
