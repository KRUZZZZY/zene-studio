# Cluster new-p9-412

**Verdict: FIXED**

The finding is real. `SaSpectrumView::drawSpectrum()` reads `m_decaySum` at
`plugins/SpectrumAnalyzer/SaSpectrumView.cpp:182` before it can call
`refreshPaths()` at line 185, where the value is later reset. The member now
starts at `0.0f` in `plugins/SpectrumAnalyzer/SaSpectrumView.h:107`.

The new regression is `tests/src/core/SaSpectrumViewTest.cpp:9-17`, registered
as CTest `SaSpectrumView` in `tests/CMakeLists.txt:1157-1162`. It checks the
member declaration directly because constructing this widget requires the
full `GuiApplication` singleton. The same test failed against the original
declaration and after the fix was reversed, then passed with the fix restored.

## Verification evidence

All build and test output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs`.
The CTest family ran one test.

| Stage | Exact command and working directory | Exit | Result |
|---|---|---:|---|
| Original RED | In `build/tests`: `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/4-test-red.log 2>&1; status=$?; echo EXIT=$status` | 8 | 1/1 failed on the missing initializer |
| Fixed GREEN | In `build/tests`: `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/7-test-green.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 passed |
| Reverse patch | In worktree root: `git diff -- plugins/SpectrumAnalyzer/SaSpectrumView.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Production fix removed for proof |
| Reverted RED | In `build/tests`: `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/10-test-revert.log 2>&1; status=$?; echo EXIT=$status` | 8 | 1/1 failed on the same assertion |
| Restore patch | In worktree root: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 | Production fix restored |
| Final GREEN | In `build/tests`: `ctest -R '^SaSpectrumView$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-412.r1/logs/13-test-final.log 2>&1; status=$?; echo EXIT=$status` | 0 | 1/1 passed |

The production rebuild command was
`cmake --build build --target zene analyzer`; it exited 0 before the original
RED run, after the fix, after reversal, and after restoration (logs 2, 5, 8,
and 11). The test/reference rebuild command, run from `build/tests`, was
`cmake --build . --target SaSpectrumViewTest partc_ref_analyzer`; it exited 0
for each corresponding stage (logs 3, 6, 9, and 12).

The initial combined build invocation from the worktree root,
`cmake --build build --target zene analyzer partc_ref_analyzer SaSpectrumViewTest`,
exited 2 because the test target is only available from the nested
`build/tests` build. The split commands above succeeded.

Required gates:

- `bash tests/complexity-gate.sh --check` — exit 0 (log 14).
- `bash tests/file-length-gate.sh --check` — exit 0 (log 15).

## Files in the commit

- `plugins/SpectrumAnalyzer/SaSpectrumView.h`
- `tests/src/core/SaSpectrumViewTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-412.md`
