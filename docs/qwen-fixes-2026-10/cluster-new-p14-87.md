**Verdict: FIXED**

new-p14-87: FIXED
new-p20-42: FIXED
new-p8-100: FIXED

Evidence:
- new-p14-87: src/core/DawProjectWrite.cpp clipRefusal now validates note channel (0..15), key (0..127), vel/rel (finite, 0..1) like reader's noteFieldRangeError. Test `invalidNoteFieldsAreRefusedByTheWriter` (RED exit 3, 3 failures on reversed fix; GREEN exit 0).
- new-p20-42: writeDawProject now performs read-back/hash only when report != nullptr. Test `writingWithoutAReportDoesNotReadTheArchive` (RED exit 3; GREEN exit 0).
- new-p8-100: modelRefusal now rejects non-finite/out-of-range mixer volume, track volume, track pan via scalarFieldsRefusal; note vel/rel covered by clipRefusal. Test `nonFiniteScalarParametersAreRefusedByTheWriter` (RED exit 3; GREEN exit 0).

Commands:
- RED: git apply -R logs/fix.patch; cmake --build build --target DawProjectInterchangeRoundTripTest (EXIT=0); ctest run (EXIT=3, 3 failures)
- GREEN: git apply logs/fix.patch; cmake --build ... (EXIT=0); ctest (EXIT=0, 59/59 passed)
- Gates: python3 feature_check.py (EXIT=0, all PASS)

## Self-check

- Regression test FAILS with product change reverted and PASSES restored: RED ctest exit 3 (3 failures), GREEN exit 0 (59/59 passed). See logs/test-red.log and test-green.log.
- Test is deterministic: no sleeps/timing. Bug1/3 use injected invalid values (channel=16, key=128, NaN/inf/out-of-range doubles). Bug2 uses umask(0466) seam to create an unreadable written file.
- Fix at cause: note field validation in clipRefusal; scalar validation in scalarFieldsRefusal; read-back gated on report != nullptr. Checked callers: dawProjectXmlFromModel and writeDawProject.
- No collateral damage: validation ranges match reader (0..15, 0..127, 0..1, 0..2); old valid models still write. Bug2 only skips read when no report requested.
- Verdicts: all three REAL/FIXED; code evidence cited above.
