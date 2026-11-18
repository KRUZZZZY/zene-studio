# Cluster new-p10-57

Verdict: **ALREADY-FIXED**.

The reported state mismatch is absent in this worktree. `layerJson()` constructs the per-layer object directly and inserts only `id`, `index`, `base`, `plugin`, and `display_name` (`src/core/ControlCommandsRackLayers.cpp:59-68`). `layersState()` uses that function for layer-0 and every added layer (`src/core/ControlCommandsRackLayers.cpp:79-92`). There is no `level` field in the serializer. A repo-wide search found no `mixJson`, `layerSetRefusal`, or `rack.layer_set` implementation in `src` or `tests`. The finding's described implementation and setter refusal do not match this branch.

No production or test change was made, so a RED regression and the patch reversal proof do not apply: there is no buggy behavior in the current implementation to reproduce. The existing `InstrumentLayersCommandsTest` family was rebuilt and run as a smoke check; CTest reported 1/1 test passed.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| Finding symbols | `rg -n '\b(mixJson|layerSetRefusal)\b|rack\.layer_set' src tests` | Exit 1; no matches. |
| Serializer field | `rg -n 'level' src/core/ControlCommandsRackLayers.cpp` | Exit 1; no matches. |
| RED regression | Not applicable; finding is already fixed on this branch, so no regression was added. | N/A |
| Test target build | `cmake --build build --target InstrumentLayersCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-57.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | Build exit 0. |
| Existing family test | From `build/tests`: `ctest -R InstrumentLayersCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-57.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | CTest exit 0; 1/1 test passed. |
| Patch reversal proof | Not applicable; no product patch was made. | N/A |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-57.r1/logs/gate-complexity-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-57.r1/logs/gate-file-length-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | Exit 0. |

The requested `fff` grep tool was unavailable (`command -v fff`, exit 1), so targeted `rg` searches were used for the code and logs. Searches for `error`, `FAILED`, and `Assertion` across the log folder returned no matches (exit 1 each).

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p10-57.md`.
