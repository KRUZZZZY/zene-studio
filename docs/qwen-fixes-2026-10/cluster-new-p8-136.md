# Cluster new-p8-136: Inserting a note runs two commands instead of one

**Verdict: FALSE-POSITIVE**

## Finding and evidence

`EditorSource::insertAfter()` calls `note.add` and then `note.select` at [ItemListNoteSources.cpp:156-159](../../src/gui/noteeditor/ItemListNoteSources.cpp#L156). `note.add` is mutating and checkpoints the clip ([ControlCommandsNotes.cpp:112-126](../../src/core/ControlCommandsNotes.cpp#L112)). `note.select` is explicitly control-surface view state and sets `cmd.mutating = false` ([ControlCommandsNotes.cpp:447-464](../../src/core/ControlCommandsNotes.cpp#L447)); the reversibility table classifies it as `NotMutating` ([ControlReversibilityTablePassive.cpp:137-139](../../src/core/ControlReversibilityTablePassive.cpp#L137)). The registry records transactions only for successful changed mutating calls ([ControlRegistry.cpp:357](../../src/core/ControlRegistry.cpp#L357)). Therefore the selection call does not add an undo step.

The existing GUI test [ItemListPanelTest.cpp:171-178](../../tests/src/gui/ItemListPanelTest.cpp#L171) inserts a note, invokes Undo once, and verifies the clip returns from four notes to three. It passed on the rebuilt test binary. No production or test change was needed.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| Build affected test target | `cmake --build build --target ItemListPanelTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-136.r1/logs/build-1.log 2>&1` | EXIT=0 |
| RED regression | N/A — the finding is false-positive, so no failing regression or product patch was appropriate | N/A |
| Existing behavior test (run from `build/tests`) | `ctest -R ItemListPanelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-136.r1/logs/test-1.log 2>&1` | EXIT=0; 1/1 CTest tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-136.r1/logs/gate-1.log 2>&1` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-136.r1/logs/gate-2.log 2>&1` | EXIT=0 |

No fix patch or reverse-application proof was applicable because no product patch was made. The fff grep tool was not available in this session; build, test, and gate logs were searched with `rg` for errors, failures, assertions, and test/gate summaries.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p8-136.md`
