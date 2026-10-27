IMPLEMENTED

# Cluster Q050-288 — Correct Sample Editor capability docs

Unit Q050-288 (P2, S, docs, area L05, member `R-editing-74`): the cited KNOWN-LIMITATIONS section
still said the destructive waveform editor was "socket-only, no interface" with "no range/selection
model", while the Sample Editor dialog, its eight range buttons and the registered
`sample.edit_range` command had landed. The section is rewritten around the implementation, the
agent-facing descriptions that repeated the stale claim are corrected, and a doc-truth test now
cross-checks the prose against the live registry and the GUI so the two cannot drift again.

Branch `feat/050-Q050-288-oc`, base `050/m0-train-12` (`b9ab28248`). Nothing was committed ahead of
the base before this unit; no earlier patch existed to keep.

**Round 2 (proof rework).** The loop's automated proof check reverted the branch's `src/` hunks, kept
the tests and rebuilt: all five named tests passed, so the change was not proven. Cause and fix: the
doc-truth slot read only `docs/KNOWN-LIMITATIONS.md` and `data/manual/index.html`, which that check
does not revert, so it could never fail on the reverted build. `tests/src/gui/SampleEditorDocsTest.cpp`
gains `everyWholeSourceDescriptionNamesItsRealGuiEntryPoint`, which reads the five whole-source
commands' **registered descriptions** from `ControlRegistry` and asserts each names its real GUI entry
point (`Insert Test Signal...`, `Process menu`, `Normalize...`, `Reverse`) and that `sample.fade` no
longer says the release has no range/selection model. Those are the strings in the reverted
`src/core/ControlCommandsSample*.cpp` hunks, so the same check now fails without the change
(`negctl.prove` → `ok: True`, reverted EXIT=8, restored EXIT=0).

## Integration onto `050/m0-train-17` (round 4, 2026-10-27)

The accepted patch (`integration/Q050-288-cf3618e61a` @ `7f8416a38`) was three-way applied onto
`050/m0-train-17` @ `639e1d23e` and conflicted in eight files. The train had moved under the patch in
two ways that matter: (a) another lane rewrote the same `sample.*` descriptions and the same
KNOWN-LIMITATIONS section to name the Process menu and the More menu's **Insert Test Signal...** form,
and (b) **Q050-106** added a ninth `sample.edit_range` value, `mono` (a WHOLE-source `(L + R) / 2`
downmix, `src/core/ControlCommandsSampleRange.cpp:60-61`), a **To Mono** button with
`controlCommand = sample.edit_range` and a **Split Stereo...** button (`sample.split_stereo`) in the
editor (`src/gui/SampleEditorDialog.cpp:250-257`), and **Convert to Mono** on the Process menu
(`src/gui/shell/ShellCommandMenus.cpp:223-229`). No new design was needed; every resolution keeps
both sides.

