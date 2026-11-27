FALSE-POSITIVE

new-p21-30: FALSE-POSITIVE

## The finding

"mixer.create_return can crash on a failed channel creation … `mixer()->mixerChannel(index)->id()` is dereferenced without checking createChannel's result … when: Mixer::createChannel fails and returns -1" (`src/core/ControlCommandsMixerTools.cpp:184`).

The dereference is real and current — `src/core/ControlCommandsMixerTools.cpp:184` `auto made = std::make_shared<int>(mixer()->mixerChannel(index)->id());`, and its sibling inside the same command's helper at `src/core/ControlCommandsMixerTools.cpp:157` `MixerChannel* made = mixer()->mixerChannel(index);`. The premise that produces the bad index does not exist on this branch.

## Decision: the failure it describes cannot happen

`Mixer::createChannel()` has no failure path and no sentinel:

- `src/core/Mixer.cpp:1107` `int Mixer::createChannel()`; `src/core/Mixer.cpp:1118` `const int index = m_mixerChannels.size();` is taken **before** `src/core/Mixer.cpp:1120` `m_mixerChannels.push_back( new MixerChannel( index, this ) );`; the function's only return is `src/core/Mixer.cpp:1137` `return index;`, after `doneChangeInModel()`. Every path through the body returns that index.
- There is no channel cap to fail against: the mixer's channels are an unbounded vector and no `MAX_MIXER_CHANNELS` constant exists (`docs/specs/SPEC-dynamic-routing.md:32` records exactly that).
- The one way the call can fail is allocation — `new MixerChannel` (`src/core/Mixer.cpp:1120`) or the vector's growth throwing `std::bad_alloc` — which propagates as an exception. An `index >= 0` guard placed after the call cannot observe it, so the proposed fix would be dead code that never fires.
- `include/Mixer.h:663-669` `mixerChannel(int)` returns `m_mixerChannels[_ch]` for an in-range index; the LMMS_TESTING branch (`include/Mixer.h:665-667`) refuses an out-of-range index instead of reading it.

