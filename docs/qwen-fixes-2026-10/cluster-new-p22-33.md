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

Sources: `logs/reversal-transcript-1.log` (run dir `new-p22-33.r1/logs/`) and `logs/reapply-transcript-1.log` (clean-tree reverse, reapply, rebuild, focused test, and tree checks, each with its `EXIT=` line). Line numbers below are in `reversal-transcript-1.log` unless a row names `reapply-transcript-1.log`. Each command line is followed by its `EXIT=` line.

| Step | Command (as in transcript) | Transcript lines | Exit |
|---|---|---|---|
| Write product-only patch from commit | `git diff 42ae1c823f^ 42ae1c823f -- src/core/ControlCommandsMixerListen.cpp > logs/fix.patch` | 1-2 | 0 |
| Reverse-apply fix (RED state) | `git apply -R logs/fix.patch` | 3-4 | 0 |
| Rebuild, reversed | `cmake --build <worktree>/build --target zene MixerListenTest` | 5, 44 | 0 |
| Focused ctest, reversed (RED) | `ctest -R '^MixerListenTest$' --output-on-failure` (in `build/tests`) | 45, 137 | 8 |
| Reapply fix | `git apply logs/fix.patch` (after `git apply -R logs/fix.patch` on the clean tree) | reapply-transcript-1.log 1-4; reversal-transcript-1.log 138-139 | EXIT=0 (`reapply-transcript-1.log` line 4) |
| Check fix is back in tree | `git diff --quiet HEAD -- src/core/ControlCommandsMixerListen.cpp` | 141-142 | 0 |
| Rebuild, reapplied | `cmake --build /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p22-33/build --target zene MixerListenTest` | reapply-transcript-1.log 5-44; reversal-transcript-1.log 181 | EXIT=0 (`reapply-transcript-1.log` line 44) |
| Focused ctest, reapplied (GREEN) | `ctest -R '^MixerListenTest$' --output-on-failure` (in `build/tests`) | 183, 191 | 0 |
| Mixer family | `ctest -R 'Mixer' --output-on-failure` (in `build/tests`) | 192, 244 | 0 |
| Feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 245, 256 | 0 |

**Reversed RED and initial RED are the same pre-fix state.** Both run with the fix absent. The transcript holds the exit for that state (line 137, EXIT=8, the three failures at lines 45-137). The initial RED log (`build-red-1.log` / `test-red-1.log`) has no exit line, so its exit is the same pre-fix state recorded in the transcript, not its own log.

**Reapply exit, recorded.** The `EXIT=` line at `reversal-transcript-1.log` line 139 is empty because the shell is zsh, which does not set `PIPESTATUS`. The reapply was rerun from the clean committed tree in `reapply-transcript-1.log`: `git apply -R` is at lines 1-2 (`EXIT=0`), `git apply` is at lines 3-4 (`EXIT=0`), the reapplied build is at lines 5-44 (`EXIT=0`), the focused ctest is at lines 45-53 (`EXIT=0`), and the HEAD diff check and `git status --short` are at lines 54-57 (both `EXIT=0`, status empty).

The logs show the outcomes (3 failures in both RED runs, 100% passed in both GREEN runs). The exit codes and command lines above come from the transcript only.

## Feature gate

Command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p22-33`
Transcript: lines 245-256 of `logs/reversal-transcript-1.log`. Exit: 0 (line 256). Result: `RESULT: PASS`, 8/8 checks PASS, no FAIL. The check block below is from the earlier run (`logs/feature-check-1.log`).

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

## Self-check

Answers to the five `?` items of the pre-submit run (`logs/submit-check-1.log`). Verdict is FIXED (line 1).

1. **Test fails reverted, passes with the change.** RED with the fix absent: `MixerListenTest` ctest exit 8, 3 failed (`aNonFiniteSendPanIsRefused`, `aNonFiniteSendPanChangesNothing`, `aRefusedSendPanRecordsNoUndoStep`), `reversal-transcript-1.log` lines 45-137 and EXIT=8 at line 137. Reversed again in `test-reversed-1.log`. Restored GREEN: ctest exit 0 at `reversal-transcript-1.log` line 191 and, from a clean tree, `reapply-transcript-1.log` lines 1-4 (apply) and 45-53 (ctest, EXIT=0). Per-test exit codes are not recorded: ctest reports one entry (`MixerListenTest`, #263), so the three tests share exit 8 (red) and exit 0 (green). Each test's own red result is in `test-red-1.log`.
2. **Deterministic.** No wall-clock, sleep or thread race. Each test calls `Engine::getSong()->clearProject()`, creates channels until there are at least 3, uses fixed channel indices 1 and 2 (`MixerListenTest.cpp:289-341`), and uses constant inputs: `std::numeric_limits<double>::quiet_NaN()` and the literal `0.5`. The undo test checks the state after `control.undo` with no timing.
3. **Fix complete, callers consistent.** The diff touches only `sendPan` (`ControlCommandsMixerListen.cpp:110-111`): `!std::isfinite(pan) ||` is added to the existing range check. No shared helper is added or changed, so no other command is affected. Callers of `mixer.send_pan` are the ctest cases in `MixingRoutingTest.cpp:137-168` (including the missing-send `NotFound` case at 167-168), the menu entry `ShellCommandMenus.cpp:413` (passes pan 0.0, so the NaN branch is not reached from the menu), and the reversibility-table entry in `ControlReversibilityTableMixing.cpp` (a table entry, not a caller). Reorder/renumbering is covered by `MixingRoutingTest.cpp:182` (`aRouteUndoStepFollowsItsChannelsThroughARenumbering`). Sibling commands: the grep of quoted ids in `src/core` finds 49 distinct `mixer.*` ids: add_channel, adjust, apply_masking_proposal, channel_eq, channel_format, clear_clips, console_apply, console_strip, cpu_load, create_return, crossfader, cue_master, cue_send, downmix_options, eq_compare, find, get_state, hardware_get, hardware_insert, hardware_insert_ping, hardware_output, listen, listen_mode, masking_report, options, order_by_tracks, post_inserts, remove_channel, rename_channel, route_remove, route_to, send_enable, send_many, send_meters, send_to, set_many, set_pan, set_strip, set_volume, sidechain_to, snapshot_list, snapshot_recall, snapshot_remove, snapshot_store, solo_mode, strip_get, surround_get, surround_pan. All are unchanged and none is on this path except `mixer.send_pan`. `mixer.set_pan` and the cue sends (`mixer.cue_send`, `ControlCommandsCueMix.cpp:71`) have the same NaN gap and are listed under Other findings, not fixed here.
4. **No collateral damage.** The change adds one disjunct to an existing check. Every finite pan in -1..1 takes the same path as before, and the valid 0.5 edit in `aRefusedSendPanRecordsNoUndoStep` still succeeds with one undo step. Only values that were never valid (NaN, and infinities, which the old check already refused) change behaviour. Saved projects store pan through the model, not through this command, so old projects and saved data load as before. Commit `42ae1c823f` touches only `ControlCommandsMixerListen.cpp` and the test file; no project load/save file is in the diff.
5. **Verdict not ALREADY-FIXED or FALSE-POSITIVE.** Not applicable; the verdict is FIXED, with the guard at `ControlCommandsMixerListen.cpp:111`.

Execution context (from the audio-thread question): `sendPan` runs as a `ControlRegistry::invoke` command (`ControlRegistry.cpp:238`), dispatched through `runOnUiThread` (`ControlRegistry.cpp:219`, `:274`). The guard adds no allocation, lock or log; it only reads `pan`. The refusal uses the existing `InvalidArgs` failure path.
