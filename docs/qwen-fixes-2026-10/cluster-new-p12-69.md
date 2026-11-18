# Cluster new-p12-69

**Verdict: ALREADY-FIXED**

The reported early return cannot skip header synchronization in this checkout. In [DeviceCard.cpp](../../src/gui/shell/DeviceCard.cpp#L427), `DeviceCard::refreshActivity()` calls `syncHeader()` at line 431, before it checks whether the activity indicator's `lit` property is unchanged at line 433. `syncHeader()` updates the power, fold, and delta buttons and the body visibility at lines 417–424. No production or test file was changed.

## Evidence

| Check | Command / evidence | Result |
| --- | --- | --- |
| Finding evidence | `src/gui/shell/DeviceCard.cpp:427–438` | `syncHeader()` at 431 precedes the unchanged-activity return at 433. |
| RED regression | N/A | No regression was added: the described faulty ordering is absent, so this is not a fix patch to prove RED/GREEN against. |
| GREEN regression | N/A | Same reason as RED. |
| Affected-family build | `cmake --build build --target DevicePanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-69.r1/logs/build-1.log 2>&1; echo EXIT=$?` | EXIT=0. |
| Affected-family test | From `build/tests`: `ctest -R DevicePanel --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-69.r1/logs/test-1.log 2>&1; echo EXIT=$?` | EXIT=0; 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-69.r1/logs/complexity-gate-3.log 2>&1; echo EXIT=$?` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-69.r1/logs/file-length-gate-2.log 2>&1; echo EXIT=$?` | EXIT=0. |

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-69.md` (this report only)
