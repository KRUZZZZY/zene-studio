# Cluster new-p9-140

## Verdict: FIXED

The finding was real. [ExprSynth.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-140/plugins/Xpressive/ExprSynth.h:58) declared `ExprFront::m_data` without initialization. [ExprSynth.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-140/plugins/Xpressive/ExprSynth.cpp:527) allocates `ExprFrontData` inside a catch-all constructor, and its destructor unconditionally deletes `m_data` at line 565. If that allocation throws, the member previously remained indeterminate.

The header now initializes `m_data` to `nullptr`. A caught allocation failure therefore leaves a safe value for `delete`. The focused regression at [ExprSynthAllocationTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-140/tests/src/plugins/ExprSynthAllocationTest.cpp:64) forces the next allocation to throw, seeds the failed object's first member with another live `ExprFrontData` pointer, and observes whether destruction frees that live data. The original code fails this check; the fixed code passes. The test is registered as CTest `ExprSynth` in [tests/CMakeLists.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-140/tests/CMakeLists.txt:1780).

The configured build tree was updated by the build-system regeneration during `cmake --build`; the test was CTest test #313. The affected family contained one CTest test.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Initial target build and CTest tree update | `cmake --build build --target zene xpressive ExprSynthAllocationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 | Built the test before the product fix and registered it in the regenerated build tree. |
| Initial RED, original header | `ctest -R '^ExprSynth$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | 8 | 1 test ran; `!deletedSeedData` failed as expected. |
| Save and reverse product fix | `git diff -- plugins/Xpressive/ExprSynth.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/fix.patch; result=$?; echo EXIT=$result` | 0 | Only the product header fix was removed. |
| Rebuild with fix reversed | `cmake --build build --target zene xpressive ExprSynthAllocationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 | Rebuilt the affected plugin and test target. |
| RED, after reversing fix | `ctest -R '^ExprSynth$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | 8 | Same test failed again; this proves the regression detects the original bug. |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/fix.patch; result=$?; echo EXIT=$result` | 0 | Restored the exact saved fix. |
| Rebuild fixed targets | `cmake --build build --target zene xpressive ExprSynthAllocationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/build-6.log 2>&1; result=$?; echo EXIT=$result` | 0 | Rebuilt `zene`, `xpressive`, and `ExprSynthAllocationTest`. |
| GREEN regression | `ctest -R '^ExprSynth$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/complexity-gate-3.log 2>&1; result=$?; echo EXIT=$result` | 0 | Pass. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/file-length-gate-3.log 2>&1; result=$?; echo EXIT=$result` | 0 | Pass. |
| Source scope registration | `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/fork-sources-gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | Pass. |
| Test registration | `bash tests/unregistered-tests-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-140.r2/logs/unregistered-tests-gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | Pass. |

## Files committed

- `plugins/Xpressive/ExprSynth.h`
- `tests/src/plugins/ExprSynthAllocationTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-140.md`
