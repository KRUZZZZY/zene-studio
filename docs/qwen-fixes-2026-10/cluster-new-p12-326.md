FALSE-POSITIVE

new-p12-326: FALSE-POSITIVE
new-p16-302: FIXED

Batch note: this batch has two clusters. The new-p12-326 verdict is FALSE-POSITIVE. The product diff in this batch (`src/gui/clips/AutomationClipView.cpp`, `src/core/AutomationClip.cpp`) belongs to new-p16-302, not to new-p12-326.

# new-p12-326: the unconnected-paint crash is a false positive

Finding: an empty connection list makes `AutomationClip::firstObject()` return null, so the dereference in the automation clip paint path crashes.

Verdict: the null-model claim is false. `AutomationClip::firstObject()` has two return paths, and both return a non-null pointer. The paint path's sampling loop already guards the `valuesAfter` null case. The p12-326 verdict itself made no product change; the product diff in this batch belongs to new-p16-302.

## Evidence

Line numbers are post-edit. The new-p16-302 edit adds 9 lines at `AutomationClipView.cpp:115-123`, which shifts the lines below it. Pre-edit numbers are given in parentheses.

- `src/gui/clips/AutomationClipView.cpp:280-281` (pre-edit 271-272): `m_clip->firstObject()->minValue<float>()` and `->maxValue<float>()` dereference the result with no null check. This is the claimed crash site.
- `src/core/AutomationClip.cpp:286-298`: `AutomationClip::firstObject()`. Line 293 `return model;` is taken when `m_objects` is non-empty and its front is non-null. Lines 296-297 `static FloatModel fm(0, DEFAULT_MIN_VALUE, DEFAULT_MAX_VALUE, 0.001f); return &fm;` are the fallback taken otherwise. Both paths return non-null. This range is unchanged by the batch.
- `include/AutomationClip.h:239` and `:244`: the inline wrappers call `firstObject()->minValue` / `->maxValue`, so they reach the same fallback. Unchanged by the batch.
- `src/gui/clips/AutomationClipView.cpp:336` (pre-edit 327): `if (values == nullptr) { continue; }`, the existing sampling guard for `valuesAfter`.
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:5`: "The crash the finding describes is not reachable: the no-target case never yields a null model."
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:9`: "The null-model claim is false", with the fallback at `AutomationClip.cpp:296-297`.
- `docs/qwen-fixes-2026-10/cluster-new-p9-439.md:105`: "The null-dereference claim is FALSE-POSITIVE: `AutomationClip::firstObject()` never returns null."

## Commands and exit codes (p12-326 evidence)

