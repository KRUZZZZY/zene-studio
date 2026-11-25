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

## Self-check

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p14-275 > .../logs/submit-3.log 2>&1; echo EXIT=$?`
gave EXIT=1. Commits, clean tree, report and tests items: PASS. The only item left is `FAIL gates (feature_check.py)`.
That FAIL is the guard refusal quoted under "Commands and results" (the gate script's baseline `git checkout --detach`
exits 97 in this lane). The FAIL cannot be cleared from inside the lane without getting around the guard, and the
run's rules forbid that, so the gate step is **BLOCKED**. The table was never produced. Gate status for this commit
(one doc file, no product, test or list change) is unverified.

- **Regression test fails without the fix and passes with it:** not applicable. The verdict is FALSE-POSITIVE and no
  product code changed, so there is nothing to revert. The existing `AudioClipLoopTest` passes unchanged
  (build-1.log EXIT 0, test-1.log EXIT 0, "100% tests passed out of 1").
- **Deterministic test:** no new test was added. `AudioClipLoopTest::refusalsAreTyped` drives `clip.set_loop` through
  the control registry with fixed tick values (`start` 48 / `length` 32) on an in-memory song, with no timing, sleeps
  or threads.
- **Fix at the cause and complete; callers and siblings:** no fix was made. I checked every path that could pair the
  stretcher with a loop: the audible render `play()` (`src/core/SamplePlayHandle.cpp:616-634`), the muted advance
  `advanceSourceSilently` (`:807-846`), the window choice in `SampleTrack` (`src/tracks/SampleTrack.cpp:289-290`), and
  both command entries (`ControlCommandsClipEnvelope.cpp:207-208`, `ControlCommandsNoteTools.cpp:330-331`). All of
  them are gated by `audioloop::refusal` / `audioloop::engaged`.
- **No collateral damage:** the commit contains only `docs/qwen-fixes-2026-10/cluster-new-p14-275.md`
  (`git show --stat HEAD`). Project loading, saved data and render behaviour are untouched.
- **Code or contract that shows FALSE-POSITIVE:** `src/core/SamplePlayHandle.cpp:129-134,164,183` (the stretcher needs
  `!rendersLinearly || transpose != 0`); `src/core/ClipAudioLoop.cpp:69-77,79-84` (the loop is engaged only when
  `rendersLinearly && transpose == 0`); `src/core/ClipAudioLoop.cpp:197-210` and `include/SamplePlayHandle.h:318`
  (`m_loopMode` is `Off` unless engaged). The intended contract is the refusal text at `src/core/ClipAudioLoop.cpp:71,73`:
  "a warped or tempo-leading clip's audio cannot loop in this release" / "a transposed clip's audio cannot loop in
  this release".

## Other findings

- `SamplePlayHandle(SampleClip*, const SampleWindow&)` reads the clip state that decides `m_preservePitch`
  (`src/core/SamplePlayHandle.cpp:164,183`) and the state that decides `audioloop::engaged`
  (`src/core/ClipAudioLoop.cpp:199`) in separate, non-atomic reads. In principle, a control-thread edit landing
  between them (for example, removing the last warp marker of a looped clip) could pair a stale
  `m_preservePitch` with a freshly engaged loop for one handle. This is the same snapshot-tearing exposure the
  whole constructor has (window, warp and edits are read the same way). It does not show the finding's
  deterministic trigger and was not reproduced here. Local TSan does not run on this box, so this is
  reasoning only.

Delegation: Haiku: searched tests/ for the loop-refusal coverage (its search tools were unavailable, so it found `AudioClipLoopTest` by reading), built `AudioClipLoopTest` and ran it (build-1.log, test-1.log, both EXIT 0; I checked the log tails myself). I did the diagnosis, the code reading and the report myself. There was no fix to design, so no Sonnet or DeepSeek task was needed.
