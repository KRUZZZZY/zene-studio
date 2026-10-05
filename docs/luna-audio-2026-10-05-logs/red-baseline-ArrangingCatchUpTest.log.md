# Preserved campaign output: red-baseline-ArrangingCatchUpTest.log

Original output SHA-256: `dbc274577322db86ee320f46d0ec336ce092ab5ada71b7c49f662101fb8e061c`.

~~~~text
********* Start testing of ArrangingCatchUpTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
  environment variable "LMMS_LV2_DEBUG" to nonempty.
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ArrangingCatchUpTest::initTestCase()
QWARN  : ArrangingCatchUpTest::aSplitCutsAtAFrameBetweenTwoTicks() qt.core.qobject.connect: QObject::connect: No such slot lmms::SampleClip::updateLength()
PASS   : ArrangingCatchUpTest::aSplitCutsAtAFrameBetweenTwoTicks()
QWARN  : ArrangingCatchUpTest::zeroCrossingSnapMovesTheCutOntoACrossing() qt.core.qobject.connect: QObject::connect: No such slot lmms::SampleClip::updateLength()
PASS   : ArrangingCatchUpTest::zeroCrossingSnapMovesTheCutOntoACrossing()
PASS   : ArrangingCatchUpTest::aFrameCutThatCannotBeIsRefused()
PASS   : ArrangingCatchUpTest::aTrimMovesTheStartToAFrameAndUndoesWholly()
JO-ID 8408425 already in use by sampleclip!
JO-ID 8408425 already in use by sampleclip!
PASS   : ArrangingCatchUpTest::theLeadSavesLoadsAndIsResetWhenAbsent()
PASS   : ArrangingCatchUpTest::theZeroCrossingToggleIsAMode()
PASS   : ArrangingCatchUpTest::aSpacerIsOneStepAndSurvivesASave()
PASS   : ArrangingCatchUpTest::aProjectWithoutSpacersWritesNone()
PASS   : ArrangingCatchUpTest::theReferenceCopiesTracksAndClips()
QWARN  : ArrangingCatchUpTest::theReferenceCopiesDevices() "The plugin \"amplifier\" wasn't found or could not be loaded! Reason: \"Plugin not found.\""
SKIP   : ArrangingCatchUpTest::theReferenceCopiesDevices() this test binary finds no built-in effect plug-ins
   Loc: [../../tests/src/core/ArrangingCatchUpTest.cpp(322)]
PASS   : ArrangingCatchUpTest::theReferenceCloses()
PASS   : ArrangingCatchUpTest::aSwitchKeepsTheProjectLeftAsTheReference()
PASS   : ArrangingCatchUpTest::aSwitchRefusesToLoseAnUntitledSession()
QWARN  : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate() "The plugin \"eq\" wasn't found or could not be loaded! Reason: \"Plugin not found.\""
QWARN  : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate() "The plugin \"lomm\" wasn't found or could not be loaded! Reason: \"Plugin not found.\""
QWARN  : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate() "The plugin \"analyzer\" wasn't found or could not be loaded! Reason: \"Plugin not found.\""
PASS   : ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate()
FAIL!  : ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie() Compared values are not the same
   Actual   (beforeRender): 0
   Expected (1)           : 1
   Loc: [../../tests/src/core/ArrangingCatchUpTest.cpp(429)]
FAIL!  : ArrangingCatchUpTest::switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod() '!survived' returned FALSE. (monitoring Off left a staged monitor in the render list)
   Loc: [../../tests/src/core/ArrangingCatchUpTest.cpp(451)]
PASS   : ArrangingCatchUpTest::cleanupTestCase()
Totals: 15 passed, 2 failed, 1 skipped, 0 blacklisted, 1388ms
********* Finished testing of ArrangingCatchUpTest *********
~~~~
