FIXED

# cluster ci-017caf2edd434b66 - CI regression: build / mingw64

## Verdict

FIXED. The finding is REAL on this branch: the hosted `mingw64` job at
`3ab01006f613b223654f56481b789b605c444a84` stopped in `src/gui/ControlCommandsArrangerView.cpp`
with

```
include/ArrangerZoom.h:81:64: error: 'Qt::GestureState' has not been declared
```

The job builds against the tobydox PPA's MinGW Qt 5 (`qt5base-mingw-w64`,
`.github/workflows/deps-ubuntu-24.04-mingw.txt`), which is configured without gesture support, so
its QtCore/qtcore-config.h carries `QT_NO_GESTURES` - the same way the same file carries
`QT_NO_ICONV` for a disabled feature. Everything gesture-typed is behind that macro:

* `Qt::GestureState` and `Qt::GestureType` - `/usr/include/qt6/QtCore/qnamespace.h:1658`
  (`#ifndef QT_NO_GESTURES`), same guard in Qt 5.15's qnamespace.h:1682
* `QWidget::grabGesture`/`ungrabGesture` - `QtWidgets/qwidget.h:363`
* `QGestureEvent`, `QPinchGesture`, `QGesture` - `QtWidgets/qgesture.h:52`

So the product had three unguarded uses on that configuration, plus one in the test:

* `include/ArrangerZoom.h:81` - the exported API took `Qt::GestureState`
* `src/gui/ArrangerZoom.cpp:193` - the definition (and `Qt::GestureFinished`/`GestureCanceled` at :195)
* `src/gui/ArrangerAids.cpp:31,33,123,125,131-140,143,145` - the `PinchZoom` filter, its Qt gesture
  includes, `grabGesture` and `QPinchGesture`
* `tests/src/gui/ArrangerTimelineTest.cpp:277` - the canceled-pinch case named `Qt::GestureCanceled`

The same defect was fixed on the train-21 line by `6072ae1357` + `14cf88571f`, and that fix is not
an ancestor of this branch (`git merge-base --is-ancestor 6072ae1357 HEAD` -> exit 1), so it is not
ALREADY-FIXED here: the code at HEAD still has the unguarded declaration. This entry re-lands the
guard on the current code, and adds a regression that rebuilds the affected translation units under
a gesture-less Qt.

## The fix (cause, not symptom)

* `include/ArrangerZoom.h:80-89`: the exported `finishPinch(int, int, bool ended)` is the one
  definition on every Qt, and the `Qt::GestureState` overload is an inline forwarder inside
  `#ifndef QT_NO_GESTURES`. A gesture-less build therefore has the API it can call and no
  reference to a type that does not exist; a gestures build keeps the exact same call shape and
  the same `finished || canceled` rule (one expression, in the inline forwarder).
* `src/gui/ArrangerZoom.cpp:193-196`: definition matches the bool declaration.
* `src/gui/ArrangerAids.cpp:29-37,120-161,172-174`: the two gesture includes, the `PinchZoom` class
  and its installation are inside `#ifndef QT_NO_GESTURES`. On a gesture-less build the arranger
  simply installs no pinch filter - the same behaviour the platform's Qt offers at all; the
  cross-hair overlay and the single-touch selection path are untouched.
* `tests/src/gui/ArrangerTimelineTest.cpp:277-281`: the canceled-pinch case calls the bool form
  under `QT_NO_GESTURES` and the gesture-state form otherwise. The assertion is identical.

Callers and siblings checked: `finishPinch` has exactly two call sites (`src/gui/ArrangerAids.cpp:143`
and the test above) and one definition; `ArrangerZoom.h` is included by `ArrangerZoom.cpp`,
`ArrangerAids.cpp`, `ArrangerNavigator.cpp`, `editors/SongEditor.cpp`,
`ControlCommandsArrangerView.cpp`, `ArrangerKeysNavigatorTest.cpp` and
`ArrangerOverlaysVideoLaneTest.cpp` - all seven compile in the regression, so nothing else names
`Qt::GestureState`. No other file in `src/`/`include/` includes a Qt gesture header
(`rg -n 'QPinchGesture|QGestureEvent|grabGesture|Qt::PinchGesture' src include` -> only the two
guarded sites).

## Regression

`tests/arranger-zoom-no-gestures-check.py` (ctest `ArrangerZoomNoGestures`, registered in
`tests/CMakeLists.txt:6487-6499`, manifest `tests/fork-sources.txt`) is deterministic and needs no
gesture-less Qt of its own: it takes the affected translation units' exact compile lines from this
build's `compile_commands.json` and rebuilds them against a shadow `QtCore/qtcore-config.h` whose
`QT_FEATURE_gestures` is `-1` and which defines `QT_NO_GESTURES` - the configuration Qt's own
configure writes for that feature set, under which Qt's headers stay coherent (forcing the macro on
a gestures-enabled Qt instead breaks qevent.h itself, which is not the failing platform). Two probes
keep the simulation honest: `Qt::GestureStarted` must be undeclared with the shadow and declared
without it. It then compiles the six affected TUs (including the QtTest one, moc'd from the same
compile line) and both API spellings. Nothing here is timed, sampled or raced; there is no clock,
no sleep and no thread.

