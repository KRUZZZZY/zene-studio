# Cluster new-p10-326: Quadrature LFO invalid inputs

**Verdict: FIXED**

## Finding and change

`include/QuadratureLfo.h:38-44` now initializes the derived phase state safely before accepting a sample rate. `include/QuadratureLfo.h:53-59` rejects non-finite frequencies and refuses frequency changes without a valid positive sample rate. `include/QuadratureLfo.h:69-77` ignores non-positive sample-rate updates, preserving the last valid state.

Regression coverage is in `tests/src/core/DeviceDspTest.cpp:209-246`: after two ticks it checks finite output for a NaN frequency, a zero-rate constructor, and a zero-rate update. On the original implementation all three cases failed their finite-output assertions.

The suggested `tests/reference/Flanger/FlangerEffect.cpp` is built as `partc_ref_flanger`, not registered as a standalone CTest in this build tree. The regression uses the existing `DeviceDspTest`; `PluginPortsMigrationTest` was also run as the Flanger behavior check.

## Evidence

| Phase | Exact CTest command (run from `build/tests`) | Exit | Result |
|---|---|---:|---|
| Original-code RED | `ctest -R '^DeviceDspTest$' --output-on-failure` | 8 | 1 CTest entry ran; all 3 new LFO cases failed with non-finite output. |
| Fixed GREEN | `ctest -R '^(DeviceDspTest\|PluginPortsMigrationTest)$' --output-on-failure` | 0 | 2/2 CTest entries passed. |
| Reversed-fix RED | `ctest -R '^(DeviceDspTest\|PluginPortsMigrationTest)$' --output-on-failure` | 8 | 2 entries ran; `DeviceDspTest` failed all 3 LFO cases and `PluginPortsMigrationTest` passed. |
| Restored-fix GREEN | `ctest -R '^(DeviceDspTest\|PluginPortsMigrationTest)$' --output-on-failure` | 0 | 2/2 CTest entries passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

The green and reversal runs used the same CTest selection. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-326.r1/logs/` as `test-1.log` through `test-4.log`, `build-1.log` through `build-4.log`, and `gate-1.log` / `gate-2.log`.

| Build phase | Exact command | Exit |
|---|---|---:|
| Original-code RED setup | `cmake --build build --target zene DeviceDspTest` | 0 |
| Initial GREEN | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger PluginPortsMigrationTest` | 0 |
| Reversed-fix RED | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger PluginPortsMigrationTest` | 0 |
| Restored-fix GREEN | `cmake --build build --target zene DeviceDspTest flanger partc_ref_flanger PluginPortsMigrationTest` | 0 |

The sensitivity check reversed only `include/QuadratureLfo.h` using `git diff -- include/QuadratureLfo.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-326.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-326.r1/logs/fix.patch` (exit 0), then restored it with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-326.r1/logs/fix.patch` (exit 0).

The session did not expose the requested fff grep tool (`ALL_TOOLS` had no fff entry and `command -v fff` returned no path). I searched each required log term with `rg -n -i` instead.

## Files committed

- `include/QuadratureLfo.h` — reject invalid LFO inputs while preserving valid state.
- `tests/src/core/DeviceDspTest.cpp` — regression cases for NaN and zero sample rates.
- `tests/upstream-modifications.txt` — inherited-header change reason.
- `docs/qwen-fixes-2026-10/cluster-new-p10-326.md` — this report.
