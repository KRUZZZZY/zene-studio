# Preserved campaign output: asan-baseline-run1.log

Original output SHA-256: `43aa06208c3f79445395a4655c659dc58e289ec3384a1c550c5363c4b9b004cb`.

~~~~text
PASS   : ArrangingCatchUpTest::initTestCase()
PASS   : ArrangingCatchUpTest::aSplitCutsAtAFrameBetweenTwoTicks()
PASS   : ArrangingCatchUpTest::zeroCrossingSnapMovesTheCutOntoACrossing()
PASS   : ArrangingCatchUpTest::aFrameCutThatCannotBeIsRefused()
PASS   : ArrangingCatchUpTest::aTrimMovesTheStartToAFrameAndUndoesWholly()
PASS   : ArrangingCatchUpTest::theLeadSavesLoadsAndIsResetWhenAbsent()
PASS   : ArrangingCatchUpTest::theZeroCrossingToggleIsAMode()
PASS   : ArrangingCatchUpTest::aSpacerIsOneStepAndSurvivesASave()
PASS   : ArrangingCatchUpTest::aProjectWithoutSpacersWritesNone()
PASS   : ArrangingCatchUpTest::theReferenceCopiesTracksAndClips()
PASS   : ArrangingCatchUpTest::theReferenceCloses()
PASS   : ArrangingCatchUpTest::aSwitchKeepsTheProjectLeftAsTheReference()
PASS   : ArrangingCatchUpTest::aSwitchRefusesToLoseAnUntitledSession()
PASS   : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate()
FAIL!  : ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie() Compared values are not the same
FAIL!  : ArrangingCatchUpTest::switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod() '!survived' returned FALSE. (monitoring Off left a staged monitor in the render list)
PASS   : ArrangingCatchUpTest::cleanupTestCase()
Totals: 15 passed, 2 failed, 1 skipped, 0 blacklisted, 2916ms
~~~~
