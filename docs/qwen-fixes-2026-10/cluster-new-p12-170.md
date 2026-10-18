# Cluster new-p12-170: OpulenZ render span size

## Verdict: BLOCKED

The finding is real and the behavior fix is implemented. `OpulenzInstrument` cached the constructor period at [plugins/OpulenZ/OpulenZ.cpp:164](../../plugins/OpulenZ/OpulenZ.cpp) and allocated `renderbuffer` at line 165, then `playImpl()` (line 395) rendered `frameCount` frames regardless of the supplied span. It now bounds each emulator update by `frameCount` at line 400 and writes only within the supplied span at line 408. A period-size change therefore cannot overrun the output or leave its tail unwritten. The render path reuses the constructor allocation.

The regression is registered at [tests/src/plugins/PluginPortsMigrationTest.cpp:726](../../tests/src/plugins/PluginPortsMigrationTest.cpp), implemented at line 2562, and the inherited-file reason is recorded at [tests/upstream-modifications.txt:2276](../../tests/upstream-modifications.txt).

The focused regression is RED without the production change and GREEN with it. Final acceptance is blocked by repository checks outside this change: both required ratchet gates report regressions in files not changed here, and the full plugin test family has one unrelated missing LADSPA Decimator fixture.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED family run (from `build/tests`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-1-red.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 44 passed, 2 failed, 2 skipped. The new OpulenZ assertion failed as expected; `nonRealtimeLadspaEffectsAreInvalid` also failed because the SWH Decimator fixture was unavailable. |
| Focused GREEN run with fix (from `build/tests`) | `./PluginPortsMigrationTest opulenzRendersTheProvidedSpanSize > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-2-green-focused.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Regression passed. |
| Family run with fix (from `build/tests`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-3-green-family.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 45 passed, 1 failed, 2 skipped. The only failure was the same missing Decimator fixture. |
| Reverse-patch RED proof (from `build/tests`) | `./PluginPortsMigrationTest opulenzRendersTheProvidedSpanSize > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-4-reversed-red.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 1 | After removing only the production diff and rebuilding, the OpulenZ regression failed. QtTest reported 2 passed, 1 failed. |
| Restored-patch GREEN proof (from `build/tests`) | `./PluginPortsMigrationTest opulenzRendersTheProvidedSpanSize > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-5-restored-green.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | After reapplying the production diff and rebuilding, the same regression passed. |
| Final family run (from `build/tests`) | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/test-6-final-family.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | 1 registered CTest target ran 48 QtTest cases: 45 passed, 1 failed, 2 skipped. The sole failure was the missing Decimator fixture. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/gate-3-complexity-final.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 1 | Reported six over-target functions in other files; none are among the changed files. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/gate-4-file-length-final.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 1 | Reported 14 over-500-line files in other paths; none are among the changed files. |

Affected-family count: one CTest target, 48 QtTest cases. The focused regression passes after the fix and fails when the production patch is reversed.

### Build commands

| Command | Exit |
|---|---:|
| `cmake --build build --target PluginPortsMigrationTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target zene -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `cmake --build build --target opulenz PluginPortsMigrationTest zene -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `git diff -- plugins/OpulenZ/OpulenZ.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/fix.patch` | 0 |
| `cmake --build build --target opulenz PluginPortsMigrationTest zene -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/build-5-reversed.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/fix.patch` | 0 |
| `cmake --build build --target opulenz PluginPortsMigrationTest zene -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-170.r1/logs/build-6-restored.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The requested `fff grep` tool was not available in this session; relevant log lines were searched with `rg` in the specified log folder.

## Files in this change

- `plugins/OpulenZ/OpulenZ.cpp`: render the supplied output span in bounded chunks.
- `tests/src/plugins/PluginPortsMigrationTest.cpp`: cover shorter and longer spans through the loaded plugin.
- `tests/upstream-modifications.txt`: record the inherited source change and its regression.
- `docs/qwen-fixes-2026-10/cluster-new-p12-170.md`: this evidence report.
