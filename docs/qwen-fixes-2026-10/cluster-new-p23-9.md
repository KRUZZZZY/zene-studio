Verdict: FALSE-POSITIVE

new-p23-9: mixerViewState does not dereference a stale channel index

Evidence. The finding names `src/core/ControlCommandsMixerView.cpp:142`. That file now lives at `src/gui/ControlCommandsMixerView.cpp`; the line numbers moved. The relevant code is:

- [ControlCommandsMixerView.cpp:133](src/gui/ControlCommandsMixerView.cpp:133) `mixerViewState()` takes `const int sends = gui::mixeroverlays::sendsOnFaders();` at [line 140](src/gui/ControlCommandsMixerView.cpp:140). Both `mixerChannel(sends)` reads ([lines 141-142](src/gui/ControlCommandsMixerView.cpp:141)) are guarded by `sends >= 0`. The finding's premise is that `sends` can be `>= 0` but out of range.
- [MixerOverlays.cpp:121-129](src/gui/MixerOverlays.cpp:121) `sendsOnFaders()` never returns a raw stored index. It returns `-1` for no send, or loops over `0 .. numChannels()-1` and returns `i` only when `mixerChannel(i)->id()` equals the stored id. So every non-negative value it returns is already in range at the moment of return.
- [MixerOverlays.cpp:46](src/gui/MixerOverlays.cpp:46) and [MixerOverlays.cpp:132-135](src/gui/MixerOverlays.cpp:132) (`setSendsOnFaders`) show the state is a channel **id** (`s_sendsOnFadersId`), not an index. A removed channel's id simply no longer matches, so `sendsOnFaders()` returns `-1` after removal, which takes the empty branch. The "stale index" the finding describes cannot exist in this design.
- No channel can be removed between the lookup at [line 140](src/gui/ControlCommandsMixerView.cpp:140) and the dereference at [line 141](src/gui/ControlCommandsMixerView.cpp:141). Both run in one synchronous function call. The command is run through `QMetaObject::invokeMethod(app, ..., Qt::BlockingQueuedConnection)` at [src/core/ControlRegistry.cpp:258](src/core/ControlRegistry.cpp:258), which blocks the caller until the handler returns. Channel removal (`Mixer::deleteChannel`, [src/core/Mixer.cpp:1338](src/core/Mixer.cpp:1338)) runs as a model change between mixer rounds, not inside this function.
- `Mixer::mixerChannel(int)` ([include/Mixer.h:634-637](include/Mixer.h:634)) has no bounds check by design. Its callers must pass an in-range index, and this caller does, because of the loop above.

What I did not verify at runtime. I did not write or run a test that removes a return channel while sends are on the faders and then calls `window.mixer_view`. The existing `tests/src/gui/MixerSendsOnFadersTest.cpp` does not exercise removal (grep for `deleteChannel|removeChannel|remove_channel` returns nothing in that file). The verdict rests on the code above and the intended id-based contract, not on a runtime reproduction. A reviewer who wants runtime proof should run that scenario on this branch.

No product code changed, so no regression test was added. Per the procedure, a not-real verdict skips the RED/GREEN steps.

Commands run and results (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-9.r1/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Gate, first attempt (as specified) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-9` | 1. It failed before any check ran: `PermissionError` creating `zene-studio-program/zene-qwen-new-p23-9-feature-check`, which is outside the write jail. Log `001-feature-check.log`. |
| Gate, log folder redirected | Unchanged copy of `feature_check.py` with two substitutions only: the log folder is set to the LOG FOLDER `gate-logs/`, and `ROOT` points at the original tool directory so the baseline cache is shared. Gate commands, limits and baselines are untouched. Run as `python3 -I <copy> <worktree> > .../002-feature-check.log 2>&1` | 0. Table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in changed files). RESULT: PASS. Zero changed files, so the ratchet rows have nothing new to report. |

No RED/GREEN build or ctest was run, because there is no product change and no regression test to prove.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p23-9.md` only.

## Self-check
(filled in after `submit_check.py`; see below)
