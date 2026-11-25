FALSE-POSITIVE

# cluster new-p14-275: "Pitch-preserving playback ignores audio-loop wrapping"

Finding (Luna patrol): `src/core/SamplePlayHandle.cpp:~592`. The finding says the pitch-preserving render
(`renderPreservingPitch`) reads one finite source window, ignores the loop mode and never wraps its cursor,
so "a looped clip [that] uses pitch-preserving stretch" goes silent at the window end.

## Verdict: FALSE-POSITIVE

The finding's trigger cannot happen. On this branch a clip's audio loop and the pitch-preserving stretcher
rule each other out. Both conditions come from the same clip state, read in the same constructor:

1. **When a clip goes through the stretcher.** `src/core/SamplePlayHandle.cpp:129-134`, `rendersThroughStretcher`:
   ```cpp
   return (warpModeKeepsPitch(mode) && !warpModeRepeatsTransients(mode) && !rendersLinearly)
       || (transpose != 0 && warpModeUsesRubberBand(mode));
   ```
   `m_preservePitch` is set from it at `src/core/SamplePlayHandle.cpp:183`, with
   `m_rendersLinearly = clip->rendersLinearly()` (`:164`). So `m_preservePitch` requires the clip to be
   **non-linear** (warp markers or a non-following tempo, `include/SampleClip.h:233-236`) **or transposed**.

2. **When a clip's audio loop is engaged.** `src/core/ClipAudioLoop.cpp:79-84`, `audioloop::engaged`
   returns false whenever `audioloop::refusal(clip)` is non-empty. `refusal` (`src/core/ClipAudioLoop.cpp:69-77`) is
   non-empty exactly when:
   - `!clip.rendersLinearly()`: "a warped or tempo-leading clip's audio cannot loop in this release" (`:71`)
   - `edits.transpose != 0`: "a transposed clip's audio cannot loop in this release" (`:73`)
   - `edits.hasSpeed()`, or the clip is a record clip (`:74-75`)

3. **The handle's loop mode depends on that check.** `m_loopMode` defaults to `Sample::Loop::Off`
   (`include/SamplePlayHandle.h:318`). The only place that sets it is `SamplePlayHandle::snapshotAudioLoop`
   (`src/core/ClipAudioLoop.cpp:197-210`), and that function returns early on `!audioloop::engaged(*clip)`
   (`:199`). The unfolded loop window, which can run past the clip's own window, is likewise only used
   when `audioloop::engaged` (`src/tracks/SampleTrack.cpp:289-290`). Otherwise the window is the normal
   `prefades::window`.

The conditions in (1) are the complement of those in (2): a clip that renders through the stretcher is
non-linear or transposed, and every such clip fails `engaged`. So no handle has both
`m_preservePitch == true` and `m_loopMode == Loop::On`. A pitch-preserving handle always renders
the clip's plain window, and stopping at that window's end is the correct end of the clip's audio (the
same end `Sample::render` stops at on the resampler path; see the doc comment at
`src/core/SamplePlayHandle.cpp:732-736`).

Every path in is gated:
- The command path refuses to author the combination: `clip.set_loop` → `audioloop::refusal`
  (`src/core/ControlCommandsClipEnvelope.cpp:207-208`), and audio rotation does the same
  (`src/core/ControlCommandsNoteTools.cpp:330-331`).
- The render path checks again. If a project file or a later edit (warping or transposing a looped clip)
  leaves a stored loop on a clip that now renders through the stretcher, `engaged` is false at handle
  construction, so the loop is dormant and is not half-applied.
- The silent (muted) advance path `advanceSourceSilently` (`src/core/SamplePlayHandle.cpp:807-846`) uses the
  same `m_preservePitch` / `m_loopMode` split, so the same reasoning covers it.

The fix the finding proposes ("a loop-aware source reader that wraps at the authored loop points") would add a
feature this release explicitly refuses: looping warped, tempo-leading or transposed clips. It does not fix a
defect, so no product code was changed.

## Commands and results

