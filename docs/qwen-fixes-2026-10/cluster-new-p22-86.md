FALSE-POSITIVE

# Cluster new-p22-86: `vca.edit_trim` startTimeOffset "underflow"

Finding (Luna patrol, `src/core/ControlCommandsVcaEditMore.cpp:237`): `offset = startTimeOffset - dStart` "can become
negative/wrap before setStartTimeOffset" on a locked trim with a large positive `dStart`, and the suggested fix is to
refuse the whole edit when the resulting offset is out of range.

## Verdict: not a defect

Three independent reasons, each checked against the code.

1. **No wrap is possible.** `tick_t` is `std::int32_t` (`include/LmmsTypes.h:35`), so a value only "wraps" past
   ±2^31. The inputs are schema-bounded: `vca.edit_trim` takes `start` in `[0, MaxSongLength]` and `end` in
   `[1, MaxSongLength]` (`src/core/ControlCommandsVcaEditMore.cpp:387-388`), and `MaxSongLength = 9999 * DefaultTicksPerBar`
   = 9999 * 192 = 1,919,808 ticks (`include/Song.h:81`, `include/TimePos.h:39`). So `|dStart| <= 1.92e6`.
   More importantly, a head trim preserves the audio origin: `origin = start + startTimeOffset` is invariant
   (`src/core/ControlCommandsClipTrim.cpp:135`, `plan->offset = offset + (oldStart - start)`, which is `origin - newStart`).
   So `offset = origin - start` stays within a few million ticks however many trims are applied. Repeated edits cannot
   accumulate toward 2^31.

2. **A negative offset is the intended state, not an error.** A head trim moves the source offset against the start so
   the audio holds its song position (`ControlCommandsVcaEditMore.cpp:235-236`; the same rule in
   `ControlCommandsClipTrim.cpp:131-135`). Trimming a clip at offset 0 rightward by `d` yields offset `-d` by design.
   - `tests/src/core/ControlVcaLockedEditsTest.cpp:136` already asserts this: after a locked `+100` head trim the member's
     `startTimeOffset` is `-100`, and the test passes on this branch (see the evidence table).
   - `src/core/AafImportBuild.cpp:103` writes negative offsets deliberately.
   - `clip.slip` accepts the whole signed range `integerProperty(-MaxSongLength, MaxSongLength)` (`src/core/ControlCommandsClipTrim.cpp:228`) and the engine clamps
     the rendered frame into the source window (`src/core/ControlCommandsClipTrim.cpp:243-244` comment;
     `src/core/SampleClip.cpp:831-852`, `sourceFrameAt`, which computes `origin = start + offset` and maps the timeline
     onto the window).

3. **The suggested refusal would be a behaviour change.** `clip.trim` (the sibling the comment cites) applies the same
   unvalidated formula with no range refusal (`ControlCommandsClipTrim.cpp:135`, `:200`). Adding a refusal to
   `vca.edit_trim` only would make locked and unlocked head trims disagree, and it would refuse legitimate edits that
   produce a small negative offset. The brief's "out of range" also has no contract to be measured against.

No code was changed and no regression test was added, since there is no defect to regress. The existing
`ControlVcaLockedEditsTest.cpp:136` assertion already pins the contract the finding asks about.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| offset computed as `startTimeOffset - dStart` | `src/core/ControlCommandsVcaEditMore.cpp:237` (set at `:241`) |
| `tick_t` is signed 32-bit | `include/LmmsTypes.h:35` |
| trim args bounded to `[0, MaxSongLength]` / `[1, MaxSongLength]` | `src/core/ControlCommandsVcaEditMore.cpp:387-388` |
| `MaxSongLength = 9999 * DefaultTicksPerBar` (1,919,808) | `include/Song.h:81`, `include/TimePos.h:39` |
| head-trim offset rule, no range check | `src/core/ControlCommandsClipTrim.cpp:135`, `:200` |
| negative offsets from AAF import | `src/core/AafImportBuild.cpp:103` |
| engine maps via `origin = start + offset`, clamped to window | `src/core/SampleClip.cpp:838`, `:852` |
| existing test pins `-100` after a `+100` locked head trim | `tests/src/core/ControlVcaLockedEditsTest.cpp:136` |

