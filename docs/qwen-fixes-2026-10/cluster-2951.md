FIXED

# Cluster 2951 - project.subproject_refresh undo overwrote per-clip sample paths

## Verdict evidence (REAL)
- `src/core/ControlCommandsSubprojects.cpp` (before the fix, `refreshSubproject`): `apply(state)` called `pointClips(owner, state.audio)`, which `setSampleFile`s EVERY SampleClip on the track. The undo lambda was `apply(before)`, so every clip was put back on the single `before.audio`, never on its own file.
- Reproduced by the new regression test: a second sample clip playing `custom-2951.wav` on a subproject track; refresh, then `control.undo`; that clip came back on `inner-2951-<ms>.wav` (the subproject's render).

## Fix
`src/core/ControlCommandsSubprojects.cpp`: `clipSamples(track)` records each clip's absolute sample file (clip order) before the refresh; the undo step, after `apply(before)`, calls `restoreClips(track, files)`, which puts each clip back on its own file (only clips that differ are touched; skipped if the clip count differs, which cannot happen under stack-ordered undo). Redo and the refresh itself are unchanged (a refresh still points the track's clips at the new render, as the command documents). No new command, no audio-thread code, still one undo step.

Known limit, unchanged by this fix: `setSampleFile` resets a clip's warp markers/window, so a clip that was refreshed and then undone loses those edits (the refresh itself already did that).

## Regression test
`tests/src/core/ControlBounceRenderTest.cpp`: `undoingARefreshRestoresEachClipsOwnSample` (no timing or sleeps).

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/2951.r1/logs/)
- RED (test only, no fix): `cmake --build build --target zene ControlBounceRenderTest -j4 > build-red.log` EXIT=0; `ctest -R ControlBounceRenderTest --output-on-failure > test-red.log` (from build/tests) EXIT=8. Failed at ControlBounceRenderTest.cpp:584: Actual `.../inner-2951-1795603635315.wav`, Expected `.../custom-2951.wav`.
- GREEN: build EXIT=0 (build-green.log); ctest EXIT=0 (test-green.log), 15/15 slots pass.
- 4b: `git diff -- src/core/ControlCommandsSubprojects.cpp > fix.patch` EXIT=0; `git apply -R fix.patch` EXIT=0; rebuild EXIT=0 (build-revert.log); ctest EXIT=8 (test-revert.log), same slot fails at :584. `git apply fix.patch` EXIT=0; rebuild EXIT=0 (build-reapply.log); ctest EXIT=0 (test-reapply.log).

## Gates
`python3 .../feature_check.py <worktree> > gates-1.log` EXIT=1: it did not run. The script's own `git -C .../gate-baselines/tree checkout -q --detach 9913d36d...` was refused by bunny-guard ("not allowed in a Bunny lane"), so no PASS/FAIL table exists. I did not work around it. Not verified: complexity/file-length gates. The new functions are small (clipSamples CCN 3, restoreClips CCN 5, both well under 10); the file is ~325 lines.

## Self-check
(see below, filled after submit_check)

Delegation: Haiku ran the RED build+test, and the GREEN / revert / reapply build+test runs (with the patch apply/revert) and reported exit codes; I verified them against the logs and the diff.

## Other findings
- None.
