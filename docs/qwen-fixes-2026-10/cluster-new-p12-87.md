# Cluster new-p12-87

Verdict: **FIXED**

## Evidence

| Item | Evidence |
| --- | --- |
| Finding | `plugins/FrequencyShifter/FrequencyShifterEffect.cpp:262-280` resized the delay ring without resetting its write position or sample-count delay. |
| Fix | `updateSampleRate()` now clears the resized delay ring, resets `m_writeIndex` and the damp state, and sets `m_trueDelay` from the active millisecond controls at the new sample rate. |
| Regression | `tests/src/plugins/PluginPortsMigrationTest.cpp:780-842` processes at 192 kHz with maximum delay, changes to 44.1 kHz, checks both indices fit the resized ring, then processes another block. |
| RED | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/04-test-red.log 2>&1` — exit 8; failed the write-index bounds assertion. |
| Reversal proof | Reversing the production diff reproduced the same assertion failure: `08-test-reverted.log`, exit 8. Reapplying the diff restored GREEN. |
| GREEN | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/10-test-proof-green.log 2>&1` — exit 0. |
| Affected family | One CTest (`PluginPortsMigrationTest`), 9 QtTest rows. RED totals: 8 passed, 1 failed, 0 skipped. The green CTest run passed 1/1. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/11-complexity-gate.log 2>&1` — exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/12-file-length-gate.log 2>&1` — exit 0. |
| Log search | The `fff` tool and executable were unavailable in this session; the requested log terms were searched with `rg`. |

## Build and test commands

All builds targeted `zene`, `frequencyshifter`, and `PluginPortsMigrationTest`. The first two test-integration builds exposed and corrected a test API mismatch and a link to the module-private derived type; no ctest was run from those builds.

| Exact command | Exit | Result |
| --- | ---: | --- |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/01-build-red.log 2>&1` | 2 | Initial test compile error. |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/02-build-red.log 2>&1` | 2 | Initial test link error. |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/03-build-red.log 2>&1` | 0 | Corrected regression built before the production fix. |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/04-test-red.log 2>&1` | 8 | RED assertion, 8 passed and 1 failed. |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/05-build-green.log 2>&1` | 0 | Build after fix. |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/06-test-green.log 2>&1` | 0 | GREEN after fix. |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/07-build-reverted.log 2>&1` | 0 | Build with production diff reversed. |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/08-test-reverted.log 2>&1` | 8 | Reversal reproduced the same RED assertion. |
| `cmake --build build --target zene frequencyshifter PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/09-build-proof-green.log 2>&1` | 0 | Build after restoring the production diff. |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/10-test-proof-green.log 2>&1` | 0 | GREEN after restoring the production diff. |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/11-complexity-gate.log 2>&1` | 0 | Complexity ratchet passed. |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/12-file-length-gate.log 2>&1` | 0 | File-length ratchet passed. |

Patch proof commands and exit codes:

```text
git diff -- plugins/FrequencyShifter/FrequencyShifterEffect.cpp plugins/FrequencyShifter/FrequencyShifterEffect.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/fix.patch — 0
git diff -- plugins/FrequencyShifter/FrequencyShifterEffect.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/test-access.patch — 0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/fix.patch — 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/test-access.patch — 0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/test-access.patch — 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-87.r1/logs/fix.patch — 0
```

## Files committed

- `plugins/FrequencyShifter/FrequencyShifterEffect.cpp`
- `plugins/FrequencyShifter/FrequencyShifterEffect.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-87.md`

Commit message: `Fix cluster new-p12-87 Frequency Shifter ring resize`