| File | Resolution |
|---|---|
| `src/core/ControlCommandsSample.cpp`, `src/core/ControlCommandsSampleEdit.cpp` (7 hunks) | the train's wording kept (More menu / form pre-fill / "A sample clip's Process menu offers it as ..."); the accepted patch's additions put back into it: `sample.generate` and `sample.amplify` point at `sample.edit_range`, and `sample.fade` drops the stale "no range/selection model" for the patch's "whole-source bake has no range parameter ... `fade_in` / `fade_out`" sentence |
| `docs/KNOWN-LIMITATIONS.md` | the accepted section kept, with the train's newer facts merged in: Insert Test Signal... on the More menu (header and Inspector) with its pre-filled form, MCP = same command; plus Q050-106's whole-source **To Mono** (`op: "mono"`), **Split Stereo...** (`sample.split_stereo`) and Process > **Convert to Mono**, and a note that `mono` is the eighth enum value and not a denoiser |
| `data/manual/index.html` | the train's paragraph (with its To Mono / Split Stereo sentence) kept; the patch's "range buttons are ..." and bounds sentences inserted before it and the Process / Crop the Sample sentence after it |
| `tests/CMakeLists.txt`, `tests/all-sources.txt`, `tests/fork-sources.txt` | union (train's `RecordModeMenuTest` / `SamplerTakeGuiTest` / Q050-156... entries kept, `SampleEditorDocsTest` added) |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | took the train's, then **regenerated live** (`snapshot_live.py`): 863 ids / 76 groups; diff vs the train is only the three `sample.*` descriptions still changed (`generate`, `amplify`, `fade`) plus capture metadata |
| `tests/src/gui/SampleEditorDocsTest.cpp` (no textual conflict; a **demonstrated regression** of the merge) | the accepted test asserted exactly 7 `op` values and exactly 8 `sample.edit_range` buttons; on train-17 there are 8 and 9 (Q050-106's `mono` / **To Mono**), so it would fail deterministically. The counts are updated to 8 and 9, and the test gains assertions that `mono` is the extra value, that the **To Mono** button exists, and that the limitations page documents To Mono (`` `op: "mono"` ``) and `sample.split_stereo`. Still exhaustive (no denoise value, no extra edit button), so no test is weakened |

Failure paths and platform hazards, all together: the only executable change is three registered
description strings (UI-thread registration) — no audio-thread, undo, format, schema or
reversibility-class change; the merge's semantic hazard was the stale exact counts above (fixed and
proven); Q050-106's `mono` is not a range edit, so the docs say so rather than list it among the
range tools. The test uses Qt-5-era API only. Builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-q288`
(the sandbox can't write ccache's configured temp dir). `feature_check.py` dies with
`PermissionError` creating `../zene-integrate-Q050-288-639e1d23e4-feature-check` (outside the
write-jail), so its own `SIMPLE`/`RATCHET` gate lists and baseline logic were run by importing the
module, with logs in the run folder (`/tmp/q288-gates.py`).

Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-288.r4/logs/`):

| Step | Command | Exit |
|---|---|---|
| build | `cmake --build build --target zene SampleEditorDocsTest SampleEditorDialogTest AudioAnalysisTest ControlSampleOperatorTest -- -j4` (`build-2.log`) | 0 |
| focused tests | `ctest -R '^(SampleEditorDocsTest\|SampleEditorDialogTest\|AudioAnalysisTest\|ControlSampleOperatorTest)$' --output-on-failure` (`test-1.log`) 4/4; `SampleEditorDocsTest -V` 6 passed, 0 skipped (`test-2.log`) | 0 |
| red: product hunks reverted to train-17 (`data/manual/index.html`, `docs/KNOWN-LIMITATIONS.md`, `src/core/ControlCommandsSample.cpp`, `src/core/ControlCommandsSampleEdit.cpp`), test kept | rebuild (`build-4-reverted.log`) EXIT=0; `ctest -R '^SampleEditorDocsTest$' -V` (`test-4-reverted.log`): `theDocsNameTheRangeOperationsAndTheirBounds` FAIL `(-3 dB)`, `everyWholeSourceDescriptionNamesItsRealGuiEntryPoint` FAIL `sample.generate does not point at 'sample.edit_range'`; 4 passed, 2 failed | 8 |
| green: restored | rebuild (`build-5-restored.log`) EXIT=0; same ctest (`test-5-restored.log`) 6 passed | 0 |
| snapshot | `snapshot_live.py $PWD` → 863 ids / 76 groups (`snapshot-1.log`) | 0 |
| doc figures | `python3 tests/doc-figures-gate.py` (`docfig-1.log`) | 0 |
| six-test focused run | `ctest -R '^(SampleEditorDocsTest\|SampleEditorDialogTest\|AudioAnalysisTest\|ControlSampleOperatorTest\|ControlCommandsSnapshot\|ReversibilityContractTest)$'` (`build-7.log` EXIT=0, `test-7.log`) 6/6 | 0 |
| gates | fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item) — all PASS (`gates-1.log`) | 0 |

