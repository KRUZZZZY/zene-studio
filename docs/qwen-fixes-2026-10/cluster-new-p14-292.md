# Cluster new-p14-292

**Verdict: FIXED**

new-p14-292: FIXED
new-p12-244: FIXED
5326: ALREADY-FIXED

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

## new-p12-244 - MIDI input overflow silently loses events

**Verdict: FIXED**

### Evidence

- [ScriptEngine.cpp:803-807](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/src/core/ScriptEngine.cpp:803) `pushMidiInEvent()` used to call
  `m_midiIn.write(&event, 1);` and ignore the result: a write of zero means the ring
  buffer was full and the event was dropped, silently. It now passes the written count
  to `countDroppedMidiInEvent(...)`
  ([ScriptEngine.cpp:806](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/src/core/ScriptEngine.cpp:806)).
- [ScriptEngine.h:448](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:448) `countDroppedMidiInEvent()` counts a refused
  write into `m_midiInDropped` ([ScriptEngine.h:444](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:444)), and
  [ScriptEngine.h:386](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:386) `droppedMidiInEventCount()` exposes the
  count to the host - the same shape as the command queue's `droppedCommandCount()`.
  The non-blocking drop policy is unchanged: the buffer still drops rather than
  stalling the MIDI thread.
- The count is a plain `std::atomic<quint64>` with relaxed ordering (the discipline
  the instruction budget and the command queue's drop counter already use): the
  producer thread increments it, the host reads it. No allocation, no lock, no
  logging on the producer's path.
- [ScriptEngineTest.cpp:807](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/tests/src/core/ScriptEngineTest.cpp:807)
  `testMidiInOverflowIsCounted()` is the regression: 2000 events pushed into the
  1024-slot ring must be counted as dropped (`dropped == pushed - pending`), and the
  events that did fit must be the FIRST ones (the ring drops the new events, it does
  not overwrite the old ones).

### Verification record (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-292.r2/logs`)

| Check | Exact command | Exit |
|---|---|---:|
| RED (whole product patch reverted) | `git diff -- include/ScriptEngine.h src/core/ScriptEngine.cpp > logs/fix-b.patch` (0), `git apply -R logs/fix-b.patch` (0), `cmake --build build --target ScriptEngineTest -j8 > logs/build-12-redB.log 2>&1` | 2 (build fails: `no member named droppedMidiInEventCount` - `logs/build-12-redB.log`) |
| RED (behaviour reverted, API kept) | `git apply logs/fix-b.patch` (0), `git diff -- src/core/ScriptEngine.cpp > logs/fix-b-cpp.patch` (0), `git apply -R logs/fix-b-cpp.patch` (0), `cmake --build build --target ScriptEngineTest -j8 > logs/build-13-redB-semantic.log 2>&1` | 0 |
| RED test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-11-redB-semantic.log 2>&1` (from `build/tests`) | 8 - `FAIL! testMidiInOverflowIsCounted() 'dropped > 0' returned FALSE (the overflow was not counted)` |
| GREEN (fix restored) | `git apply logs/fix-b-cpp.patch` (0), `cmake --build build --target zene ScriptEngineTest -j8 > logs/build-14-greenB.log 2>&1` | 0 |
| GREEN test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-12-greenB.log 2>&1` (from `build/tests`) | 0 - 1/1 test passed |
| Affected family | `ctest -R '^(ScriptEngineTest|ScriptCommandBridgeTest|ScriptBudgetActionTest)$' --output-on-failure` (from `build/tests`) | 0 - 3/3 tests passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 - RESULT: PASS |

### Files committed

- `include/ScriptEngine.h`
- `src/core/ScriptEngine.cpp`
- `tests/src/core/ScriptEngineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-292.md`

## 5326 - A dangling symlink can escape the project sandbox

**Verdict: ALREADY-FIXED**

### Evidence (the code that shows it)

- The finding names `QFileInfo::exists` as the defect: it is false for a dangling
  symlink, so an ancestor walk climbs past the link and validates only the
  in-project parent. The walk is
  [deepestExistingAncestor()](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:60), and its loop condition stops at a
  symlink as well as at an existing path:
  `while (probe.size() > root.size() && !QFileInfo::exists(probe) && !QFileInfo(probe).isSymLink())`
  ([ScriptEngine.h:63](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/include/ScriptEngine.h:63)).
- That walk is what the write path validates through:
  [ScriptEngine.cpp:782](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/src/core/ScriptEngine.cpp:782)
  `QFileInfo(deepestExistingAncestor(candidate, root)).canonicalFilePath()` - a dangling
  link has no canonical path, so the result is empty and the path is refused with
  "escapes the project directory".
- The fix is committed on this branch: `3d267da16 fix(script): sandbox ancestor walk
  stepped past a dangling symlink` (it added `include/ScriptEngine.h`, changed
  `src/core/ScriptEngine.cpp` and added the regression).
- The regression is
  [testSandboxRejectsADanglingSymlinkEscape()](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-292/tests/src/core/ScriptEngineTest.cpp:499):
  it creates `escape -> <outside>/planted.txt` (a target that does NOT exist, so the
  link dangles), writes through `lmms.projectFile():write('escape', 'planted')`,
  requires the refusal, and asserts the file outside the project was NOT created. It
  also asserts the tightening did not refuse a link that stays inside the project.

### Verification record (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-292.r2/logs`)

| Check | Exact command | Exit |
|---|---|---:|
| RED (the committed fix's own condition removed) | edit `include/ScriptEngine.h:63` back to `while (probe.size() > root.size() && !QFileInfo::exists(probe))`, `cmake --build build --target ScriptEngineTest -j8 > logs/build-8-5326-red.log 2>&1` | 0 |
| RED test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-7-5326-red.log 2>&1` (from `build/tests`) | 8 - `FAIL! testSandboxRejectsADanglingSymlinkEscape() Compared values are not the same` (the write followed the dangling symlink and created the file outside the project) |
| GREEN (condition restored) | restore `include/ScriptEngine.h:63`, `cmake --build build --target zene ScriptEngineTest -j8 > logs/build-9-5326-green.log 2>&1` | 0 |
| GREEN test run | `ctest -R '^ScriptEngineTest$' --output-on-failure > logs/test-8-5326-green.log 2>&1` (from `build/tests`) | 0 - 1/1 test passed |

The RED run proves the defect was real in the shape the finding describes, and the
GREEN run proves it is already fixed on this branch: no product change was needed, so
this cluster contributes no code change.

### Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p14-292.md`

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it
  (exit codes).**
  new-p14-292: RED = exit 8 (`logs/test-2-red-semantic.log`: `FAIL!
  testScriptFileAboveTheSourceLimitIsRefused() 'error.contains("too large")' returned
  FALSE (unexpected symbol)`), GREEN = exit 0 (`logs/test-3-green.log`).
  new-p12-244: RED = exit 8 (`logs/test-11-redB-semantic.log`: `FAIL!
  testMidiInOverflowIsCounted() 'dropped > 0' returned FALSE (the overflow was not
  counted)`), GREEN = exit 0 (`logs/test-12-greenB.log`).
  5326: ALREADY-FIXED - reverting the committed fix's own condition made its existing
  regression fail (exit 8, `logs/test-7-5326-red.log`), and restoring it made the
  family pass (exit 0, `logs/test-8-5326-green.log`).
  The full product patch was also reverted with `git apply -R` in both directions
  (`logs/fix.patch`, `logs/fix-cpp.patch`, `logs/fix-b.patch`, `logs/fix-b-cpp.patch`);
  in that state the test target does not even build, because the regression uses the
  new API (`logs/build-2-red-nofix.log`, `logs/build-12-redB.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam
  or injected values it uses).**
  new-p14-292 writes a file of a known size and reads the engine's own size limit
  (`MaxScriptSourceBytes`, `memoryBudget()`); the memory budget is restored before
  any assertion, so a failure cannot leak into another test. new-p12-244 pushes a
  fixed number of events (2000) into a ring of a known capacity and compares the
  drop count with `pushed - pending`; no timing, no sleeping, no thread racing: the
  producer and the reader are the same thread in the test.
- **The fix is at the cause and complete: every caller and sibling of what you
  changed was checked and stays consistent.**
  `runFile()` has two callers - `src/core/ControlCommandsScript.cpp:216` (`script.run`)
  and `src/core/main.cpp:1536` (the menu's script runner) - and both go through the
  same bounded reader, so neither can allocate an unbounded source. `runString()`
  takes an in-memory `QString` from the caller and is deliberately NOT limited (the
  caller's own string is already in memory, and the Lua copy of it is charged to the
  budget). `readScriptSource()` keeps the old `cannot open script '%1': %2` message
  for an unopenable file, so the error a client sees for that case is unchanged.
  For the MIDI counter: `pushMidiInEvent()` is the only producer of the ring
  (`m_midiIn`), `popMidiInEvent()`/`midiInPending()` are the consumers and are
  unchanged; the counter is the same shape as the command queue's
  `droppedCommandCount()`, and no Lua binding was added (the Lua surface is a
  versioned ratchet - `tests/lua-api-surface.py` - and a bug fix must not move it).
- **No collateral damage: old projects and saved data still load as before, and any
  limit or check you added does not hit other callers that legitimately exceed it.**
  Neither change touches project data, save/load or the model: the source limit
  applies to script FILES only, and the shipped scripts and the test scripts are far
  below 16 MiB (the limit is `min(memoryBudget, 16 MiB)`, and the memory budget's
  floor is 512 KiB - a script bigger than the budget could never load into the Lua
  state anyway). The MIDI change adds a counter and changes no event's fate: the
  buffer still drops rather than blocking, and the events that fit are still the
  first ones (asserted).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
  intended contract that shows it.**
  5326: `include/ScriptEngine.h:63` (the `isSymLink()` condition in the ancestor
  walk), used by `src/core/ScriptEngine.cpp:782`, committed as `3d267da16`, with the
  regression `tests/src/core/ScriptEngineTest.cpp:499`. The RED run above (the fix's
  own condition removed) shows the finding describes a real defect and that this
  branch already carries the fix.
- **Realtime, undo and commands.** No audio-thread path was touched: `runFile()` and
  `pushMidiInEvent()` run on the UI/host thread and the script worker thread, never
  on the audio thread. `pushMidiInEvent()` still allocates nothing, takes no lock and
  logs nothing - it now only reads the ring buffer's return value and bumps a
  relaxed atomic. No new control command was added, so the `ControlCommandsSnapshot`
  canary does not move. No undo step was added or changed (neither fix touches
  journalled state).
- **Gates.** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  -> exit 0, `RESULT: PASS` (`logs/gate-2-feature-check.log`, the final run over the
  committed tree): fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity ("no new item in your
  files") and file-length ("no new item in your files") all PASS.
  `src/core/ScriptEngine.cpp` shrank from 652 to 646 code lines (the runFile() body
  moved into the header), so the grandfathered 662-line ratchet entry is not
  approached; `include/ScriptEngine.h` is 253 code lines, under the 500-line limit.
  No gate baseline was changed.
- **Completion check.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py
  <worktree> new-p14-292` -> exit 0, `RESULT: PASS - submit`
  (`logs/submit-check-1.log`): commits on the branch PASS, working tree clean PASS,
  report PASS, tests added or changed PASS, gates PASS. Every ASK item is answered
  in this section.
- **Final test run over the committed tree.**
  `cmake --build build --target ScriptEngineTest ScriptCommandBridgeTest
  ScriptBudgetActionTest ScriptBindingsTest ScriptDawBindingTest -j8` -> exit 0
  (`logs/build-16-final.log`), then `ctest -R '^(ScriptEngineTest|ScriptCommandBridgeTest|
  ScriptBudgetActionTest|LuaApiSurface|ScriptBindingsTest|ScriptDawBindingTest)$'
  --output-on-failure` from `build/tests` -> exit 0, 6/6 tests passed
  (`logs/test-14-final-family.log`).
- **Known pre-existing failure, not caused by this work.**
  `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion()`
  fails in this environment (a huge allocation succeeds because Linux overcommits).
  It fails identically with the product patch reverted (`logs/test-5-pristine-budget.log`,
  exit 8) and with the patch applied, so it is not a regression of this lane.
