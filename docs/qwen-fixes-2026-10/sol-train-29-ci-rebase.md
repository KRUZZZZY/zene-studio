# Train 29 CI repair replay reconciliation (failure 34)

The selected train at c7e71afe47 retains the Windows ARM64 signal repair but
omits bugs/qwen-new-p21-173. Replaying a21e15e372e2 fails only on the two
provenance additions in tests/upstream-modifications.txt: their old context
included three FreeBoy rows belonging to that omitted selection.

This replacement retains every product, regression, registration and historical
report byte from a21e15e372e2. It adds those two provenance rows against the
current ledger without restoring the omitted branch's rows. It supersedes
sol-repair-1794605312345629946 only; the Windows repair and the separate
NoteCanvas complexity repair remain required. No tests, assertions, gates,
baselines or resource limits change from the preserved repair.

Changed paths and reasons are recorded in the original integration report and
source ledgers; this additional registered report records the replay repair.
The original report's negative controls and verification refer to failure 32.

## Current checkout verification

- `git diff --binary a21e15e372e2^ a21e15e372e2 | git apply --check -`:
  exit 1, only tests/upstream-modifications.txt fails.
- `git diff --binary a21e15e372e2^ a21e15e372e2 -- . ':(exclude)tests/upstream-modifications.txt' | git apply --check`:
  exit 0; applied that delta and placed its two ledger entries beside the existing SongEditor declaration,
  avoiding dependence on the changing end of the selected train ledger.
- `git diff --binary 3edb6accd286^ 3edb6accd286 | git apply --check`:
  exit 0 after reconciliation; the pending complexity repair can follow this one.
- Configure uses exactly the owner option command reproduced in
  sol-train-29-ci-integration.md, in this checkout's sole build directory.
- Configure: exit 0 (5.5 seconds configuring, 47.1 seconds generating).
- `cmake --build build --target zene FollowActionEditorTest ArrangerTimelineTest -j12`:
  exit 0, 1244 steps; 90 GB free after the build (12 GB stop threshold retained).
- From `build/tests`,
  `ctest --output-on-failure -R '^(Qt5OverloadedSignalCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures|FollowActionEditorTest|ArrangerTimelineTest)$'`:
  exit 0, 6/6 passed in 34.53 seconds. The Qt 5 compiler probe retains its
  rejected bare-signal control; the no-gestures check compiles the affected real
  translation units and verifies both pinch API configurations.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/fork-sources-gate.sh`: exit 0.
- `bash tests/evidence-gate.sh`: exit 0.
- `bash tests/unregistered-tests-gate.sh`: exit 0, 793 test sources scanned.
- `git diff --check`: exit 0.
- Read-only Luna review (`repair34-review`): no blocking defect; confirmed retained
  source/test coverage and compatibility with the pending NoteCanvas repair.
- Direct byte comparison: all 12 product, regression and historical report paths
  match a21e15e372e2. The CMake added/deleted lines match its patch exactly;
  differences outside that patch are the deliberately omitted FreeBoy selection.
  Removing the two newly inserted provenance entries yields the current HEAD's
  ledger exactly.

The local registration/evidence gates inspect tracked files; the runner must
repeat them after committing the explicitly registered new paths. The full train,
native hosted builds, and the separately pending NoteCanvas complexity repair
remain required. This replay repair does not assert that all release gates pass.
