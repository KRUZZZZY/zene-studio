# Cluster new-p12-50-port

Verdict: **FIXED**

`selection.save` now rejects every non-string `clips` item with `InvalidArgs` before clip resolution. The command schema also declares string items. The registry's schema validator checks the array type but not its items, so the handler performs the item check directly.

## File and regression evidence

- [ControlCommandsViewMemory.cpp](../../src/gui/ControlCommandsViewMemory.cpp:308): checks each item with `isString()` before `firstUnknownClip()`; the `clips` schema declares string items at the registration site.
- [ControlEditCommandsTest.cpp](../../tests/src/core/ControlEditCommandsTest.cpp:210): `selectionSaveRejectsNonStringClipIds` invokes `selection.save` with `clips: [17]` and expects `InvalidArgs`.
- Original-code red result: actual error kind `1` (`NotFound`), expected `3` (`InvalidArgs`). Reversing the final production patch reproduced the same assertion failure; reapplying it passed.

## Command evidence

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-50-port.r1/logs`. Build commands ran from the worktree; `ctest` ran from `build/tests`.

| Phase | Exact command | Exit |
|---|---|---:|
| Initial regression build | `cmake --build build --target zene ControlEditCommandsTest > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN | `ctest -R ControlEditCommandsTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverted-patch rebuild | `cmake --build build --target zene ControlEditCommandsTest > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Reverted-patch RED | `ctest -R ControlEditCommandsTest --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Restored-patch rebuild | `cmake --build build --target zene ControlEditCommandsTest > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Restored-patch GREEN | `ctest -R ControlEditCommandsTest --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-final-1.log 2>&1; echo EXIT=$?` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-final-2.log 2>&1; echo EXIT=$?` | 1 |

Both gate exits are 1 due to pre-existing baseline gaps on this port branch (the same regressions exist on the clean tree; this change introduced no new regressions). Complexity and file-length deltas introduced by this change are empty / only the pre-existing ControlEditCommandsTest.cpp line count (511 -> 519 code lines), matching the reference fix.

Proof patch round-trip:
- `git diff -- src/gui/ControlCommandsViewMemory.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` — EXIT=0
- `git apply .../logs/fix.patch` — EXIT=0

## Files committed

- `src/gui/ControlCommandsViewMemory.cpp`
- `tests/src/core/ControlEditCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-50-port.md`
