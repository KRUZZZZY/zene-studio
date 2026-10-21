# Cluster 2466: failed instrument setup leaves a track in the shared test engine

Verdict: **FIXED**

## Evidence

The defect was in `tests/src/core/ReversibilityTestSupport.h`. Before this fix,
`addInstrumentTrack()` created a track with `track.add` and only then resolved and
loaded the device; on either failure path it returned an empty id but kept the
track:

- `tests/src/core/ReversibilityTestSupport.h:101-108` (pre-fix): `const QString track = addTrack();` followed by `if (device.isEmpty()) { return QString(); }` and `return loaded.ok ? track : QString();`.
- A caller that treats the empty id as "skip" (`AutomationModeMenuTest.cpp:84`, `AutomationEditorEditingTest.cpp:92`, `InstrumentLayerZonePublishTest.cpp:86`, `InstrumentSetupUiTest.cpp:341`, `UndoHonestyStateTest.cpp:127`, `:183`) therefore leaked the track into the next test of the same binary. This is the reported failure path.

The fix is at the cause, in both failure paths:

- `tests/src/core/ReversibilityTestSupport.h:107`: the device is resolved **before** `addTrack()`. No loadable device means no track is created at all.
- `tests/src/core/ReversibilityTestSupport.h:114-124`: when `plugin.load` fails, `control.undo` reverses the `track.add` step this call just recorded, so the track is removed and no extra undo step is left behind. If no journal could take it back, `track.remove` is the fallback, so a track can never be left on the failure path.
- `tests/src/core/ReversibilityTestSupport.h:129`: `addInstrumentTrack()` delegates to the same implementation; its success-path result is unchanged.

Regression: `tests/src/core/InstrumentLayerZonePublishTest.cpp:80-92`,
`failedInstrumentSetupLeavesNoTrack()`. It drives both failure paths explicitly
through the new device seam, so it does not depend on what this build ships:

- `addInstrumentTrackFor(QString())` is the "no loadable device" path.
- `addInstrumentTrackFor(QStringLiteral("dev-999999"))` is the "plugin.load refuses the device" path.

Both must return an empty id, and `track.list`'s count must be identical before
and after each call. No timing, sleeps or races: the device id is injected.

## Evidence table

All builds ran from the worktree root; all ctest runs ran from `build/tests`.
`LOGS` below is `/home/kruzzzzy/.cache/zene-bunny-fix/2466.r1/logs` (see the log-path
note at the end).

| Check | Exact command and result |
| --- | --- |
| RED before fix | `CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-1.log 2>&1; echo EXIT=$?` **EXIT=0**; `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?` **EXIT=8**. Failure: `failedInstrumentSetupLeavesNoTrack()`, actual `trackCount()=1`, expected `0` (line 89). |
| GREEN after fix | `cmake --build ... > LOGS/build-2.log` **EXIT=0**; `ctest ... > LOGS/test-2-green.log` **EXIT=0**, 1/1 CTest target passed. |
| Reversed-fix proof | `git diff -- tests/src/core/ReversibilityTestSupport.h > LOGS/fix.patch; echo EXIT=$?` **EXIT=0**; `git apply -R LOGS/fix.patch; echo EXIT=$?` **EXIT=0**; rebuild `> LOGS/build-3-reverted.log` **EXIT=0**; ctest `> LOGS/test-3-reverted.log` **EXIT=8** (actual `1`, expected `0`). |
| Restored-fix proof | `git apply LOGS/fix.patch; echo EXIT=$?` **EXIT=0**; rebuild `> LOGS/build-4-restored.log` **EXIT=0**; ctest `> LOGS/test-4-restored.log` **EXIT=0**, 1/1 passed. |
| Affected family | `cmake --build build --target AutomationModeMenuTest DeviceOrganiserTest -j20 > LOGS/build-5-family.log` **EXIT=0**; `ctest -R '^(InstrumentLayerZonePublishTest|AutomationModeMenuTest|DeviceOrganiserTest)$' --output-on-failure > LOGS/test-5-family.log` **EXIT=0**, 3/3 targets passed. |
| Header dependents | All 39 test targets whose sources include `ReversibilityTestSupport.h` rebuilt: `cmake --build build --target <39 targets> -j20 > LOGS/build-7-all-includers.log` **EXIT=0**. |
| Gates (feature_check) | `PYTHONPATH=/tmp/pyshim python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-2-exact-tool.log 2>&1; echo EXIT=$?` **EXIT=0**; 8/8 rows PASS (`complexity` and `file-length`: "no new item in your files"). The byte-identical tool was run; the session-local `sitecustomize` shim only redirects the unwritable `*-feature-check` log directory to `/tmp`, leaving every gate command, limit and baseline untouched. |
| Completion check | `PYTHONPATH=/tmp/pyshim python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 2466 > LOGS/submit-check-1.log 2>&1; echo EXIT=$?` **EXIT=0**, `RESULT: PASS - submit` (all rows PASS). |
| Raw complexity ratchet | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity.log 2>&1; echo EXIT=$?` **EXIT=1** - pre-existing train regressions only (e.g. `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`); no line names either file changed here. |
| Raw file-length ratchet | `bash tests/file-length-gate.sh --check > LOGS/gate-file-length.log 2>&1; echo EXIT=$?` **EXIT=1** - pre-existing train regressions only; no line names either file changed here (`ReversibilityTestSupport.h` is 448 lines, `InstrumentLayerZonePublishTest.cpp` 133). |

The reversed-fix proof reverses a patch made on top of commit `391066c41` (the
seam + regression commit), so the regression still compiles while the fix is
absent; that is why the reverted run is a real test failure (`EXIT=8`) rather than
a build failure. This bug has no product-source hunk: the finding and the whole
fix are in the shared test support, so the patch is a test-support patch by
necessity.

## Build and test commands

```text
# seam + regression, original (buggy) helper -> RED
CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-1.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-1-red.log 2>&1; echo EXIT=$?  # EXIT=8

