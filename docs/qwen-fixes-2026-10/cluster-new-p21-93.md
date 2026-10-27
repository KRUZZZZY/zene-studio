Verdict: FIXED

# new-p21-93: findMixerRoute stale sender index

## Finding and QA correction

The brief's specific pointer explanation was wrong: `Mixer::mixerChannel(int)` indexes an unchecked `std::vector`, so removing a channel does not leave a null slot. However, the failure was real. `findMixerRoute` dereferenced `Engine::mixer()` and indexed the channel vector without validating the sender index. `mixer.send_pan` and `feedback.disable` also retained endpoint indices in their undo closures. Mixer UI reorders call `Mixer::moveChannelLeft` directly (`src/gui/MixerView.cpp:664-685`) without adding a journal step, so a later undo could use an in-range stale index and change a different route. An out-of-range sender segfaulted in `findMixerRoute`.

`MixerListenTest::aSendPanUndoFollowsItsChannelsThroughAReorder` reproduces the wrong-route write: after moving the destination, undo changed the other route's pan from 0.75 to 0.0. It also removes an endpoint and confirms that replay safely does nothing. `MixerListenTest::feedbackDisableUndoFollowsItsEndpointsAfterAReorder` reproduces a feedback edge restored onto the wrong channel after a reorder. `MixerListenTest::findMixerRouteRejectsAnOutOfRangeSender` reproduces the unchecked lookup as SIGSEGV. The false-positive verdict is therefore superseded by this QA finding.

## QA fix

- `src/core/ControlMixerSupport.cpp`: `findMixerRoute` now returns `nullptr` if the mixer is absent, the sender index is out of range, or the channel pointer is null.
- `src/core/ControlCommandsMixerListen.cpp`: send-pan undo and redo retain persistent channel IDs and resolve their live indices when replayed. If an endpoint was removed, replay is a no-op.
- `src/core/ControlCommandsFeedback.cpp`: feedback-disable undo retains persistent endpoint IDs and resolves them before recreating routes, skipping removed endpoints.
- `tests/src/core/MixerListenTest.cpp`: added deterministic pan/feedback reorder, endpoint-removal, and out-of-range regressions.

## Verification

Commands ran in the worktree unless noted. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-93.r1/logs/`.

| Step | Command | Exit | Evidence |
| --- | --- | --- | --- |
| Product fix build | `cmake --build build --target zene MixerListenTest -j4` | 0 | `qa-compact-feedback-build-1.log` |
| Product fix GREEN | `ctest -R '^MixerListenTest$' --output-on-failure` from `build/tests` | 0 | 1/1 CTest target passed (`qa-compact-feedback-green-1.log`). |
| Product hunks reversed build | `cmake --build build --target zene MixerListenTest -j4` | 0 | `qa-final-reversed-build-1.log` |
| Product hunks reversed | `ctest -R '^MixerListenTest$' --output-on-failure` from `build/tests` | 8 | Pan and feedback undo assertions failed; the out-of-range lookup segfaulted (`qa-final-reversed-ctest-1.log`). |
| Restored fix build | `cmake --build build --target zene MixerListenTest -j4` | 0 | `qa-final-restored-build-1.log` |
| Restored fix GREEN | `ctest -R '^MixerListenTest$' --output-on-failure` from `build/tests` | 0 | 1/1 CTest target passed (`qa-final-restored-green-1.log`). |
| Required feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-93` | 0 | Final table: all eight rows pass; no new complexity or file-length item in touched files. |

An intermediate gate run found `tests/src/core/MixerToolsTest.cpp` at 510 lines. The regression was moved into the existing, shorter `MixerListenTest.cpp`; no gate or assertion was changed. The final direct feature-check run passed. The lane's original write-jail failure did not reproduce in QA.

## Self-check

- **RED/GREEN:** With only product changes reversed, `cmake --build build --target zene MixerListenTest -j4` exited 0 and `ctest -R '^MixerListenTest$' --output-on-failure` from `build/tests` exited 8 (`qa-final-reversed-build-1.log`, `qa-final-reversed-ctest-1.log`). After restoring the product hunks, the same build exited 0 and ctest exited 0 (`qa-final-restored-build-1.log`, `qa-final-restored-green-1.log`).
- **Deterministic:** The pan test uses fixed channel IDs and pan values plus a synchronous `Mixer::moveChannelLeft(3)` call. The feedback test creates one explicit two-channel cycle and reorders a third channel. Neither uses a clock, sleep, or race. The out-of-range check passes a fixed maximum `mix_ch_t` value.
- **Cause and callers:** `rg -n 'findMixerRoute\(' src/core/*.cpp src/gui/*.cpp` exited 0. Live command handlers resolve endpoints before lookup; `recordMixerRouteStep`, send-enable undo, send-pan undo, and feedback-disable undo now resolve persistent IDs at replay; mixer overlay callers use current channel indices.
- **Sibling commands:** `feedback.disable` is the writer that retains and restores loop-closing edges, and it now resolves their stable endpoint IDs. `feedback.enable` only toggles the mode, while `feedback.get_state` only reads the mode and edge list; neither is affected. The response keeps the same endpoint ID fields.
- **Collateral:** The change does not alter project serialization, channel limits, or audio-thread behavior. The bounds guard excludes only invalid sender indices; valid current indices retain their existing behavior. Feedback route JSON retains the same stable endpoint IDs. The focused test and final feature gate pass.
- **False-positive evidence:** Not applicable to the final verdict, which is FIXED. The original null-slot description was inaccurate; `include/Mixer.h:634-637` performs unchecked vector indexing, and the stale undo path and out-of-range lookup are demonstrated above.
