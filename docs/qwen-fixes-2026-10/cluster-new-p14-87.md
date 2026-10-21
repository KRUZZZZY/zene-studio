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
