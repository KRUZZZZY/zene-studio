# Cluster new-p9-542

Verdict: **FIXED**

`VstEffectControls::loadSettings()` reads the plugin parameter dump and replaces its parameter models at [VstEffectControls.cpp:71](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-542/plugins/VstEffect/VstEffectControls.cpp:71). It now calls the replacement helper at line 85. The helper at [VstEffectControls.h:48](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-542/plugins/VstEffect/VstEffectControls.h:48) deletes the prior models before creating the new set. This releases their QObject ownership and signal connections when settings are loaded again.

The regression at [VstEffectControlsTest.cpp:54](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-542/tests/src/plugins/VstEffectControlsTest.cpp:54) replaces a two-model set twice and checks that the owner still has two model children. Before the fix, the assertion failed with four children. The same failure was reproduced after applying the reverse patch.

## Verification evidence

All test commands ran from `build/tests`; each ctest invocation selected one affected-family test. RED runs returned 8, and GREEN runs returned 0.

| Phase | Exact command | Result |
|---|---|---|
| RED before fix | `ctest -R VstEffectControlsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` | EXIT=8; assertion observed 4 children, expected 2 |
| GREEN after fix | `ctest -R VstEffectControlsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0 |
| Reverse product patch | `git diff -- plugins/VstEffect/VstEffectControls.h plugins/VstEffect/VstEffectControls.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/fix.patch` | EXIT=0 |
| RED without fix | `ctest -R VstEffectControlsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status` | EXIT=8; same assertion observed 4 children, expected 2 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/fix.patch` | EXIT=0 |
| GREEN after restore | `ctest -R VstEffectControlsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status` | EXIT=0 |
| Final affected-target build | `cmake --build build --target zene vsteffect VstEffectControlsTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/build-12.log 2>&1; build_exit=$?; echo EXIT=$build_exit; exit "$build_exit"` | EXIT=0 |
| Final GREEN after build | `ctest -R VstEffectControlsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/test-5.log 2>&1; test_exit=$?; echo EXIT=$test_exit; exit "$test_exit"` | EXIT=0; ctest ran 1/1 test |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/gate-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit; exit "$gate_exit"` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-542.r1/logs/gate-2.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit; exit "$gate_exit"` | EXIT=0 |

The requested `fff` grep tool was not available in this session, so relevant log checks used single-term `rg` searches without printing whole logs.

## Files committed

- `plugins/VstEffect/VstEffectControls.cpp`
- `plugins/VstEffect/VstEffectControls.h`
- `tests/CMakeLists.txt`
- `tests/src/plugins/VstEffectControlsTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-542.md`
