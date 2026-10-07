# Cluster new-p8-177: short notes hide probability

**Verdict: FIXED**

`src/gui/noteeditor/NoteCanvas.cpp:371-382` computed probability alpha, then the `r.width() <= 4` branch filled a narrow note with an opaque color and returned before `paintEdge()`. At low zoom, a short probabilistic note therefore looked like an always-played note. The branch now applies that alpha to both the ordinary and selected sliver fill.

The regression in `tests/src/gui/LaneReadoutTest.cpp:202-231` renders a one-tick note at 100% and 50% probability, samples its center pixel, and checks both selected and unselected states. It failed on the original renderer and passed with the fix.

## Evidence

| Check | Result |
| --- | --- |
| RED before fix | `LaneReadoutTest`, 1/1 CTest test failed (exit 8); the pixel-color assertion failed |
| GREEN after fix | `LaneReadoutTest`, 1/1 passed (exit 0) |
| Reversal proof | Removing only the production patch, rebuilding `zene` and `LaneReadoutTest`, and rerunning the test failed (exit 8); restoring the patch and repeating passed (exit 0) |
| Affected-family count | 1 CTest executable; 9 QtTest entries including init and cleanup |
| Complexity gate | exit 0 |
| File-length gate | exit 0 |
| Files committed | `src/gui/noteeditor/NoteCanvas.cpp`, `tests/src/gui/LaneReadoutTest.cpp`, this report |

The fff grep tool was not available in this session (`command -v fff` returned 1), so I filtered the logs with `rg` for the requested error, FAILED, and Assertion terms.

## Commands and observed exit codes

All build, test, and gate output was written to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs`.

```sh
cmake --build build --target LaneReadoutTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-1.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
ctest -R '^LaneReadoutTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-1.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=8 (RED; initial regression assertion)
cmake --build build --target LaneReadoutTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-2.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
ctest -R '^LaneReadoutTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-2.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=8 (RED; final regression test on original renderer)
cmake --build build --target LaneReadoutTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-3.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
ctest -R '^LaneReadoutTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-3.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0 (GREEN)
git diff -- src/gui/noteeditor/NoteCanvas.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"
# EXIT=0
cmake --build build --target zene LaneReadoutTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-4-red-no-fix.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-4-red-no-fix.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
ctest -R '^LaneReadoutTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-4-red-no-fix.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-4-red-no-fix.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=8 (RED; reversal proof)
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"
# EXIT=0
cmake --build build --target zene LaneReadoutTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-5-green-restored.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/build-5-green-restored.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
ctest -R '^LaneReadoutTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-5-green-restored.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/test-5-green-restored.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0 (GREEN; reversal proof restored)
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/gate-1-complexity.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/gate-1-complexity.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/gate-2-file-length.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-177.r1/logs/gate-2-file-length.log; printf 'EXIT=%s\n' "$result"; exit "$result"
# EXIT=0
```
