Verdict: FALSE-POSITIVE

new-p18-60: "Monstro may destroy note data that was never constructed" (`plugins/Monstro/Monstro.cpp:1128` in the finding; that line is now `m_osc1SSF.saveSettings`, unrelated. The cleanup method is at `plugins/Monstro/Monstro.cpp:1111-1114`).

## Evidence

The early return does leave `m_pluginData` null, but the engine never calls the cleanup method with a null pointer.

1. Early return leaves the pointer null: `plugins/Monstro/Monstro.cpp:1085` returns when no scratch slot can be leased, before anything is constructed. Construction happens only after the lease succeeds, at `plugins/Monstro/Monstro.cpp:1087-1093`.
2. The cleanup method has no null check: `plugins/Monstro/Monstro.cpp:1111-1114` calls `std::destroy_at(static_cast<MonstroSynth*>(_n->m_pluginData))` directly.
3. The only production caller of the cleanup path is guarded. `src/core/NotePlayHandle.cpp:160-162`:
   ```cpp
   if( m_pluginData != nullptr )
   {
       m_instrumentTrack->deleteNotePluginData( this );
   }
   ```
   The pointer starts null (`src/core/NotePlayHandle.cpp:60`). A handle that was never rendered, or whose render returned early, has a null `m_pluginData`, so the destructor never calls the cleanup. `src/tracks/InstrumentTrack.cpp:800-805` only forwards to the instrument.
4. Repo-wide search (`rg -n "deleteNotePluginData\s*\("`, excluding `build/`): the only non-test call site is `src/core/NotePlayHandle.cpp:162`. Every other hit is a test calling the method directly on its own handle. I did not check each test call for an unrendered Monstro handle; they are a test-harness API, not the engine path.
5. Monstro is the only writer of `m_pluginData` on a Monstro note. It is set at `plugins/Monstro/Monstro.cpp:1091`, after the lease. Nothing else writes it on that instrument.

So on the path the finding describes, "a note finalized after the early return", the cleanup is skipped and no UB happens. The finding's "undefined behaviour" claim holds only for a direct call with a null handle, which no engine code makes.

Latent hazard, not reachable today: a future direct caller passing an unrendered handle would hit `destroy_at(nullptr)`. A defensive null check in `deleteNotePluginData` would be harmless. I did not add it. The verdict is not-real, so no product code changed, and the behaviour outside the bug should not change.

## Regression test

No regression test was added. The bug is not reproducible through any engine path, so a test cannot fail on the current code for the reported scenario, and a test that passes before and after would not prove anything. The RED/GREEN sequence (step 4b) does not apply because there is no product change to revert.

## Commands and exit codes

Build and test: none run. There is no product change to rebuild or test.

Gates, run from the worktree (the `feature_check.py` wrapper is in the report below):

| Command | EXIT |
| --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1, the wrapper could not create its log dir `<worktree>-feature-check` (outside the write-jail, `PermissionError`). Log: `logs/feature-check-1.log` |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/complexity-gate.sh --check` | 1 (red on the train by design; 7 `REGRESSION:` lines, none in `plugins/Monstro` or any file this branch changes) |
| `bash tests/file-length-gate.sh --check` | 1 (red on the train by design; 22 `REGRESSION:` lines, none in `plugins/Monstro` or any file this branch changes) |

I ran the six simple gates and two ratchet gates directly, with the same commands the wrapper uses, and wrote their logs to the LOG FOLDER (`gate-*-2.log`). The wrapper itself cannot run here because it writes outside the worktree.

Both ratchet gates are red on the base. This branch changes only `docs/qwen-fixes-2026-10/cluster-new-p18-60.md`, so no item they list is in a file this branch changes. The gate-level result is a pass for the new-items-in-my-files rule.

`submit_check.py` output is in `logs/submit-check-1.log`, EXIT 1. Its remaining FAIL items are answered below.

## Self-check

- Commits on the branch: this report is committed with the branch.
- Working tree clean: yes after the commit.
- Report exists with a Self-check: this section.
- Tests added or changed: none. The finding is not real, so there is no bug to regress. Adding a test that passes before and after would not be regression proof, so I did not add one.
- Gates: all six simple gates exit 0. Complexity and file-length exit 1 on the base and show no new items in files this branch changes.
- Regression test fails without the fix and passes with it: not applicable, no fix. No fix was made.
- Deterministic test: not applicable, no test.
- Fix at the cause and complete: not applicable. The cause is not present on the reachable path, as evidenced in the Evidence section.
- No collateral damage: no code changed.
- FALSE-POSITIVE evidence: `plugins/Monstro/Monstro.cpp:1085`, `plugins/Monstro/Monstro.cpp:1087-1093`, `plugins/Monstro/Monstro.cpp:1111-1114`, `src/core/NotePlayHandle.cpp:60`, `src/core/NotePlayHandle.cpp:160-162`, `src/tracks/InstrumentTrack.cpp:800-805`.

## Open items for the reviewer

- The wrapper `feature_check.py` cannot write its log dir from this sandbox. Its log dir is `<worktree>-feature-check`, outside the write-jail. I ran the same commands directly instead.
- The direct test-harness calls to `deleteNotePluginData` were not checked one by one for an unrendered Monstro handle.