(An earlier attempt, `build-3-reverted.log` / `test-3-reverted.log`, reverted nothing because the zsh
loop did not split the file list; it is not evidence and is superseded by runs 4-5.)

## Integration onto `050/m0-train-16` (round 3, 2026-10-26)

The accepted patch (`feat/050-Q050-288-oc` @ `a45ce5ca4`) was three-way applied onto
`050/m0-train-16` @ `cf3618e61` and conflicted in three registration/metadata files. Every
resolution is a union; no design changed and no test or gate was weakened.

| File | Conflict | Resolution |
|---|---|---|
| `tests/CMakeLists.txt` (offscreen `set_tests_properties` list) | train added `CPULoadWidgetTest`; patch added `SampleEditorDocsTest` | both kept |
| `tests/fork-sources.txt` (two `git diff` recipe comment lines + tail) | train added ReverbSC/RecordingRecoveryMenu entries and the Q050-148…cluster-5110 blocks; patch added `SampleEditorDocsTest.cpp` | train lines kept, `tests/src/gui/SampleEditorDocsTest.cpp` appended to the EqCurveTest recipe lines and as its own entry at the end |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | both sides regenerated it | took the train's, then **regenerated live** from the integrated build (`snapshot_live.py`): **849 ids / 76 groups** (the 833 below is the figure at the patch's old base). Diff vs the train: only the five `sample.*` description strings this patch changes plus the capture stamp/instance metadata - no id moved |

Failure paths and platform hazards considered: the only executable change is five registered
description strings (UI-thread registration), so there is no audio-thread, undo, format, or
reversibility-class change on either side of the merge; the train's newer commits do not touch
`ControlCommandsSample*.cpp`, `SampleEditorDialog.cpp`, the clip menus or `sample.edit_range`, so the
doc-truth test still reads the same GUI/registry it was written against (it passes on the integrated
tree). The new test uses Qt-5-era API only (unchanged from the accepted patch). Build note: this
session's sandbox could not write ccache's default temp dir, so builds ran with
`CCACHE_TEMPDIR=/tmp/ccache-tmp-q288` (no effect on output); `feature_check.py` writes its logs to a
sibling of the worktree, outside this session's write-jail, so a /tmp copy with only the log path
redirected to the run folder was used.