No product code was changed, so there is no RED/GREEN pair (FALSE-POSITIVE: the evidence is the code above).
The existing test that pins the command-side refusal was built and run unchanged. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-275.r1/logs/`.

| # | Command | EXIT | Result |
|---|---------|------|--------|
| 1 | `cmake --build build --target AudioClipLoopTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 | `[100%] Built target AudioClipLoopTest` |
| 2 | `cd build/tests && ctest -R AudioClipLoopTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 0 | `1/1 Test #406: AudioClipLoopTest ... Passed`, `100% tests passed out of 1` |
| 3 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-2.log 2>&1; echo EXIT=$?` | 1 | **Gate step blocked in this lane**, see below |

`AudioClipLoopTest::refusalsAreTyped` (`tests/src/core/AudioClipLoopTest.cpp:199-217`) puts a clip into
`WarpTempoMode::SourceTempo`, so it no longer renders linearly. It then asserts that `clip.set_loop`
returns `ControlErrorKind::Refused` (`:206-209`). That is the command-side half of the mutual exclusion.

Gate run 3 never reached the gates. The lane guard refused the script's own baseline checkout:
```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
subprocess.CalledProcessError: ... returned non-zero exit status 97.
```
Per the run's rules ("if a command you need is refused, stop that step"), I did not work around this. This
commit adds only this report (a doc file) and no product, test or list-file change, so the complexity,
file-length and registration gates have nothing new to judge. The PASS/FAIL table itself was not produced and
is unverified.

## Round 2

The round-1 review's verdict reads exactly:

> proof fits the claim: no artifact shows `code-reference-or-test` (kind code-reference (path and line that show the behaviour) or a regression-test)

The round-1 manifest held three `code-reference` artifacts with `path` and `line` fields. The schema
(`schemas/proof.json`) and the checker are not reachable from this sandbox: `~/.config/kads/zene` holds only
`state/`. So I could not tell which field was missing. Round 2 keeps the code-references and adds the other
accepted kind: a test that pins the contract the verdict rests on, with a real RED.

**Contract test.** `AudioClipLoopTest::aClipThatRendersThroughTheStretcherNeverEngagesItsLoop`
(`tests/src/core/AudioClipLoopTest.cpp:219-246`). It loops a clip through `clip.set_loop` and checks the loop is
engaged. Then:
- **(A)** It makes the clip tempo-leading (`SourceTempo` at 120 BPM, `WarpStretchMode::PreservePitch`), which is
  the stretcher path. It asserts `!rendersLinearly()`, that the loop is still stored in `ClipEdits` (as an old
  project could store it), and `!audioloop::engaged`. After restoring `FollowProject`, the loop is engaged again.
- **(B)** It writes `transpose = 2` with the Rubber Band-backed `WarpStretchMode::Beats`, also the stretcher path,
  and asserts the stored loop is still not engaged.

The test is deterministic: fixed values on an in-memory song, no timing, threads or sleeps.

**RED / GREEN.** No product code changes in this cluster, so RED uses a temporary mutation that puts the
finding's scenario back. It deletes the `!refusal(clip).isEmpty() ||` guard from `audioloop::engaged`
(`src/core/ClipAudioLoop.cpp:82`). That lets a stretched clip engage its loop, which is the only way the
stretcher could meet a loop. The mutation was reverted afterwards (`git diff -- src/` empty; `git status`
shows only the test file).

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-275.r2/logs/`. Build:
`cmake --build build --target AudioClipLoopTest -j4 > $L/build-NN.log 2>&1; echo EXIT=$?`. Test (from build/tests):
`ctest -R AudioClipLoopTest --output-on-failure > $L/test-NN.log 2>&1; echo EXIT=$?`.

| Step | Build | EXIT | Test | EXIT | Result |
|------|-------|------|------|------|--------|
| GREEN, code as committed | build-01.log | 0 | test-01.log | 0 | `100% tests passed out of 1` |
| RED, guard mutated out | build-02.log | 0 | test-02.log | 8 | `FAIL! : AudioClipLoopTest::aClipThatRendersThroughTheStretcherNeverEngagesItsLoop() '!audioloop::engaged(*subject)' returned FALSE.` `Loc: [../../tests/src/core/AudioClipLoopTest.cpp(234)]`, `Totals: 12 passed, 1 failed` |
| GREEN, mutation reverted | build-03.log | 0 | test-03.log | 0 | `100% tests passed out of 1` |

**Gates.** `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $L/gates-04.log 2>&1`
gave EXIT=1. As in round 1, the run stopped at the script's baseline step: the lane guard refuses
`git -C .../gate-baselines/tree checkout -q --detach 9913d36d53...` (exit 97). The six simple gates and the two
ratchet gates do not need that baseline, so they were run directly from the worktree.
Their logs are in `.../new-p14-275.r2/delegate/01-deepseek/logs/`, because the DeepSeek delegate's sandbox could not write to `$L`.

