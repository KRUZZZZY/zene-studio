FALSE-POSITIVE

# Cluster new-p21-100 - `addUndoStep(QVector<JournallingObject*>)` null/empty handling

Finding: `src/core/ControlReversibility.cpp:154` forwards the vector to
`ProjectJournal::addJournalCheckPoint` "without rejecting null entries or an empty vector, so a
null/deleted journalling object can be dereferenced during undo".

## Verdict

FALSE-POSITIVE. The journal already rejects null entries and empty steps before any dereference, and
undo never dereferences a caller's raw pointer. The only place a `JournallingObject*` is dereferenced
is recording time, after the null check. No caller in the tree passes a null or stale pointer.

## Evidence (file:line, branch `bugs/qwen-new-p21-100`, base `639e1d23e`, no product file changed)

1. Wrapper: `src/core/ControlReversibility.cpp:154-159` - guards only the journal pointer
   (`if (journal == nullptr) { return; }`) and forwards the vector.
2. Null entries are skipped **before** the dereference, at record time:
   `src/core/ProjectJournal.cpp:257-258` (`for( JournallingObject * jo : objects )` /
   `if( jo == nullptr ) { continue; }`), then `captureState( jo, &saved )` at `:260`.
3. Empty step is a no-op, nothing is pushed: `src/core/ProjectJournal.cpp:264`
   (`if( step.objects.isEmpty() ) { return; }`).
4. Undo does not use the caller's pointer at all. It restores by id:
   `src/core/ProjectJournal.cpp:99-101` (`restoreStep`: `m_joIDs.value( saved.joID, nullptr )`,
   `if( jo == nullptr ) { continue; }`) and `:77-78` (`restoreState`, same guard).
5. A destroyed object's id is nulled, so a deleted object is also skipped on undo:
   `include/ProjectJournal.h:351-354` (`freeID` -> `reallocID( _id, nullptr )`),
   `src/core/ProjectJournal.cpp:303-309` (`reallocID` stores `m_joIDs[_id] = _obj`, i.e. nullptr),
   called from `src/core/JournallingObject.cpp:59-65` (`~JournallingObject`).
6. Callers guard before reaching `addUndoStep` (checked every call site of the vector overload):
   - `src/core/ControlCommandsNoteMoves.cpp:66-72` - `clip` and `to` both null-checked above (`:66`, `:67`).
   - `src/core/ControlCommandsAudioSlice.cpp:158-162`, `src/core/ControlCommandsArrangeTools.cpp:185-198`,
     `src/core/ControlCommandsClipArrange.cpp:312`, `src/core/ControlCommandsTrackVersions.cpp:113`,
     `src/core/ControlCommandsTrackPresets.cpp:93`, `src/core/ControlCommandsSamplerTake.cpp:341`:
     the objects come from `resolveClip` / `resolveTrack` / a live range (`src/core/ControlEditSupport.cpp:163-185`
     returns a `ClipRef` from `enumerateClips()`), so they are live at call time.
   - `src/core/ControlCommandsRange.cpp:113-121` and `:242` - tracks from `range.tracks()`, live at call time.
   - `src/core/ControlCommandsMixerMeters.cpp:149` - `&channel->m_crossfadeSideModel`, an address of a member, never null.
   - `src/gui/shell/TransportReadouts.cpp:285` - `&numerator, &denominator`, addresses of members.
   - `src/core/ControlCommandsMidiReconnectShared.h:131`, `src/core/ControlCommandsSessionShared.h:443` - same overload.

What the code cannot guard: a caller that passes a pointer whose object was already destroyed before the
call. That is a use-after-free at the call site, and `addUndoStep` cannot detect it. Every caller above
obtains its pointers within the same command, so the case does not arise.

Observation, not a defect in this finding: a vector whose entries are ALL null records nothing and pushes
no step, silently. The journal's contract is "no object, no step"; no caller in the tree hits it. Changing
that contract is a behaviour change outside this bug, so it is not made here.

## Tests and fix

No fix, so no RED/GREEN cycle was run and no test was added. A not-real verdict cites the code above
(Definition of done item 1). The claim is a code-reading verdict: nothing was executed to demonstrate the
null path, so the proof is the ordering of the guards at `ProjectJournal.cpp:257-264` and `:99-101`.

## Commands run and EXIT codes

Logs live in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-100.r1/logs/`.

| Step | Command (run from the worktree) | EXIT | Log |
| --- | --- | --- | --- |
| Gate script, combined | `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` |
| Gate script cause | `feature_check.py:71` `logs.mkdir()` on `<worktree>-feature-check/` -> `PermissionError` (outside the write-jail; script never ran its gates) | - | `feature-check-1.log` |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-2.log` |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-2.log` |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-2.log` |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-2.log` |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-2.log` |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-2.log` |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` |
| Ratchet: file-length | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log` |
| Completion check | `python3 .../qwen/submit_check.py <worktree> new-p21-100` | 1 | `submit-check-1.log` |

The first attempt at the piecewise loop (`gate-*-1.log`) used bash parameter expansion under fish and
exited 127 (command not found) for every row except check-namespace. Those `-1` logs are invalid and are
superseded by the `-2` logs above; `gate-check-namespace-1.log` is the only valid one of that set.

Ratchet result. `gate-complexity-2.log` lists 7 `REGRESSION` lines and `gate-file-length-2.log` lists 22.
None of them names a file this branch changes. The branch has no commit beyond its base
`639e1d23e`, so `feature_check.py`'s rule ("fail only on items NEW in files you changed") passes both
rows for this branch. These are train-wide items, red on purpose.

## Self-check

Mechanical items from `submit_check.py` (`submit-check-1.log`, run before this commit):
- commits on the branch: FAIL before the commit, PASS after this report is committed (`cluster-new-p21-100.md` is the only new commit).
- working tree clean: PASS after commit.
- report with verdict in first lines: PASS (first line is the verdict).
- tests added or changed: PASS as claim-only closure (verdict is FALSE-POSITIVE and no `src/`, `include/`, `plugins/` file changed).
- gates (feature_check.py): cannot be satisfied in this jail. Its own log directory is outside the worktree, which is not writable. The six non-ratchet gates were run individually and exit 0; the two ratchets exit 1 with no item in a file this branch changes. The combined `feature_check.py` exit status is therefore unavailable, and the `gates` row in the completion check stays FAIL for that reason.

ASK items:
- Regression test fails with the change reverted and passes with it: not applicable. No product change, so no test is required for a FALSE-POSITIVE verdict (`submit_check.py` treats it as claim-only).
- Test is deterministic: not applicable, no test added.
- Fix is at the cause and complete: not applicable, no fix. Every caller of the vector overload was checked (evidence item 6).
- No collateral damage: nothing changed, so no saved data or caller behaviour changes.
- Not-real verdict with evidence: `ProjectJournal.cpp:257-264`, `:99-101`, `:77-78`, `include/ProjectJournal.h:351-354`, `ProjectJournal.cpp:303-309`, `JournallingObject.cpp:59-65` (evidence items 2-5).
