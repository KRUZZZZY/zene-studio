# Cluster new-p10-113 — FIXED

**Verdict: FIXED**

`plugins/Dispersion/Dispersion.cpp:70` now caps the frequency used by the coefficient calculation at 0.499 of the sample rate. This keeps the all-pass sine term on the stable side of Nyquist, including at 32 kHz when the saved control frequency is 20 kHz. The frequency control's saved value and displayed range remain unchanged.

`tests/src/plugins/PluginPortsMigrationTest.cpp:406` adds an integration regression through the real Dispersion plugin module and audio buffer path. At 32 kHz it loads 20 kHz frequency and 0.707 resonance, processes a stereo impulse for 4096 frames, and requires finite output with a peak below 1000. Without the production cap, this reaches the 1000 runaway bound.

## Evidence

The affected family is `PluginPortsMigrationTest`: **1 CTest target, 9 Qt test cases**. The accepted RED run has 8 passing cases and the new regression failing; GREEN has all 9 passing.

| Stage | Exact command | Exit | Result and log |
|---|---|---:|---|
| RED build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-3.log` |
| RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | 8 | New regression fails: peak reaches 1000; 8 passed, 1 failed. `test-4.log` |
| GREEN build | `cmake --build build --target zene PluginPortsMigrationTest` | 0 | `build-4.log` |
| GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V` (from `build/tests`) | 0 | 9 passed, 0 failed; measured peak 0.991152. `test-5.log` |
| Remove-fix proof | `git diff -- plugins/Dispersion/Dispersion.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-113.r1/logs/fix.patch` then `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-113.r1/logs/fix.patch` | 0, 0 | Exact commands and statuses in `proof-1.log` |
| Reverse build and RED proof | `cmake --build build --target zene PluginPortsMigrationTest`; `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V` (ctest from `build/tests`) | 0, 8 | With the production patch removed, the same regression fails at peak 1000; 8 passed, 1 failed. `build-5.log`, `test-6.log` |
| Restore-fix proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-113.r1/logs/fix.patch` | 0 | `proof-2.log` |
| Restored build and GREEN proof | `cmake --build build --target zene PluginPortsMigrationTest`; `ctest -R '^PluginPortsMigrationTest$' --output-on-failure -V` (ctest from `build/tests`) | 0, 0 | 9 passed, 0 failed. `build-6.log`, `test-7.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

All six `cmake --build build --target zene PluginPortsMigrationTest` invocations exited 0. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-113.r1/logs`.

Harness setup probes before the accepted RED were not counted as regression proof: `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` exited 0 in `test-1.log`, and the verbose repeat exited 0 in `test-2.log`, because the first fixture attempt wrote the logarithmic frequency as an ignored XML attribute. The next verbose run exited 8 in `test-3.log` on a fixture assertion that expected that attribute to round-trip. The fixture was corrected to set the serialized `<freq value="...">` child, and the accepted RED is `test-4.log` above. No production change had been made during these probes.

The session did not expose the requested fff grep tool, so logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `plugins/Dispersion/Dispersion.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-113.md`
