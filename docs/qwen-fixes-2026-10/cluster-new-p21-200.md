FALSE-POSITIVE

# Cluster new-p21-200: Kicker deleteNotePluginData leaves m_pluginData dangling

Finding: after `std::destroy_at` in `KickerInstrument::deleteNotePluginData`, `m_pluginData` is not reset, so a reused
NotePlayHandle "still looks populated" and `playNoteImpl` casts freed storage (use-after-free).

## Verdict: FALSE-POSITIVE

The reuse path that the finding describes cannot happen. A reused handle is re-constructed, and the constructor
initialises `m_pluginData` to `nullptr`, so `playNoteImpl` always takes the construction branch for a new note.
No production code reads the stale pointer after the destructor runs.

## Evidence (file:line on branch `bugs/qwen-new-p21-200`, base `639e1d23e`)

1. The only production trigger of `deleteNotePluginData` is the NotePlayHandle destructor, and it runs once per handle
   lifetime. `src/core/NotePlayHandle.cpp:160-162` calls `m_instrumentTrack->deleteNotePluginData(this)` when
   `m_pluginData != nullptr`. `src/tracks/InstrumentTrack.cpp:800-806` only forwards it to `m_instrument`.
   `rg -n deleteNotePluginData src include` finds no other production caller.
2. Handles are pooled, and reuse goes through a fresh constructor. `NotePlayHandleManager::release` calls the explicit
   destructor at `src/core/NotePlayHandle.cpp:821`, and puts the storage back in `s_available` at `:823`.
   `NotePlayHandleManager::acquire` placement-news the handle at `src/core/NotePlayHandle.cpp:814`.
3. The constructor resets the field. `src/core/NotePlayHandle.cpp:60` initialises `m_pluginData( nullptr )` in the
   member-init list. A reused handle therefore starts with `nullptr`, not with the dead pointer.
4. The Kicker construction guard is correct for a fresh handle. `plugins/Kicker/Kicker.cpp:166-175` constructs the
   `SweepOsc` into `pluginDataStorage()` only when `!nph->m_pluginData`. The finding's "playNoteImpl skips construction
   and casts freed storage" needs a non-null `m_pluginData` on a fresh handle, which point 3 rules out.
5. The dangling value is never read. The stale pointer lives only inside the destructed object, between the destructor
   and the placement new in `acquire`. Nothing dereferences it in that window.

## What is still true (not the reported bug)

`plugins/Kicker/Kicker.cpp:221-227` does not clear `_n->m_pluginData` after `std::destroy_at`, so a second direct call
would destroy the object twice. The sibling `plugins/Watsyn/Watsyn.cpp:492-498` does clear it, with a comment about
this exact double-cleanup. No production path makes a second call, and the test harnesses that call the hook directly
clear the pointer themselves (`tests/src/plugins/WatsynWaveSnapshotTest.cpp:127-130`,
`tests/src/plugins/VibedWaveformHandoffTest.cpp:196-197`). Making the Kicker hook idempotent would be a hygiene change
with no reachable failure, so I did not change product code. The owner can decide whether to align it with Watsyn.

`tests/reference/Kicker/Kicker.cpp:214-216` is a reference copy of the pre-port code, which uses `delete`. It is not the
product path.

## No product change

`git diff` of product files is empty. No regression test was written, because a not-real verdict has no RED state.
I did not build or run ctest. The verdict rests on the code reading above, not on a run.

## Commands run

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/feature-check-1.log` |
| 2 | `python3 -I $TMPDIR/fc-p21-200/feature_check.py <worktree>` (copy of the same script; only the log folder line changed, see below) | 0 | `logs/feature-check-2.log` |

Run 1 failed before any gate ran: `logs.mkdir` targets `<worktree>/../<name>-feature-check`, in the projects directory,
and returned `PermissionError`. That directory is outside my write-jail. Run 2 is a byte-identical copy except line 70,
which points the log folder at the LOG FOLDER (the copy's own gate-baseline cache held the file for base `639e1d23e`,
so no base recomputation was needed). Gate logic, limits and baselines are unchanged. The table from run 2:

```
feature-check zene-qwen-new-p21-200 (base 639e1d23e, 0 changed files; logs in .../logs/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

Gate exit codes for the report: feature_check (copy) EXIT=0. Per-gate exit codes are in `logs/feature-check/*.log`.

Not run: RED/GREEN regression, the `git apply -R` / `git apply` proof (no fix exists to reverse), `ctest`.

## Self-check

- **Regression test RED/GREEN (exit codes):** not applicable. The verdict is FALSE-POSITIVE, so there is no product
  change to revert. No test was run and none was written, and no exit codes exist for that step.
- **Deterministic test:** not applicable (no test added). The "tests added or changed" item is claim-only.
- **Fix at the cause, every caller and sibling checked:** no fix. The callers were checked instead:
  `src/core/NotePlayHandle.cpp:160-162` (destructor, the only production caller), `src/tracks/InstrumentTrack.cpp:800-806`
  (forwarder), `src/core/Instrument.cpp:103` (base no-op). The direct test calls are listed above.
- **No collateral damage:** no product file changed (`git show --stat 74bc13a1c` touches only this report).
- **Verdict evidence (file:line):** `src/core/NotePlayHandle.cpp:60` (ctor sets `m_pluginData(nullptr)`),
  `:814` (placement-new on acquire), `:821` (explicit destructor on release),
  `plugins/Kicker/Kicker.cpp:166-175` (construction guard), `:221-227` (hook without reset).
- **submit_check gate item (FAIL, environment):** `submit_check.py` runs `feature_check.py` on the real worktree. That
  script creates `<worktree>/../<name>-feature-check`, in the projects directory, and it fails there with
  `PermissionError` (log: `logs/feature-check-1.log`, EXIT=1). A copy of the same script with only the log folder
  changed passes all 8 rows (`logs/feature-check-2.log`, EXIT=0). Under a permitted log location the real gate should
  give the same table, since the diff against the base is empty; this is not proven by an unmodified run.
