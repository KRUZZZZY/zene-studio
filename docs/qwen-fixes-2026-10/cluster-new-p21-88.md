Verdict: FALSE-POSITIVE

new-p21-88: placeEffect dereferences a possibly null chain (`src/core/ControlDevicePlacement.cpp:72`, `chain->effects()`)

The finding's "when" condition is that `placeEffect` is called with a null chain. No caller on this branch does that. Every caller either null-checks the chain before the call, or dereferences the chain itself before reaching `placeEffect` (so a null would crash in the caller, not in `placeEffect`), or passes a member address or a chain that is non-null by construction. No change is made, so there is no regression test to red/green; the evidence is the caller audit below.

## Evidence (HEAD `cf3618e61`)

`placeEffect` is declared `LMMS_EXPORT` at `include/ControlDevicePlacement.h:56`. Repo-wide search (excluding `build/`) finds five callers and no others (`rg -n "placeEffect\(" . --glob '!build/**'`, EXIT=0):

| Caller | Chain source | Null guard before `placeEffect` |
| --- | --- | --- |
| `src/core/ControlCommandsPlugin.cpp:148-153` (`loadEffect`) | `resolveDeviceChain(...)` | `if (chain == nullptr) { return error; }` at `:149`, before the call at `:153` |
| `src/core/ControlCommandsTrackPresets.cpp:109-114` (`pluginReplace`) | `old->effectChain()` | none needed: `chain->effects()` is dereferenced at `:110`, before the call at `:114`; a null chain would fail there first |
| `src/core/ControlDeviceMoves.cpp:101-113` (`recreateDevices`) | `chainOf()` | none at this call site: `chain->effects()` is dereferenced at `:106`, before the call at `:113`. The redo closure in the same file guards null (`:81`, `bool stopped = chain == nullptr;`) |
| `src/core/ControlCommandsW3Engine.cpp:147` | `&to->m_fxChain` | `to` is non-null: `to != nullptr ? ... : nullptr` at `:130`, `from == nullptr` returns at `:131`, and `to->index()` at `:134` dereferences it |
| `src/core/ControlStructuralSupport.cpp:764-769` (undo/redo of a device load) | `chainOf()` into `live` | `restored` is non-null only when `live != nullptr` (`:766`), and the call is under `if (restored != nullptr)` at `:769` |

Sibling check: `recreateDevices` (`ControlDeviceMoves.cpp:101`) has no null guard on its initial path, although its redo closure at `:81` does. I traced the initial-path `chainOf` closures and none returns null on a path I could find: the rack-wrap closure (`ControlCommandsRackWrap.cpp:165`) uses a chain it has just added; `destinationResolver` (`ControlCommandsDeviceCopy.cpp:77-84`) re-resolves args that were validated by `destinations()` just before the copy; `channelChain` (`ControlCommandsDeviceTools.cpp:212-219`) is only called for an existing mixer channel. This is a possible hardening follow-up in `recreateDevices`, not the bug in this finding, and it is not changed here. Adding a null guard to `placeEffect` alone would not touch any path that can reach a null chain today.

## Commands run

| Step | Command | Result |
| --- | --- | --- |
| Read finding site | `Read src/core/ControlDevicePlacement.cpp` (lines 1-82) | `chain->effects()` at `:72` without a check |
| Caller search | `rg -n "placeEffect|resolveChainTarget|resolveDeviceChain" src tests` | EXIT=0; callers as in the table above |
| Repo-wide caller check | `rg -n "placeEffect\(" . --glob '!build/**' --glob '!.git/**'` filtered to exclude the known caller files | EXIT=0 (only the declaration at `include/ControlDevicePlacement.h:56`) |
| Real gate, first attempt | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=1: `PermissionError` creating `<worktree>-feature-check`, a directory outside the write-jail. No check ran. Log: `logs/gates-1.log` |
| Gate with log path redirected | `python3 $TMPDIR/gate-copy-new-p21-88/feature_check.py <worktree> > .../logs/gates-2.log 2>&1` | EXIT=0, RESULT: PASS (8/8 rows PASS, including complexity and file-length: no new items). The copy differs from the original only in its log path (`logs/gate-copy-diff.txt`); its three baseline files for base `cf3618e61` were copied from `qwen/gate-baselines/` |

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: not applicable. The verdict is FALSE-POSITIVE, so no product change is made and no regression test is added. The "when" condition of the finding does not occur on any caller I could trace.
- The test is deterministic: not applicable (no test).
- The fix is at the cause and complete: not applicable (no fix). The sibling `recreateDevices` gap is noted above and left for a separate decision.
- No collateral damage: nothing changed in product code, so no load, render, or save path is affected.
- Verdict evidence: every caller of `placeEffect` is cited with file:line above, and the header declaration is `include/ControlDevicePlacement.h:56`.