| Gate | Command | EXIT |
|------|---------|------|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 (`PASS: every tracked source in scope is registered`) |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| file-length | `bash tests/file-length-gate.sh --check` | 0 (`PASS (check mode: no regressions; baseline not written)`) |
| complexity | `bash tests/complexity-gate.sh --check` | 2: `complexity-gate: lizard is not installed`; **not measured** |

`lizard` is not installed on this box: `which lizard` finds nothing and `import lizard` fails. Installing it is a
system change outside this lane, so I did not install it. The only new function has no branches (CCN 1, from
the diff), so it cannot be a new complexity item, but the gate itself did not run.

## Self-check

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-275`
(its exit code and output are recorded in the commit-time log `submit-*.log` in the r2 log folder). The `gates`
row can only show FAIL while `feature_check.py`'s baseline checkout is refused by the lane guard (exit 97, quoted
above). That step is **BLOCKED** in this lane; every gate it wraps was run directly and is listed above.

- **The test fails without the change and passes with it.** The cluster has no product change, since the
  verdict is FALSE-POSITIVE. The equivalent proof is the mutation of the contract the verdict rests on: test-02
  EXIT 8 with the `audioloop::engaged` refusal guard removed; test-01 and test-03 EXIT 0 with the code as
  committed.
- **Deterministic.** The new test drives `clip.set_loop` through the control registry, then direct setters
  (`setWarpTempoMode`, `setSourceTempo`, `setWarpStretchMode`, `setClipEdits`) with fixed values (length 32
  ticks, 120 BPM, transpose 2). There is no wall clock, sleep or thread.
- **At the cause and complete; callers and siblings.** No product code changed. Every path that could pair the
  stretcher with a loop was checked:
  - the audible render `play()` (`src/core/SamplePlayHandle.cpp:616-634`)
  - the muted advance `advanceSourceSilently` (`:807-846`)
  - SampleTrack's window choice (`src/tracks/SampleTrack.cpp:289-290`)
  - both command entries (`src/core/ControlCommandsClipEnvelope.cpp:207-208`,
    `src/core/ControlCommandsNoteTools.cpp:330-331`)

  All of them are gated by `audioloop::refusal` / `audioloop::engaged`.
- **No collateral damage.** The branch's diff adds only this report and a test function. Project loading,
  saved data and render behaviour are untouched. The full `AudioClipLoopTest` suite passes (test-03: 13
  functions, EXIT 0).
- **Code or contract that shows FALSE-POSITIVE:**
  - `src/core/SamplePlayHandle.cpp:129-134,164,183`: the stretcher needs `!rendersLinearly || transpose != 0`.
  - `src/core/ClipAudioLoop.cpp:69-77,79-84`: the loop is engaged only when `rendersLinearly && transpose == 0`.
  - `src/core/ClipAudioLoop.cpp:197-210` and `include/SamplePlayHandle.h:318`: `m_loopMode` stays `Off`
    unless the loop is engaged.
  - The refusal text at `src/core/ClipAudioLoop.cpp:71,73` states the intended contract.
  - The contract is now pinned by `tests/src/core/AudioClipLoopTest.cpp:219-246`.

## Other findings

- `SamplePlayHandle(SampleClip*, const SampleWindow&)` reads the clip state that decides `m_preservePitch`
  (`src/core/SamplePlayHandle.cpp:164,183`) and the state that decides `audioloop::engaged`
  (`src/core/ClipAudioLoop.cpp:199`) in separate, non-atomic reads. In principle, a control-thread edit landing
  between them (for example, removing the last warp marker of a looped clip) could pair a stale
  `m_preservePitch` with a freshly engaged loop for one handle. This is the same snapshot-tearing exposure the
  whole constructor has (window, warp and edits are read the same way). It does not show the finding's
  deterministic trigger and was not reproduced here. Local TSan does not run on this box, so this is
  reasoning only.

Delegation (round 1): Haiku: searched tests/ for the loop-refusal coverage (its search tools were unavailable, so it found `AudioClipLoopTest` by reading), built `AudioClipLoopTest` and ran it (build-1.log, test-1.log, both EXIT 0; I checked the log tails myself). I did the diagnosis, the code reading and the report myself. There was no fix to design, so no Sonnet or DeepSeek task was needed.

Delegation (round 2): Sonnet wrote the contract test and ran the GREEN / mutation-RED / GREEN sequence (logs 01-03); I checked the diff and the log tails. DeepSeek ran the eight gate scripts directly (logs in delegate/01-deepseek/logs); I checked the tails of the fork-sources, complexity and file-length logs. I wrote the manifest, the report and the commit. Haiku was not needed this round.
