# Cluster new-p10-529

**Verdict: FALSE-POSITIVE**

The finding names `src/gui/editors/AutomationEditorKeys.cpp:91`, which is absent from this checkout. The current implementation is `src/gui/editors/AutomationEditor.cpp`. It has no `setKeyPoint` implementation and does not set an accessibility description. Its constructor sets the editor's fixed accessible name at line 105. `setCurrentClip()` at lines 172–188 only disconnects the old clip, assigns the new clip, reconnects signals, and emits `currentClipChanged()`. `keyPressEvent()` at lines 254–287 handles navigation keys; Escape reaches the default branch and is ignored. The editor has no stored selected-point state: `m_action` and the drag fields describe an active mouse edit.

This branch therefore has no point-selection description that can remain stale after Escape or a clip switch. No product code or regression test was changed.

## Evidence and verification

| Check | Result |
|---|---|
| Historical source path | Absent (`test ! -e` returned 0). |
| `setKeyPoint` / `AutomationEditorKeys` in the current editor | No matches (`rg` returned 1). |
| `setAccessibleDescription` in the current editor | No matches (`rg` returned 1). |
| RED / GREEN regression | Not applicable: the described implementation is absent, so step 2 classified the finding as false-positive and skipped regression creation. |
| Affected-family test count | Not applicable; 0 tests run. No build or ctest was needed for this verdict. |
| Complexity gate | Exit 0. |
| File-length gate | Exit 0. |

Exact evidence commands and captured exit codes:

```text
test ! -e src/gui/editors/AutomationEditorKeys.cpp; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=0
rg -n "AutomationEditorKeys|setKeyPoint" include/AutomationEditor.h src/gui/editors/AutomationEditor.cpp; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=1
rg -n "setAccessibleDescription" include/AutomationEditor.h src/gui/editors/AutomationEditor.cpp; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=1
rg -n "setAccessibleName|setCurrentClip|keyPressEvent" src/gui/editors/AutomationEditor.cpp; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=0
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-529.r1/logs/gate-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-529.r1/logs/gate-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"  -> EXIT=0
```

The fff grep tool was unavailable in this environment. Each final gate log was searched with `rg` for one term per invocation (`error`, `FAILED`, `Assertion`); all six searches returned 1 with no matches.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p10-529.md`.
