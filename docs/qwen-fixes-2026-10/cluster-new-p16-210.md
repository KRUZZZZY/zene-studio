FIXED

# cluster new-p16-210: bound print() message assembly at the capture limit

## Verdict

FIXED. `print()` in a script assembled the whole message before the 1 MiB capture cap was applied, so one large print built a message far past the cap. The fix stops conversion at the cap.

## Evidence

- `src/core/ScriptEngine.cpp:264-285` (the print binding) built the whole `QString` from every argument before the log call at `:282`. The 1 MiB cap in `src/core/ScriptConsole.cpp:66-79` was applied only after that assembly, so the cap came too late to limit memory or work.
- Reversed-fix run (`test-1.log:27`): `print assembled 40000399 chars; limit is 1048576`. The regression script prints 400 × 100000-char strings.

## Cause-level fix

- `print` stops converting once the message reaches `CaptureBuffer::characterLimit()`. A single argument that crosses the limit is truncated to the room left, so the message is at most the limit (`ScriptEngine.cpp` print loop: early `break`, and `qMin(length, room)` per argument).
- The limit is defined once. `include/ScriptConsole.h` adds `static constexpr qsizetype characterLimit() { return 1024 * 1024; }`, and `src/core/ScriptConsole.cpp` sets `CaptureCharacterLimit = CaptureBuffer::characterLimit()`.
- Nothing else changes: no audio-thread, undo, or control-command code is touched.

## Test matrix

| Case | Coverage in this change | Result |
|---|---|---|
| Exhaustion is capped (400 × 100000 chars) | New test `ScriptEngineTest::printDoesNotAssemblePastTheCaptureLimit` | RED with fix reverted (`test-1.log:27`), GREEN with fix (`test-2`, exit 0) |
| Normal sub-limit print is unchanged | Not added by this change; covered only by the existing `ScriptEngineTest` run | Full `ScriptEngineTest` binary exit 0 with fix (`test-2`); not separately verified |
| Non-string / `tostring` args via `luaL_tolstring` | Not added by this change; conversion call unchanged | Code read only; not separately tested |
| Per-run reset | Existing regression directly above the new test (`takeLogMessages() == {"fresh run"}`) | Passes in both states (`test-2` exit 0; `test-1` shows 29 other tests passing) |
| Print runs on the script worker, not the audio thread | The new test connects with `Qt::DirectConnection`, so the lambda runs on the emitting worker | Test design; no audio-thread code is changed |
| Pure Qt/Lua, no platform branch | Diff contains no `#ifdef` or platform code | Code read only |

## Commands and exit codes

The quiet runner prints only the exit code and summary, so the inner command lines are not recorded in the logs. Log directory: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-210.r1/logs/`.

| Step | Exit | Source |
|---|---|---|
| build-0 | 0 | Log ends `Built target tripleoscillator`; no EXIT line in log (inferred) |
| test-0 | 0 | `100% tests passed out of 1`; no EXIT line in log (inferred) |
| build-1 (fix reverted) | 0 | `build-1.log:37` `EXIT=0` |
| test-1 (fix reverted) | 8 | `test-1.log:97` `EXIT=8`; RED: `FAIL! printDoesNotAssemblePastTheCaptureLimit`, 40000399 chars |
| build-2 (fix restored) | 0 | `build-2.log:147` `EXIT=0` |
| test-2 (fix restored) | 0 | `test-2.log:8` `EXIT=0`; GREEN |
| test-3 (fix restored, ScriptMemoryBudgetTest) | 8 | `test-3.log:33` `EXIT=8`; see Other findings |
| fix.patch | 0 | Patch file recorded; step exit as stated in the packet |
| git apply -R | 0 | Packet record (no log line) |
| build-3 (reversed) | 0 | Log ends `Built target ScriptBudgetActionTest`; no EXIT line (inferred) |
| test-4 (reversed) | 8 | CTest `ScriptEngineTest (Failed)`; no EXIT line (inferred) |
| test-5 (reversed, ScriptMemoryBudgetTest) | 8 | CTest `ScriptMemoryBudgetTest (Failed)`; `test-5.log:17` same failure |
| git apply | 0 | Packet record (no log line) |
| build-4 (restored) | 0 | Log ends `Built target ScriptBudgetActionTest`; no EXIT line (inferred) |
| test-6 (restored) | 0 | `100% tests passed out of 1`; no EXIT line (inferred) |
| test-7 (restored, ScriptMemoryBudgetTest) | 8 | CTest `ScriptMemoryBudgetTest (Failed)`; `test-7.log:17` same failure |
| gates-1 | 0 | `RESULT: PASS`; `gates-1.log:2-5` all PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace) |

## Registration

No registration edit was needed. The changed product files already appear in `tests/fork-sources.txt`: line 1370 (`include/ScriptConsole.h`), 2215 (`src/core/ScriptConsole.cpp`), 2219 (`src/core/ScriptEngine.cpp`). The `fork-sources` gate passed (`gates-1.log:2`). `tests/src/core/ScriptEngineTest.cpp` was not among the matches for those lines; the gate result is the evidence that it is accepted.

## Other findings

- **Pre-existing failure, not caused by this change.** `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion()` fails at `tests/src/core/ScriptMemoryBudgetTest.cpp:265` (`allocate(...) == nullptr returned FALSE`). It fails identically with the fix restored (`test-3`), reversed (`test-5`), and restored (`test-7`). It is documented in `docs/qwen-fixes-2026-10/cluster-new-p16-34.md:51`. That doc says the cause was not investigated. The brief's attribution to Linux overcommit with `realloc(SIZE_MAX)` is **not verified** here.
- **TripleOscillator prerequisite.** The `tripleoscillator` target must be built before `ScriptEngineTest` runs (`build-0.log`, `build-1.log`: `Built target tripleoscillator`). With it built, `testInstrumentParameterReadWrite` logged `TripleOscillator parameterCount 27` (`test-1.log:62`, `test-4.log:62`). The environment note in `cluster-new-p16-34.md:53` (loads as `dummy`) did not reproduce in this run.

## Self-check

Written before the post-commit `submit_check` run. Its table, exit code, and the answers to any FAIL or `?` lines are recorded in the follow-up commit that edits this section.
