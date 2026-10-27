# cluster new-p20-13 — `render.sections` refuses existing output paths

Verdict: FIXED

The finding is real. `render.sections` wrote each section file over an existing file of the same name, with no check: the generated paths were made unique only among the chosen sections (`ControlCommandsRenderSections.cpp:159-175`), and the writer opens its target with `QFile::WriteOnly | QFile::Truncate` (`src/core/audio/AudioFileDevice.cpp:47`). The fix is a preflight: every generated output path is checked before the first render starts, and an existing file, directory or symbolic link (even a dangling one) refuses the whole batch with a typed refusal that names the path.

An earlier round of this report recommended an explicit `overwrite` flag and left the choice to the owner. The review direction was refusal, which is the finding's first fix option, so this change adds no argument and changes no schema.

## Decision and behaviour change

- **Contract chosen:** refuse. A generated path that already holds something refuses the whole `render.sections` call before anything renders, with `kind: refused` and a message naming the path. No `overwrite` option exists; one can be added later as a separate change.
- **Visible behaviour change:** the GUI's Export Sections (`src/gui/RenderRangeMenu.cpp:115-124`) into a folder that already holds a section's file is now refused, and the message names the file. The user removes the file or picks another folder. Projects and saved data are not read or written by this command, so they load as before.
- **Not changed:** `render.stems` (it overwrites by design and documents that) and `render.render`.

## Product change

`src/core/ControlCommandsRenderSections.cpp`:
- `existingOutputPath` (`:179-189`): returns the first generated path where `QFileInfo::exists()` or `isSymLink()` is true. The symbolic-link test catches a dangling link, which `exists()` alone would miss, and the writer would follow it.
- preflight (`:279-286`): after the stems and extension are computed and before the render loop, the call is refused with `ControlErrorKind::Refused`.
- description (`:342-346`): one sentence saying the check runs before any render and that the whole call is refused.

Before this change, the loop rendered and overwrote each section in order (`ControlCommandsRenderSections.cpp:263-291` at the pre-fix commit `5da6a524a`), so an earlier section could be written before a later one replaced an existing file.

## Test

`tests/control-render-sections.py`, new `check_existing_file_refuses_batch`, run last in `run_checks`. It replaces the characterization check from earlier rounds, which expected the overwrite. The new check:
1. Adds `Bridge` (first, no file), `Chorus` and `Outro`.
2. Stages a non-WAV `Chorus.wav` and asks for `[Bridge, Chorus]`. Expects `kind: refused` naming `Chorus.wav`, Chorus's bytes unchanged, and only `Chorus.wav` in the directory (no `Bridge.wav`).
3. Removes `Chorus.wav` and reruns the same batch. Expects success with 2 files, so a refusal is not a lasting block.
4. Makes a dangling symbolic link `Outro.wav` and asks for `[Outro]`. Expects a refusal naming `Outro.wav` and that the link target is not created. If symbolic links are unavailable the check prints that and skips the dangling-link part.

