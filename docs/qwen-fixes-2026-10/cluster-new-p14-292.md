# Cluster new-p14-292

new-p14-292: FIXED

## new-p14-292 - Script-file loading bypasses the Lua memory budget

**Verdict: FIXED**

### Evidence

- [ScriptEngine.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/src/core/ScriptEngine.cpp:326) `runFile()` no longer reads the file itself. It calls
  `readScriptSource(path, scriptSourceLimit(memoryBudget()), source, error)`
  ([ScriptEngine.cpp:328-331](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/src/core/ScriptEngine.cpp:328)),
  which replaced the old `const QString source = QString::fromUtf8(file.readAll());` - the
  unbounded `readAll()` and the whole-file `QString` conversion were the host-side
  allocations that sat outside `ScriptMemoryState`'s Lua allocator budget.
- [ScriptEngine.h:79](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:79) `MaxScriptSourceBytes` (16 MiB) is the hard
  source-size limit; [ScriptEngine.h:91](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:91) `scriptSourceLimit()` makes the
  memory budget the tighter of the two bounds when one is set (a source that cannot fit
  in the Lua state cannot run anyway).
- [ScriptEngine.h:107](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:107) `readScriptSource()` refuses the file on its
  SIZE before reading it (`file.size() > limit`), reads it in 64 KiB chunks
  ([ScriptEngine.h:135](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:135)) and stops at the limit, so the
  conversion to `QString` never sees more than `limit` bytes.
- [ScriptEngineTest.cpp:745](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/tests/src/core/ScriptEngineTest.cpp:745)
  `testScriptFileAboveTheSourceLimitIsRefused()` is the regression: a file one byte above
  the limit (with a VALID api header, so the refusal cannot be about the version or the
  syntax) must be refused with "too large"; a file above a 1 MiB memory budget must be
  refused the same way; a script inside the limit must still run.

### Verification record (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-292.r2/logs`)

| Check | Exact command | Exit |
|---|---|---:|
| Baseline (before the fix) | `cmake --build build --target ScriptEngineTest -j8 > logs/build-baseline.log 2>&1` then `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-baseline.log 2>&1` (from `build/tests`) | 0 / 0 |
| RED (whole product patch reverted) | `git diff -- include/ScriptEngine.h src/core/ScriptEngine.cpp > logs/fix.patch` (0), `git apply -R logs/fix.patch` (0), `cmake --build build --target ScriptEngineTest -j8 > logs/build-2-red-nofix.log 2>&1` | 2 (build fails: `MaxScriptSourceBytes was not declared`, `no member named droppedMidiInEventCount` - `logs/build-2-red-nofix.log`) |
| RED (behaviour reverted, API kept) | `git apply logs/fix.patch` (0), `git diff -- src/core/ScriptEngine.cpp > logs/fix-cpp.patch` (0), `git apply -R logs/fix-cpp.patch` (0), `cmake --build build --target ScriptEngineTest -j8 > logs/build-3-red-semantic.log 2>&1` | 0 |
| RED test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-2-red-semantic.log 2>&1` (from `build/tests`) | 8 - `FAIL! testScriptFileAboveTheSourceLimitIsRefused() 'error.contains("too large")' returned FALSE (unexpected symbol)`: the old code read and converted the 16 MiB file before refusing it |
| GREEN (fix restored) | `git apply logs/fix-cpp.patch` (0), `cmake --build build --target zene ScriptEngineTest -j8 > logs/build-4-green.log 2>&1` | 0 |
| GREEN test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-3-green.log 2>&1` (from `build/tests`) | 0 - 1/1 test passed |
| Affected family | `ctest -R '^(ScriptEngineTest|ScriptCommandBridgeTest|ScriptBudgetActionTest)$' --output-on-failure > logs/test-6-family-green.log 2>&1` (from `build/tests`) | 0 - 3/3 tests passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1-feature-check.log 2>&1` | 0 - RESULT: PASS (9/9 gates) |

### Files committed

- `include/ScriptEngine.h`
- `src/core/ScriptEngine.cpp`
- `tests/src/core/ScriptEngineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-292.md`
