# Cluster new-p11-8-port: Ctrl-middle-click on a locked clip

**Verdict: FALSE-POSITIVE** (on the newest line, 050/m0-train-5 — the line this branch is cut from)

The port is not applicable to this line. The behaviour the finding reports is this line's accepted,
tested contract, and the finding's own premise no longer holds here: on this line the *context
menu's* mute does not work on a locked clip either, so the mouse gesture and the menu give the same
answer. The old fix (commit `0d9ca6fed` on `bugs/qwen-new-p11-8`) was cut from a base that did not
have Luna cluster 5578; 5578 is on this line and asserts the opposite contract for the same edit.

## The code that shows it (this branch)

- `src/gui/clips/ClipView.cpp:654` — the locked-clip guard returns for every non-right click, so the
  Ctrl+middle press never reaches the mute branch at `src/gui/clips/ClipView.cpp:799-809`.
- `src/gui/clips/ClipView.cpp:1319-1332` — `ClipView::toggleMute()` **skips a locked clip** and
  counts it, then `cliplocks::announceKept()` (src/gui/clips/ClipLockFeedback.cpp:170-176) tells the
  user that the locked clip stayed put. This is Luna cluster 5578 (commit `9e533aef3`, "a locked
  clip's own context menu still muted, recolored and auto-resized it"), which is on this line.
- `tests/src/gui/ArrangerTimelineTest.cpp:496-527` — the accepted regression
  `aLockedClipKeepsEveryMutatingMenuAction` pins it, and says so in the test itself (line 510):
  "Mute: the same edit the Ctrl+middle-click gesture already refuses at the press."
- `include/EditLocks.h:40-47` — the documented contract: "A locked clip - or any clip of a locked
  track - cannot be moved, resized, deleted or edited". (Line 44-45's "Mute, solo, arm, the mixer
  and automation stay open on a locked **track** - a lock protects the edit, not the mix" is about a
  locked *track*'s mix state, not about a locked clip's mute, which 5578 decided is an edit of the
  clip.)
- `src/core/EditLocks.cpp:48-57` and `src/core/EditLocks.cpp:198-209` — the registry's preflight
  refuses every `clip.*` command that names a locked clip, so the agent surface refuses it as well.

## Why the old fix cannot be ported here (measured, not argued)

| Experiment | Command and exit | Result |
|---|---|---|
| The old branch's regression, verbatim, on this tree | `ctest -R '^ArrangerTimelineTest$'` — **EXIT=8** (`test-1.log`) | 27 passed, 1 failed: `'clip->isMuted()' returned FALSE. (Ctrl-middle-click did not mute the locked clip)` — the assertion is at `tests/src/gui/ArrangerTimelineTest.cpp:265` of the old branch (commit `0d9ca6fed`) |
| The old fix's product hunk alone (the press guard, `old-fix-product.patch`) | `git apply .../old-fix-product.patch` — **EXIT=0**; `cmake --build build --target ArrangerTimelineTest -j12` — **EXIT=0** (`build-2.log`); `ctest -R '^ArrangerTimelineTest$'` — **EXIT=8** (`test-2.log`) | still 27 passed, 1 failed, the same `isMuted()` assertion: the hunk changes nothing, because `ClipView::toggleMute()` refuses the locked clip on its own |
| The old branch's product behaviour (press hunk **and** the old branch's `toggleMute` without 5578's lock check) | build — **EXIT=0** (`build-3.log`); `ctest -R '^ArrangerTimelineTest$'` — **EXIT=8** (`test-3.log`) | 26 passed, **2 failed**: `aLockedClipKeepsEveryMutatingMenuAction` and `aLockedClipInASelectionStaysWhileTheOthersChange` — the two accepted 5578 regressions |

So the old fix and this line's cluster 5578 assert opposite answers for one and the same edit
(mute a locked clip): the old fix wants the mouse path to mute it, 5578 wants every path to refuse
it. Making the old test green on this line requires deleting 5578's lock check, which breaks two
accepted regressions — that is a revert of a newer accepted fix, not a port.

## The newer live line agrees

- `050/m0-train-8` (the newest train) still has the **original** press guard
  (`src/gui/clips/ClipView.cpp:656` in that tree) and 5578's `toggleMute` lock check
  (`src/gui/clips/ClipView.cpp:1321-1341`), and it has **no** `aLockedClipCanBeMutedButNotDeletedByMiddleClick`
  in `tests/src/gui/ArrangerTimelineTest.cpp`.
- On that line the GUI mute of an unlocked clip goes through the registered `clip.set_mute` command
  (`src/gui/clips/ClipView.cpp:1331-1339` in `050/m0-train-8`), and the registry's lock preflight
  refuses a `clip.*` command that names a locked clip (`src/core/EditLocks.cpp:48-57`,
  `src/core/EditLocks.cpp:198-209`): the mouse, the menu and the agent surface are one answer there too.
