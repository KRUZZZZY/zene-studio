# Cluster new-p12-200

**Verdict: FIXED**

## Summary

One retrospective take could label frames recorded at several sample rates with a single
rate: `RetroAudioCapture`'s ring stored frames without their rate while the writer
assigned one rate to the whole window. Capture now stays rate-homogeneous: when a period
arrives at a rate different from the frames already retained, the stale frames are
dropped through the ring's realtime-safe producer-side discard
(`RetroAudioRing::discardRetained()` on a real transition, `dropPeriod()` when a
snapshot is in flight) and the new rate is published together with a fresh generation
in one atomic tag, so `takeWindow()` validates frames and label as one snapshot. The take
command labels the file with `window.sampleRate` instead of the live engine rate.

## Code evidence

- `src/core/RetroAudioCapture.cpp:121-165` `push()` - rate check, deferred-period drop.
- `src/core/RetroAudioCapture.cpp:167-187` `transitionRateIfNeeded()` - held==0 init, equal no-op,
  pending deferral, real transition discards + bumps generation.
- `src/core/RetroAudioCapture.cpp:190-240` `takeWindow()` - tag-validated copy, refused on
  unsettled snapshot.
- `src/core/ControlCommandsRecordingRetro.cpp:174-179` `retroToTake()` - uses
  `window.sampleRate > 0 ? window.sampleRate : engineSampleRate()`.
- `src/core/AudioEngine.cpp:978-979` - caller passes `static_cast<int>(inputSampleRate())`.

## Regression test

`tests/src/core/RetroAudioCaptureTest.cpp` function
`aDeferredTransitionPeriodIsNotAdmittedAfterTheSnapshotReleases` (QTest). Without the fix
the deferred period is admitted to the old-rate ring and the window holds 400 frames
(300 old-rate + 100 new-rate) where 300 are expected.

## Runnable RED sequence

Define once:

```
LOGDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-200.r5/logs
WT=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-200
```

Working directory is `$WT` unless a step says otherwise.

1. GREEN baseline (fixed tree):

```
cd "$WT" && cmake --build build --target zene RetroAudioCaptureTest -j4 \
  > "$LOGDIR/build-1-green.log" 2>&1   # EXIT=0
cd "$WT/build/tests" && ctest -R '^RetroAudioCaptureTest$' --output-on-failure \
  > "$LOGDIR/test-2-green.log" 2>&1    # EXIT=0
```

2. Reintroduce the bug (fixed -> buggy). Exact edit in
`src/core/RetroAudioCapture.cpp` (the deferred-period branch):

```
-		m_ring.dropPeriod(static_cast<std::size_t>(count));
+		m_ring.push(frames, static_cast<std::size_t>(count));
```

Apply it from a clean fixed tree with:

```
cd "$WT" && python3 - <<'PY'
from pathlib import Path
p = Path("src/core/RetroAudioCapture.cpp")
s = p.read_text()
old = "\t\tm_ring.dropPeriod(static_cast<std::size_t>(count));"
new = "\t\tm_ring.push(frames, static_cast<std::size_t>(count));"
assert s.count(old) == 1, s.count(old)
p.write_text(s.replace(old, new))
PY
```

3. Build and run RED:

```
cd "$WT" && cmake --build build --target zene RetroAudioCaptureTest -j4 \
  > "$LOGDIR/build-3-red.log" 2>&1     # EXIT=0
cd "$WT/build/tests" && ctest -R '^RetroAudioCaptureTest$' --output-on-failure \
  > "$LOGDIR/test-4-red.log" 2>&1      # EXIT=8
```

Observed failure (`test-4-red.log`): `window.frames.size()` Actual 400, Expected 300
(`aDeferredTransitionPeriodIsNotAdmittedAfterTheSnapshotReleases`).

4. Restore the fix:

```
cd "$WT" && python3 - <<'PY'
from pathlib import Path
p = Path("src/core/RetroAudioCapture.cpp")
s = p.read_text()
old = "\t\tm_ring.push(frames, static_cast<std::size_t>(count));"
new = "\t\tm_ring.dropPeriod(static_cast<std::size_t>(count));"
# Only revert the deferred-period branch: it is the one followed by the early return.
marker = new + "\n\t\treturn;"
assert s.count(old + "\n\t\treturn;") == 1
p.write_text(s.replace(old + "\n\t\treturn;", new + "\n\t\treturn;", 1))
PY
```

