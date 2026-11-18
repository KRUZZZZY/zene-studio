# Cluster new-p9-350

**Verdict: FIXED**

`plugins/Patman/Patman.cpp:183` now calls `unloadCurrentPatch()` in the empty-path branch of `PatmanInstrument::setFile()` before clearing the path and returning. The regression at `tests/src/plugins/AudioPluginTest.cpp:482` loads a valid one-sample patch through the built Patman module, keeps only a `weak_ptr`, clears the patch through the `setFile` slot, and checks that the sample bank and sample are released. The test-only friend is at `plugins/Patman/Patman.h:121`; `tests/CMakeLists.txt:2428` makes the test build the Patman module it loads.

## Evidence

| Check | Command | Result |
| --- | --- | --- |
| RED, original behavior | `cd build/tests && ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | Exit 8; `clearingPatmanPatchReleasesItsSampleBank()` failed because `m_patchSamples` was not empty. |
| GREEN, fixed behavior | `cd build/tests && ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | Exit 0; 1/1 ctest passed. |
| Patch reversal proof | `cd build/tests && ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status; exit $status` | Exit 8; the same assertion failed after rebuilding without the behavior fix. |
| Restored patch | `cd build/tests && ctest -R AudioPluginTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/test-4.log 2>&1; status=$?; echo EXIT=$status; exit $status` | Exit 0; 1/1 ctest passed. |
| Final case count | `cd build/tests && ctest -R AudioPluginTest -V --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/test-5.log 2>&1; status=$?; echo EXIT=$status; exit $status` | Exit 0; 1 ctest ran, 12 QtTest functions passed, 0 failed or skipped. |

The initial standalone target selection was unavailable in the already-configured build and was not configured: `cmake --build build --target PatmanInstrumentTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/build-1.log 2>&1; echo EXIT=$?` returned exit 2 (`No rule to make target`). The regression was placed in the existing `AudioPluginTest` target instead.

Successful builds of `patman`, `AudioPluginTest`, and `zene` returned exit 0 at each verification point:

- `cmake --build build --target patman AudioPluginTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status; exit $status`
- `cmake --build build --target patman AudioPluginTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/build-4.log 2>&1; status=$?; echo EXIT=$status; exit $status`
- `cmake --build build --target patman AudioPluginTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/build-5.log 2>&1; status=$?; echo EXIT=$status; exit $status`
- `cmake --build build --target patman AudioPluginTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/build-6.log 2>&1; status=$?; echo EXIT=$status; exit $status`

The patch-reversal sequence returned exit 0 for both patch operations and reproduced the RED test:

- `git diff -- plugins/Patman/Patman.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/fix.patch; status=$?; echo PATCH_REVERSE_EXIT=$status; exit $status` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/fix.patch` — exit 0.

| Ratchet | Command | Exit |
| --- | --- | --- |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-350.r1/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status; exit $status` | 0 |

## Files committed

- `plugins/Patman/Patman.cpp`
- `plugins/Patman/Patman.h`
- `tests/CMakeLists.txt`
- `tests/src/plugins/AudioPluginTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-350.md`
