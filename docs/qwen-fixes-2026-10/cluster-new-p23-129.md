Verdict: FIXED

new-p23-129: FIXED

Evidence: `EditorSource::clipId()` and `EditorSource::grid()` dereferenced `m_editor` with no null guard, while `clip()` was guarded (original line 62; the fixed file has it at [:65](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-129/src/gui/noteeditor/ItemListNoteSources.cpp:65); the original `clipId()`/`grid()` were at lines 63-64). `m_editor` is a `QPointer`, so it becomes null when the note editor is deleted while a list still holds its source. The unguarded calls are reached from the list's own paths: `NotesSource::step()` and `ControllerSource::step()` call `grid()` ([ItemListNoteSources.cpp:147](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-129/src/gui/noteeditor/ItemListNoteSources.cpp:147), [:261](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-129/src/gui/noteeditor/ItemListNoteSources.cpp:261)), and `NotesSource::remove()` calls `clipId()` unconditionally ([:167](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-129/src/gui/noteeditor/ItemListNoteSources.cpp:167)). `ControllerSource::write()` also reaches `clipId()` ([:317](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-129/src/gui/noteeditor/ItemListNoteSources.cpp:317)). The constructor also called `connect(editor, ...)` with a null sender when built without an editor, which Qt reports as `invalid nullptr parameter`.

Fix (ItemListNoteSources.cpp, EditorSource only, behaviour unchanged when an editor is present):
- `clipId()` returns an empty string without an editor; `grid()` returns 1 (the floor `std::max(1, ...)` already gives) without an editor.
- The constructor connects the two editor signals only when `editor != nullptr`; `follow()` already handles a null clip.
- Sibling checks: `NotesSource::value()`'s default branch reads `m_editor->state().lane` (line 128), and `setValue`/`insertAfter` read `m_editor->state()` (lines 140, 154). Each is reached only after a `clip() != nullptr` check or `rowCount() > 0`, and `clip()` is null whenever `m_editor` is null, so those are unreachable without an editor and were left unchanged. `ControllerSource::lane()` is reached only through `laneId()`, which requires a clip.
- No new commands, no new files, no audio-thread code (the classes are GUI list sources), no undo change, no saved-data change.

Regression test: tests/src/gui/ItemListPanelTest.cpp:497 `aSourceWithoutItsEditorStaysEmpty` (the editor is deleted while both source kinds are alive) and :514 `aSourceBuiltWithoutAnEditorStaysEmpty`. Both assert `rowCount()==0`, `step()` returns 1, and `remove({})` returns false (refused by the command, no crash). Deterministic: no timers or waits.

Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-129.r1/logs/`). Build was run with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p23-129` because the default ccache temp directory was not writable from this sandbox (`build-red-1.log`, EXIT=2, ccache permission error; not a code result).