Evidence on the integrated tree (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-288.r3/logs/`):

| Step | Command | Exit |
|---|---|---|
| build | `cmake --build build --target zene SampleEditorDocsTest SampleEditorDialogTest AudioAnalysisTest ControlSampleOperatorTest -- -j4` (`build-2.log`) | 0 |
| focused tests | `ctest -R '^(SampleEditorDocsTest\|SampleEditorDialogTest\|AudioAnalysisTest\|ControlSampleOperatorTest)$'` (`ctest-3.log`) 4/4 | 0 |
| snapshot | `snapshot_live.py $PWD` → 849 ids / 76 groups (`snapshot-4.log`) | 0 |
| doc figures | `python3 tests/doc-figures-gate.py` (`docfig-5.log`) | 0 |
| red: `src/` hunks reverted | rebuild `SampleEditorDocsTest` (`build-6-reverted.log`) EXIT=0; `ctest -R '^SampleEditorDocsTest$'` (`ctest-7-reverted.log`): `everyWholeSourceDescriptionNamesItsRealGuiEntryPoint` FAIL "sample.generate still says Socket-only" | 8 |
| green: restored | rebuild (`build-8-restored.log`) EXIT=0; `ctest -R '^(SampleEditorDocsTest\|SampleEditorDialogTest\|AudioAnalysisTest\|ControlSampleOperatorTest\|ControlCommandsSnapshot\|ReversibilityContractTest)$'` 6/6 (`ctest-9-restored.log`) | 0 |
| gates | `feature_check.py` - all 8 rows PASS (`feature-check-11.log`) | 0 |

## What was built

- `docs/KNOWN-LIMITATIONS.md:2333-2372` — the section rewritten. It now leads with the Sample
  Editor's selected-range editing: the buttons **-3 dB**, **+3 dB** (gain), **Silence**, **Delete**,
  **Reverse**, **Fade In**, **Fade Out**, **Crop**, and the `sample.edit_range` `op` values `gain`
  (with `gain_db`), `silence`, `delete`, `reverse`, `fade_in`, `fade_out`, `crop`. It states that
  each button is the same registered command an agent runs (one implementation, one undo step, a Clip
  checkpoint against the engine's `min(100 steps, 16 MiB)` bound), that Silence keeps the range length
  while Delete closes the gap, that Crop keeps only the range, and the two refusals (deleting
  everything the clip plays; moving warp markers). It then states the real bounds: range gain applies
  **no limiter**; the destructive fades are **linear** with no curve choices; the Sample Editor's range
  tools have **no noise reduction** (`sample.edit_range` registers no denoise operation), with the
  plug-in denoiser named as a separate effect rather than a range operation. It keeps the whole-source
  **Process** menu bakes distinct (`sample.normalize` / `amplify` / `reverse` / `fade`, refused for a
  trimmed `srcin/srcout` window), names `sample.generate` behind **Insert Test Signal...**, and
  distinguishes the editor's **Crop** (`sample.edit_range` `op: "crop"`) from **Crop the Sample**
  (`sample.crop`), with `clip.set_fade` / `clip.set_gain` as the non-baking window controls.
- `data/manual/index.html:64` — the Sample Editor paragraph names the same eight buttons, says Silence
  keeps the range's length and Delete closes the gap, states the no-limiter and linear-fade bounds and
  the absent noise reduction, and points at the separate Process-menu bakes and Crop the Sample.
- `src/core/ControlCommandsSample.cpp:33-38,203-204` and `src/core/ControlCommandsSampleEdit.cpp:31-34,165,214,276-277,313-314`
  — the five whole-source commands' registered descriptions and the TU comment said "Socket-only in
  this release" / "no interface reaches them" / "this release has no range/selection model". Each now
  names its real GUI entry point (`Insert Test Signal...`, Process > `Normalize...` / `Amplify...` /
  `Reverse` / `Fade`) and points at `sample.edit_range` for the selected-range surface. Text only: no
  id, schema, handler, undo path or project format changed.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` — regenerated from a live instance
  (`833 ids / 76 groups`); only the five description strings and the capture stamp/instance metadata
  moved, no id changed. README's canary already read 833/76 and `tests/doc-figures-gate.py` passes.
- `tests/src/gui/SampleEditorDocsTest.cpp` (new) — the doc-truth proof, registered in
  `tests/CMakeLists.txt` (offscreen property) and `tests/all-sources.txt`:
  - `theDocsNameTheRangeOperationsAndTheirBounds` reads `docs/KNOWN-LIMITATIONS.md` and
    `data/manual/index.html` from the source tree and, for each of the eight buttons, asserts the
    label is in both documents, that the button exists in the dialog with
    `controlCommand == sample.edit_range`, and that the documented `op` is in the live registry's
    `sample.edit_range` enum. It asserts the page says "no limiter", "linear" and "no noise
    reduction", that the stale "socket-only, no interface" headline is absent, and that the
    whole-source bakes and `sample.crop` stay named and distinct. A renamed button, a renamed `op`
    or a restored stale sentence fails the test.
  - `everyButtonIsOneUndoStepThroughTheRegisteredCommand` drives all eight buttons: undo depth rises
    by exactly one, the expected transform is measured on the frames (gain by dB, silence zeroed,
    delete/crop lengths, reverse order, fade endpoints), one `control.undo` restores the pre-edit
    frames bit-exactly and the depth returns.
  - `theClipMenuOpensTheEditorAndKeepsTheBakesSeparate` builds the clip menu and asserts
    `Sample Editor...` declares `sample.edit_range`, Process > Normalize.../Amplify.../Reverse declare
    their own ids, and `Crop the Sample` declares `sample.crop`.

No command, schema, project-format or audio-path change.

## Acceptance

| # | Criterion | Test / proof | How it proves it | Command | Exit |
|---|---|---|---|---|---|
| 1 | Rewrite the cited KNOWN-LIMITATIONS section to list the current gain, silence, delete, reverse, fade and crop range operations | `SampleEditorDocsTest::theDocsNameTheRangeOperationsAndTheirBounds` (`tests/src/gui/SampleEditorDocsTest.cpp`); diff at `docs/KNOWN-LIMITATIONS.md:2333-2372` | reads the section and asserts every label (-3 dB, +3 dB, Silence, Delete, Reverse, Fade In, Fade Out, Crop) and every `sample.edit_range` `op` value (`gain`, `silence`, `delete`, `reverse`, `fade_in`, `fade_out`, `crop`) is named, the `op` list taken from the live registry enum so prose and registry cannot drift; asserts the old "socket-only, no interface" headline is gone; with the docs reverted and nothing else changed the same slot fails on the first label | `cd build/tests && ctest -R '^SampleEditorDocsTest$' --output-on-failure` | 0 (restored) / 8 (docs reverted, 1 failed) |
| 2 | Keep the real bounds explicit: no limiter, fades are linear, noise reduction absent; check command and GUI names against the implementation | same slot; `AudioAnalysisTest::aRangeFadeReachesItsEndpoint`; `ControlSampleOperatorTest::amplifyBakesTheGainAndOneUndoRestoresTheFrames`, `fadeBakesTheRampAndOneUndoRestoresTheFrames`; `SampleEditorDialogTest::anOperationActsOnTheFieldsRange` | the slot asserts the page says "no limiter", "linear" and "no noise reduction"; the gain path multiplies frames with no clipper (`src/core/ControlCommandsSampleRange.cpp:60-77`) and the range fades are linear ramps there, corroborated by the existing fade and gain bakes; the slot also asserts the whole-source bakes and `sample.crop` are named and distinct, and a second slot asserts the clip-menu wiring (editor `sample.edit_range`; Process bakes their own ids; `Crop the Sample` `sample.crop`) | `ctest -R '^(SampleEditorDialogTest|AudioAnalysisTest|ControlSampleOperatorTest|SampleEditorDocsTest)$' --output-on-failure` | 0 |
| 3 | One undo step per range edit (the page now promises it) | `SampleEditorDocsTest::everyButtonIsOneUndoStepThroughTheRegisteredCommand` | for each of the eight buttons: depth +1, the transform measured on the frames, one `control.undo` restores the pre-edit frames bit-exactly, depth back to its old value | `ctest -R '^SampleEditorDocsTest$' --output-on-failure` | 0 |

Negative control: with only the documentation hunks reverted (product code, test and manifests left
as they are), `SampleEditorDocsTest` reports `FAIL! theDocsNameTheRangeOperationsAndTheirBounds
'limitations.contains(action.label)' returned FALSE. (-3 dB)` and exits 8; restoring the docs and
re-running the same binary exits 0.

## Commands, format and undo

No new or changed command ids, schemas or handlers, so no reversibility-class change and no A16
histogram move (`ReversibilityContractTest` passes unchanged). `sample.edit_range` keeps its
`true_inverse` (Clip checkpoint) row. No project-format change; old projects load and render as
before.

## Known limits (as the corrected page now states)

- Range gain applies **no limiter**; values past 0 dBFS stay.
- The destructive range fades are **linear**; there are no curve choices.
- The Sample Editor has **no noise reduction** range operation (the plug-in denoiser is a separate
  effect).
- The whole-source Process bakes refuse a clip playing a trimmed `srcin/srcout` window.

## Logs

Sandbox note: this session's file policy denies writes outside the worktree (the requested log
directory `~/.local/share/zene-bunny-fix/qwen/runs/Q050-288.r1/logs`, the workspace root and the
`projects/` parent all answer EACCES to `touch`/`mkdir`; only paths under this worktree are writable),
so every build/test/gate log was written under `build/qwen-logs/Q050-288.r1/` inside the worktree
instead (`build/` is git-ignored). The gate harness also cannot create its
`<worktree>-feature-check` log directory next to the worktree, so the check was run with
`PYTHONPATH=build/qwen-logs/Q050-288.r1/redirect`, whose `sitecustomize.py` redirects only that
directory into `build/qwen-logs/Q050-288.r1/feature-check/` and leaves the real
`submit_check.py`/`feature_check.py` commands, thresholds and comparisons untouched:
`PYTHONPATH=$PWD/build/qwen-logs/Q050-288.r1/redirect python3
~/.local/share/zene-bunny-fix/qwen/submit_check.py $PWD Q050-288` → EXIT=0, "RESULT: PASS - submit"
(`submit-check-*.log` in that directory: every run of the check on this branch, each `RESULT: PASS
- submit`; same table in `gate-feature-check-3.log`, run through the equivalent `run-feature-check.py`
wrapper before the commit).

