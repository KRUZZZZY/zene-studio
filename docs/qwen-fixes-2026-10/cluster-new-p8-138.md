# Cluster new-p8-138: delete all selected clips

Verdict: **FIXED**

## Finding evidence

The finding was real. `ClipsSource::remove` at `src/gui/ItemListSurfaceSources.cpp:254` used only `rows.first()`, leaving other chosen clips in the arrangement. The clips table now permits extended selection at `src/gui/ItemListPanel.cpp:255`; selection sync leaves multi-row selections intact at line 368. The source captures stable clip IDs, then invokes one `clip.delete` for each selected ID so row changes do not change the targets.

The regression is `tests/src/gui/ItemListPanelTest.cpp:188`. With the product patch reversed and the target rebuilt, it failed on `!secondRemains`. With the patch restored, it passed and also checks the clips table's extended-selection mode.

## Evidence table

| Check | Working directory | Exact command | Exit |
| --- | --- | --- | ---: |
| RED build | Worktree root | `cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/build-6.log 2>&1; build_code=$?; echo "EXIT=$build_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/build-6.log; exit "$build_code"` | 0 |
| RED test | `build/tests` | `ctest -R ItemListPanelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/test-6.log 2>&1; test_code=$?; echo "EXIT=$test_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/test-6.log; exit "$test_code"` | 8 |
| Reapply product patch | Worktree root | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/fix.patch` | 0 |
| GREEN build | Worktree root | `cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/build-7.log 2>&1; build_code=$?; echo "EXIT=$build_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/build-7.log; exit "$build_code"` | 0 |
| GREEN test | `build/tests` | `ctest -R ItemListPanelTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/test-7.log 2>&1; test_code=$?; echo "EXIT=$test_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/test-7.log; exit "$test_code"` | 0 |
| Affected-family count | `build/tests` | CTest reported `1/1` ItemListPanelTest executed | 1 |
| Complexity gate | Worktree root | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/gate-3.log 2>&1; gate_code=$?; echo "EXIT=$gate_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/gate-3.log; exit "$gate_code"` | 0 |
| File-length gate | Worktree root | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/gate-4.log 2>&1; gate_code=$?; echo "EXIT=$gate_code" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/gate-4.log; exit "$gate_code"` | 0 |

The RED test log reports `!secondRemains` false. The GREEN test log reports 100% passed, 0 failed. The reversal command used for the proof was:

```sh
git diff -- src/gui/ItemListPanel.cpp src/gui/ItemListSurfaceSources.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-138.r1/logs/fix.patch
```

Exit: 0. The rebuilt reverse-patch test failed with exit 8 as required.

## Files in this commit

- `src/gui/ItemListPanel.cpp`
- `src/gui/ItemListSurfaceSources.cpp`
- `tests/src/gui/ItemListPanelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-138.md`
