Session View completion (board task #641, the #596 engine halves) - MEASURED evidence
=====================================================================================

What this file is. The raw result of the only build and test run this lane made, recorded
here because the evidence is the point: branch 030/session-completion, worktree
zene-030/wsess, 2026-09-15. Nothing in it is a summary of an intention - every line was
produced by a command whose exit code is stated beside it.

                     BUILD
=====================================================================================
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON -DWANT_SESSION_VIEW=ON \
      -DWANT_WASM=OFF -DWANT_STEM_SPLIT=OFF -DWANT_DEBUG_CPACK=OFF -DWANT_COVERAGE=OFF
CONFIGURE_EXIT=0                      (53.0 s; validates the CMakeLists edits)

make -C build -j2 SessionFollowTest SessionArrangementRecordTest ReversibilityContractTest \
                SessionSchedulerTest SessionSchedulerRenderTest SessionModelTest ControlRegistryTest
BUILD_EXIT=0                          (0 errors; lmmsobjs is 508 objects)

Objects for every source this lane added or changed are on disk and were compiled here:
  build/src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionFollow.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecord.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/ControlCommandsSessionRecordLand.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/ControlReversibilityTableSessionView.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/SessionFollow.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/SessionScheduler.cpp.o
  build/src/CMakeFiles/lmmsobjs.dir/core/ControlRegistryRegistrations.cpp.o

                     THE PROOF
=====================================================================================
cd build/tests
QT_QPA_PLATFORM=offscreen ctest -R "SessionFollowTest|SessionArrangementRecordTest|\
ReversibilityContractTest|SessionSchedulerTest|SessionSchedulerRenderTest|SessionModelTest" \
  --output-on-failure
CTEST_EXIT=0

    Start 126: ReversibilityContractTest          Passed
    Start 127: SessionModelTest                   Passed    1.40 sec
    Start 128: SessionSchedulerTest               Passed    0.05 sec
    Start 129: SessionSchedulerRenderTest         Passed    1.43 sec
    Start 130: SessionFollowTest                  Passed    0.04 sec
    Start 131: SessionArrangementRecordTest       Passed    0.04 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) =   0.09 sec

Run from <build>/tests, as AGENTS.md rule 7 requires (the top-level build dir reports 0
tests). Both tests registered by this lane pass; the three session tests that already
existed still pass, so the engine changes did not regress the launch scheduler or the
behaviour-preservation render.

                     THE A16 HISTOGRAM, RE-MEASURED
=====================================================================================
The run above MEASURED the contract table rather than assuming it (this is the assertion
that failed first and the value the constant now carries):

  the table has 306 rows (telemetry client in, no wasmtime, WANT_SESSION_VIEW=ON)
  -> THIS LANE'S OWN MEASUREMENT of its own tree (2026-09-15), kept as that record: the
     release's figure - and the only one in circulation - is the A16-HISTOGRAM block in
     docs/RELEASE-NOTES-v0.3.0-alpha.md, which the registered ctest re-checks on every run
  the table's classes measure 154 true_inverse, 26 snapshot, 7 irreversible, 119 not_mutating
  -> documentedHistogram() base (telemetry out, wasm out): 304 / 154 / 26 / 7 / 117

The figures this branch's test carried before that measurement (285 / 152 / 21 / 6 / 106,
i.e. 293 rows with telemetry) were 13 rows short of the branch's real table before this
lane's +6 rows and 22 short after them: the other lanes that merged into this base added
rows without moving the constant. docs/RELEASE-NOTES-v0.3.0-alpha.md's histogram section
moved with it.

                     WHAT THE BUILD FOUND (real defects, fixed in this branch)
=====================================================================================
1. src/core/ControlReversibilityTableSessionView.cpp did not compile - an #else/#endif with
   no matching #ifdef, left by an edit that removed the guard from the head of the branch:

     ControlReversibilityTableSessionView.cpp:126:2: error: #else without #if
     ControlReversibilityTableSessionView.cpp:131:2: error: #endif without #if

   Fixed in 2099cf280 (the guard is back; the file is an EMPTY table with a valid pointer
   when LMMS_HAVE_SESSION_VIEW is off, the shape ControlReversibilityTableWasmRender.cpp
   uses, and its six rows otherwise).

2. include/SessionArrangementRecorder.h used LMMS_EXPORT without including lmms_export.h,
   so any TU that reached it first failed:

     SessionArrangementRecorder.h:76:19: error: variable
       'lmms::LMMS_EXPORT lmms::SessionArrangementRecorder' has initializer but incomplete type

   tests/src/core/SessionArrangementRecordTest.cpp reaches it first. Fixed by including
   "lmms_export.h" by name in that header.

3. The Follow Action fire time was ONE STEP EARLY: SessionFollow.cpp's actionTime() helper
   subtracted the step from an action tick that had not yet advanced, so every fire reported
   the previous action time (the first fire on a one-bar chain reported tick 0 instead of
   192, and the stop landed at 0 instead of the grid line).
   SessionArrangementRecordTest::followStopEndsTheClip caught it by measuring the tick the
   stop was recorded at. Fixed: the fire reports slot.followNextTick BEFORE the advance.

4. The chain belongs to the CLIP that is playing, which the first revision of the
   hands-free test got wrong by arming ONE cell and expecting the column to keep walking
   after the switch: it fired once and stopped. That is the per-clip model the data layer
   persists (a ClipSlot attribute, not a per-column one), and it is now pinned by
   SessionArrangementRecordTest::aCellWithNoPlanDoesNotFollow.

                     NOT RUN, STATED PLAINLY
=====================================================================================
* The zene BINARY was not built, so no --control-socket transcript and no ctest that needs
  the running instance was run on this branch (the M1 milestone's ControlSessionLaunch and
  ControlSessionLifecycleTranscript ctests, and every control-*.py transcript). Their
  evidence in this tree is the committed tests/control-session-m1-transcript.txt and the
  registrations already in tests/CMakeLists.txt; nothing this lane added changes them.
* tests/run-all-gates.sh was not run (it needs the binary and the full suite).
* The CoverageMatrixSnapshot's derived tools/mcp-zene-control/zene_control/commands_snapshot.json
  is stale for the six new ids, as the lane brief says it will be: it is regenerated once at
  a merge, from a live instance.
* ONE TEST IN THE SAME BUILD RUN FAILED AND IS NOT THIS LANE'S:
  ControlRegistryTest::mixerSetPanRefusesTyped (21 passed, 1 failed) - it expects
  ControlErrorKind::Refused (5) and measures InvalidArgs (1) for mixer.set_pan.
  `git diff --name-only HEAD~6..HEAD | grep -i "mixer\|pan"` is EMPTY: this branch changes no
  mixer file, and the last commit touching src/core/ControlCommandsMixer*.cpp is another
  lane's wip(stable-ids) checkpoint (0770b7a0b). It is a pre-existing red on this base, in
  the same class as the file-length regressions Gate 7 reports for files this lane never
  opened.