Test matrix:

| Path | Case | Covered by |
| --- | --- | --- |
| normal | gestures build keeps `finishPinch(..., Qt::GestureState)` and the pinch filter | `ArrangerZoomNoGestures` "still exists with gestures"; `ArrangerTimelineTest` (built and run on this gestures host) |
| failure/edge | gesture-less build: header, definition, pinch filter and test TU all compile | `ArrangerZoomNoGestures` shadow compile of the six TUs |
| failure/edge | canceled pinch still records the starting zoom (behaviour preserved across the API shape) | `ArrangerTimelineTest::aCanceledPinchKeepsItsStartingZoomInHistory` |
| platform | MinGW/Qt5-without-gestures is the only configuration that sees this | the shadow config reproduces the CI diagnostic verbatim; the hosted job runs no tests, so the check is the gate |

## Evidence table (commands as run, exit codes as observed)

Logs: the designated run log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-017caf2edd434b66.r2/logs` rejects every
write in this session (`touch` and a Python `open(...,'w')` both `PermissionError`;
`findmnt` shows the btrfs subvolume is `rw`, the mode is 0755 and owned by this user, so this is a
harness sandbox, not the repository). To keep every build/test log as a NEW numbered file I wrote
them to `build/qwen-logs/` in this worktree and mirrored the whole set to
`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ci-017caf2edd434b66.r2-logs/`, which is
writable. The commands below name the file in the mirrored set.

| # | Evidence | Command | Result |
| --- | --- | --- | --- |
| 1 | Pre-fix reproduction on this branch (RED) | `python3 tests/arranger-zoom-no-gestures-check.py --build-dir build --moc /usr/lib/qt6/moc > build/qwen-logs/check-1-red.log 2>&1` | EXIT=1; `ArrangerZoom.h:81:64: error: 'Qt::GestureState' has not been declared`, plus `QPinchGesture does not name a type` and `class QWidget has no member named grabGesture` |
| 2 | Build after the fix | `cmake --build build --target zene ArrangerTimelineTest ArrangerZoomBoundTest ArrangerKeysNavigatorTest ArrangerOverlaysVideoLaneTest -j$(nproc) > build/qwen-logs/build-1.log 2>&1` | EXIT=0 |
| 3 | Regression GREEN with the fix | `cd build/tests && ctest -R "ArrangerZoomNoGestures\|ArrangerTimelineTest\|ArrangerZoomBoundTest\|ArrangerKeysNavigatorTest\|ArrangerOverlaysVideoLaneTest" --output-on-failure > ../qwen-logs/test-3-green-refixed.log 2>&1` | EXIT=0; 5/5 passed |
| 4 | Step 4b RED: product patch reversed | `git diff 3ab01006f6 HEAD -- include/ArrangerZoom.h src/gui/ArrangerZoom.cpp src/gui/ArrangerAids.cpp > build/qwen-logs/fix.patch; git apply -R build/qwen-logs/fix.patch` | DIFF_EXIT=0, REVERT_EXIT=0 |
| 5 | Step 4b: rebuild the reversed tree | `cmake --build build --target zene ArrangerTimelineTest -j$(nproc) > build/qwen-logs/build-7-red-final.log 2>&1` | EXIT=0 |
| 6 | Step 4b RED run | `cd build/tests && ctest -R "ArrangerZoomNoGestures\|ArrangerTimelineTest" --output-on-failure > ../qwen-logs/test-8-red-final-committed.log 2>&1` | EXIT=8; 1/2 passed, `ArrangerZoomNoGestures` failed, 7 gesture cases do not build |
| 7 | Step 4b: patch re-applied | `git apply build/qwen-logs/fix.patch` | EXIT=0 |
| 8 | Step 4b GREEN run | `cmake --build build --target zene ArrangerTimelineTest -j$(nproc) > build/qwen-logs/build-8-green-final.log 2>&1` then `cd build/tests && ctest -R "Arranger" --output-on-failure > ../qwen-logs/test-9-green-final-committed.log 2>&1` | build EXIT=0; ctest EXIT=0, 8/8 passed |
| 9 | Whole `Arranger` family | `cmake --build build --target ArrangerTakePunchTest ArrangerFileDropTest ArrangerClipboardKeysTest -j$(nproc)` then `cd build/tests && ctest -R "Arranger" --output-on-failure > ../qwen-logs/test-6-arranger-all.log 2>&1` | build EXIT=0; ctest EXIT=0, 8/8 passed |
| 10 | Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > build/qwen-logs/feature-check-<n>.log 2>&1` | EXIT=0; 8/8 PASS (last run feature-check-5.log) (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| 11 | Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-017caf2edd434b66 > build/qwen-logs/submit-check-<n>.log 2>&1` | EXIT=0; every mechanical item PASS (last run submit-check-3.log), all five ASK items answered in the Self-check section |

The first gate run before the script split was EXIT=1 for one reason only:
`complexity ... NEW: new function over target: compile_source@... (CCN 12); main@... (CCN 11)`
(`build/qwen-logs/feature-check-1.log`). I split `compile_source` into `moc_include` +
`compile_source` and `main` into `load_build`/`write_fixtures`/`main` (no threshold or baseline was
touched); the rerun is row 10, 8/8 PASS.

The check itself was then hardened in a second commit (`2ac31f9f63`): on a host whose Qt has no
gestures the typed `Qt::GestureState` fixture is SKIPped rather than failed, because there the six
shadow compiles ARE the shipped configuration and that spelling genuinely does not exist - a
supported platform must not fail its own regression. On this gestures host it still runs and must
pass (`check-5-green.log`). The reversal proof above (rows 4-8) and the gates (row 10) were rerun
after that commit, against the tree being submitted.


## Files committed

* `include/ArrangerZoom.h`
* `src/gui/ArrangerZoom.cpp`
* `src/gui/ArrangerAids.cpp`
* `tests/src/gui/ArrangerTimelineTest.cpp`
* `tests/arranger-zoom-no-gestures-check.py` (new; registered in `tests/fork-sources.txt`)
* `tests/CMakeLists.txt` (one `add_test` + one timeout; declared in `tests/upstream-modifications.txt`)
* `tests/fork-sources.txt`, `tests/upstream-modifications.txt` (registration/ledger)
* `docs/qwen-fixes-2026-10/cluster-ci-017caf2edd434b66.md` (this report)

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Rows 4-8 above are the reversal proof on the rebuilt tree: with
`include/ArrangerZoom.h`, `src/gui/ArrangerZoom.cpp` and `src/gui/ArrangerAids.cpp` reverted,
`ctest -R "ArrangerZoomNoGestures|ArrangerTimelineTest"` exits 8 (1/2 passed, `ArrangerZoomNoGestures`
failed; the check reports exactly the CI diagnostic,
`ArrangerZoom.h:81:64: error: 'Qt::GestureState' has not been declared`, plus the
`QPinchGesture`/`grabGesture` errors, and the bool-API fixture fails to compile). With the patch
re-applied and rebuilt, the whole `Arranger` family exits 0 with 8/8 tests passed (row 8). The RED
and GREEN runs use the same build tree and the same committed test; only the product files differ.

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).** The seam is the build's own `compile_commands.json`: the check takes each
affected translation unit's recorded compile line and adds a shadow `QtCore/qtcore-config.h`
containing `#define QT_FEATURE_gestures -1` and `#define QT_NO_GESTURES 1`. It is a pure compiler
invocation with `-fsyntax-only`; there is no clock, no sleep, no thread, no sampling and no
network. The two probe fixtures are the negative control of the seam itself: with the shadow
`Qt::GestureStarted` must be undeclared (so a check that silently lost the shadow fails), and
without it the same probe must compile (so a host that has gestures still measures something).

**The fix is at the cause and complete: every caller and sibling of what you changed was checked and
stays consistent.** `finishPinch` has one definition and two call sites, both updated (the pinch
filter forwards `pinch->state()` to the inline overload, which applies the same
`finished || canceled` rule; the test calls the bool form where the enum does not exist). Every
translation unit that includes `ArrangerZoom.h` compiles under the shadow
(`ArrangerZoom.cpp`, `ArrangerAids.cpp`, `ControlCommandsArrangerView.cpp`,
`ArrangerNavigator.cpp`, `editors/SongEditor.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`), and
the two remaining test includes (`ArrangerKeysNavigatorTest.cpp`,
`ArrangerOverlaysVideoLaneTest.cpp`) are unaffected because neither names a gesture type - they are
built and run in row 9. `rg` over `src/` and `include/` shows the only other Qt gesture API uses are
inside the guarded `PinchZoom` class; no second path into the pinch exists.

**No collateral damage: old projects and saved data still load as before, and any limit or check you
added does not hit other callers that legitimately exceed it.** No serialization, project,
undo/redo or command code changed; the fix is a compile-time guard plus one boolean-typed parameter
with the identical rule. On a gestures build the `Qt::GestureState` overload is inline and does the
same comparison, so the pinch history behaviour and the `arrangement.zoom_back` walk are unchanged -
proved by `ArrangerTimelineTest`, including `aCanceledPinchKeepsItsStartingZoomInHistory`, still
passing (rows 3, 8). No new limit or runtime check was added, so no other caller can be affected.
Nothing was added to an audio-thread path: `finishPinch`/`ArrangerAids` are UI-thread (zoom/gesture
handling), and the change only moves an enum comparison into an inline forwarder.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
that shows it.** Not applicable - the verdict is FIXED, and the branch evidence that rules
ALREADY-FIXED out is above: at HEAD `include/ArrangerZoom.h:81` still declared
`finishPinch(int, int, Qt::GestureState)` unconditionally, and the historical fix `6072ae1357`
(plus its test half `14cf88571f`) is on `fix/ci-train21`/`product/ci/050-m0-train-21`, not an
ancestor of this branch.