(Equivalent: `git checkout -- src/core/RetroAudioCapture.cpp` on a clean tree.)

5. Rebuild and run GREEN:

```
cd "$WT" && cmake --build build --target zene RetroAudioCaptureTest -j4 \
  > "$LOGDIR/build-5-green.log" 2>&1   # EXIT=0
cd "$WT/build/tests" && ctest -R '^RetroAudioCaptureTest$' --output-on-failure \
  > "$LOGDIR/test-6-green.log" 2>&1    # EXIT=0
```

6. Unaffected family and gates:

```
cd "$WT/build/tests" && ctest -R '^ControlRecordInputs$' --output-on-failure \
  > "$LOGDIR/test-7-ci.log" 2>&1       # EXIT=0
cd "$WT" && bash tests/complexity-gate.sh --check \
  > "$LOGDIR/gate-8-ccn.log" 2>&1      # EXIT=1 (pre-existing, unrelated files)
cd "$WT" && bash tests/file-length-gate.sh --check \
  > "$LOGDIR/gate-9-len.log" 2>&1      # EXIT=1 (pre-existing, unrelated files)
cd "$WT" && python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py "$WT" \
  > "$LOGDIR/feature-check.log" 2>&1   # EXIT=0
```

## Commands and exit codes

| Step | Command (cwd) | Exit |
|---|---|---|
| build fixed | `cmake --build build --target zene RetroAudioCaptureTest` ($WT) | 0 |
| test fixed | `ctest -R '^RetroAudioCaptureTest$'` ($WT/build/tests) | 0 |
| build buggy | `cmake --build build --target zene RetroAudioCaptureTest` ($WT) | 0 |
| test buggy (RED) | `ctest -R '^RetroAudioCaptureTest$'` ($WT/build/tests) | 8 (400 vs 300) |
| restore | edit revert / `git checkout -- src/core/RetroAudioCapture.cpp` ($WT) | 0 |
| build restored | `cmake --build build --target zene RetroAudioCaptureTest` ($WT) | 0 |
| test restored (GREEN) | `ctest -R '^RetroAudioCaptureTest$'` ($WT/build/tests) | 0 |
| ControlRecordInputs | `ctest -R '^ControlRecordInputs$'` ($WT/build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check` ($WT) | 1 (pre-existing) |
| file-length gate | `bash tests/file-length-gate.sh --check` ($WT) | 1 (pre-existing) |
| feature_check | `python3 .../feature_check.py $WT` | 0 |

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes):**
  RED `ctest -R '^RetroAudioCaptureTest$'` exit 8, observed `window.frames.size()` Actual 400 vs Expected 300
  (`test-4-red.log`); GREEN exit 0 (`test-6-green.log`). The test is unchanged between the two runs.
- **Deterministic (no wall-clock timing/sleeps/races; name the seam):** the regression uses the
  `RetroAudioCapture::setPushPauseTestHook` test seam plus `ring().beginSnapshot()` /
  `ring().endSnapshot()` to create a deterministic in-flight snapshot; the producer is joined
  before any assertion. The 5-second steady_clock loop is only a safety bound for waiting on the
  hook and is not a pass criterion.
- **Fix at the cause and complete (callers/siblings checked):** the single caller
  `AudioEngine.cpp:978` passes `inputSampleRate()`; `transitionRateIfNeeded()` covers held==0,
  equal rate, pending deferral, and real transition; `dropPeriod()` acknowledges a still-pending
  snapshot. Command-side sibling `retroToTake` now labels with `window.sampleRate`. The other
  `record.*` commands (`arm_track`, `disarm_all`, etc.) live in unchanged files and do not touch
  the ring or rate handling, so they are unaffected.
- **No collateral damage:** no project/save format or persisted state changes; arm state is runtime
  mode state; no new limit applied to other callers; old projects load unchanged.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE:** the bug was real and is fixed here.
- **Sibling commands in changed command file (`ControlCommandsRecordingRetro.cpp`):**
  `record.retro_capture_arm` (arm flag only, no rate logic),
  `record.retro_capture_status` (counters; `sample_rate` field is engine rate for capacity seconds),
  `record.retro_capture_to_take` (now labels the take with `window.sampleRate`). All three consistent
  with the fix. The remaining `record.*` commands listed in the ASK are in files this cluster did
  not change and are not affected.
- **Gates:** `feature_check.py` exit 0; complexity and file-length gates exit 1 on pre-existing
  unrelated files only (none in this cluster's changed files).