| Log | Command | Exit |
|---|---|---|
| `build-1-baseline.log` | pre-change build of `SampleEditorDialogTest` | 0 |
| `build-2..6-docstest.log` | build of `SampleEditorDocsTest` (2-4 are the pre-reconfigure and compile iterations, 5 the const-correctness failure, 6 the successful build) | 6 → 0 |
| `build-7-zene.log` | `zene SampleEditorDialogTest AudioAnalysisTest ControlSampleOperatorTest` | 0 |
| `build-8-rev.log` | `ReversibilityContractTest` | 0 |
| `test-docstest-1.log` | `ctest -R '^SampleEditorDocsTest$'` before the docs line-wrap fix | 8 (1 failed) |
| `test-docstest-2.log` | same after the fix | 0 |
| `test-docstest-reverted-1.log` | same binary, documentation hunks reverted (negative control) | 8 (1 failed) |
| `test-neighbours-1.log` | the four-test focused run above | 0 |
| `test-snapshot-rev-1.log` | `ctest -R '^(ControlCommandsSnapshot|ReversibilityContractTest)$'` | 0 |
| `snapshot-1.log` | `snapshot_live.py $PWD` → 833 ids / 76 groups | 0 |
| `gate-*.log` | each gate, and `gate-feature-check-3.log` for the one-table run | all 0 |
| `build-9-docstest-r2.log`, `build-12-restored.log` | rebuild of `SampleEditorDocsTest` with the round-2 slot, and the four targets after restoring the product files | 0 |
| `build-10-negctl.log` | rebuild with the two `src/` description files reverted (round-2 negative control) | 0 |
| `test-docstest-negctl-2.log` | `ctest -R '^SampleEditorDocsTest$'` on that reverted build | **8** (1 failed: `sample.generate still says Socket-only`) |
| `test-restored-3.log` | the same four tests after restoring, full rebuild of all four targets | 0 (4/4) |
| `test-final-targeted.log` | six-test focused run on the final tree | 0 (6/6) |
| `negctl-selfrun-2.log` | the loop's own `negctl.prove()` against this branch | `ok: True`, reverted 8, restored 0 |
| `submit-check-*.log` | every run of the pre-submit completion check; all mechanical items PASS | 0 |