## Commands run

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p20-13`. Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-13.r3/logs/`. Build commands set `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p20-13`, because ccache's default temp directory is outside the write-jail (the same `Permission denied` as in round 2).

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| Build with fix | `cmake --build build --target zene -j4` | `build-fix-1.log` | 0 |
| GREEN | `ctest -R ControlRenderSections -V` (from `build/tests`) | `test-fix-1.log` | 0, `100% tests passed out of 1`, the six new checks `ok` (lines 67-72) |
| Save product hunk | `git diff -- src/core/ControlCommandsRenderSections.cpp > fix.patch` (51 lines) | `fix.patch` | 0 |
| Reverse hunk | `git apply -R fix.patch` | (none) | 0 |
| Build without fix | `cmake --build build --target zene -j4` | `build-red-1.log` | 0 |
| RED | `ctest -R ControlRenderSections -V` | `test-red-1.log` | 8, `1 tests failed`. Failed: refusal (line 67), Chorus bytes (69), no earlier section (71), dangling-link refusal (74), link target not created (76). Passed: the rerun succeeds (73), which the old overwrite also satisfies. |
| Restore hunk | `git apply fix.patch` | (none) | 0 |
| Build with fix | `cmake --build build --target zene -j4` | `build-green-2.log` | 0 |
| GREEN | `ctest -R ControlRenderSections -V` | `test-green-2.log` | 0, `100% tests passed out of 1`, 50 `ok` lines |
| GUI sibling | `cmake --build build --target RenderRangeMenuTest -j4` then `ctest -R RenderRangeMenu --output-on-failure` | `build-rrm-1.log`, `test-rrm-1.log` | 0, 0 (1 test; it stubs `render.sections` and never renders) |
| Gate | `bash tests/fork-sources-gate.sh` | `gate-fork-sources-1.log` | 0 |
| Gate | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce-1.log` | 0 |
| Gate | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-1.log` | 0 |
| Gate | `tests/scripted/check-namespace` | `gate-check-namespace-1.log` | 0 |
| Gate | `bash tests/no-tautology-gate.sh` | `gate-no-tautology-1.log` | 0 |
| Gate | `python3 tests/doc-figures-gate.py` | `gate-doc-figures-1.log` | 0 |
| Ratchet | `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | 1: 7 items, same as before, none names `ControlCommandsRenderSections` |
| Ratchet | `bash tests/file-length-gate.sh --check` | `gate-file-length-1.log` | 1: 22 items, same as before, none names a changed file |
| Wrapper | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `feature-check-1.log` | 1: `PermissionError` creating its sibling log folder (same as earlier rounds) |

Ratchets: both are red on the train with the same item counts, and neither names a file this branch changes.

## Self-check

**1. The regression test fails with the product change reverted and passes with it.** Yes. With the hunk reversed (`git apply -R fix.patch`), `ctest -R ControlRenderSections` exits 8 (`test-red-1.log`). The refusal, the byte check, the no-partial-output check and the dangling-link checks fail, and the log shows `Bridge.wav` written and `Chorus.wav` replaced. With the hunk restored, the same test exits 0 (`test-green-2.log`).

**2. The test is deterministic.** It asserts no timing and sleeps for nothing. The refusal is synchronous: the preflight runs before any render child starts, so the reply does not depend on timing. The inputs are fixed: section bounds `3..6 * ONE_BAR`, the `STALE_BYTES` constant, and file names. Files live in `sections-existing` under the run's own temp directory. The one timed wait is the reply timeout (`SECTIONS_TIMEOUT`), which is never asserted. The success rerun waits for its reply, so it cannot race the file check.

**3. The fix is at the cause, and every caller and sibling was checked.** The preflight sits in the one function that writes section files, and it runs before the loop that calls `render.render`. Callers: the GUI menu (`RenderRangeMenu.cpp:115-124`, which reports the refusal; its test `RenderRangeMenuTest` passes), and any socket caller that re-exports into one folder, which is now refused by design. Sibling: `render.stems` (`ControlCommandsRenderStems.cpp:266`) overwrites by design, so it is not changed. The symbolic-link case is covered because `isSymLink()` is checked alongside `exists()`. Duplicate names inside a batch are still handled by `sectionFileStems`, which is unchanged.

**4. No collateral damage.** The check looks only at the names this call would generate, so a folder with other files, or a fresh folder, behaves as before. The only new refusal is for a second export into a folder that already holds a generated name. That is intended and is stated in the command description and the refusal message. No saved data is read or written, and no command is added or changed except the description text.

**5. Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so that item does not apply.

**Wrapper.** `feature_check.py` cannot write its sibling log folder from this sandbox (`feature-check-1.log`, `PermissionError`). I ran the same commands it runs, directly, with their exit codes in the table above. The wrapper's own table was not produced.

**Other QA commit.** A commit `50def37f4` ("qa-fix(new-p20-13): refuse existing section outputs") exists in this repository but is not on this branch. I did not cherry-pick it; this fix was written independently from the requirement, on top of `5da6a524a`.