Each command below wrote its output to a log under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-326.r4/logs/`.

1. `rg -n 'firstObject|values == nullptr' src/core/AutomationClip.cpp src/gui/clips/AutomationClipView.cpp include/AutomationClip.h` → EXIT=0 (`evidence-1.log`)
2. `sed -n '286,298p' src/core/AutomationClip.cpp` → EXIT=0 (`evidence-2.log`)
3. `sed -n '265,275p;320,330p' src/gui/clips/AutomationClipView.cpp` → EXIT=0 (`evidence-3.log`). These are pre-edit line ranges.
4. `sed -n '1,12p;95,110p' docs/qwen-fixes-2026-10/cluster-new-p9-439.md` → EXIT=0 (`evidence-4.log`)
5. `rg -n 'null-model claim is false|crash the finding describes is not reachable' docs/qwen-fixes-2026-10/cluster-new-p9-439.md` → EXIT=0 (`evidence-5.log`; re-run this session: EXIT=0)
6. `sed -n '105p' docs/qwen-fixes-2026-10/cluster-new-p9-439.md` → EXIT=0 (`evidence-5.log`; re-run this session: EXIT=0)

The build and test runner was not run for the p12-326 verdict itself. Its build and test evidence is in the new-p16-302 section below.

## Not added: paint guard test

No paint guard test was added. A guard test for the paint path would pass with or without any p12-326 product change, so it cannot meet the reverse-RED rule (it must fail on the unfixed code and pass on the fix). For a false-positive verdict there is no fix to test.

# new-p16-302: disconnecting an automation target is one undo step

Finding: disconnecting an automation target from the clip's Connections submenu must be one undo step, and undo must restore the target. The regression test is `AutomationClipDisconnectTest::disconnectActionIsOneUndoStep`.

## Product changes (post-edit file:line)

- `src/gui/clips/AutomationClipView.cpp:106` `disconnectObject(QAction*)`.
- `src/gui/clips/AutomationClipView.cpp:115-121`: the stale-target guard. It iterates `m_clip->objectsSnapshot()` and returns before any change unless the action's model is still a live target.
- `src/gui/clips/AutomationClipView.cpp:122`: `m_clip->addJournalCheckPoint();`, taken only for a live target.
- `src/gui/clips/AutomationClipView.cpp:124`: `m_clip->removeObject(model)`, the existing removal.
- `src/core/AutomationClip.cpp:1277-1280`: the comment, then `clearObjects();` (:1279) and `m_idsToResolve.clear();` (:1280), before the existing `clear();` at :1281. The saved `<object>` list replaces the live targets and stale resolve ids instead of appending to them (undo/redo restore path).

Diff size: `src/core/AutomationClip.cpp` +4, `src/gui/clips/AutomationClipView.cpp` +9, `tests/src/gui/AutomationClipDisconnectTest.cpp` +31, `tests/upstream-modifications.txt` +2.

## Test

`tests/src/gui/AutomationClipDisconnectTest.cpp:300-327`: `disconnectActionIsOneUndoStep()`. It runs as CTest test 485, `AutomationClipDisconnectTest`.

The test drives the `disconnectObject` slot that the Connections submenu's `triggered(QAction*)` signal calls (`AutomationClipView.cpp:211-212`), with the same `target.id()` payload the menu builds (`setData(object->id())` at `AutomationClipView.cpp:207`).

The only wait is the existing view-creation readiness guard, `QTRY_VERIFY_WITH_TIMEOUT(viewOf(clip) != nullptr, 5000)`. It is not a timing proof.

## Test matrix

| Case | Covered by `disconnectActionIsOneUndoStep` | Evidence |
|---|---|---|
| Normal disconnect | Yes: objects empty after disconnect, `journal->undoDepth() == 1` | test-12-green-final-full, PASS |
| Undo restore | Yes: one object, same pointer as target, `valueAt(0) == 25` | test-12-green-final-full, PASS |
| Redo removal | Yes: objects empty after redo | test-12-green-final-full, PASS |
| Stale-action guard | **No.** The guard at `AutomationClipView.cpp:115-121` has no test in this batch. The test file contains no stale case. | not covered; see NOTES |
| Undo bound and exhaustion | Unchanged code. `UndoBoundsTest` and `UndoHistoryPersistenceTest` pass in the family run. Exhaustion is not separately tested. | test-4-family, 7/7 PASS |
| Render-path constraints | The paint path (`AutomationClipView.cpp` paint code, now ~280-336) is not changed. The new loop runs only in `disconnectObject`, over `objectsSnapshot()`. The offscreen GUI driver passes. | screenshot-1.log, EXIT=0 |
| Linux/Qt6 scope | Tested on Linux with Qt6 (`build/CMakeCache.txt`: `Qt6_DIR=/usr/lib/cmake/Qt6`), `QT_QPA_PLATFORM=offscreen`. No other platform was built or run. | build and test logs below |

## Run evidence

Logs are under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-326.r4/logs/`. Each EXIT value is the `EXIT=` line the run appended to its log, read in this session, except the rows whose log is `none` (their EXIT comes from the p4b run record) and the `build-1-red` row (its log has no `EXIT=` line). `<logs>` below means the logs folder above. The commands are the exact invocations run for p2, p3, p4b, feature check, control shell, sha256 and registry. Commands run from the worktree root `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-326`.