## Commands run (EXIT codes are the real ones)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Confirm test is registered | `cd build/tests && ctest -N -R ControlVcaLockedEdits` | 0 (1 test) | `logs/ctest-list-1.log` |
| Build zene + test target | `cmake --build build --target zene ControlVcaLockedEditsTest -j4` | 2 | `logs/build-1.log` (ccache: permission denied on its temp dir under `~/.local/share/zene-bunny-fix/dsh`; not a code error) |
| Same build, ccache disabled | `CCACHE_DISABLE=1 cmake --build build --target zene ControlVcaLockedEditsTest -j4` | 0 | `logs/build-2.log` |
| Existing locked-trim family | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R ControlVcaLockedEditsTest --output-on-failure` | 0 ("100% tests passed out of 1") | `logs/test-2.log` |
| Feature gate (as scripted) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `logs/feature-check-1.log` shows it aborted with `PermissionError` creating the sibling folder `zene-qwen-new-p22-86-feature-check` (outside the write-jail) |
| Feature gate (same checks, logs in run folder) | inline `python3 -I` driver using `feature_check.SIMPLE`/`RATCHET`, `logs=<run>/logs` | 0 | `logs/feature-check-1.log` |

Feature-gate table (from the inline run; worktree has no changed files, base `37eaf0755`):

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (script exit 1, no new item in changed files; red on the train on purpose) |
| file-length | PASS (script exit 1, no new item in changed files; red on the train on purpose) |

Regression RED/GREEN (step 4/4b) was not run, because there is no fix to prove. The existing family run above is the
behaviour check.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p22-86.md` (this report). No product or test files changed.

## Self-check

Completion check run: `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p22-86`, output in
`logs/submit-check-1.log` (EXIT=1 before this section was added). Each item, with its answer:

- **Commits on the branch / working tree clean:** this report is committed on `bugs/qwen-new-p22-86`; `git status` is
  clean after the commit. No product file changed.
- **Report has a Self-check section:** this section.
- **Regression test FAILS with the product change reverted and PASSES with it:** not applicable. The verdict is
  FALSE-POSITIVE, so there is no product change to revert and no regression test was added. The existing assertion
  `tests/src/core/ControlVcaLockedEditsTest.cpp:136` (`startTimeOffset == -100` after a locked `+100` head trim) already
  pins the contract, and it passes on this branch (`logs/test-2.log`, EXIT=0, 1 of 1).
- **Test deterministic (no wall-clock, sleeps or races):** the existing test file has no `sleep`, `qWait`, `QTimer`,
  `wait`, `thread` or `msleep` (checked with `rg` on the file, no matches). No new test was written.
- **Fix at the cause and complete / callers and siblings consistent:** not applicable, no fix. The sibling `clip.trim`
  (`src/core/ControlCommandsClipTrim.cpp:135`, `:200`) uses the same unvalidated formula, and the audit found it
  consistent with `vca.edit_trim`.
- **No collateral damage:** not applicable, nothing changed. Refusing negative offsets would change saved and legitimate
  edits (`ControlVcaLockedEditsTest.cpp:136`), which is why no refusal was added.
- **FALSE-POSITIVE evidence:** given in the verdict section above with file:line.

**Gate row `gates (feature_check.py)` is FAIL for an environment reason, not a code one.** The script creates its log
folder `<worktree>-feature-check`, a sibling of the worktree in `projects/zene-studio-program/`, which is outside this
lane's write-jail. It raised `PermissionError` (`logs/feature-check-1.log`, EXIT=1). I did not edit the script or create
that folder. I ran the same six simple gates and two ratchet gates through the script's own `SIMPLE`/`RATCHET` lists
with the logs written to the run folder: all eight pass (table above). The two ratchet gates exit 1 on the train by
design and report no new item in changed files, and this branch changes no files. The row therefore needs the script's
log path changed to somewhere writable, or a run from an unrestricted shell, before it can read PASS on its own.