So the index `create_return` passes to `mixerChannel()` is the position of the channel it just pushed (`>= 1`; channel 0 is the master), and the pointer it dereferences is a live channel. The same contract is relied on, unchecked, by every sibling: `src/core/ControlCommandsMixer.cpp:287`, `src/core/ControlCommandsBus.cpp:120` (`createBusChannel()` at `src/core/Mixer.cpp:1720-1722` returns `createChannel()`'s index), `src/core/ControlCommandsDrumRack.cpp:118`, `src/core/ControlCommandsDawProject.cpp:230`. Adding a negative-index check to `create_return` alone would be inconsistent with all of them and would still be unreachable.

The command's real failure paths are already refusals that run **before** anything is created: `src/core/ControlCommandsMixerTools.cpp:171-174` refuses a channel that names no live channel (`resolveMixerChannel` → error) and refuses the master (`"the master cannot feed a return"`); `src/core/ControlCommandsMixerTools.cpp:178` refuses an out-of-range `amount`. The send itself is null-checked (`src/core/ControlCommandsMixerTools.cpp:162-163`), and `buildReturn` ends by returning the index of the channel it created (`src/core/ControlCommandsMixerTools.cpp:165`).

Runtime confirmation on this build, through the existing test seam (`include/Mixer.h:671-677`, `src/core/Mixer.cpp:1049-1061` count every `mixerChannel()` lookup that names no channel): a scratch Qt case ran `mixer.create_return` → `control.undo` → `control.redo` → `control.undo` and asserted `Mixer::takeRefusedLookupsForTesting() == 0`, then, as a negative control on the same binary, read `mixerChannel(999999)` and asserted the count was `1`, so the zero is not vacuous. Both assertions passed (`logs/test-4-scratch-seam.log`, `Totals: 3 passed, 0 failed`). The scratch case was removed again; its patch is `logs/scratch-evidence.patch` and the tree is clean.

This is the same contract already adjudicated for a sibling finding in `docs/qwen-fixes-2026-10/cluster-new-p21-42.md` ("createChannelFor dereferences mixer and channel without checks", FALSE-POSITIVE).

## Test matrix (what was examined, and what covers it)

| Path | Finding |
| --- | --- |
| Normal path | existing `aReturnIsABusFedByASend` (`tests/src/core/MixerToolsTest.cpp:198`) creates the return, checks it is a bus, checks the send, one undo removes it. |
| Failure path | arg resolution and the master refusal run before creation (`ControlCommandsMixerTools.cpp:171-174`) and return typed refusals. |
| Exhaustion | none exists: the channel vector is unbounded, no cap constant (`docs/specs/SPEC-dynamic-routing.md:32`). |
| Reconfiguration (undo/redo) | the scratch case above drove create → undo → redo → undo; no refused lookup, channel count restored. |
| Audio thread | untouched: the handler is a control command on the control thread; no product line changed. |
| Platforms | no code change, so no new build requirement. |

## Commands and results

All commands ran in `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p21-30` (branch `bugs/qwen-new-p21-30`, base `9913d36d5`); logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p21-30.r1/logs`.

| Command | Log | EXIT |
| --- | --- | --- |
| `cmake --build build --target MixerToolsTest -j4` | `build-1.log` | 0 |
| `ctest -R MixerTools --output-on-failure` (from `build/tests`, 1/1 `MixerToolsTest` passed) | `test-1.log` | 0 |
| scratch case added, `cmake --build build --target MixerToolsTest -j4` | `build-2.log` | 0 |
| `ctest -R MixerTools --output-on-failure` with the scratch case (1/1 passed) | `test-2.log` | 0 |
| scratch case with the negative control, `cmake --build build --target MixerToolsTest -j4` | `build-3.log` | 0 |
| `./MixerToolsTest scratchCreateReturnNamesARealChannel` (from `build/tests`; 3 passed, 0 failed) | `test-4-scratch-seam.log` | 0 |
| scratch case removed (`cp …/logs/MixerToolsTest.cpp.bak tests/src/core/MixerToolsTest.cpp`, `git status --porcelain` empty), `cmake --build build --target MixerToolsTest -j4` | `build-4.log` | 0 |
| `ctest -R MixerTools --output-on-failure` after removal (1/1 passed) | `test-5.log` | 0 |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 1 — see "Gates" below |
| The six simple gates, run one by one in the worktree: `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` | `gate-*.log` | 0, 0, 0, 0, 0, 0 |
| The two ratchet gates by hand: `bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check` | `gate-complexity.log`, `gate-file-length.log` | 2 (train-red ratchet, no item in a file this job touched), 0 |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-30` | `submit-1.log` | 1 — every row PASS except `gates (feature_check.py)`, which is the blocked baseline step above |

Because no product file changed, the ratchet rows of `feature_check.py` have nothing of mine to compare: they can only report items in files the branch changed, and the branch changes no source file.

## Gates

`feature_check.py` cannot finish in this lane: it computes its base listing by running `git -C <cache>/tree checkout -q --detach 9913d36d…`, which the lane's git guard refuses, and the cache directory is read-only for this lane:

```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12). Stage explicit paths and commit on your own branch only.
…
subprocess.CalledProcessError: Command '[…]' returned non-zero exit status 97.   (gates-1.log, EXIT=1)

git -C …/gate-baselines/tree rev-parse HEAD  →  31f559ab479dcd1b822545aae477222f58f32c0d   (not the base)
HEAD=9913d36d5397025d1b7092097f50f6d50c652dc4  FIX-BASE=050/m0-train-53
BASE=$(git merge-base HEAD 050/m0-train-53) = 9913d36d5397025d1b7092097f50f6d50c652dc4  (so this worktree *is* the base tree, clean)
touch …/qwen/gate-baselines/.probe → "Read-only file system"   (so the missing cache entry cannot be written either)
```

I did not bypass the guard and did not edit the gate. Instead I ran the eight commands the wrapper runs, one by one, in the base-clean worktree; their exit codes are in the table above. The consequence is that `submit_check.py`'s single `gates (feature_check.py)` row reads FAIL for this environmental reason, not for a gate in the product. The same block is recorded in `docs/qwen-fixes-2026-10/cluster-5032.md` and `docs/qwen-fixes-2026-10/cluster-1447.md`.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it.** Not applicable: the verdict is FALSE-POSITIVE and no product line was changed, so there is nothing to revert. The claim is carried by the code references above and by the seam experiment (`test-4-scratch-seam.log`, EXIT 0, 3 passed), whose negative control shows the counter really reports a lookup that names no channel on this build.
- **The test is deterministic.** No test is added. The scratch case used no sleeps, no wall-clock time and no threads: it invoked the command through `ControlRegistry::instance()->invoke` and read a synchronous counter (`Mixer::takeRefusedLookupsForTesting`) before and after; the audio device is stopped in that fixture's `initTestCase` (`tests/src/core/MixerToolsTest.cpp:102`).
- **The fix is at the cause and complete; every caller and sibling checked.** There is no fix. The contract `createChannel()` → valid index was checked at `src/core/Mixer.cpp:1107-1138` and against every caller: `ControlCommandsMixerTools.cpp:156-157,184`, `ControlCommandsMixer.cpp:287`, `ControlCommandsBus.cpp:120` (via `createBusChannel`, `src/core/Mixer.cpp:1720`), `ControlCommandsDrumRack.cpp:118`, `ControlCommandsDawProject.cpp:230`. None of them can receive a negative index, and none guards for one — the intended contract is uniform.
- **No collateral damage.** No product file, header, save format or serialized field changed; the only tracked change is this report. Old projects load exactly as before. No limit or check was added, so no other caller can be rejected by it.
- **FALSE-POSITIVE evidence.** `src/core/Mixer.cpp:1107-1138` (no failure path), `src/core/Mixer.cpp:1118,1120,1137` (index taken before the push, single return), `include/Mixer.h:663-669` (`mixerChannel` contract and the LMMS_TESTING refusal of an out-of-range index), `docs/specs/SPEC-dynamic-routing.md:32` (no channel cap), plus the runtime seam result above and the matching earlier adjudication in `docs/qwen-fixes-2026-10/cluster-new-p21-42.md`.
- **Realtime, undo, commands.** No audio-thread code touched; `mixer.create_return` keeps its single undo step and its `TrueInverse` entry in `src/core/ControlReversibilityTableDevices.cpp:259-260`; no control command was added, so the `ControlCommandsSnapshot` canary is unchanged.
- **Gates.** The six simple gates each exit 0 in the worktree (`gate-*.log`). The two ratchet gates were run by hand: `complexity` exits 2 because the ratchet is red on the train on purpose and lists no item in a file this job changed (this job changes no source file), `file-length` exits 0. `submit_check.py` reports exactly one FAIL, its `gates (feature_check.py)` row, and that row is the blocked baseline step quoted above — the script cannot check out the base commit in its cache tree (guard exit 97) and cannot write the missing cache entry (read-only file system). No product gate is red.

## Other findings

- `feature_check.py` is unusable as installed for any base commit without a cached listing in a lane: it needs `git checkout --detach` (refused by the lane guard) and a writable `gate-baselines` (read-only here). Fixing that is the harness's job, not this job's.
- The `mixer.create_return` redo path (`src/core/ControlCommandsMixerTools.cpp:191`) re-runs `buildReturn` and stores the new id without checking that the send from `fromId` was created again; `buildReturn` silently skips the send when no channel holds that id any more (`src/core/ControlCommandsMixerTools.cpp:160-165`). That is a redo-fidelity question, not the reported crash, and out of scope here.
