Verdict: FALSE-POSITIVE

new-p21-93: findMixerRoute dereferences mixer/channel without null checks (src/core/ControlMixerSupport.cpp:182-190)

## Finding as stated

Luna's claim: `Engine::mixer()->mixerChannel(fromIndex)` returns nullptr for a removed channel, and the loop at
`ControlMixerSupport.cpp:184-185` then dereferences it. A null guard on `Engine::mixer()` and `from` is the proposed fix.

## Why the stated mechanism is not real

1. **`mixerChannel()` never returns nullptr for a valid index.** `Mixer::mixerChannel(int)` is
   `return m_mixerChannels[_ch];` (`include/Mixer.h:634-637`), an unchecked `std::vector<MixerChannel*>` read
   (`include/Mixer.h:786`). Every element is a fresh `new MixerChannel` (`src/core/Mixer.cpp:1075`), and removal
   erases the element, so the vector shrinks (`src/core/Mixer.cpp:1441`). A removed channel is therefore not a null
   slot. An index past the end is undefined behaviour inside `mixerChannel`, not a `nullptr` that `findMixerRoute`
   could see. A guard added to `findMixerRoute` would not catch that case.
2. **The intended contract for "no such channel" is a typed result, not a null pointer from the index path.**
   `resolveMixerChannel` (`src/core/ControlMixerSupport.cpp:45-84`) resolves by persistent id and returns
   `NotFound` for a missing channel. `findMixerRoute` takes raw indices by design and returns `nullptr` only for
   "no such route" (`ControlMixerSupport.cpp:189`). Every caller that can meet a missing route already handles that
   case (`ControlCommandsMixerRoutes.cpp:305-310`, `ControlCommandsMixerListen.cpp:98-99`, `MixerOverlays.cpp:77-78`).
3. **Every caller passes an index that was resolved live in the same call.** Callers listed by
   `rg -n 'findMixerRoute\(' src/core/*.cpp src/gui/*.cpp` (log `evidence-2.log`):
   - `ControlMixerSupport.cpp:298`: `liveRouteEnds` resolves the ids to live indices first (`ControlMixerSupport.cpp:268-279`).
   - `ControlCommandsMixerRoutes.cpp:147`, `:304`: indices from `resolveRoutingEnds` (`ControlMixerSupport.cpp:192-197`).
   - `ControlCommandsMixerTools.cpp:111`: `channelWithId` then `->index()` (`ControlCommandsMixerTools.cpp:106-111`).
   - `ControlCommandsMixerTools.cpp:124`, `ControlCommandsMixerListen.cpp:97`: indices from `resolveRoutingEnds`.
   - `MixerOverlays.cpp:77`, `:87`: `apply()` only reaches `showFader`/`roleOf` for `i < numChannels()` (`MixerOverlays.cpp:161`), and `roleOf` returns `Volume` for `index == 0` or no target.
   - `ControlCommandsFeedback.cpp:187`: `restoreEdges` calls `mixer->createChannelSend(edge.from, edge.to, ...)` on the same indices first (`ControlCommandsFeedback.cpp:186`). That function indexes the vector unchecked too (`src/core/Mixer.cpp:1600-1601`), so a stale index fails there before `findMixerRoute` runs. A guard in `findMixerRoute` changes nothing for this path.
4. **`Engine::mixer()` is null only outside the engine lifetime.** `Engine::s_mixer` is set in `Engine::init` (`src/core/Engine.cpp:73`) and nulled by `deleteHelper(&s_mixer)` in `Engine::destroy` (`src/core/Engine.cpp:112`). The control and GUI callers above run only while the engine is up. I did not find a path that calls `findMixerRoute` after `destroy`, but I did not trace the shutdown order end to end, so treat that part as checked by reading, not by running.

## Sibling observation (not part of this verdict; not fixed here)

