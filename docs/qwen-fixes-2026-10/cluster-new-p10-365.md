# Cluster new-p10-365

**Verdict: FIXED**

## Evidence

- [ScriptConsole.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-365/src/core/ScriptConsole.cpp:46) limits retained output to 1 Mi UTF-16 characters and 4,096 lines per run. It reserves space for one `[script output truncated: further lines dropped]` notice.
- [ScriptEngine.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-365/src/core/ScriptEngine.cpp:384) resets the capture budget when a new invocation starts. The notice is emitted to the existing log signal and console once when capture first reaches either limit.
- [ScriptEngineTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-365/tests/src/core/ScriptEngineTest.cpp:229) prints two 600,000-character lines, checks the captured list contains only the first line and notice, then checks a new run captures fresh output.
- With the production patch reversed, the regression failed because `captured.size()` was 3 instead of 2 (`test-6.log`). After restoring the patch, the same test family passed (`test-7.log`).

## Verification record

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs`. The build command below rebuilt `zene`, `tripleoscillator`, and every test target that includes `ScriptEngine.h`; both recorded proof builds exited 0.

| Check | Exact command or result | Exit |
|---|---|---:|
| No-fix RED | From `build/tests`: `ctest -R '^ScriptEngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/test-6.log; echo EXIT=$rc; exit 0` | 8 |
| Fixed GREEN | From `build/tests`: `ctest -R '^ScriptEngineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/test-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/test-7.log; echo EXIT=$rc; exit 0` | 0 |
| Affected-family count | CTest reported `1/1 Test #203: ScriptEngineTest ... Passed`; one registered test executed. | 1/1 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (logged as `gate-5.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (logged as `gate-6.log`) | 0 |

The proof used `git diff -- include/ScriptConsole.h include/ScriptEngine.h src/core/ScriptConsole.cpp src/core/ScriptEngine.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/fix.patch` (exit 0), `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/fix.patch` (exit 0), and then `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-365.r1/logs/fix.patch` (exit 0). The no-fix and restored builds used `cmake --build build --target zene tripleoscillator ScriptEngineTest ScriptMemoryBudgetTest ScriptBindingsTest ScriptStabilisationTest ScriptDawBindingTest ScriptCommandBridgeTest ScriptBudgetActionTest ControlAutomationScriptTest`, logged as `build-7.log` and `build-8.log`; each exited 0.

Earlier `bash tests/file-length-gate.sh --check` runs logged as `gate-2.log` and `gate-3.log` returned exit 1 while `ScriptEngine.cpp` grew from 662 to 692, then 670 measured lines. The capture policy and `ScriptEngine::logMessage` implementation were moved into the existing ScriptConsole source; the final unmodified ratchet passes above. No gate baseline was changed.

## Files committed

- `include/ScriptConsole.h`
- `include/ScriptEngine.h`
- `src/core/ScriptConsole.cpp`
- `src/core/ScriptEngine.cpp`
- `tests/src/core/ScriptEngineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-365.md`
