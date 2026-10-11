# Cluster new-p9-418

**Verdict: FIXED**

`SerializingObject::setHook()` moved a hook's backlink to the new object without clearing the old
object's `m_hook`. The old object therefore retained a dangling pointer after the hook was
destroyed. `setHook()` now clears the previous owner's `m_hook` before moving the backlink.

## Evidence

- [src/core/SerializingObject.cpp:82](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-418/src/core/SerializingObject.cpp:82) contains `setHook()`; lines 93–98 detach the previous owner before setting the backlink.
- [tests/src/core/SerializingObjectHookTest.cpp:62](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-418/tests/src/core/SerializingObjectHookTest.cpp:62) reassigns one hook and asserts the first object is detached.
- The test is registered in [tests/CMakeLists.txt:81](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-418/tests/CMakeLists.txt:81), and the new test source and inherited source changes are recorded in `tests/fork-sources.txt` and `tests/upstream-modifications.txt`.

| Evidence | Command | Result |
| --- | --- | --- |
| Original implementation RED | `ctest -R SerializingObjectHookTest --output-on-failure` (from `build/tests`) | EXIT=8; 1/1 failed at `first.hook() == nullptr` |
| Fix restored GREEN | Same command | EXIT=0; 1/1 passed |
| Reverse-patch proof | `git diff -- src/core/SerializingObject.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs/fix.patch` | EXIT=0; rebuilt test failed again with EXIT=8 |
| Affected test family | `ctest -R 'SerializingObjectHookTest|ClipSerialisationTest' --output-on-failure` (from `build/tests`) | EXIT=0; 2/2 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=0 |

## Commands and logs

Build and test commands ran from the worktree root unless the row says `build/tests`. Each command's
output is in the listed file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs`.

| Command | Exit | Log |
| --- | ---: | --- |
| `cmake --build build --target SerializingObjectHookTest` (initial target lookup) | 2 | `build-1.log` |
| `cmake --build build --target help` | 0 | `build-2.log` |
| `cmake --build build --target all -j2` (bunny-guard rejected target `all`) | 97 | `build-3.log` |
| `cmake --build build --target cmake_check_build_system` | 0 | `build-4.log` |
| `cmake --build build --target SerializingObjectHookTest` (registered regression target, before the production edit) | 0 | `build-5.log` |
| `cmake --build build --target zene SerializingObjectHookTest ClipSerialisationTest` (each build) | 0 | `build-6.log`, `build-7.log`, `build-8.log` |
| `ctest -R SerializingObjectHookTest --output-on-failure` (from `build/tests`, original implementation) | 8 | `test-1.log` |
| Same ctest command (from `build/tests`, fixed implementation) | 0 | `test-2.log` |
| `ctest -R 'SerializingObjectHookTest|ClipSerialisationTest' --output-on-failure` (from `build/tests`) | 0 | `test-3.log` |
| `git diff -- src/core/SerializingObject.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs/fix.patch` | 0 | `proof-1.log` |
| Same ctest command (from `build/tests`, with the fix reversed) | 8 | `test-4.log` |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-418.r1/logs/fix.patch` | 0 | `proof-2.log` |
| Same ctest command (from `build/tests`, after restoring the fix) | 0 | `test-5.log` |
| `ctest -R 'SerializingObjectHookTest|ClipSerialisationTest' --output-on-failure` (from `build/tests`, after restoring the fix) | 0; 2 tests passed | `test-6.log` |
| `bash tests/complexity-gate.sh --check` | 0 | `gate-5.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-6.log` |
| `git diff --check` | 0 | terminal output |

## Files committed

- `src/core/SerializingObject.cpp`
- `tests/src/core/SerializingObjectHookTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-418.md`
