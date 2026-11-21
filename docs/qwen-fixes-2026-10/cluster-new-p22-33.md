FIXED

# Cluster new-p22-33: refuse a non-finite send pan

`mixer.send_pan` accepted a NaN pan as a successful edit. The range check `pan < -1.0 || pan > 1.0` is false for NaN, so NaN passed it. The fix adds `!std::isfinite(pan)` to that check, and the refusal goes through the existing InvalidArgs path.

## Real evidence

- `src/core/ControlCommandsMixerListen.cpp:109` (pre-fix line; now line 111): the range check that let NaN through. RED log: `'!refused.ok' returned FALSE. (a NaN send pan was accepted as a successful edit)` (`logs/test-red-1.log`).
- `src/core/ControlVocabulary.cpp:80`: `numberProperty()` declares only `type: number`. The schema does not exclude NaN, so the handler is the only guard.
- `src/core/AutomatableModel.cpp:637`: `AutomatableModel::fittedValue` clamps with `std::clamp`. For a NaN input the comparisons are false, so the value is returned unchanged. This follows from `std::clamp` semantics; it was not tested separately.

## Test matrix

Target: `MixerListenTest` (ctest #263). Logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p22-33.r1/logs/`.

| State | Result | Log |
|---|---|---|
| RED (fix absent) | 9 passed, 3 failed: `aNonFiniteSendPanIsRefused`, `aNonFiniteSendPanChangesNothing`, `aRefusedSendPanRecordsNoUndoStep` | test-red-1.log |
| Reversed (fix removed again) | same 3 failures, 9 passed | test-reversed-1.log |
| GREEN | 100% passed (1/1) | test-green-1.log |
| Reapplied GREEN | 100% passed (1/1) | test-reapplied-1.log |
| Mixer family | 100% passed, 23/23 | test-family-1.log |

## Commands and exit codes

| Step | Log (build / test) | Exit | Source of exit |
|---|---|---|---|
| Initial RED | build-red-1.log / test-red-1.log | 8 | run packet; the log holds no exit line |
| Reversed RED | build-reversed-1.log / test-reversed-1.log | 8 | run packet; the log holds no exit line |
| GREEN | test-green-1.log | 0 | run packet; the log holds no exit line |
| Reapplied GREEN | build-reapplied-1.log / test-reapplied-1.log | 0 | run packet; the log holds no exit line |
| Mixer family | test-family-1.log | 0 | run packet; the log shows 23/23 passed |

The logs show the outcomes (3 failures in both RED runs, 100% passed in both GREEN runs). They do not record the exit codes or the exact command lines. The exact commands are not in the packet or the logs.

## Feature gate

Command: `python3 .../qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1; echo EXIT=$?`
Exit: 0 (unpiped). Result: `RESULT: PASS`, 8/8 checks PASS, no FAIL.

```
feature-check kads-new-p22-33 (base fa4a5f2e7, 2 changed files; ...)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Other findings

Same non-finite gap in other command families. Not fixed here.

- `src/core/ControlCommandsMixer.cpp:213-219`: pan `minimum`/`maximum` schema plus a range check. NaN passes the same way.
- `src/core/ControlCommandsCueMix.cpp:71`: `if (level < 0.0 || level > 2.0 || pan < -1.0 || pan > 1.0)`. NaN passes the pan check.
- Reference pattern: `src/core/ControlCommandsMixerTools.cpp:250` already guards `!std::isfinite(pan)`.
