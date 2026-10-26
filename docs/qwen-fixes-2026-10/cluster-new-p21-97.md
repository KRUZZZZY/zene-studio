Verdict: FALSE-POSITIVE

new-p21-97: `resolveNeed` dereferences a channel without checking `createChannel`'s result

Evidence: the finding is at [ControlProjectTransplantChannels.cpp:125](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/ControlProjectTransplantChannels.cpp:125) (`const int index = mixer->createChannel();`) and dereferences at [ControlProjectTransplantChannels.cpp:126](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/ControlProjectTransplantChannels.cpp:126). The claimed failure, a negative or invalid index from a failed creation, cannot happen with the current `Mixer::createChannel()`:

- [Mixer.cpp:1073](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/Mixer.cpp:1073) sets `index = m_mixerChannels.size()` and [Mixer.cpp:1075](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/Mixer.cpp:1075) pushes `new MixerChannel(index, this)` into that slot. The plain `new` expression throws on allocation failure; it never yields `nullptr`.
- The function's only `return` is [Mixer.cpp:1094](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/Mixer.cpp:1094) (`return index;`). Nothing in `createChannel` returns -1 or a refusal code.
- So the returned index always names the slot just pushed, `0 <= index < size()`. [Mixer.h:634-637](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/include/Mixer.h:634) returns `m_mixerChannels[_ch]` (a `std::vector<MixerChannel*>`, [Mixer.h:786](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/include/Mixer.h:786)), and that element is non-null by construction. The dereference at line 126 is therefore safe.
- Index 0 is the master: the `Mixer` constructor creates it through `createChannel()` ([Mixer.cpp:1023-1033](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/Mixer.cpp:1023), call at 1030). The lookup loop in `resolveNeed` starts at 1 ([ControlProjectTransplantChannels.cpp:119](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/core/ControlProjectTransplantChannels.cpp:119)), so it never matches master by name.
- Sibling callers check `created < 0` ([TrackFolder.cpp:361-365](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/tracks/TrackFolder.cpp:361) and [TrackFolder.cpp:494-498](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-97/src/tracks/TrackFolder.cpp:494)). Those branches are dead under the same contract, so the transplant path differs only in lacking an unreachable check.

Decision: no product change. The finding's trigger ("channel creation fails or returns an invalid index") has no code path on this branch, so there is no defect to regress. If `createChannel()` ever gains a refusal return, `resolveNeed` should check it at that time. That is a future-contract note, not a bug in the current tree. No regression test was written, because a test that passes on the unchanged code proves nothing here (lessons: a test that passes both ways is not regression proof).

Commands run (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-97.r1/logs/`):

| Step | Command | Result |
| --- | --- | --- |
| Inspect finding site | `Read src/core/ControlProjectTransplantChannels.cpp` offset 150, limit 120 (and lines 95-155 from the brief) | read OK |
| Locate createChannel / accessor / callers | `rg -n "createChannel|int Mixer::createChannel|mixerChannel\(int" src/core/Mixer.cpp include/Mixer.h`; `rg -n "createChannel\(\)" src include tests` | read OK |
| Read createChannel body and constructor | `Read src/core/Mixer.cpp` offsets 1022-1034 and 1055-1105; `Read include/Mixer.h` offset 630 | read OK |
| Feature gate runner | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1` | EXIT=1 (see note A) |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-97 > logs/submit-check-1.log 2>&1` | EXIT=1 (see Self-check) |
| Gate fork-sources | `bash tests/fork-sources-gate.sh > logs/gate-fork-sources-1.log 2>&1` | EXIT=0 |
| Gate all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/gate-all-sources-1.log 2>&1` | EXIT=0 |
| Gate no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/gate-no-upstream-1.log 2>&1` | EXIT=0 |
| Gate check-namespace | `tests/scripted/check-namespace > logs/gate-check-namespace-1.log 2>&1` | EXIT=0 |
| Gate no-tautology | `bash tests/no-tautology-gate.sh > logs/gate-no-tautology-1.log 2>&1` | EXIT=0 |
| Gate doc-figures | `python3 tests/doc-figures-gate.py > logs/gate-doc-figures-1.log 2>&1` | EXIT=0 |
| Ratchet complexity (gate 4) | `bash tests/complexity-gate.sh --check > logs/gate-complexity-1.log 2>&1` | EXIT=1; 7 REGRESSION lines, none names a file this branch changes (see note B) |
| Ratchet file-length (gate 7) | `bash tests/file-length-gate.sh --check > logs/gate-file-length-1.log 2>&1` | EXIT=1; 22 REGRESSION lines, none names a file this branch changes (see note B) |
| Regression RED / GREEN | not run: no product change, so there is nothing to revert or reapply | N/A |

Note A: `feature_check.py` stopped before running any gate. It creates its log directory at `<worktree>/../<name>-feature-check` (`logs.mkdir`, `feature_check.py:71`), which is `zene-studio-program/zene-qwen-new-p21-97-feature-check`. That path is outside the write-jail and returned `PermissionError: [Errno 13]`. I did not edit the gate script. The table is therefore not produced; the eight gate commands above are the ones it would run, run directly with logs in the LOG FOLDER.

Note B: the ratchet gates' pass rule (`feature_check.py:85`, `mine = [...] and item_file(l) in changed`) fails only on a REGRESSION item whose file is in the branch's changed set. This branch changes only `docs/qwen-fixes-2026-10/cluster-new-p21-97.md`. The script's base-listing comparison (`gate-baselines/`, `feature_check.py:21` and `:50`) lives in the loop's tooling directory and was not computed in this session, so I have not confirmed the table row itself. The ratchet's current REGRESSION lines do not name the report, the transplant file, or the Mixer files.

## Self-check

- **Regression test RED/GREEN (exit codes):** N/A. The verdict is FALSE-POSITIVE, so no product change exists to revert or reapply, and I did not write a test that would pass on the unchanged code. The submit check's "tests added or changed" row therefore stays unmet by design.
- **Deterministic test:** N/A (no test added).
- **Fix at the cause and complete, with every caller and sibling checked:** no fix. Callers and siblings checked: `Mixer::createChannel` callers in `src/tracks/TrackFolder.cpp:361,494`, `src/gui/MixerView.cpp:281`, and the constructor (`src/core/Mixer.cpp:1030`). All of them rely on the same non-negative, valid-index contract.
- **No collateral damage:** no code changed, so no existing project or save path is affected.
- **FALSE-POSITIVE evidence (code):** `src/core/Mixer.cpp:1073,1075,1094`; `include/Mixer.h:634-637,786`; `src/core/Mixer.cpp:1023-1033`; `src/core/ControlProjectTransplantChannels.cpp:119-127`.
- **Gates:** eight gate scripts exit 0 except the two ratchets (complexity and file-length, EXIT=1), which are red on the train on purpose and are not reachable by this branch's change set (note B). `feature_check.py` itself could not run because of the write-jail path (note A). `submit_check.py` reports the commit and report rows as unmet only until this commit lands; its "gates" row fails for the note-A reason.
- **Submit check:** `submit_check.py` returned FAIL (log `submit-check-1.log`) on: no commit on branch (fixed by committing this report); report missing (fixed by writing it); self-check section (added here); tests added or changed (unmet by design, see RED/GREEN above); gates (note A). Nothing else was retried.
