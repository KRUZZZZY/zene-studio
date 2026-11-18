FALSE-POSITIVE

# Cluster new-p15-31 — "a locked head trim shifts sample content in the wrong direction"

Branch `bugs/qwen-new-p15-31`, base `37eaf07557`. Finding: `src/core/ControlCommandsVcaEditMore.cpp:233-241` (the line numbers below are the current tree's; the offset line is now `:237`).

The finding proposes flipping the head-trim offset from `- dStart` to `+ dStart`. That would move the audio. The current sign is the one the codebase's offset convention requires, and the sign the finding proposes is the one it breaks.

## Verdict and evidence

**FALSE-POSITIVE.** The code computes the offset with the sign the clip model uses, and it is the same sign `clip.trim` uses.

1. **What the offset is.** For a sample clip the audio's song origin is `startPosition + startTimeOffset`, not a source pointer:
   - `src/core/SampleClip.cpp:838` — `originTicks = startPosition().getTicks() + startTimeOffset().getTicks()`.
   - `src/core/SampleClip.cpp:839-849` — linear branch: `elapsed` is the timeline distance past that origin, and the frame returned is `sourceIn + (elapsed - lead)`.
   - `src/core/SampleFrameEdits.cpp:50` — `tickOfFrame` uses the same origin, `start + offset`.
   So a sample at source frame `sourceIn + k` sounds at song tick `origin + k`.

2. **What a head trim must do to hold the audio.** Moving the start by `dStart` moves the origin by `dStart + Δoffset`. Holding the origin fixed needs `Δoffset = -dStart`. The current code does exactly that: `src/core/ControlCommandsVcaEditMore.cpp:237` — `offset = startTimeOffset - dStart`, with the delta taken at `:214` (`dStart = start - anchorStart`). The finding's `+ dStart` moves the origin by `2*dStart` and displaces the audio.

3. **Same sign as `clip.trim`.** `src/core/ControlCommandsClipTrim.cpp:131-135` — the head-trim rule: `plan->offset = startTimeOffset + (oldStart - start)`, which equals `startTimeOffset - dStart`.

4. **Owner decision.** `docs/m0-owner-decisions-2026-10-08.md:24` records the rule for trims of a linked group: "the source offset moves against the start, `clip.trim`'s own head-trim rule".

5. **Existing contract test, run on this tree.** `tests/src/core/ControlVcaLockedEditsTest.cpp:123-140` (`aLockedTrimMovesEveryMembersEdgesByTheAnchorsDeltas`) moves the start +100 and asserts the member's `startTimeOffset` is -100. It passes (see the run below).

6. **Sample-clip pin (added).** `tests/src/core/ControlVcaLockedEditsTest.cpp:151-177` (`aLockedSampleTrimHoldsEveryMembersAudioAtItsSongTick`) loads a 4-second buffer into both locked sample clips, records the source frame heard at song ticks 1100 and 1200, runs the locked trim (`start` 1100, `end` 1400), and asserts both clips still hear the same frames at those ticks. Under the finding's `+ dStart` it fails; with the current code it passes.

The finding's "selects earlier content" does not occur: the frame heard at each song tick is unchanged by the trim.

## Commands and results

Environment note: the first build failed on a ccache temp-file permission error (`CCACHE_DIR` temp path not writable from the jail), not on code. Later builds set `CCACHE_TEMPDIR=$HOME/.cache/ccache-tmp-new-p15-31`.

| Step | Command (cwd = worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target ControlVcaLockedEditsTest -j4` | 2 (ccache temp permission, environment) | `build-1.log` |
| Build with writable ccache temp | `CCACHE_TEMPDIR=... cmake --build build --target ControlVcaLockedEditsTest -j4` | 0 | `build-2.log` |
| Existing test, unmodified tree | `ctest -R ControlVcaLockedEditsTest --output-on-failure` (in `build/tests`) | 0 (1/1 ran) | `test-1.log` |
| Add pin, build | `... --target ControlVcaLockedEditsTest -j4` | 0 | `build-3.log` |
| Pin, unmodified product (GREEN) | `./ControlVcaLockedEditsTest -v2` (in `build/tests`) | 0 (10/10; pin PASS) | `test-2v.log` |
| Mutation: `src/core/ControlCommandsVcaEditMore.cpp:237` `- dStart` → `+ dStart` | `... --target ControlVcaLockedEditsTest -j4` | 0 | `build-5-flipped.log` |
| Mutation, RED | `./ControlVcaLockedEditsTest -v2` (in `build/tests`) | **2** (FAIL) | `test-4-flipped-RED.log` |
| Revert the mutation (Edit back to `- dStart`), rebuild | `... --target ControlVcaLockedEditsTest -j4` | 0 | `build-6-reverted.log` |
| Reverted, GREEN | `./ControlVcaLockedEditsTest -v2` | 0 (10/10, pin PASS) | `test-5-GREEN.log` |
| Reverted, ctest | `ctest -R ControlVcaLockedEditsTest` | 0 (100% of 1) | `test-6-ctest.log` |

RED detail (the flipped sign): `aLockedSampleTrimHoldsEveryMembersAudioAtItsSongTick` fails at `sourceFrameAt(TimePos(1100))`: actual `0`, expected `39375` (the frame heard at tick 1100 before the trim). `aLockedTrimMovesEveryMembersEdgesByTheAnchorsDeltas` also fails on the offset (actual `100`, expected `-100`). The flip is therefore caught both by the existing contract test and by the new audio test.

The working tree product file was restored; `git diff -- src/core/ControlCommandsVcaEditMore.cpp` is empty.

**Affected family.** `ctest -N -R Vca` (in `build/tests`, `ctest-family-list.log`) lists 9 registered tests, EXIT 0. Only `ControlVcaLockedEditsTest` is built in this build tree; I did not build the other eight. No product code changed, so their results cannot change from this commit; I did not run them.

**Gates.** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before running any gate: it creates `<worktree>-feature-check/` as a sibling of the worktree, which is outside the write-jail (`PermissionError`, `gate-feature-1.log`). I ran each of its commands directly from the worktree, logging to the LOG FOLDER (second run, after the test edit, `gate2-*.log`):

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (raw) | `bash tests/complexity-gate.sh --check` | 1 (6 REGRESSION lines) |
| file-length (raw) | `bash tests/file-length-gate.sh --check` | 1 (23 REGRESSION lines) |

The two ratchet gates exit 1 on the train by design. Neither raw output names `tests/src/core/ControlVcaLockedEditsTest.cpp`, the only changed file, so no regression is in a changed file. The script's base-comparison step (its `gate-baselines` cache and base checkout) was not run because those paths are outside the jail.

**Files committed:** `tests/src/core/ControlVcaLockedEditsTest.cpp` (new slot only; registration is unchanged because the file already exists in `tests/CMakeLists.txt`) and this report. No product file is changed.

## Self-check

- **The regression test fails with the product change reverted and passes with it (exit codes).** There is no product change to revert: the current code is correct. The test's proof is the mutation above: with the finding's proposed sign, the test fails (EXIT 2, `test-4-flipped-RED.log`); with the current sign it passes (EXIT 0, `test-5-GREEN.log`).
- **The test is deterministic.** It uses an injected in-memory buffer of fixed frames, fixed ticks (1100, 1200, 1400) and the existing `vca.edit_trim` command. No timing, sleeps or threads.
- **The offset is at the cause and consistent with its siblings.** `ControlCommandsVcaEditMore.cpp:237` agrees with `ControlCommandsClipTrim.cpp:135` and with the origin formula at `SampleClip.cpp:838`. The only caller of `editTrim` is the registered `vca.edit_trim` command (`ControlCommandsVcaEditMore.cpp:383-391`). I did not change any product code, so no sibling needed changing.
- **No collateral damage.** No product code changed. The test adds a slot to an existing test file; no other test or saved format is affected.
- **The FALSE-POSITIVE evidence (code or contract).** `src/core/SampleClip.cpp:838-849`, `src/core/SampleFrameEdits.cpp:50`, `src/core/ControlCommandsClipTrim.cpp:135`, `docs/m0-owner-decisions-2026-10-08.md:24`, and the test run above.
- **Unresolved, not caused by the code.** `feature_check.py` cannot write its logs from this jail (`<worktree>-feature-check/`), so its own PASS table is not produced; the six simple gates and both ratchet commands were run individually instead, as listed above.
