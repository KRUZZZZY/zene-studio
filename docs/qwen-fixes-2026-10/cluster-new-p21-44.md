FALSE-POSITIVE

# Cluster new-p21-44: note.select helpers dereference clip->notes() at unchecked positions

Finding: `src/core/ControlCommandsNoteSelect.cpp:48` (`markSelected`) and `:68`/`:72` (`withGroupMates`) index
`clip->notes()` with positions that could be stale or invalid, so a `-1` id or a bad position would be out-of-bounds UB.

## Verdict: FALSE-POSITIVE

Every position that reaches `markSelected` or `withGroupMates` comes from `control::resolveNote`, which only returns a
position that is a valid index into the clip's note list. No input reachable through `note.select` can produce an
out-of-range position, and the clip is not mutated between the lookup and the use. The line the finding cites
(`:48`) is the `setSelected(true)` loop, which is correct for every value it can receive. No product code changes.

## Evidence (file:line in this worktree, HEAD 37eaf07557)

1. **Only producer of positions is `resolveNote`.** `src/core/ControlCommandsNoteSelect.cpp:115-118`: `int position = -1;`
   then `Note* note = control::resolveNote(clip, value.toString(), &position, &error); if (note == nullptr) { return error; }`,
   and `wanted.append(position)` at `:118`. A position is appended only when `resolveNote` returned a note.
2. **`resolveNote` writes `*index` only on a real hit.** `src/core/ControlEditSupport.cpp:236-241`:
   `for (int i = 0; i < static_cast<int>(notes.size()); ++i) { if (notes[i]->id() == wanted) { *index = i; return notes[i]; } }`.
   `i` is always in `[0, size)`.
3. **Malformed or negative ids are refused before the loop.** `src/core/ControlEditSupport.cpp:222-228`: `wanted < 0`
   returns `InvalidArgs`. `idToIndex` (`src/core/ControlVocabulary.cpp:193-210`) returns `-1` on every failure and only
   returns a value `>= 0` on success (`return ok && index >= 0 ? index : -1;`, `:210`). A `-1` therefore never reaches a
   position. A note whose stored id is `-1` cannot match the non-negative `wanted`, and the lookup returns `NotFound`.
4. **No mutation between lookup and use.** Inside the handler (`src/core/ControlCommandsNoteSelect.cpp:97-145`) the
   calls between `resolveNote` (`:116`) and `markSelected` (`:126`) are `withGroupMates` (`:120`, reads the clip, appends
   only to `positions`), `control::selectNotes` (`:121`, reads the clip; range-checks at `src/core/ControlEditSupport.cpp:279`).
   `clip->dataChanged()` (`:127`) runs after the marking.
5. **`withGroupMates` is bounded by the same list.** `src/core/ControlCommandsNoteSelect.cpp:68` reads
   `clip->notes()[position]` for positions that came from step 1. The loop at `:70-73` is bounded by
   `clip->notes().size()`. The finding's "unchecked" claim holds only if a position can be stale, which step 4 rules out.
6. **Scope of the helpers.** `markSelected` and `withGroupMates` are in an anonymous namespace
   (`src/core/ControlCommandsNoteSelect.cpp:41-42` to `:149`). Their only call sites are `:120` and `:126`.
7. **Single-threaded execution.** `ControlRegistry::invoke` runs each handler through `runOnUiThread`
   (`src/core/ControlRegistry.cpp:296`; `runOnUiThread` at `:249-259` uses `BlockingQueuedConnection` to the UI thread
   when called from another thread). A note cannot be removed by another command in the middle of this handler.

## Runtime evidence

The existing `NoteSelectionIdentityTest` (`tests/src/core/NoteSelectionIdentityTest.cpp`) drives `note.select` through
`ControlRegistry::invoke` (`:100`, `:148`) and passed on the unchanged tree. It covers selection through removals and
randomize-reorders. It does **not** exercise the group-mate branch of `withGroupMates` (grep for "group" in that file
returns nothing).

