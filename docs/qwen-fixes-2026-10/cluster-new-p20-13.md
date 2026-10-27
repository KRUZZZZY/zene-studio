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

## Test added (round 2)

`tests/control-render-sections.py`, new check `check_existing_file_is_replaced`, run last in `run_checks`. It stages `Chorus.wav` with non-WAV bytes in its own directory (`sections-existing`), exports the one section `sec-9` with `render.sections`, and checks that the file is replaced by a valid, bounded WAV. It pins the **current** contract, so it is a characterization check, not a regression test for a fixed bug. If the owner picks option 2 above, this check changes on purpose.

## Commands run

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p20-13`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-13.r2/logs/`. Build commands set `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p20-13`, because ccache's default temp path is outside the write-jail (`build-mutant-1.log`, EXIT 2, `Permission denied`). That changes only where ccache writes its scratch files.

| Command | Log | EXIT |
| --- | --- | --- |
| `cmake --build build --target zene -j4` (no change, confirming the binary is current) | `build-1.log` | 0 |
| `ctest -N -R ControlRenderSections` | `ctest-list-1.log` | 0 (1 test) |
| `ctest -R ControlRenderSections --output-on-failure` | `test-1.log` | 0 (output hidden on success) |
| `python3 tests/control-render-sections.py build/zene` (direct, with `QT_QPA_PLATFORM=offscreen`) | `script-direct-1.log` | 0, `PASS ... every check held` |
| Mutant: `ControlCommandsRenderSections.cpp` refuses an existing file (temporary, saved as `mutation.patch`), then `cmake --build build --target zene -j4` | `build-mutant-2.log` | 0 |
| same script against the mutant | `script-mutant-2.log` | 1, one `FAILED` line (`render.sections answered success with a file over the existing one`, reply `refused / Chorus already exists`); all other checks ok |
| `git apply -R mutation.patch`, then `git diff --stat` on the product file | (none) | 0, empty diff |
| `cmake --build build --target zene -j4` (restored) | `build-restored-1.log` | 0 |
| same script against the restored binary | `script-restored-1.log` | 0, `PASS` |
| `ctest -R ControlRenderSections -V` | `test-2.log` | 0, `100% tests passed out of 1`; the three new-check lines are at 66-69 |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `feature-check-2.log` | 1: the wrapper cannot create its sibling log folder (`PermissionError`), so no gate ran. See Self-check. |
| `bash tests/fork-sources-gate.sh` | `gate-fork-sources-2.log` | 0 |
| `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce-2.log` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-2.log` | 0 |
| `tests/scripted/check-namespace` | `gate-check-namespace-2.log` | 0 |
| `bash tests/no-tautology-gate.sh` | `gate-no-tautology-2.log` | 0 |
| `python3 tests/doc-figures-gate.py` | `gate-doc-figures-2b.log` | 0 |
| `bash tests/complexity-gate.sh --check` | `gate-complexity-2.log` | 1: 7 items, none names `control-render-sections` (`grep`: 0 matches) |
| `bash tests/file-length-gate.sh --check` | `gate-file-length-2.log` | 1: 22 items, none names `control-render-sections` (`grep`: 0 matches) |

The two ratchets were also red in round 1 with the same item counts (7 and 22). The file under test is 383 lines (`wc -l`), under the 500-line limit. Neither ratchet lists a changed file.

## Self-check

**Wrapper.** `feature_check.py:71` does `logs.mkdir` on `<worktree>-feature-check`, a sibling of the worktree. That path is outside the write-jail, so the wrapper cannot write its logs (`feature-check-2.log`, `PermissionError`). I ran the same gate commands directly, as in round 1, and the table above has their exit codes. The wrapper's own table was not produced.

**Regression proof (the "?" item about reverting).** This round changes no product code, so there is no RED→GREEN for a fix. What I can show is that the new check is sensitive: with a temporary refuse-existing-file mutation in `ControlCommandsRenderSections.cpp`, the check fails (`script-mutant-2.log`, EXIT 1), and every other check still passes. With the mutation reverted (`git apply -R`, empty `git diff`), the check passes (`script-restored-1.log`, `test-2.log`, both EXIT 0). The product file is back to the committed state.

**Determinism.** The check asserts no timing. It uses `session.result`, not `session.timed`, so the only bound is the reply timeout (`SECTIONS_TIMEOUT`), and the duration is never asserted. The stale bytes are the fixed `STALE_BYTES` constant. The section bounds are fixed (`3*ONE_BAR` to `4*ONE_BAR`), and the preset is the fixed `PRESET`. The directory is `sections-existing` inside the run's own temp directory (`instance.tmp`). The surface does not answer until the render child has finished, so the check cannot race the render. The result is read after that reply.

**Fix at the cause, every caller and sibling.** There is no product fix. The callers and siblings were read for the decision: the GUI menu (`src/gui/RenderRangeMenu.cpp:115-124`), the registration and reversibility table (`src/core/ControlReversibilityTablePassive.cpp`), the sibling `render.stems` (`ControlCommandsRenderStems.cpp:266`, unchanged), and `tests/control-render-sections.py`. Under option 2, every caller that exports twice into one folder would change, and the GUI's second export would need the same handling. That is the open decision.

**Collateral.** No product behaviour changed, and the existing checks write their own subdirectories (`sections`, `sections-portable`, `stems`), which the new check does not touch. The new check runs last, so it cannot change the section count that the earlier checks assert. The mutation was only a temporary experiment, and it was reverted before the final runs.

**Verdict.** NEEDS-CONTEXT (not ALREADY-FIXED or FALSE-POSITIVE). The code and contract evidence is in the sections above (`AudioFileDevice.cpp:47`, `ControlCommandsRenderSections.cpp:263-274`, `docs/A16-REVERSIBILITY.md:163`, `ControlCommandsRenderStems.cpp:266`, `ControlCommandsChain.cpp:149-156`). The contract decision is the owner's.