The undo steps in `ControlCommandsMixerListen.cpp:103` capture raw indices (`from`, `to`) and replay them through
`setSendPan` → `findMixerRoute` (`ControlCommandsMixerListen.cpp:85-87`). Unlike `setSendEnabled`
(`ControlCommandsMixerTools.cpp:106-110`, BUG 6) and `recordMixerRouteStep` (`ControlMixerSupport.cpp:283-311`), this
path does not resolve live ids at undo time. It would act on the wrong route, or read out of range, if a channel was
removed or moved after the step was recorded without its own undo step. I could not establish whether such a
topology change can happen outside the undo system while a pan step is still on the stack. `UndoHistory::clear()`
(`src/core/UndoHistory.cpp:483`) has no callers in `src/` or `src/gui/` (`evidence-8.log`), and I did not locate where
project load resets the control undo history. This is an unproven candidate for a separate cluster, with
NEEDS-CONTEXT on its reachability. It is not the finding as written.

## Commands run and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-93.r1/logs/`.

| Step | Command (run from the worktree) | Exit | Log |
| --- | --- | --- | --- |
| Mixer index writes and reads | `rg -n 'm_mixerChannels\.(erase\|push_back\|pop_back)\|return m_mixerChannels\[' src/core/Mixer.cpp include/Mixer.h` | 0 | `evidence-1.log` |
| Callers of `findMixerRoute` | `rg -n 'findMixerRoute\(' src/core/*.cpp src/gui/*.cpp` plus the `mixerChannel(index)` sites | 0 | `evidence-2.log` |
| Channel delete and move call sites | `rg -n 'deleteChannel\(\|moveChannel...' src include` (non-test) | 0 | `evidence-3.log` |
| Undo teardown and load paths | `sed` of `src/tracks/TrackFolder.cpp:125-145`, `src/core/ControlCommandsDrumRack.cpp:120-140`, `src/core/ControlCommandsBus.cpp:160-170` | 0 | `evidence-4.log` |
| Undo-history clearing search | `rg -n -i 'clear\w*undo\|undo\w*clear...'` | 0 (no matches) | `evidence-5.log` |
| Undo implementation search | `rg -l 'addUndoStep\(' ...`; `rg` for `UndoHistory` clears | 0 | `evidence-6.log`, `evidence-7.log`, `evidence-8.log` |
| Feature-check wrapper, as designed | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gate-feature-check-1.log` |
| Gate 1, fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-2-fork-sources.log` |
| Gate 2, all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-3-all-sources-reproduce.log` |
| Gate 3, no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-4-no-upstream-regression.log` |
| Gate 4, check-namespace | `tests/scripted/check-namespace` | 0 | `gate-5-check-namespace.log` |
| Gate 5, no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-6-no-tautology.log` |
| Gate 6, doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-7-doc-figures.log` |
| Gate 7, complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `gate-8-complexity.log` |
| Gate 8, file-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `gate-9-file-length.log` |

Gate summary is in `gate-summary-1.log`.

## Gate notes

- **`feature_check.py` did not run.** It tries to create `<worktree>-feature-check/` beside the worktree
  (`feature_check.py:70-71`), which is outside the allowed write paths, and failed with PermissionError
  (`gate-feature-check-1.log`). I did not change the script. I ran its eight gate commands one by one from the worktree instead (table above), so the logs are the same commands but not its single table.
- **Complexity and file-length are red on the train.** The regressions in those logs are on files this branch does
  not touch: `tests/src/core/VideoTrackTest.cpp (651)` and `tests/src/gui/ArrangerTimelineTest.cpp (948)` in
  `gate-9-file-length.log`, plus 7 complexity regressions in `gate-8-complexity.log`. This branch has no source
  changes (`git status` was clean before this report), so none of them is in a file it changed.

## Regression test

Not written. The verdict is FALSE-POSITIVE, and no product code changed. There was nothing to show RED and GREEN.
Step 4b does not apply. Definition-of-done item 2 is not needed for a false positive.

## Self-check

To be filled in after `submit_check.py` runs (see below).
