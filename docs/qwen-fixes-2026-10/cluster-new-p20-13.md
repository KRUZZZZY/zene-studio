# cluster new-p20-13 — `render.sections`: existing files in the output directory

Verdict: NEEDS-CONTEXT

The overwrite is real in the code: `render.sections` writes a section's file over an existing file with the same name, with no check. Whether that is a bug depends on a contract the repo states two ways, and the two fixes the finding offers each change behaviour the owner has not chosen. So no code is changed and no regression test is added. The decision is below under "Decision needed".

## Evidence (file:line, current tree)

Reproduced by reading the code only. I did not build or run the command.

- `src/core/ControlCommandsRenderSections.cpp:159-175` `sectionFileStems` makes the stems unique among the chosen sections only. It never looks at the output directory.
- `src/core/ControlCommandsRenderSections.cpp:257-261` checks only that the directory exists or can be created.
- `src/core/ControlCommandsRenderSections.cpp:263-274` the loop calls `render.render` with `out = <dir>/<stem>.<fmt>`. There is no `QFileInfo::exists` check before the call. (The finding's "263-270" is this block.)
- `src/core/ControlCommandsRenderSections.cpp:282-290` each reply entry records the freshly rendered `path`, `frames` and `sha256`. Nothing in the reply says a file was replaced.
- The writer truncates: `src/core/audio/AudioFileDevice.cpp:47` opens the output with `QFile::WriteOnly | QFile::Truncate`, and `src/core/ProjectRenderer.cpp:183` builds the encoder on that path. So a pre-existing `Verse.wav` is truncated and replaced. This is the path the finding describes.

## Contract evidence: overwrite is intended for render outputs

- `docs/A16-REVERSIBILITY.md:163` (`render.render` row): "overwriting it is the caller's decision". Each section file goes through `render.render`.
- `src/core/ControlCommandsRenderStems.cpp:266` ("overwriting what is there") and `:348-350` (a re-export "overwrites the same names"). The sibling batch export overwrites on purpose, and stages then promotes the set.
- `src/core/RenderQueue.cpp:160-193` `commitStaged`: the render queue replaces an existing destination, with an aside-and-restore so a failed rename puts the old file back.
- `src/gui/RenderRangeMenu.cpp:115-124` `exportSections` picks a folder and calls `render.sections`. Exporting again into the same folder is the normal GUI flow.

## Contract evidence: refusal is the convention for single-file saves

- `src/core/ControlCommandsChain.cpp:149-156` (`storeWrite`): an existing file is refused "unless `overwrite:true`".
- `docs/DAWPROJECT-INTERCHANGE.md:185` and `:338`: `dawproject.export` refuses an existing file without `overwrite`.
- `src/core/ControlCommandsRenderSections.cpp:312-327`, the command description, says nothing about existing files either way.

## Why neither fix was taken

- **Refuse every existing name** (the finding's first option). This breaks the GUI's second export into the same folder, which is a behaviour change for legitimate callers (definition of done item 4). It is also against the sibling `render.stems`, which overwrites deliberately.
- **Add an `overwrite` argument** (the second option). This changes the registered command's `argsSchema` (`ControlCommandsRenderSections.cpp:328-332`), so the owner should choose the default. Opt-in refusal would also need the GUI to pass `overwrite` or to ask the user first.

## Decision needed

Pick one contract for `render.sections`:

1. **Keep overwrite and say so.** Docs-only change: add to the `render.sections` description that existing files of the same name are replaced, as `render.stems` does. This matches the A16 "caller's decision" rule and the GUI flow. Nothing else changes.
2. **Refuse existing section files unless `"overwrite":true`** (matches `chain.save` and `dawproject.export`). Needs the argsSchema change, a GUI prompt or pass-through for re-export, and a regression test that fails before and passes after.

My recommendation is 2, because it is the convention the codebase already uses to stop silent clobbering, and the GUI change is small. If the owner wants the batch export to behave like `render.stems`, pick 1. Either way I can make the change once the choice is made.

## Commands run

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p20-13`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-13.r1/logs/`.

No product code was changed, so no build or ctest was run for this verdict.

| Command (run from the worktree) | Log | EXIT |
| --- | --- | --- |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `feature-check-1.log` | 1 (wrapper failed before any gate ran, see Self-check) |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-1.log` | 0 |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce-1.log` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-1.log` | 0 |
| `tests/scripted/check-namespace` | `gate-check-namespace-1.log` | 0 |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-1.log` | 0 |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-1.log` | 0 |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1 (7 items; none in a file this branch changes) |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-1.log` | 1 (22 items; none in a file this branch changes) |
| `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p20-13` | `submit-check-1.log` | 1 (see Self-check) |

## Self-check

**The wrapper cannot run here.** `feature_check.py:71` does `logs.mkdir` on `<worktree>-feature-check`, a sibling of the worktree at `projects/zene-studio-program/zene-qwen-new-p20-13-feature-check`. That path is outside the write-jail, and the mkdir failed with `PermissionError` (`feature-check-1.log`). I ran the same commands the wrapper runs (its `SIMPLE` and `RATCHET` lists) directly and wrote their output to the LOG FOLDER. That is the table above. The wrapper's own table was not produced.

**Ratchet verdict by the wrapper's rule.** The wrapper fails a ratchet only for an item in a file the branch changed. The only changed file is this report. None of the 7 complexity items or 22 file-length items names `docs/` or this report (`grep` of both logs: 0 matches). Both ratchets are red on the train on the baseline, and this branch adds nothing to them.

**Completion check (`submit-check-1.log`, EXIT 1):**
- `FAIL commits on the branch` and `FAIL tests added or changed`: expected for this verdict. There is no fix and no regression test, because the contract needs an owner decision first (see "Decision needed"). The report is committed alone.
- `FAIL working tree clean`: this was the uncommitted report at the time of the check. It is committed in the next step.
- `FAIL gates (feature_check.py)`: the wrapper cannot run in this sandbox (above). The six direct gates and both ratchets are logged above.
- The five "?" items:
  - Regression test fails without the fix, passes with it: **not applicable**. No product change and no test were made. This is the reason the verdict is NEEDS-CONTEXT and not FIXED.
  - Deterministic test: **not applicable**, no test was added.
  - Fix at the cause and complete, every caller and sibling checked: **not applicable**. The callers and siblings of `render.sections` were read so the decision is informed: the GUI menu (`src/gui/RenderRangeMenu.cpp:115-124`), the control socket, `src/core/ControlReversibilityTablePassive.cpp`, and `tests/control-render-sections.py` (each check writes its own subdirectory, `sections` at line 151 and `sections-portable` at line 212, so no check exports twice into one folder and none tests re-export). Any fix must touch the GUI caller too.
  - No collateral damage: **not applicable**. The collateral each option would cause is set out under "Why neither fix was taken".
  - Verdict is not ALREADY-FIXED or FALSE-POSITIVE, so the "?" item on that does not apply. The overwrite is real in the code (evidence above), and the open question is which contract the owner wants.

**Not verified:** I did not build `zene` or run `render.sections` on a directory that already holds a same-named file. The claim that the file is replaced rests on the code path (`AudioFileDevice.cpp:47`, `ProjectRenderer.cpp:183`) and was not seen at run time.
