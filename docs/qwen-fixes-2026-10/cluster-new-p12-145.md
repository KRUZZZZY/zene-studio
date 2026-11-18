# Cluster new-p12-145

**Verdict: ALREADY-FIXED**

The full mixer sync path refreshes the tooltip and accessible name when a channel is renamed elsewhere. `MixerView::syncWithMixer()` calls `mixerstripinfo::describeStrip()` before `MixerChannelView::syncName()` ([MixerView.cpp:358-359](../../src/gui/MixerView.cpp#L358)). `describeStrip()` builds the tooltip using the current channel name and latency, appends any note and return-channel detail, then updates both the tooltip and accessible name ([MixerStripInfo.cpp:256-265](../../src/gui/MixerStripInfo.cpp#L256)). `syncName()` itself only updates the elided line edit ([MixerChannelView.cpp:539-545](../../src/gui/MixerChannelView.cpp#L539)), but that is not the only operation in the synchronization path.

A temporary test exercised `mixer.rename_channel` followed by `syncMixer()`. Its first expected name was too long for the fixed-width field and was elided, so that run failed on the visible-text expectation; it did not demonstrate this finding. With a short name (`Cue`), the tooltip, accessible name, and line edit all updated on the unchanged production code. The temporary test was removed because it passed before any fix. No production or test source files were changed.

## Evidence

| Check | Result |
|---|---|
| RED | No valid product RED: the finding is already covered by the full sync path. The preliminary test run exited 8 only because `Lead from Inspector` was elided in the 56 px field. |
| Corrected path check | `MixerModesTest` passed with the short external rename on unchanged production code; this confirms the existing behavior and is not a fix regression. |
| Final affected-family run | 1 CTest target (`MixerModesTest`), 1/1 passed. |
| Complexity gate | Exit 0. |
| File-length gate | Exit 0. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p12-145.md` only. |

## Commands and exit codes

The initial temporary-test build and its invalid first run:

```text
cmake --build build --target MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-1.log; exit $result
EXIT=0
```

```text
ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-1.log; exit $result
EXIT=8
```

The corrected temporary-test build and path check, still with no production change:

```text
cmake --build build --target MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-2.log; exit $result
EXIT=0
```

```text
ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-2.log; exit $result
EXIT=0
```

The final build after removing the temporary test and final affected-family run:

```text
cmake --build build --target zene MixerModesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/build-3.log; exit $result
EXIT=0
```

```text
ctest -R '^MixerModesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/test-3.log; exit $result
EXIT=0
```

Gates:

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/gate-1.log; exit $result
EXIT=0
```

```text
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-145.r1/logs/gate-2.log; exit $result
EXIT=0
```

The fff grep tool was not exposed in this session; log matches were checked with `rg` using the requested error, FAILED, and Assertion terms.
