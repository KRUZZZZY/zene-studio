# Cluster new-p9-542

Verdict: **FIXED**

`VstEffectControls::loadSettings()` now deletes each prior parameter model before clearing and resizing `knobFModel` ([plugins/VstEffect/VstEffectControls.cpp:76](../../plugins/VstEffect/VstEffectControls.cpp#L76), cleanup at line 81). Destroying the old `FloatModel` also removes its QObject ownership and signal connections.

The regression invokes `VstEffectControls::loadSettings()` twice and checks that its QObject tree still contains only the two current parameter models ([tests/src/plugins/VstEffectControlsTest.cpp:69](../../tests/src/plugins/VstEffectControlsTest.cpp#L69)). A dump provider compiled only with `LMMS_TESTING` feeds the real model-loading path; the test does not call the cleanup helper. CMake compiles the production controls implementation into this test target ([tests/CMakeLists.txt:980](../../tests/CMakeLists.txt#L980)).

## Verification evidence

Each ctest invocation ran from `build/tests`. The selected affected-family count was **1 test**; GREEN ran 1/1 successfully. The RED assertion reported 4 child models where 2 were expected.

| Phase | Exact command | Result |
|---|---|---|
| Baseline target build | `cmake --build build --target zene vsteffect VstEffectControlsTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/build-2.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0 |
| RED before cleanup | `ctest -R '^VstEffectControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=8; actual 4, expected 2 |
| Capture product patch | `git diff -- plugins/VstEffect/VstEffectControls.cpp plugins/VstEffect/VstEffectControls.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/fix.patch` | EXIT=0 |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/fix.patch` | EXIT=0 |
| Reversed target build | `cmake --build build --target zene vsteffect VstEffectControlsTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0 |
| RED after reversing cleanup | `ctest -R '^VstEffectControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=8; actual 4, expected 2 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/fix.patch` | EXIT=0 |
| Restored target build | `cmake --build build --target zene vsteffect VstEffectControlsTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0 |
| GREEN after restore | `ctest -R '^VstEffectControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r2/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status; exit "$status"` | EXIT=0 |

An earlier setup build wrote `build-1.log`, but its tool response detached without returning the exit code. I did not use it as evidence; the target build was rerun and captured as `build-2.log` with EXIT=0.

## Files committed

- `plugins/VstEffect/VstEffectControls.cpp`
- `plugins/VstEffect/VstEffectControls.h`
- `tests/CMakeLists.txt`
- `tests/src/plugins/VstEffectControlsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-542.md`
