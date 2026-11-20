FIXED

# cluster new-p14-25: link.audio_set_enabled keeps `send` when the call omits it

## 1. Verdict evidence

- **Cause.** `mergeArgs` (`src/core/ControlCommandsLinkAudio.cpp:102`) had an unconditional `else { c.send = !c.peerAddress.empty(); }` (pre-fix lines 109-110). When a call omitted `send`, `send` was re-derived from whatever peer was already set, so a call that only sent `enabled` turned sending on.
- **Contract.** The command description states "Arguments not given keep their current value", and `docs/LINK-AUDIO.md:87` says the same. The unconditional `else` contradicted both.
- **Fix.** `src/core/ControlCommandsLinkAudio.cpp:108` changes the branch to `else if (hadPeer) { c.send = !c.peerAddress.empty(); }`. Now `send` follows the peer only when this call names a peer; otherwise it keeps its current value.
- **Doc.** `docs/LINK-AUDIO.md:87` is updated to state the rule: `send` follows `peer` only when `peer` is given and `send` is not.
- **Sole caller.** `mergeArgs` has one caller, `src/core/ControlCommandsLinkAudio.cpp:187`, so no sibling path needed changing.

## 2. Commands and results

RED/GREEN sequence, all run in the worktree `kads-new-p14-25`. Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-25.r1/logs/`.

| Step | Command (summary) | Exit | What the log shows |
|---|---|---|---|
| build-1b | build `LinkAudioStreamTest` with the new test, fix not applied | 0 | ends `Built target LinkAudioStreamTest` (build-1b.log) |
| test-1b-red | run `LinkAudioStreamTest` | 1 | `FAIL!` `omittedSendKeepsSendingOffWhileThePeerStays` at `LinkAudioStreamTestParts2.h(83)`; `Totals: 2 passed, 1 failed` (test-1b-red.log) |
| build-2 | build with fix applied | 0 | ends `Built target` (build-2.log) |
| test-2-green | run `LinkAudioStreamTest` | 0 | `Totals: 3 passed, 0 failed` (test-2-green.log) |
| test-2-family | run `LinkAudioStreamTest` + `LinkAudioCodecTest` | 0 | `100% tests passed out of 2` (test-2-family.log, `EXIT=0` line present) |
| fix.patch reverse | `git apply -R` of the fix | 0 | fix.patch saved |
| build-3-reversed | build with fix reversed | 0 | ends `Built target` (build-3-reversed.log) |
| test-3-red-reversed | run `LinkAudioStreamTest` | 1 | same `FAIL!` at `Parts2.h(83)`; `Totals: 2 passed, 1 failed` (test-3-red-reversed.log) |
| forward apply | re-apply the fix | 0 | fix.patch restored |
| build-4-restored | build with fix restored | 0 | ends `Built target` (build-4-restored.log) |
| test-4-green-restored | run `LinkAudioStreamTest` | 0 | `Totals: 3 passed, 0 failed` (test-4-green-restored.log) |
| gates-1 | project gates | 0 | `RESULT: PASS` (gates-1.log) |

**Note on exit codes.** The build and test EXIT values in the table are the values recorded for the worker runs of this session. The on-disk logs confirm the outcomes shown in the right-hand column. Most of them do not contain an `EXIT=` line, so the numeric exit codes for those steps are not independently re-checked from the log files. `test-2-family.log` is the only log that contains an `EXIT=` line.

## 3. Test matrix

One test, `LinkAudioStreamTest::omittedSendKeepsSendingOffWhileThePeerStays` (`tests/src/core/LinkAudioStreamTestParts2.h:59-122`), covers the cases below.

| Case | Lines | Assertion |
|---|---|---|
| Normal omitted-send retention | 81-89 | With `send` off and a peer set, a call that gives only `enabled` keeps `send` false, peer unchanged, state and `__transaction.before` agree. |
| Peer-supplied inference | 91-96 | A call that supplies `peer` with `send` omitted turns `send` on. |
| Explicit send override | 108-113 | A call with `send: false` while the peer stays sets `send` false. |
| Bad-peer refusal with unchanged state | 98-106 | A bad peer (`not-an-ip`) is refused; `running`, `send`, and `peer` match the state read before the call. |
| Undo/redo one step | 114-119 | `control.undo` restores `send` true; `control.redo` restores `send` false. |
| Loopback-only, listen_port 0, no sleeps | 60, 68, 94; RAII stop 73-78 | Loopback peer `127.0.0.1`, `listen_port` 0, no sleeps, no threads. The stream is stopped by a scope guard. |

The existing test `commandsAreRegisteredAndUndoable` (`tests/src/core/LinkAudioStreamTestParts1.h:438`) is not modified.

## 4. Realtime, undo, and command surface

- The change is in `mergeArgs`, which is called only from the command handler at `ControlCommandsLinkAudio.cpp:187`. The change itself adds no allocation, lock, log, or IO: it replaces an unconditional assignment with a conditional one that reads `hadPeer`.
- `mergeArgs` already converts strings with `toStdString()`. That existing behaviour is unchanged.
- The packet states that this handler runs on the command thread. That was not re-verified in this session.
- No new control command is added. No header is changed. No audio-thread code is changed.
- Undo still records the existing single before/requested step.

## 5. Other findings

- `tests/src/core/LinkAudioStreamTestParts2.h:41` has a line-number comment pointing to `ControlCommandsLinkAudio.cpp:180`. That comment is stale in HEAD and is left untouched.

## Self-check

- **RED/GREEN exit codes and log names.** RED: `test-1b-red.log` exit 1, failing at `LinkAudioStreamTestParts2.h(83)`. GREEN: `test-2-green.log` exit 0 (3 passed). Reversed RED: `test-3-red-reversed.log` exit 1 (2 passed, 1 failed, same assertion). Restored GREEN: `test-4-green-restored.log` exit 0 (3 passed). Family: `test-2-family.log` exit 0. Gates: `gates-1.log` `RESULT: PASS`. The build and test EXIT values are from the worker runs recorded this session; see the note in section 2.
- **Deterministic seams.** The new test uses loopback only (`127.0.0.1`), `listen_port` 0, and no sleeps or threads (`Parts2.h:60, 68, 94`). The stream is stopped by a scope guard (`Parts2.h:73-78`).
- **Cause and sole caller audit.** Cause: the unconditional `else` in `mergeArgs` (pre-fix lines 109-110). Sole caller: `ControlCommandsLinkAudio.cpp:187`, verified with grep over `src` and `tests`. No sibling path changed.
- **No collateral damage.** Stream config is session state and is not saved in projects (`docs/LINK-AUDIO.md:77`). The no-peer default is pinned by the existing `commandsAreRegisteredAndUndoable` test, which is unmodified.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED.
- **Sibling commands (each id listed by submit_check).**
  - `link.audio_get_state` (`ControlCommandsLinkAudio.cpp:140-152`): read-only, handler returns `stateJson()` and never calls `mergeArgs`. Not affected.
  - `link.audio_set_enabled` (`ControlCommandsLinkAudio.cpp:159`): this command. The fix is at `:108`.
  - `link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_session_tempo`, `link.set_start_stop_sync`: defined in `src/core/ControlCommandsLink.cpp`, not in the changed file. None calls `mergeArgs`: the only call site in `src` or `tests` is `ControlCommandsLinkAudio.cpp:187`, found by grep. `theStreamLeavesLinkSyncAlone` (`tests/src/core/LinkAudioStreamTestParts2.h:124`) checks that the `link.get_state` keys (`enabled`, `model`, `quantum`, `start_stop_sync`, `session_tempo`, `peer_count`) are unchanged while the audio stream runs. Not affected.
  - Additions: this change adds no commands, so there is no new sibling.