## Self-check

**Round 3 (integration on `cf3618e61`), checked again on the integrated tree:** red/green re-proven
(`src/` hunks reverted → `SampleEditorDocsTest` EXIT=8; restored → 6/6 EXIT=0); every criterion's
test re-run EXIT=0 (table in "Integration" above); no command, undo path, format or audio-path code
changed by the merge resolution. `submit_check.py` (`submit-check-12.log`, EXIT=1) reports one FAIL,
"gates (feature_check.py)": it runs `feature_check.py`, which in this session dies with
`PermissionError` creating `../zene-integrate-Q050-288-cf3618e61a-feature-check` (outside the write-jail;
`feature-check-10.log`), not on a gate. The same script with only that log path moved to the run folder
passes all 8 rows (`feature-check-11.log`, EXIT=0). The two WARNs are explained under
"Submitted-check warnings" below (snapshot figure now 849/76, README canary matches, doc-figures EXIT=0).
The answers below are from rounds 1-2 and still hold on the integrated tree.

- **? Each new test FAILS with the product change reverted, then PASSES restored (exit codes).**
  Round 1 answered this by reverting the two markdown files by hand. The loop's proof check reverts
  the branch's *product* hunks instead (the `src/` files) and keeps the tests, so that answer did not
  meet it: `SampleEditorDocsTest` passed with the source reverted, because its doc assertions read
  files the harness does not revert. Fixed in `259c23aae` by adding
  `SampleEditorDocsTest::everyWholeSourceDescriptionNamesItsRealGuiEntryPoint`, which reads the five
  `sample.*` **descriptions back from the live registry** and asserts each names its real GUI entry
  point and that `sample.fade` no longer claims "no range/selection model". Those strings live in the
  reverted `src/` hunks, so the claim is now in the product change the harness reverses. Running the
  loop's own `negctl.prove()` on this branch (via
  `build/qwen-logs/Q050-288.r1/negctl-selfrun-2.log`, same code path as the loop's check) returns
  `{'ok': True, ..., 'reverted_rc': 8, 'restored_rc': 0, 'tree_clean': True}`: with the two `src/`
  files reverted and everything else kept, `AudioAnalysisTest, ControlCommandsSnapshot,
  ReversibilityContractTest, SampleEditorDialogTest, SampleEditorDocsTest` run and
  `SampleEditorDocsTest` fails with `FAIL! ... '!description.contains("Socket-only")' returned FALSE.
  (sample.generate still says Socket-only)` (EXIT=8, `Totals: 5 passed, 1 failed`); restored, the
  same five pass (EXIT=0). Confirmed twice (`negctl-selfrun-2.log`, `negctl-selfrun-4.log`); one
  attempt in between (`negctl-selfrun-3.log`) had its link step Terminated by the box's load
  (`make[3]: *** [tests/CMakeFiles/SampleEditorDocsTest.dir/build.make:2152 ...] Terminated`,
  `reverted_build_rc: 2`), which the harness still counts as "fails without the change" but is not an
  assertion result — hence the rerun, which reproduced the clean assertion failure. Before the new slot existed the same harness run reported
  `'ok': False` with all five passing on the reverted build — that is the reviewer's finding, now
  closed. The hand-reverted markdown control is kept too: with only the documentation hunks reverted
  the doc slot fails on `-3 dB` (EXIT=8, `test-docstest-reverted-1.log`).
