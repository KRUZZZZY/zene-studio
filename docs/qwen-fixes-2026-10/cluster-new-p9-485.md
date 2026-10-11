# Cluster new-p9-485

**Verdict: FIXED**

`src/core/TempoSyncKnobModel.cpp:160` now marks the direct `m_custom.dataChanged` to `updateCustom()` connection unique. Re-entering Custom mode therefore retains one connection and one update per meter edit. The regression is `tests/src/core/AutomatableModelTest.cpp:121`, which switches Custom → None → Custom, edits the numerator, and checks the update count.

## Evidence

| Check | Result |
| --- | --- |
| RED before fix | `ctest -R AutomatableModelTest --output-on-failure` exited 8; `updateCount` was 5, expected 4. One test ran. |
| GREEN after fix | Same ctest command exited 0; 1/1 test passed. |
| Revert proof | Reverted only the production patch, rebuilt `zene` and `AutomatableModelTest`, and reran ctest: exit 8, again 5 vs 4. |
| Restore proof | Reapplied the production patch, rebuilt `zene` and `AutomatableModelTest`, and reran ctest: exit 0, 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check`: exit 0. |
| File length gate | `bash tests/file-length-gate.sh --check`: exit 0. |

Build and test transcripts are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-485.r1/logs/` as `build-1.log` through `build-4.log`, `test-1.log` through `test-4.log`, and `gate-1.log` / `gate-2.log`. The fff grep tool was not available in this session; each transcript was searched with one-term `rg -n` checks.

## Commands and exit codes

The following are the build, test, patch proof, and gate commands run. Build/test output was redirected to the numbered transcript named above; each command's exit status was captured separately.

```text
cmake --build build --target AutomatableModelTest                         EXIT=0 (build-1.log)
ctest -R AutomatableModelTest --output-on-failure                         EXIT=8 (test-1.log; RED)

cmake --build build --target zene AutomatableModelTest                    EXIT=0 (build-2.log)
ctest -R AutomatableModelTest --output-on-failure                         EXIT=0 (test-2.log; 1/1 passed)

git diff -- src/core/TempoSyncKnobModel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-485.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-485.r1/logs/fix.patch
                                                                          EXIT=0
cmake --build build --target zene AutomatableModelTest                    EXIT=0 (build-3.log)
ctest -R AutomatableModelTest --output-on-failure                         EXIT=8 (test-3.log; reverted fix, RED)

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-485.r1/logs/fix.patch
                                                                          EXIT=0
cmake --build build --target zene AutomatableModelTest                    EXIT=0 (build-4.log)
ctest -R AutomatableModelTest --output-on-failure                         EXIT=0 (test-4.log; 1/1 passed)

bash tests/complexity-gate.sh --check                                     EXIT=0 (gate-1.log)
bash tests/file-length-gate.sh --check                                    EXIT=0 (gate-2.log)
```

## Files in the fix commit

- `src/core/TempoSyncKnobModel.cpp`
- `tests/src/core/AutomatableModelTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-485.md`
