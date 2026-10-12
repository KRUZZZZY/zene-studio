# Cluster new-p10-465: Xpressive graph selection

Verdict: **FIXED**

`Xpressive` has five graph choices (`NUM_EXPRS == 5`), but its selected-graph model accepted values through 6. The model now uses `NUM_EXPRS - 1` as its maximum. `XpressiveView::updateLayout()` also resets any out-of-range value to W1 before selecting graph pointers.

The regression in `tests/src/plugins/PluginPortsMigrationTest.cpp:222` loads the real Xpressive module and checks that direct model assignments and `AutomatableModel::loadSettings()` both clamp 5 and 6 to graph 4.

The finding describes `selectedGraph` as persisted project state. The current Xpressive `saveSettings()` and `loadSettings()` implementations (`plugins/Xpressive/Xpressive.cpp:112,148`) do not save or restore this model. The report therefore does not claim that exact project-load path was reproduced. The out-of-domain model range is present and the view now has a defensive fallback for invalid values.

## Evidence

| Check | Exact command | Exit / result |
|---|---|---|
| Initial RED on original production code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` (run from `build/tests`) | 8; actual value 5, expected 4 |
| Reverse-patch RED proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/test-8.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` (run from `build/tests`) | 8; actual value 5, expected 4 |
| Restored-patch GREEN proof | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/test-9.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` (run from `build/tests`) | 0; `1/1` CTest tests passed |
| Affected-family count | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 1 registered CTest test (`PluginPortsMigrationTest`) |
| Reversed build | `cmake --build build --target zene xpressive PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/build-9.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| Restored build | `cmake --build build --target zene xpressive PluginPortsMigrationTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/build-10.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/gate-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/gate-2.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |

Reverse-patch commands both exited 0:

- `git diff -- plugins/Xpressive/Xpressive.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/fix.patch`
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/fix.patch`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-465.r1/logs/fix.patch`

Test-development attempts are retained in the same log folder: `build-1.log` records the initial test compile error; `test-2.log` records a null-parent GUI harness crash; and `test-3.log` records an incorrect expectation before the regression was narrowed to the model and its loader. Those attempts were corrected before the final reverse-patch proof above.

## Files in the commit

- `plugins/Xpressive/Xpressive.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-465.md`
