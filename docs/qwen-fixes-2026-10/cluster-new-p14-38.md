FIXED
new-p14-38: FIXED
new-p17-19: FIXED
Evidence: src/core/ControlCommandsNoteTools.cpp invert/transposeInKey now return no reversible transaction when nothing changes; invert reports changedCount instead of notes.size(). src/core/ControlCommandsNoteTransform.cpp transpose/velocityOffset/velocityScale only advertise __transaction when changes. Regression tests: ControlNoteScaleVerbsTest::invertNoOpDoesNotAdvertiseAnUndo, invertCountsOnlyNotesThatMove, zeroSemitoneTransposeDoesNotAdvertiseAnUndo. RED: ctest -R ControlNoteScaleVerbsTest EXIT=8 (3 FAIL). GREEN: ctest -R ControlNoteScaleVerbsTest EXIT=0 (all pass). feature_check EXIT=0.

## Self-check
- The regression test FAILS with product change reverted and PASSES with it: RED ctest exit 8 with 3 FAIL; GREEN ctest exit 0. Recorded in logs/test-red2.log and logs/test-green2.log.
- Deterministic: uses journal/transaction count seams and fixed note keys; no sleeps or races.
- Fix at cause: conditional checkpoint transforms (invert, transposeInKey, transpose, velocityOffset, velocityScale) now do not advertise undo when unchanged; invert counts only moved notes.
- No collateral: no new limits affecting other callers; unchanged clips report 0 and no transaction.