| Step | Command | Log | EXIT |
|---|---|---|---|
| p2 build (pre-fix) | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-1-red.log` | 0 (from the p2 run record) |
| p2 RED (pre-fix) | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-2-red.log` | 8 |
| p3 build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-2-green.log` | 0 |
| p3 GREEN | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-3-green.log` | 0 |
| p3 family build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target AutomationModesTest AutomationEditorEditingTest AutomationPreFirstValueTest AutomationTrackTest UndoBoundsTest UndoHistoryPersistenceTest -j4` | `build-3-family.log` | 0 |
| p3 family | from `build/tests`: `ctest -R '^(AutomationClipDisconnectTest\|AutomationModesTest\|AutomationEditorEditingTest\|AutomationPreFirstValueTest\|AutomationTrackTest\|UndoBoundsTest\|UndoHistoryPersistenceTest)$' -j2 --output-on-failure` | `test-4-family.log` | 0 |
| p4b save fix patch | `git diff -- src/gui/clips/AutomationClipView.cpp src/core/AutomationClip.cpp > <logs>/fix.patch` | none | 0 |
| p4b check fix patch | `git diff -- src/gui/clips/AutomationClipView.cpp src/core/AutomationClip.cpp \| cmp - <logs>/fix.patch` | none | 0 |
| p4b reverse fix | `git apply -R <logs>/fix.patch` | none | 0 |
| p4b reversed build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-8-red-reversed.log` | 0 |
| p4b reversed | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-9-red-reversed-full.log` | 8 |
| p4b reapply fix | `git apply <logs>/fix.patch` | none | 0 |
| p4b check restored | `git diff -- src/gui/clips/AutomationClipView.cpp src/core/AutomationClip.cpp \| cmp - <logs>/fix.patch` | none | 0 |
| p4b restored build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-9-green-restored.log` | 0 |
| p4b restored | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-10-green-restored-full.log` | 0 |
| p4b save core patch | `git diff -- src/core/AutomationClip.cpp > <logs>/core.patch` | none | 0 |
| p4b reverse core | `git apply -R <logs>/core.patch` | none | 0 |
| p4b core-only build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-10-red-core-only.log` | 0 |
| p4b core-only | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-11-red-core-only-full.log` | 8 |
| p4b reapply core | `git apply <logs>/core.patch` | none | 0 |
| p4b check final | `git diff -- src/gui/clips/AutomationClipView.cpp src/core/AutomationClip.cpp \| cmp - <logs>/fix.patch` | none | 0 |
| p4b final build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene AutomationClipDisconnectTest -j4` | `build-11-green-final.log` | 0 |
| p4b final | from `build/tests`: `ctest -R '^AutomationClipDisconnectTest$' --output-on-failure` | `test-12-green-final-full.log` | 0 |
| p4b final verbose | from `build/tests`: `ctest -V -R '^AutomationClipDisconnectTest$'` | `test-12b-green-final-verbose.log` | 0 |
| feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-326` | `gate-1.log` | 0 |
| control shell | `QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-326.r4/logs/shots` (from the worktree root; as recorded in the earlier report text, not re-run) | `screenshot-1.log` | 0 |
| sha256 | `sha256sum /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-326.r4/logs/shots/shell-arrange.png` → `3f9233ec8840a80207d4511e6186cf43315252ba967c5aa8d2489e75d730e618` (re-run this session: same hash) | `screenshot-sha.log` | 0 |
| registry newline | `tail -c1 tests/upstream-modifications.txt \| od -An -tx1` → `0a` (re-run this session) | `registry-newline.log` | 0 |

Builds (`build-8` to `build-11`, each ends with `[100%] Built target AutomationClipDisconnectTest`):
- `build-8-red-reversed.log` → EXIT=0
- `build-9-green-restored.log` → EXIT=0
- `build-10-red-core-only.log` → EXIT=0
- `build-11-green-final.log` → EXIT=0

Tests:
- p2 RED, `test-2-red.log`: `1/1 Test #485: AutomationClipDisconnectTest ...Failed`. `FAIL! : AutomationClipDisconnectTest::disconnectActionIsOneUndoStep() Compared values are not the same`. EXIT=8.
- p3 GREEN, `test-3-green.log`: `1/1 Test #485 ... Passed 3.58 sec`, `100% tests passed out of 1`. EXIT=0.
- p3 family, `test-4-family.log`: `100% tests passed out of 7`, including `UndoBoundsTest` (Test #247), `UndoHistoryPersistenceTest` (#248), `AutomationClipDisconnectTest` (#485), `AutomationTrackTest` (#640), `AutomationEditorEditingTest` (#484). EXIT=0.
- p4b RED reversed, `test-9-red-reversed-full.log`: `Failed`, `Compared values are not the same`. EXIT=8.
- p4b GREEN restored, `test-10-green-restored-full.log`: `100% tests passed out of 1`. EXIT=0.
- p4b RED core-only, `test-11-red-core-only-full.log`: `Failed`, `'clip->objects().empty()' returned FALSE`. EXIT=8.
- p4b GREEN final, `test-12-green-final-full.log`: `100% tests passed out of 1`. EXIT=0.
- p4b GREEN final verbose, `test-12b-green-final-verbose.log`: `PASS : AutomationClipDisconnectTest::disconnectActionIsOneUndoStep()`, `Test #485 ... Passed 2.99 sec`. EXIT=0.

Feature check (`gate-1.log`, run on the worktree, unpiped exit recorded):

```
feature-check zene-qwen-new-p12-326 (base fa4a5f2e7, 254 changed files; ...)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0
```

Screenshot (`screenshot-1.log`): `QT_QPA_PLATFORM=offscreen python3 tests/control-shell.py build/zene <logs>/shots` → `PASS: shell control-surface driver (every check held)`, EXIT=0. PNGs in `logs/shots/`: `shell-arrange.png`, `shell-edit.png`, `shell-mix.png`.

Screenshot hash (`screenshot-sha.log`): `sha256sum shell-arrange.png` = `3f9233ec8840a80207d4511e6186cf43315252ba967c5aa8d2489e75d730e618`, EXIT=0.

Registry (`tests/upstream-modifications.txt:6027-6028`): two new rows, for `src/gui/clips/AutomationClipView.cpp` (line 6027) and `src/core/AutomationClip.cpp` (line 6028). The test file `tests/src/gui/AutomationClipDisconnectTest.cpp` is already in `tests/fork-sources.txt` (line 3288), so it has no row of its own. The file ends with a newline (last byte `0a`, `registry-newline.log`).

## Self-check

1. **Reverse RED/GREEN.** Each state was built and run. The fix removed (reversed) fails: `build-8`/`test-9`. The core-only state (the `loadSettings` reset reverted, the view guard kept) fails: `build-10`/`test-11`. The restored state passes: `build-9`/`test-10`. The final state passes: `build-11`/`test-12` and `test-12b`. The initial pre-fix RED is `test-2`, which fails.
2. **Determinism.** The test uses a fixed target (`FloatModel{25.0f, 0.0f, 100.0f, 0.01f}`) and a fixed point (`putValue(TimePos(0), 25.0f)`). It calls the disconnect slot directly through `QMetaObject::invokeMethod` with `Qt::DirectConnection`, on a `QAction` with `setData(target.id())`, and runs undo and redo synchronously. There are no sleeps and no wall-clock assertions. The only wait is the bounded view-creation readiness guard, `QTRY_VERIFY_WITH_TIMEOUT(viewOf(clip) != nullptr, 5000)`. Each state was run once, except the final state, which passed in two runs (`test-12`, `test-12b`).
3. **Cause and completeness.** The disconnect cause is the missing journal checkpoint and the unguarded stale path, which the `AutomationClipView.cpp` change addresses. The `loadSettings` change in `AutomationClip.cpp` clears live targets and stale resolve ids before reading the saved list. It runs for every caller of `loadSettings`, not only undo/redo. Only undo/redo and the family tests cover it here.
   - **Correction to the packet.** `ControlTrackRoleLinks::detachTrackAutomation` is not inside the structural step at `ControlCommandsTrackRole.cpp:374-387`. It runs in `replaceRow` (`ControlCommandsTrackRole.cpp:205-230`, calls at :213-214). `setRole` reaches `replaceRow` before `recordSwitch` (:423). The one structural step at :385 (`addStructuralUndoStep`) records the detached links as `request.links`. So the detach is captured by the step, not run inside it. This batch does not change that path.
   - Completeness gap: the stale-action guard has no test (see the test matrix).
4. **Collateral damage.** The diff touches four files: `AutomationClip.cpp`, `AutomationClipView.cpp`, the test, and the registry. Test 485 passes. The 7-test family passes. feature_check passes. The offscreen GUI driver passes. The full test suite was not run, so other `loadSettings` callers are unverified.
   - Old projects and saved data: for a clip with no live targets, `clearObjects()` and `m_idsToResolve.clear()` do nothing, so a saved `<object>` list loads as before. The existing evidence is `UndoHistoryPersistenceTest`. Its baseline builds an automation clip linked to a parameter (`tests/src/core/UndoHistoryPersistenceTest.cpp:107-140`). The test saves the project (`:239`), reopens it (`:271`), restores it, and checks `automationLinked` (`:295`). `test-4-family` shows it passed (Test #248). This closes the save-and-reload question for this linked clip. It does not load a project file written by an older build, so older project files remain unverified.
   - Limits: this diff adds no limit or check, so none can reject a legitimate caller.
5. **FALSE-POSITIVE code citations.** Re-checked against the post-edit files: `AutomationClipView.cpp:280-281`, `:283-284`, `:336`; `AutomationClip.cpp:286-298` (unchanged); `AutomationClip.h:239`, `:244` (unchanged). The `cluster-new-p9-439.md` lines come from the earlier evidence logs and were re-read in this session.
6. **Sibling commands in the command files changed.** This batch changed no command file. The five committed files are `AutomationClipView.cpp`, `AutomationClip.cpp`, the disconnect test, the registry, and this report. `ControlCommandsTrackRole.cpp` was read for the detach path but not changed. The `app.*` and `arrangement.*` command list does not apply to this diff.

## Other findings

These are separate units and are not fixed or verified in this batch.

- **Degenerate-range NaN.** `y_scale = max - min` at `src/gui/clips/AutomationClipView.cpp:283` feeds `h` at `:284` (pre-edit 274-275). A zero range gives a non-finite `h`. This is a different defect from the null-model claim.
- **Unlocked-map paint race.** A paint-path read of a map without its lock. Not checked in this batch; no line reference is given.
