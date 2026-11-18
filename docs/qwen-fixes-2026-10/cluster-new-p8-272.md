# Cluster new-p8-272 — FIXED

`AutomationClip::thin()` kept custom-curve nodes in its removal mask but simplified across them. The custom segment's immediate endpoint could then be removed, extending a Hold or another custom curve into later segments. The fix retains that endpoint and simplifies independently between retained anchors.

## Evidence

| Check | Exact command | Exit / result |
|---|---|---|
| RED, production patch reversed | `ctest -R AutomationThinTest --output-on-failure` (cwd `build/tests`) | 8; `valueAt(75)` was 25, expected 75 (`test-8.log`) |
| GREEN, patch restored | `ctest -R AutomationThinTest --output-on-failure` (cwd `build/tests`) | 0; 1/1 test passed (`test-9.log`) |
| Affected family | `ctest -R 'Automation(Lanes|Thin)Test' --output-on-failure` (cwd `build/tests`) | 0; 2/2 targets passed (`test-10.log`) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 (`gate-5.log`) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 (`gate-6.log`) |

The regression is in `tests/src/core/AutomationThinTest.cpp:41-57`. Its linear values at ticks 0, 25, 50, 75, and 100 have a Hold curve at tick 25; thinning must retain tick 50 and keep the value at tick 75 equal to 75.

Implementation evidence: `src/core/AutomationClipTransforms.cpp:404-427` marks custom segment endpoints and simplifies between anchors; `:449-450` calls those helpers from `thin()`.

## Build and sensitivity commands

- `cmake --build build --target zene AutomationThinTest` — exit 0 after reversing the product patch (`build-10.log`).
- `cmake --build build --target zene AutomationThinTest AutomationLanesTest` — exit 0 after restoring the patch (`build-11.log`).
- `cmake --build build --target tripleoscillator` — exit 0; this plugin is needed by `AutomationLanesTest` (`build-4.log`).
- `git diff -- src/core/AutomationClipTransforms.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-272.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-272.r1/logs/fix.patch` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-272.r1/logs/fix.patch` — exit 0.

Both ratchets were rerun after the final changes in `--check` mode; neither baseline was changed. The build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-272.r1/logs`.

## Files committed

- `src/core/AutomationClipTransforms.cpp`
- `tests/src/core/AutomationThinTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-272.md`