| Evidence | Command | Result |
| --- | --- | --- |
| RED build (before fix; test added, source unchanged) | `cmake --build build --target zene ItemListPanelTest -j4 > build-red-2.log 2>&1` | EXIT=0 |
| RED: family run | `cd build/tests && ctest -R ItemListPanelTest --output-on-failure > test-red-1.log 2>&1` | EXIT=8, SIGSEGV in `aSourceWithoutItsEditorStaysEmpty` (1 test failed) |
| RED: null-editor test alone | `./ItemListPanelTest aSourceBuiltWithoutAnEditorStaysEmpty > test-red-2-nulleditor.log 2>&1` | EXIT=139, SIGSEGV, after 4 `invalid nullptr parameter` connect warnings |
| GREEN build (fix applied) | `cmake --build build --target zene ItemListPanelTest -j4 > build-green-1.log 2>&1` | EXIT=0 |
| GREEN: family run | `ctest -R ItemListPanelTest --output-on-failure > test-green-1.log 2>&1` | EXIT=0, 100% (1/1 ctest entry; binary: 20 passed, 0 failed) |
| GREEN: the two new functions alone | `./ItemListPanelTest aSourceWithoutItsEditorStaysEmpty` / `...aSourceBuiltWithoutAnEditorStaysEmpty` | EXIT=0 / EXIT=0, 3 passed each |
| 4b: revert product fix | `git diff -- src/gui/noteeditor/ItemListNoteSources.cpp > fix.patch && git apply -R fix.patch` | EXIT=0 |
| 4b: RED with fix reverted | build (`build-4b-revert.log`) then `ctest -R ItemListPanelTest` (`test-4b-revert.log`) | build EXIT=0; test EXIT=8, SIGSEGV |
| 4b: re-apply fix | `git apply fix.patch` | EXIT=0 |
| 4b: GREEN with fix re-applied | build (`build-4b-reapply.log`) then `ctest -R ItemListPanelTest` (`test-4b-reapply.log`) | build EXIT=0; test EXIT=0, 100% |
| Affected family (ItemList + NoteEditor) | `cmake --build build --target ItemListButtonsTest NoteEditorTest -j4` then `ctest -R "ItemList\|NoteEditorTest" --output-on-failure` | build EXIT=0; ctest EXIT=0, 3/3 tests passed (test-family-1.log) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | EXIT=0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | EXIT=0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | EXIT=0 |
| Gate: check-namespace | `tests/scripted/check-namespace` | EXIT=0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | EXIT=0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | EXIT=0 |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check` | EXIT=1 (red on train). Its 7 REGRESSION lines are in WasmSandbox.cpp, XyPadPairTest.cpp, DawProjectReadTracks.cpp, ControlCommandsMixerTools.cpp, ControlCommandsSessionRecordLand.cpp, ControlCommandsMixerStrip.cpp, ControlCommandsShortcuts.cpp; none is in the two changed files, so the item-level rule passes |
| Ratchet: file-length | `bash tests/file-length-gate.sh --check` | EXIT=1 (red on train). Its 22 REGRESSION lines name other files; none is in the two changed files, so the item-level rule passes |
| Feature check (script) | `python3 feature_check.py <worktree>` | EXIT=1 with `PermissionError`: the script creates `<worktree>-feature-check/` next to the worktree, outside the write-jail. Not a gate result. The six simple gates and both ratchets were run individually above instead |

Files committed: `src/gui/noteeditor/ItemListNoteSources.cpp`, `tests/src/gui/ItemListPanelTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p23-129.md`. No new files, so no registration entries were needed (the source file is already in `tests/fork-sources.txt`).

Not run: the full `ctest` tree and the other gates in `tests/run-all-gates.sh`. The family was ItemListPanelTest, ItemListButtonsTest and NoteEditorTest.

## Self-check

1. **Regression test fails with the change reverted and passes with it.** Reverted: `ctest -R ItemListPanelTest` EXIT=8 (`test-4b-revert.log`, SIGSEGV in `aSourceWithoutItsEditorStaysEmpty`); the null-editor function alone EXIT=139. Re-applied: `ctest -R ItemListPanelTest` EXIT=0 (`test-4b-reapply.log`); the binary reports 20 passed, 0 failed (`test-green-2-full.log`).
2. **Deterministic.** Neither new function uses timers, `QTRY_*`, sleeps or threads. The "editor gone" state is produced synchronously by `delete m_editor`, which nulls the source's `QPointer` at once (ItemListPanelTest.cpp:497-512). The "no editor" state is `itemlists::notes(nullptr, this)` (:514-525). Both sources are parented to the test object, so they are not freed with the panel.
3. **At the cause and complete.** Callers of the changed helpers, all checked in `src/gui/noteeditor/ItemListNoteSources.cpp`: `clipId()` at :140, :144, :157, :161, :167, :184, :317; `grid()` at :147 (`NotesSource::step`), :261 (`ControllerSource::step`), :269 (`ControllerSource::insertAfter`). With an editor present, both helpers return the same values as before. The engine's own `NoteEditor::clipId()` (`src/gui/noteeditor/NoteEditor.cpp:203`) already returns an empty id for a null clip (`include/ControlClipLinkSupport.h:96`), so the empty-id contract matches. Unguarded `m_editor->` reads left in place: :128 (`value()` default branch), :140 (`setValue`, after `clip()` check at :134), :154 (`insertAfter`, after `clip()` check at :153). `value()` is reached only for `row < rowCount()`, and `rowCount()` is 0 whenever `clip()` is null (`clip()` is null whenever `m_editor` is null). `ControllerSource::lane()` is reached only through `laneId()`, which requires `clip() != nullptr` (:292).
4. **No collateral damage.** No saved data, no file format, no limit or check added. The only behaviour change is on the null-editor path: a silent no-op in place of a crash or a Qt `invalid nullptr parameter` warning from the constructor. No new commands (so no `ControlCommandsSnapshot` change), no audio-thread code, no undo change.
5. **Verdict.** FIXED. Not an ALREADY-FIXED or FALSE-POSITIVE verdict, so no contract citation is needed beyond the file:line evidence above.

**Gates (submit_check item "gates (feature_check.py)": FAIL).** Environmental, not a code result. `feature_check.py` writes its logs to `<worktree>-feature-check/`, next to the worktree in `zene-studio-program/`, which is outside the write-jail. It stopped with `PermissionError` at `logs.mkdir` (exit 1) before running any gate. I did not write outside the jail to work around it. Instead I ran each gate command it would run, from the worktree, with the logs in the LOG FOLDER: the six simple gates EXIT=0 each, the two ratchets EXIT=1 each with no `REGRESSION:` line in either changed file (see the table above). The owner should re-run `feature_check.py` from an environment that can write beside the worktree before this is merged.
