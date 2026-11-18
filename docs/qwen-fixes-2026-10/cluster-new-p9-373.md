# Cluster new-p9-373

Verdict: **FALSE-POSITIVE**

The finding names `src/core/ProjectJournalSnapshots.cpp:203`, but that file is not tracked or present on this branch. Searches across `include` and `src` find no `detachHistory()` or `reattachHistory()` implementation. The journal reset path here is `ProjectJournal::clearJournal()` in `src/core/ProjectJournal.cpp:344`; it clears both checkpoint stacks and sets `m_retainedBytes = 0` at line 351. `Song.cpp:1415` calls this reset when clearing project state. The reported detach-with-stale-accounting behavior therefore cannot occur in this branch's implementation.

## Evidence

| Item | Result |
| --- | --- |
| Reported source | `src/core/ProjectJournalSnapshots.cpp` is absent from the tracked sources. |
| Detach/reattach API | No matches for either symbol in `include` or `src`. |
| Existing accounting reset | `src/core/ProjectJournal.cpp:344-351` clears undo/redo and resets retained bytes. |
| Project reset call | `src/core/Song.cpp:1415` invokes `clearJournal()`. |
| RED regression | N/A; the reported implementation is absent, so no regression was added. |
| GREEN regression | N/A; no affected behavior exists on this branch to verify. |
| Affected-family test count | Not run; classification was false-positive before test authoring. |
| Complexity gate | Exit 0. |
| File-length gate | Exit 0. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-373.md` only. |

## Commands and exit codes

Source inventory:

```text
rg --files src/core | rg 'ProjectJournal'
EXIT=0
```

The inventory returned `ProjectJournal.cpp`, `ProjectJournalBounds.cpp`, and `ProjectJournalStructural.cpp`; it did not return `ProjectJournalSnapshots.cpp`.

```text
git ls-files --error-unmatch src/core/ProjectJournalSnapshots.cpp
query EXIT=1 (path is not tracked)
```

```text
rg -n "detachHistory|reattachHistory" include src
EXIT=1 (no matches)
```

```text
rg -n "clearJournal|m_retainedBytes|undoDepth|retainedBytes" include/ProjectJournal.h src/core/ProjectJournal.cpp src/core/Song.cpp
EXIT=0
```

Gates (full output saved in the run log folder):

```text
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-1.log 2>&1
EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-2.log 2>&1
EXIT=0
```

The fff grep tool was not available in this session; the gate logs were searched with `rg`. Each search below returned exit 1, meaning no matching lines:

```text
rg -n 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-2.log
EXIT=1
rg -n 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-2.log
EXIT=1
rg -n 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-373.r1/logs/gate-2.log
EXIT=1
```

No build or test command was run because the finding's cited implementation and API are absent on this branch.
