# Train 29 registration replay reconciliation (failure 36)

The train at d16023e93b already contains the Windows ARM64 and NoteCanvas
complexity repairs. Replaying 22878e9ade16 fails only at the appended fork-source
registration block: its patch context names the omitted Q050-275 feature report.
The new selection adds Q050-294.3 and Q050-296.1 instead.

This replacement retains the complete intended source, regression, CMake and
provenance delta from 22878e9ade16. Its fork-source declarations are inserted
before the first existing manifest entry, away from the changing selection tail;
all current declarations and both newly selected features remain intact. This
report is registered in that same block. No source behavior, assertion, baseline,
gate or resource limit changes beyond the preserved repair.

Supersedes sol-repair-1794608813596401012 only. The separate Windows ARM64
and NoteCanvas repair records remain prerequisites. Historical reports retain
their original verification claims; results below refer to this checkout.

## Verification

- `git diff --binary 22878e9ade16^ 22878e9ade16 | git apply --check`:
  exit 1, only tests/fork-sources.txt fails.
- `git diff --binary 22878e9ade16^ 22878e9ade16 -- . ':(exclude)tests/fork-sources.txt' | git apply --check`:
  exit 0; applied it and reconciled the registration block separately.
- Configure uses the full owner command reproduced in sol-train-29-ci-integration.md,
  in the sole private build directory.
- Configure: exit 0, 5.1 seconds configuring and 44.3 seconds generating.
- `python3 -m pip install --target build/ci-python lizard==1.25.1`: exit 0;
  pins the local analyzer to the hosted CI version without changing repository dependencies.
- From `build/tests`,
  `PYTHONPATH="$PWD/../ci-python" ctest --output-on-failure -R '^(complexity-regression-check|NoteCanvasComplexityGate)$'`:
  exit 0, 2/2 passed in 26.08 seconds. The registered gate invokes the unchanged
  complexity executable with --check; no baseline is written.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/fork-sources-gate.sh`: exit 0.
- `bash tests/evidence-gate.sh`: exit 0.
- `bash tests/unregistered-tests-gate.sh`: exit 0, 794 test sources scanned.
- Direct comparison confirms all 13 product, regression and historical report
  files match 22878e9ade16 byte for byte. CMake and provenance added/deleted
  lines match its delta exactly; removing the inserted fork registration block
  yields the current HEAD manifest byte for byte.
- Read-only Luna review confirmed the retained repair delta. It initially flagged
  unrelated Q050-294.3 Markdown reports absent from the source manifest; follow-up
  review confirmed those unchanged reports are outside Gate 9's source scope,
  while all three Q050-294.3 source/test additions are registered. No blocking
  concern remains from that review; its independent Gate 9 run exits 0.
- `cmake --build build --target zene FollowActionEditorTest ArrangerTimelineTest -j12`:
  exit 0, 1245 steps; 90 GB free after build, above the unchanged 12 GB stop threshold.
- From `build/tests`,
  `ctest --output-on-failure -R '^(Qt5OverloadedSignalCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures|FollowActionEditorTest|ArrangerTimelineTest)$'`:
  exit 0, 6/6 passed in 32.79 seconds. The preserved signal test includes its real
  Qt 5 rejected/accepted compiler controls; the gesture test compiles affected
  translation units with gestures disabled and checks both pinch API forms.
- `git diff --check`: exit 0.

All eight selected checks passed across the two ctest invocations; neither
matched zero tests. Original behavioral negative-control evidence remains in the
preserved integration report. This reconciliation introduces no new behavior.
The local source/evidence gates inspect tracked files, so the runner must repeat
them after committing the explicit new paths. Full train, native hosted CI and
all release gates remain required; no overall release pass is claimed here.