- **? Every acceptance criterion is proven by a test I ran myself (name + exit code).** Criterion 1
  and 2: `SampleEditorDocsTest::theDocsNameTheRangeOperationsAndTheirBounds` inside
  `ctest -R '^SampleEditorDocsTest$'` EXIT=0; corroborated by `SampleEditorDialogTest`,
  `AudioAnalysisTest` and `ControlSampleOperatorTest` (all EXIT=0 in `test-neighbours-1.log`).
  Criterion 3: `SampleEditorDocsTest::everyButtonIsOneUndoStepThroughTheRegisteredCommand`, same
  EXIT=0. The corrected agent-facing descriptions are proven by
  `SampleEditorDocsTest::everyWholeSourceDescriptionNamesItsRealGuiEntryPoint`, which is the slot the
  loop's reverted build fails (EXIT=8 above); on the restored tree the focused six-test run
  `ctest -R '^(SampleEditorDocsTest|SampleEditorDialogTest|AudioAnalysisTest|ControlSampleOperatorTest|ControlCommandsSnapshot|ReversibilityContractTest)$'`
  is 6/6 EXIT=0 (`test-final-targeted.log`).
- **? Each edit is ONE undo step, and every new or changed command has an honest reversibility
  class.** No command was added or changed (ids, schemas and handlers are untouched; only
  `description` text changed), so no class changed and no A16 row moved — `ReversibilityContractTest`
  EXIT=0. `sample.edit_range` remains `true_inverse` / "Clip checkpoint"
  (`src/core/ControlReversibilityTableDevices.cpp:153`), and the test proves one checkpoint per
  button: depth +1 per edit and one `control.undo` restores the pre-edit frames bit-exactly.