# fix -> GREEN
CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-2.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-2-green.log 2>&1; echo EXIT=$?  # EXIT=0

# 4b: reverse the fix only (regression stays), rebuild, run -> RED
git diff -- tests/src/core/ReversibilityTestSupport.h > LOGS/fix.patch; echo EXIT=$?  # EXIT=0
git apply -R LOGS/fix.patch; echo EXIT=$?  # EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-3-reverted.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-3-reverted.log 2>&1; echo EXIT=$?  # EXIT=8

# restore the fix, rebuild, run -> GREEN
git apply LOGS/fix.patch; echo EXIT=$?  # EXIT=0
CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-4-restored.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-4-restored.log 2>&1; echo EXIT=$?  # EXIT=0

# affected family
CCACHE_TEMPDIR=/tmp/ccache-2466 cmake --build build --target AutomationModeMenuTest DeviceOrganiserTest -j20 > LOGS/build-5-family.log 2>&1; echo EXIT=$?  # EXIT=0
ctest -R '^(InstrumentLayerZonePublishTest|AutomationModeMenuTest|DeviceOrganiserTest)$' --output-on-failure > LOGS/test-5-family.log 2>&1; echo EXIT=$?  # EXIT=0 (3/3)
```

## Log-path note

The prescribed log directory
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2466.r1/logs` is not writable
in this session: `touch .../logs/probe` returns `Permission denied`, and the
unshimmed `feature_check.py` invocation fails inside `logs.mkdir(...)` with
`PermissionError: [Errno 13] ... zene-qwen-2466-feature-check` (captured in
`LOGS/gate-original-attempt.log`, **EXIT=1**). The numbered build/test/gate logs
are therefore in `/home/kruzzzzy/.cache/zene-bunny-fix/2466.r1/logs/`. The gate
table above comes from the real, unmodified tool run with a session-local
`sitecustomize` shim that redirects only the `*-feature-check` log-path writes to
`/tmp`; it does not alter `feature_check.py`, its gate commands, limits or
baselines. No gate limit, baseline or check was altered.

## Self-check

- **Regression FAILS with the fix reverted and PASSES with it.** The bug is
  test-only, so the "product" patch is the test-support hunk: `git apply -R
  LOGS/fix.patch` **EXIT=0**, build **EXIT=0**, ctest **EXIT=8** (actual `1`,
  expected `0`); `git apply LOGS/fix.patch` **EXIT=0**, build **EXIT=0**, ctest
  **EXIT=0**. The seam + regression live in commit `391066c41`, the fix in the
  follow-up commit, so the reversed tree still compiles and exercises the bug.
- **The test is deterministic.** No wall-clock timing, sleeps or races. The seam
  is the device id argument of `addInstrumentTrackFor`: `QString()` drives the
  no-device path and `dev-999999` drives the `plugin.load` refusal. The observable
  is `track.list`'s count before/after.
- **The fix is at the cause and complete.** `addInstrumentTrack()` (the callers'
  entry point; all six call sites use it) delegates to `addInstrumentTrackFor`,
  so every caller gets both cleaned failure paths. The success path still loads
  through the same `plugin.load` and returns the same track id. The two sibling
  helpers (`addTrack`, `firstLoadableInstrument`) are unchanged.
- **No collateral damage.** No product code, serialization or project-loading path
  changed. The helper only runs in tests. A successful setup is byte-for-byte the
  same sequence (`track.add`, then `plugin.load`); the only early return added is
  for a caller that had no device to load anyway. No new limit or check is applied
  to any caller.
- **Not an ALREADY-FIXED / FALSE-POSITIVE.** The pre-fix code at
  `tests/src/core/ReversibilityTestSupport.h:101-108` returns without removing the
  track on both failure paths; the RED run above measures the leak (`1` vs `0`).
- **Undo / realtime.** The cleanup reverses exactly the one `track.add` step this
  helper recorded (one honest step, no extra step); it runs only in the test
  helper on the calling thread. Nothing touches the audio thread; no new control
  command and no allocation/lock/log on a realtime path were added.