- The only merge of the old fix, `191184c2f` ("Merge bugs/qwen-new-p11-8 (accepted; 050/m0-train-6
  batch 1-1000)"), survives **only** on `050/m0-train-6-attempt1`; it is in none of `050/m0-train-6`,
  `050/m0-train-7` or `050/m0-train-8`. It was dropped.

## What this branch commits

No product change: the behaviour is already this line's contract, and changing it would break two
accepted regressions. What is committed is a regression that **pins** the contract, so the old fix
cannot be ported again silently, plus the registration line for the changed inherited test file.

- `tests/src/gui/ArrangerTimelineTest.cpp:529-559` —
  `aLockedClipIsNotMutedByCtrlMiddleClick`: a locked clip is not muted by the Ctrl+middle-click
  gesture and not deleted by it, the menu path (`ClipView::toggleMute`) refuses the very same edit,
  and an unlocked clip **is** muted by the very same gesture (so the test is not vacuous: the lock is
  what refuses it, not the fixture). It drives the real widget path (`QTest::mouseClick` on the
  `ClipView`) and the real command path (`clip.set_locked`), with no timing, sleep or race.
- `tests/upstream-modifications.txt` — the inherited test file is registered with the reason.

## Evidence

All build commands ran from the worktree root; all CTest commands ran from `build/tests`. Logs are
in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8-port.r1/logs`.

| Stage | Exact command | Exit |
|---|---|---:|
| Build with the old branch's regression (probe) | `cmake --build build --target ArrangerTimelineTest -j12 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED: the old branch's test on this tree | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Build with the old fix's product hunk | `cmake --build build --target ArrangerTimelineTest -j12 > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED: the old fix's hunk alone changes nothing | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Build with the old branch's product behaviour | `cmake --build build --target ArrangerTimelineTest -j12 > .../logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| RED: the old behaviour breaks the accepted 5578 tests | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Build with the committed regression | `cmake --build build --target ArrangerTimelineTest -j12 > .../logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| GREEN: the committed regression on this line | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Build with the old behaviour and the committed regression | `cmake --build build --target ArrangerTimelineTest -j12 > .../logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Sensitivity RED: the committed regression catches the old behaviour | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-5.log 2>&1; echo EXIT=$?` | 8 |
| Restore this line's product code | `git checkout -- src/gui/clips/ClipView.cpp; echo EXIT=$?` | 0 |
| Rebuild the app and the affected target | `cmake --build build --target ArrangerTimelineTest zene -j12 > .../logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?` | 0 |
| Qt-level case count of the final run | `cd build/tests && ./ArrangerTimelineTest > .../logs/test-9.log 2>&1; echo EXIT=$?` | 0 (28 passed, 0 failed) |
| Sibling GUI targets (not built in this tree yet) | `ctest -R '^(ArrangerTimelineTest\|ArrangerFileDropTest\|ArrangerOverlaysVideoLaneTest\|TrackViewDeferredLifetimeTest)$' --output-on-failure > .../logs/test-7.log 2>&1; echo EXIT=$?` | 8 (3 of 4 "Not Run": the executables were not built) |
| Build the sibling GUI targets | `cmake --build build --target TrackViewDeferredLifetimeTest ArrangerFileDropTest ArrangerOverlaysVideoLaneTest -j12 > .../logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| Affected family, all four targets | `ctest -R '^(ArrangerTimelineTest\|ArrangerFileDropTest\|ArrangerOverlaysVideoLaneTest\|TrackViewDeferredLifetimeTest)$' --output-on-failure > .../logs/test-8.log 2>&1; echo EXIT=$?` | 0 (4/4 passed) |
| Gates (all) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-8-port > .../logs/gate-1.log 2>&1; echo EXIT=$?` | 0 (PASS on all eight rows) |
| Final rebuild of the committed state | `cmake --build build --target ArrangerTimelineTest zene -j12 > .../logs/build-8.log 2>&1; echo EXIT=$?` | 0 |
| Final run of the affected family | `ctest -R '^(ArrangerTimelineTest\|ArrangerFileDropTest\|ArrangerOverlaysVideoLaneTest\|TrackViewDeferredLifetimeTest)$' --output-on-failure > .../logs/test-10.log 2>&1; echo EXIT=$?` | 0 (4/4 targets passed) |
| Gates after the commit | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-8-port > .../logs/gate-2.log 2>&1; echo EXIT=$?` | 0 (PASS) |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-8-port new-p11-8-port > .../logs/submit-check-2.log 2>&1; echo EXIT=$?` | 0 (RESULT: PASS - submit) |

The failure messages are quoted from the logs: `test-1.log` / `test-2.log`:
`'clip->isMuted()' returned FALSE. (Ctrl-middle-click did not mute the locked clip)`; `test-3.log` /
`test-5.log`: `Compared values are not the same` in the two 5578 cases and
`'!clip->isMuted()' returned FALSE. (a locked clip was muted by the Ctrl+middle-click gesture)` in
the committed regression.

The captured logs were searched with the `fff grep` tool (path = the log folder above, term
`isMuted`) and with `rg -n`; the failure lines quoted here are the ones those searches returned.

## Files in the cluster commit

- `tests/src/gui/ArrangerTimelineTest.cpp` (the regression; inherited file, registered)
- `tests/upstream-modifications.txt` (registration)
- `docs/qwen-fixes-2026-10/cluster-new-p11-8-port.md` (this report)

No product file is changed, so no new file has to be registered in `tests/fork-sources.txt`.

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   There is no product change to revert: the verdict is FALSE-POSITIVE, so the committed regression
   pins the behaviour that is already on this line. The sensitivity proof is the mirror image and is
   recorded above: with the old branch's product behaviour applied to this tree (the press guard of
   `old-fix-product.patch` plus the old branch's `toggleMute` without 5578's lock check) the
   committed regression **FAILS** — `ctest -R '^ArrangerTimelineTest$'` **EXIT=8** (`test-5.log`),
   25 passed / 3 failed, `'!clip->isMuted()' returned FALSE. (a locked clip was muted by the
   Ctrl+middle-click gesture)` — and with this line's product code restored it **PASSES** —
   **EXIT=0** (`test-6.log`, 1/1 CTest target; `test-9.log` shows 28 Qt cases passed, 0 failed).
2. **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
   values it uses)."** The regression uses the real widget path (`QTest::mouseClick` on the
   `ClipView`) and the real command path (`clip.set_locked` through `ControlRegistry`); it reads the
   model (`clip->isMuted()`, `clip->startPosition()`, `m_track->getClips()`) and never a clock, a
   sleep or a timer. No timing seam is needed because no time is involved.
3. **"The fix is at the cause and complete: every caller and sibling of what you changed was checked
   and stays consistent."** No product code was changed, so nothing can drift. The callers of the
   mute were all checked and left alone: the mouse press (`src/gui/clips/ClipView.cpp:799-809`), the
   context-menu action (`src/gui/clips/ClipView.cpp:1233-1235`), the shared
   `ClipView::toggleMute()` (`src/gui/clips/ClipView.cpp:1319-1332`), the right-button Ctrl path
   (`src/gui/clips/ClipView.cpp:782-791`) and the registry preflight
   (`src/core/EditLocks.cpp:198-209`) all refuse a locked clip. The two siblings of the same
   question — `ClipView::toggleSelectedAutoResize()` (`src/gui/clips/ClipView.cpp:1334-1349`) and
   `ClipView::setColor()` — refuse it too, so the four mutating actions stay consistent.
4. **"No collateral damage: old projects and saved data still load as before, and any limit or check
   you added does not hit other callers that legitimately exceed it."** Nothing in the product,
   the save format or the command surface was touched: the diff is one test file, one registration
   line and this report. `<edit-locks>` is saved and loaded exactly as before
   (`src/core/EditLocks.cpp:211-235`).
5. **"Each edit is ONE undo step, and every new or changed command has an honest reversibility
   class."** No command was added or changed (the command surface and its snapshot are untouched, so
   the canary does not move), and no edit was made to the model: the regression only reads the model.
6. **"Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or
   emits a signal."** The diff touches no audio path: the only changed file is a GUI test, which
   drives the GUI thread.
7. **"Old projects still load and render exactly as before; new data round-trips through save and
   load."** No product change, so nothing about loading, saving or rendering changes. The lock
   section itself is untouched.
8. **"Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h
   macro names, no std::filesystem outside _WIN32."** The regression uses only `QTest::mouseClick`,
   `QPoint`, `QVERIFY`/`QCOMPARE` and the project's own helpers — all used already in the same file
   (`tests/src/gui/ArrangerTimelineTest.cpp:390-392`, `505-527`).
9. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
   contract that shows it."** See the sections above: `src/gui/clips/ClipView.cpp:654`,
   `src/gui/clips/ClipView.cpp:1319-1332`, `include/EditLocks.h:40-47`,
   `src/core/EditLocks.cpp:48-57` and `src/core/EditLocks.cpp:198-209`, the accepted regressions
   `tests/src/gui/ArrangerTimelineTest.cpp:496-527` and `563-584`, and the measured experiment table
   (three RED runs, one GREEN run).
