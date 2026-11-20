FIXED

# cluster new-p14-25: link.audio_set_enabled keeps `send` when the call omits it

## 1. Verdict evidence

- **Cause.** `mergeArgs` (`src/core/ControlCommandsLinkAudio.cpp:102`) had an unconditional `else { c.send = !c.peerAddress.empty(); }` (pre-fix lines 109-110). When a call omitted `send`, `send` was re-derived from whatever peer was already set, so a call that only sent `enabled` turned sending on.
- **Contract.** The command description states "Arguments not given keep their current value", and `docs/LINK-AUDIO.md:87` says the same. The unconditional `else` contradicted both.
- **Fix.** `src/core/ControlCommandsLinkAudio.cpp:108` changes the branch to `else if (hadPeer) { c.send = !c.peerAddress.empty(); }`. Now `send` follows the peer only when this call names a peer; otherwise it keeps its current value.
- **Doc.** `docs/LINK-AUDIO.md:87` is updated to state the rule: `send` follows `peer` only when `peer` is given and `send` is not.
- **Sole caller.** `mergeArgs` has one caller, `src/core/ControlCommandsLinkAudio.cpp:187`, so no sibling path needed changing.

## 2. Commands and results

RED/GREEN sequence, all run in the worktree `kads-new-p14-25`. Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-25.r2/logs/`. Each r2 log ends with its own `EXIT=` line, and the EXIT values below are read from those lines.

| Step | Command (summary) | Exit | What the log shows |
|---|---|---|---|
| fix.patch | `git diff fa4a5f2e78 39a57e71d7 -- src/core/ControlCommandsLinkAudio.cpp docs/LINK-AUDIO.md` saved to fix.patch | 0 (from the generating command; fix.patch itself has no EXIT line) | 2 `diff --git` entries (fix.patch) |
| apply-reverse | `git apply -R` of the fix | 0 | `EXIT=0` (apply-reverse.log) |
| build-1-reversed | build with fix reversed | 0 | `EXIT=0` (build-1-reversed.log) |
| test-1-red | run `LinkAudioStreamTest` | 1 | `FAIL!` `omittedSendKeepsSendingOffWhileThePeerStays` on `'!kept.result.value(QStringLiteral("send")).toBool()' returned FALSE`; `Totals: 2 passed, 1 failed`; `EXIT=1` (test-1-red.log) |
| apply-forward | re-apply the fix | 0 | `EXIT=0` (apply-forward.log) |
| build-2-restored | build with fix restored | 0 | `EXIT=0` (build-2-restored.log) |
| test-2-green | run `LinkAudioStreamTest` | 0 | `Totals: 3 passed, 0 failed`; `EXIT=0` (test-2-green.log) |
| test-3-family | run `LinkAudioStreamTest` + `LinkAudioCodecTest` | 0 | `100% tests passed out of 2`; `EXIT=0` (test-3-family.log) |
| gates-1 | project gates | 0 | `RESULT: PASS`; `EXIT=0` (gates-1.log) |
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
- Thread evidence (code read in this session): `ControlRegistry::invoke` (`src/core/ControlRegistry.cpp:238`) calls `dispatch` (`:240`). `runHandler` (`:441`) calls `runHandlerGuarded` (`:492`), and `runHandlerGuarded` (`:422`) calls the handler synchronously at `:427`. `mergeArgs` is reached only through this path, so the handler runs in the thread that invoked the control command or test. No engine process or render path calls it: the sole-caller audit in section 1 found `ControlCommandsLinkAudio.cpp:187` as the only caller of `mergeArgs`.
- No new control command is added. No header is changed. No audio-thread code is changed.
- Undo still records the existing single before/requested step.

## 5. Other findings

- `tests/src/core/LinkAudioStreamTestParts2.h:41` has a line-number comment pointing to `ControlCommandsLinkAudio.cpp:180`. That comment is stale in HEAD and is left untouched.

## Self-check

- **RED/GREEN exit codes and log names.** RED: `test-1-red.log` ends with `EXIT=1`, shows `FAIL!` on the send-retention assertion (`omittedSendKeepsSendingOffWhileThePeerStays`, `Totals: 2 passed, 1 failed`). GREEN: `test-2-green.log` ends with `EXIT=0` and shows `Totals: 3 passed, 0 failed`. Family: `test-3-family.log` ends with `EXIT=0` and shows `100% tests passed out of 2`. Gates: `gates-1.log` ends with `EXIT=0` and shows `RESULT: PASS`. All four EXIT values were read from the r2 log files.
- **Deterministic seams.** The new test uses loopback only (`127.0.0.1`), `listen_port` 0, and no sleeps or threads (`Parts2.h:60, 68, 94`). The stream is stopped by a scope guard (`Parts2.h:73-78`).
- **Cause and sole caller audit.** Cause: the unconditional `else` in `mergeArgs` (pre-fix lines 109-110). Sole caller: `ControlCommandsLinkAudio.cpp:187`, verified with grep over `src` and `tests`. No sibling path changed.
- **No collateral damage.** Stream config is session state and is not saved in projects (`docs/LINK-AUDIO.md:77`). The no-peer default is pinned by the existing `commandsAreRegisteredAndUndoable` test, which is unmodified.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED.
- **Sibling commands (each id listed by submit_check).**
  - `link.audio_get_state` (`ControlCommandsLinkAudio.cpp:140-152`): read-only, handler returns `stateJson()` and never calls `mergeArgs`. Not affected.
  - `link.audio_set_enabled` (`ControlCommandsLinkAudio.cpp:159`): this command. The fix is at `:108`.
  - `link.get_state`, `link.set_enabled`, `link.set_quantum`, `link.set_session_tempo`, `link.set_start_stop_sync`: defined in `src/core/ControlCommandsLink.cpp`, not in the changed file. None calls `mergeArgs`: the only call site in `src` or `tests` is `ControlCommandsLinkAudio.cpp:187`, found by grep. `theStreamLeavesLinkSyncAlone` (`tests/src/core/LinkAudioStreamTestParts2.h:124`) checks that the `link.get_state` keys (`enabled`, `model`, `quantum`, `start_stop_sync`, `session_tempo`, `peer_count`) are unchanged while the audio stream runs. Not affected.
  - Additions: this change adds no commands, so there is no new sibling.
