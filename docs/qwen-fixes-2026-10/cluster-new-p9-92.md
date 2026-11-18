# Cluster new-p9-92 — FALSE-POSITIVE

## Finding

The report names `project.set_undo_history` at `src/core/ControlReversibilityTableSnapshot.cpp:151`. That command does not exist in this tree: searching for `project.set_undo_history` and `set_undo_history` returned no matches. Line 151 begins the `CODE-6` comment for `script.set_memory_budget`; the corresponding row is at lines 154–157.

The nearby undo-related command is `control.set_undo_depth` at `src/core/ControlReversibilityTableSnapshot.cpp:146–149`. Its handler, `src/core/ControlCommandsUndo.cpp:129–190`, changes the count and byte caps, reports evicted steps in `dropped`, and records an inverse that restores the old caps. `include/ProjectJournal.h:178–184` documents that lowering the count cap evicts oldest steps immediately. The table explicitly says those steps are not recoverable. This is the documented behavior and the explicit by-design case in the task instructions, so it is not changed.

## Evidence

| Check | Command / result |
| --- | --- |
| Search for the named command | `rg -n 'project\.set_undo_history|set_undo_history' .` — exit 1 (no matches) |
| Locate the actual cap command and journal API | `rg -n 'control\.set_undo_depth|void setJournalling|setJournalling\( const bool|bool ProjectJournal::setMaxUndo|setMaxUndoStates' src/core/ControlCommandsUndo.cpp src/core/ControlReversibilityTableSnapshot.cpp include/ProjectJournal.h src/core/ProjectJournal.cpp` — exit 0 |
| RED regression | N/A — no matching command or behavior exists on this branch; the finding was rejected before the regression step. |
| GREEN regression | N/A — no product fix was made. |
| Affected-family test count | N/A — no test target or ctest family was run. |
| Complexity gate | `bash -c 'bash tests/complexity-gate.sh --check; rc=$?; printf "EXIT=%s\n" "$rc"; exit "$rc"' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-92.r1/logs/03-complexity-gate.log 2>&1` — exit 0; log marker `EXIT=0`. |
| File-length gate | `bash -c 'bash tests/file-length-gate.sh --check; rc=$?; printf "EXIT=%s\n" "$rc"; exit "$rc"' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-92.r1/logs/04-file-length-gate.log 2>&1` — exit 0; log marker `EXIT=0`. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-92.md` only. |
