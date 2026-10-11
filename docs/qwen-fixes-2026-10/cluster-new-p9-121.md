# Cluster new-p9-121

Verdict: **FIXED**

`EqCurve` began with `m_modelChanged` false at `plugins/Eq/EqCurve.cpp:653`, while its first `paint()` only built the cached response image under the dirty check at `plugins/Eq/EqCurve.cpp:671`. `EqParameterWidget` creates the curve at `plugins/Eq/EqParameterWidget.cpp:74`; the dialog then applies each saved band with `changeHandle()` at `plugins/Eq/EqControlsDialog.cpp:137`. That setup requests a repaint but does not dirty the curve cache.

The constructor now starts the curve dirty. The new `EqCurveTest` applies a saved active high shelf after curve construction and checks the first paint for the response line at `tests/src/gui/EqCurveTest.cpp:94`. The test failed against the original code and failed again with the production patch removed.

The supplied `tests/reference/Eq/EqCurve.cpp` is a frozen reference source, not a registered QtTest target. `EqCurveTest` is registered in `tests/CMakeLists.txt` and compiles the production `plugins/Eq/EqCurve.cpp` implementation.

## Evidence

The affected family is `EqCurveTest`: **1 test executed** (`1/1`). The RED assertion was `foundResponsePixel returned FALSE`.

| Check | Exact command | Exit |
|---|---|---:|
| Initial build attempt (the new target was not yet generated) | `cmake --build build --target zene EqCurveTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 2 |
| Rebuilt original code and test target | `cmake --build build --target zene EqCurveTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/build-red-2.log 2>&1; echo EXIT=$?` | 0 |
| RED, original code | `ctest -R '^EqCurveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN build | `cmake --build build --target zene EqCurveTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` | 0 |
| GREEN test | `ctest -R '^EqCurveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Save and remove production fix | `git diff -- plugins/Eq/EqCurve.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target zene EqCurveTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/build-proof-red-1.log 2>&1; echo EXIT=$?` | 0 |
| Removal proof RED | `ctest -R '^EqCurveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/test-proof-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with fix restored | `cmake --build build --target zene EqCurveTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/build-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Removal proof GREEN | `ctest -R '^EqCurveTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/test-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/complexity-gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-121.r1/logs/file-length-gate-3.log 2>&1; echo EXIT=$?` | 0 |

The first build attempt returned 2 because the generated Makefile had not exposed the new target yet; the immediate retry regenerated the target and completed. The original-code and removal-proof test failures both reached the regression assertion after a successful rebuild.

## Files in the commit

- `plugins/Eq/EqCurve.cpp`
- `tests/src/gui/EqCurveTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-121.md`