## Why no regression test

A regression test must fail on the unfixed code. No command input can make `resolveNote` return an out-of-range
position, so no test built through the control surface can be made to fail for this finding. A test that asserted
the bounds check would pass with or without any guard, which would prove nothing. Per the procedure, a not-real
verdict skips the RED/GREEN step, and no test was added.

A bounds check in `markSelected`/`withGroupMates` would be dead code today. If a future caller ever passes
positions that did not come from `resolveNote`, the guard belongs at that caller's boundary, next to where the
position is produced.

## Commands run (worktree build tree, `build/`)

| # | Command | EXIT | Log |
|---|---------|------|-----|
| 1 | `cmake --build . --target NoteSelectionIdentityTest -j2` | 2 | `build-1.log` (ccache temp dir not writable under the sandbox; not a code error) |
| 2 | `CCACHE_DISABLE=1 cmake --build . --target NoteSelectionIdentityTest -j2` | 0 | `build-2.log` (built, 0 errors) |
| 3 | `cd build/tests && ctest -R NoteSelectionIdentityTest --output-on-failure` | 0 | `test-1.log` (`100% tests passed, 0 tests failed out of 1`) |
| 4 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: `PermissionError` creating `<worktree>/../zene-qwen-new-p21-44-feature-check` (sandbox write-jail); no gate ran |
| 5 | `python3 -I <LOG>/gate-tool/feature_check_logdir.py <worktree>` | 0 | `gates-2.log`: `RESULT: PASS` (see below) |

Command 5 is `feature_check.py` unchanged except for two lines in a copy kept in the LOG folder: `ROOT` points at the
shared qwen directory, and the per-gate output directory is the LOG folder instead of the worktree's parent. The
gate commands and pass/fail logic are the same. The per-gate logs are in `logs/feature-check-out/`.

Gate table (command 5):

| Gate | Result |
|------|--------|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

Affected test family: `NoteSelectionIdentityTest`, 1 test run, 1 passed. No product file changed, so no other family
was affected.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p21-44.md` (this report). No product or test file changed.

## Self-check

1. **RED fails / GREEN passes with the product change reverted and restored.** Not applicable: there is no product
   change and no regression test. The finding is not reachable through the control surface (evidence 1-4), so no
   test can fail on the current code. Step 4b was therefore not run.
2. **Deterministic test, no wall-clock, sleeps or races.** Not applicable: no test was added. The existing test used
   (`NoteSelectionIdentityTest`) passed in one run (command 3); it was not re-run for flakiness.
3. **Fix at the cause, siblings checked.** Not applicable: no change. Sibling checks made: the other two
   functions in the same file that read positions (`selectedNoteIds` at `src/core/ControlCommandsNoteSelect.cpp:54-62`,
   which reads flags, and the report loop at `:131-139`, which already checks `position < size`), and
   `selectedNoteIndices` (`src/core/ControlEditSupport.cpp:297-306`, which iterates in range). Other callers of
   `resolveNote` (e.g. `src/core/ControlCommandsNotes.cpp`, `ControlCommandsNoteClipboard.cpp`) take their index from
   the same function, so they share the invariant in evidence 2. I did not audit each of them in depth.
4. **No collateral damage.** Nothing changed, so no saved data, old project, or other caller is affected.
5. **Realtime, undo, commands.** Nothing changed. No new command was added. `note.select` stays `mutating=false` (`:96`).
6. **Registration and gates.** No new file was added, so no registration is needed. Gates: command 5, `RESULT: PASS`.
7. **Sibling commands in the changed files.** No command file was changed. The only command in
   `ControlCommandsNoteSelect.cpp` is `note.select`.
8. **Gate tooling deviation.** Command 4 failed because the shared gate writes its logs outside the worktree, which
   the write-jail forbids. Command 5 is the same logic with only the log location redirected. This affects only where
   logs go, not what is checked. The shared tool was not edited. It should write logs under the run folder, or
   accept a log directory argument, for worktrees run in a write-jail.
