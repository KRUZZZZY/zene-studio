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

Commit `0fe7e7852d08b19c7eae8e492969a7d7f4224bd4` (cluster new-p16-210) contains the four product and test files and this report. `git status --short` after the commit printed nothing (exit 0).

`python3 .../submit_check.py <worktree> new-p16-210 > .../logs/submit-1.log 2>&1` → EXIT=0, `RESULT: PASS - submit`.

| Check | Result |
|---|---|
| commits on the branch | PASS |
| working tree clean | PASS |
| report `docs/qwen-fixes-2026-10/cluster-new-p16-210.md` | PASS |
| tests added or changed `tests/src/core/ScriptEngineTest.cpp` | PASS |
| gates (`feature_check.py`) | PASS |

No FAIL lines. The five `?` questions, answered:

1. **Regression test fails reverted, passes with the change.** Reverted: `test-1` EXIT=8 (`FAIL! printDoesNotAssemblePastTheCaptureLimit`, 40000399 chars; `test-1.log:27`). Restored: `test-2` EXIT=0. The reversed run `test-4` is also EXIT=8 (inferred from CTest; see the table above).
2. **Deterministic.** No clock, sleep, or race in the test. Inputs are constants (`string.rep('x', 100000)` × 400 `print` args). The accumulator is a `std::atomic` max. The `DirectConnection` lambda runs on the emitting thread during `runString`, and `runString` returns synchronously (`RunResult`), so the lambda has finished before `QVERIFY`. The same synchronous pattern is used by the existing "fresh run" test directly above.
3. **Fix at the cause, siblings checked.** The cause is the single print loop in `ScriptEngine.cpp:265-290`; the log call is now `:289`, previously `:282`. Other `logMessage` producers were checked by grep: `ScriptEngine.cpp:606` (short MIDI string, fixed size) and `:767` (not inspected in this run). `CaptureBuffer::append` in `ScriptConsole.cpp` is unchanged, so the per-line and character caps still apply to every producer. **Not verified:** the body of `:767`.
4. **No collateral damage.** Messages under 1 MiB take the same path: the tab separator logic and per-argument conversion are unchanged for sub-limit totals. Only a `print` whose total would exceed 1 MiB is cut at the limit. The capture is an in-memory per-run console buffer (`CaptureBuffer::reset`), not saved project data, so saved data is not affected. Whether any other caller legitimately exceeds 1 MiB in one `print` was not checked; none is known from this change.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The pre-fix behaviour is shown by the reversed run (`test-1.log:27`, 40000399 chars), so the question does not apply.

The `ScriptMemoryBudgetTest` failure is the pre-existing one in "Other findings" and is not a submit-check item.
