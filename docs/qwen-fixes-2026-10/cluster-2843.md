# Cluster 2843 — "All" fails when there are no non-master channels

Verdict: **FIXED**

## Finding

`mixer.set_many` with `all:true` (Unmute All / Clear Solo, MX-027) is a valid request for
every channel but the master. When the mixer holds only its master channel, the selection
is empty and the call mishandled it.

## Evidence

`src/core/ControlCommandsMixerStrip.cpp`, `namedChannels()` (lines 93–108) and `setMany()`
(lines 110–137, before the fix): with `all:true` the loop
`for (int i = 1; i < Engine::mixer()->numChannels(); ++i)` names nothing when
`numChannels() == 1`, and `setMany` still ran the full write path — it recorded an undo
step over an empty before/after pair (line 128, `addUndoStep(...)`) and marked the song
modified. The observable defect on this branch: a spurious undo step for a call that
changed nothing (undo depth grew by one), so `control.undo` claimed a step that puts
nothing back. (Luna's note phrased the empty selection as "treated as invalid input"; on
this branch the call returned success but recorded the empty undo step — the fix line
matches exactly.)

## Regression test

`tests/src/core/MixerStripTest.cpp`, new slot `unmuteAllWithOnlyTheMaster()`: `Mixer::clear()`
leaves only the master; `mixer.set_many {all:true, muted:false}` must be ok with `count:0`
and must not change the undo depth (`control.undo_depth`). Before the fix the depth
comparison failed (32 vs 31, `test-1.log`); after the fix the whole family passes.

## Fix

`setMany()` gains an early no-op return after the `'muted'/'soloed'` check: an empty
selection (only reachable from `all:true`, since a bare empty `channels` still fails in
`namedChannels`) returns success with `count:0` and records no undo step — matching the
no-op pattern of `setStrip`'s "only 'channel'" call and `soloMode`'s unchanged mode.

## Commands and exit codes

```
cmake --build build --target MixerStripTest > LOGS/build-1.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R MixerStripTest --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?   # EXIT=8 (RED: depth 32 vs 31)
cmake --build build --target zene MixerStripTest > LOGS/build-2.log 2>&1; echo EXIT=$?   # EXIT=0
cd build/tests && ctest -R MixerStripTest --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?   # EXIT=0 (GREEN)
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?   # EXIT=0
bash tests/file-length-gate.sh --check >> LOGS/gate-1.log 2>&1; echo EXIT=$?   # EXIT=0
```

LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2843.r1/logs`.

## Registration

Both changed files — `src/core/ControlCommandsMixerStrip.cpp` and
`tests/src/core/MixerStripTest.cpp` — are already listed in `tests/fork-sources.txt`
(product files); no new files, so `tests/upstream-modifications.txt` is untouched.
