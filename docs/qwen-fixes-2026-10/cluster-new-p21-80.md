Verdict: FALSE-POSITIVE

new-p21-80: FALSE-POSITIVE

Finding: `selectInArranger` dereferences `editor->rubberBand()` without a null check (`src/gui/ControlCommandsViewMemory.cpp:110`). The finding's trigger is "an arranger whose rubber band is not yet created". That state cannot be reached: the rubber band is created in the constructor of the base class of every arranger, before any pointer to the arranger is published, and nothing ever clears it.

## Evidence

1. **The rubber band is created unconditionally, at construction.** [TrackContainerView.cpp:99](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/editors/TrackContainerView.cpp:99) builds it in the member-initializer list: `m_rubberBand( new RubberBand( m_scrollArea ) )`. The constructor then hides and disables it at [TrackContainerView.cpp:125-126](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/editors/TrackContainerView.cpp:125). The member is declared at [TrackContainerView.h:211](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/include/TrackContainerView.h:211). The accessor is [TrackContainerView.cpp:635-637](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/editors/TrackContainerView.cpp:635).

2. **Nothing ever reassigns or deletes it.** `rg -n "m_rubberBand\s*=[^=]|delete\s+m_rubberBand|m_rubberBand\s*=\s*nullptr" src include` returns only two hits, both for an unrelated `m_rubberBand` member of `SamplePlayHandle` ([include/SamplePlayHandle.h:161](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/include/SamplePlayHandle.h:161), [src/core/SamplePlayHandle.cpp:196](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/core/SamplePlayHandle.cpp:196)). Its only owner is the parent `m_scrollArea`, which lives as long as the arranger.

3. **`arranger()` only returns a fully constructed editor.** [ControlCommandsViewMemory.cpp:74-78](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/ControlCommandsViewMemory.cpp:74) returns `g->songEditor()->m_editor`. That pointer is set in the `SongEditorWindow` member initializer `m_editor(new SongEditor(song))` at [SongEditor.cpp:857](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/editors/SongEditor.cpp:857). The `new` returns only after the whole `SongEditor` constructor, and so the `TrackContainerView` base constructor that sets `m_rubberBand`, has run. So a non-null `editor` always has a non-null `rubberBand()`.

4. **No dangling-editor path either.** `GuiApplication` sets `m_songEditor` at [GuiApplication.cpp:223](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/GuiApplication.cpp:223) and clears it when the window is destroyed, at [GuiApplication.cpp:292-294](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/GuiApplication.cpp:292). So `arranger()` returns `nullptr` once the window is gone, and `selectInArranger` already handles that with its `editor == nullptr` check at [ControlCommandsViewMemory.cpp:109](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-80/src/gui/ControlCommandsViewMemory.cpp:109).

Because the null state is unreachable through the real construction path, no regression test can fail on it. No production code was changed, so no RED/GREEN test was written.

## Commands run and EXIT codes

Gate runs, all from the worktree, logged to the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-80.r1/logs`:

| Command | EXIT |
| --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `gate-1.log`) | 1 — stopped before any gate ran: `PermissionError` on `logs.mkdir` for `<worktree>-feature-check`, a sibling of the worktree outside the write-jail (`feature_check.py:71`). Not patched; no gate result. |
| `bash tests/fork-sources-gate.sh` (log `gate-fork-sources.log`) | 0 |
| `bash tests/all-sources-reproduce.sh` (log `gate-all-sources.log`) | 0 |
| `bash tests/no-upstream-regression-gate.sh` (log `gate-no-upstream.log`) | 0 |
| `tests/scripted/check-namespace` (log `gate-check-namespace.log`) | 0 |
| `bash tests/no-tautology-gate.sh` (log `gate-no-tautology.log`) | 0 |
| `python3 tests/doc-figures-gate.py` (log `gate-doc-figures.log`) | 0 |
| `git status --porcelain \| wc -l` after the gates | 0 lines (tree still clean) |

The two ratchet gates, `tests/complexity-gate.sh --check` and `tests/file-length-gate.sh --check`, were **not run**. `feature_check.py` runs them through its baseline cache (`gate-baselines/`, plus a `git worktree add` in `zene-cachyos`), and those writes are outside the write-jail. This commit changes only this report, so it adds no function or file that either gate could flag.

Other commands: `rg -n "rubberBand|arranger\(\)" src/gui/ControlCommandsViewMemory.cpp` and related `rg` lookups, plus `sed -n` reads of the cited line ranges. All were read-only.

## Self-check

- **Regression test fails with the product change reverted, and passes with it:** not applicable. There is no product change, and the null state is not reachable through the real construction path, so no test can exercise it.
- **Test is deterministic:** not applicable (no test added).
- **Fix is at the cause and complete:** not applicable. The claimed cause does not exist. Adding an unreachable null check would be churn and would not be behaviour-changing.
- **No collateral damage:** nothing in `src/`, `include/` or `plugins/` changed, so no saved data, command or audio path is affected.
- **Verdict evidence:** given above as file:line citations 1-4 (`TrackContainerView.cpp:99`, `:125-126`, `:635-637`; `TrackContainerView.h:211`; `SongEditor.cpp:857`; `GuiApplication.cpp:223`, `:292-294`; `ControlCommandsViewMemory.cpp:74-78`, `:109`).
- **Gates:** six of eight passed with EXIT 0. `feature_check.py` could not run in the jail, and the two ratchet gates were not run. Both are reported above rather than claimed.