- **? Nothing added runs on the audio thread.** The diff adds no code on any audio path: the only
  production changes are `description` string literals in the registry registrations and a comment,
  both built at registration time on the UI thread. The new test links the normal test binary only.
  No allocation, free, lock, wait, I/O or signal was added to `processImpl` / `play` / any render
  path; local TSan does not run on this box, but there is no new concurrent access to reason about.
  (The pre-submit check's audio-path heuristic matched only this answer's own prose, which quotes
  those function names as the paths NOT touched: `git diff b9ab28248..HEAD | grep -nE
  'processImpl|playImpl|processAudioBuffer|RealtimeScope|NotePlayHandle'` matches only added lines
  inside this report's Self-check section - no added line of any source file matches.)
- **? Old projects still load and render exactly as before; new data round-trips.** No project-format
  or model change: the diff touches no save/load code, no schema and no sample data path. The
  behaviour tests that exercise the range edits (`AudioAnalysisTest`, `ControlSampleOperatorTest`)
  and the editor (`SampleEditorDialogTest`) pass unchanged, EXIT=0.
- **? Qt 5.15 / MSVC / macOS < 10.15 portability.** The new test uses only Qt 5-era API:
  `QCoreApplication::applicationDirPath()`, `QFile`, `QMenu`, `QPushButton`, `QJsonArray/Object`,
  `QStringList`, `std::vector`/`std::pow` — no Qt-6-only call, no `std::filesystem`, no POSIX-only
  call, and no `small`/`near`/`far`/`interface`/`min`/`max` identifier. Registration uses the same
  `LMMS_TESTS` loop and offscreen property as its `SampleEditorDialogTest` neighbour, which already
  builds on all hosted CI targets.
- **? Sibling commands in the command files I changed.** The only production files changed are
  `src/core/ControlCommandsSample.cpp` (`sample.generate`) and
  `src/core/ControlCommandsSampleEdit.cpp` (`sample.amplify`, `sample.normalize`, `sample.reverse`,
  `sample.fade`). Each of the five descriptions was checked against its real GUI entry point
  (`ShellCommandMenus::populateTrack` → "Insert Test Signal..."; `ShellCommandMenus::sampleClipMenus`
  → Process > Normalize.../Amplify.../Reverse/Fade) and against behaviour that did not change: all
  five remain whole-source bakes, still one Clip checkpoint each, still `true_inverse` in
  `ControlReversibilityTableSample.cpp`, and `ControlSampleOperatorTest` passes unchanged. The
  `sample.*` group's remaining ids (`sample.crop`, `sample.make_unique`, `sample.edit_range`,
  `sample.spectrogram`) were read too: their descriptions were already accurate (crop/window,
  shared-buffer copy, range edit, read-only spectrum) and none of them is touched.

## Submitted-check warnings, and why they stand

- **"a GUI path for the user-facing action - no GUI file changed".** Correct and expected: this is a
  documentation unit. The GUI path (`Sample Editor...` in `src/gui/shell/ShellCommandMenus.cpp:313-315`,
  the eight buttons in `src/gui/SampleEditorDialog.cpp:180-198`) already existed and is unchanged;
  what was wrong was the prose that denied it. The new GUI test drives those existing buttons.
- **"canary figures (README) updated - new command ids: control.undo, control.undo_depth,
  sample.amplify, sample.crop, sample.edit_range".** A regex false positive: the check scans `+`
  lines for quoted `group.verb` strings, and the added sentences quote the ids whose *descriptions*
  changed. No command id was added or removed (snapshot: 833 ids / 76 groups before and after;
  `tests/doc-figures-gate.py` EXIT=0 against README's `833 ids / 76 groups` canary).
