Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s9/build/tests
    Start 20: ClipWarpPersistenceTest
1/2 Test #20: ClipWarpPersistenceTest ..........***Failed    2.24 sec
********* Start testing of ClipWarpPersistenceTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : ClipWarpPersistenceTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 117 msecs.
QDEBUG : ClipWarpPersistenceTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : ClipWarpPersistenceTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ClipWarpPersistenceTest::initTestCase()
JO-ID 8412798 already in use by sampleclip!
PASS   : ClipWarpPersistenceTest::aClipWithNoWarpSerialisesExactlyAsBefore()
JO-ID 8394650 already in use by sampleclip!
PASS   : ClipWarpPersistenceTest::warpRoundTripsThroughTheProjectFile()
JO-ID 8394237 already in use by sampleclip!
PASS   : ClipWarpPersistenceTest::anOldProjectWithoutAWarpElementLoads()
JO-ID 8408425 already in use by sampleclip!
FAIL!  : ClipWarpPersistenceTest::restoringAnElementWithoutAWarpUnwarpsTheClip() 'warped->warpMarkers().empty()' returned FALSE. ()
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s9/tests/src/core/ClipWarpPersistenceTest.cpp(283)]
PASS   : ClipWarpPersistenceTest::cleanupTestCase()
Totals: 5 passed, 1 failed, 0 skipped, 0 blacklisted, 2202ms
********* Finished testing of ClipWarpPersistenceTest *********

    Start 45: ControlWarpCommandsTest
2/2 Test #45: ControlWarpCommandsTest ..........***Failed    1.44 sec
********* Start testing of ControlWarpCommandsTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : ControlWarpCommandsTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 116 msecs.
QDEBUG : ControlWarpCommandsTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : ControlWarpCommandsTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ControlWarpCommandsTest::initTestCase()
PASS   : ControlWarpCommandsTest::requiredCommandsAreRegistered()
PASS   : ControlWarpCommandsTest::listReportsAnUnwarpedClip()
PASS   : ControlWarpCommandsTest::addMoveRemoveEditsTheMap()
PASS   : ControlWarpCommandsTest::setReplacesTheMapAndTheTempoMode()
PASS   : ControlWarpCommandsTest::refusalsAreTypedAndChangeNothing()
PASS   : ControlWarpCommandsTest::aNonSampleClipIsRefusedTyped()
PASS   : ControlWarpCommandsTest::contractRowsClassifyTheGroup()
FAIL!  : ControlWarpCommandsTest::markerEditsAreReversibleThroughTheJournal() Compared values are not the same
   Actual   (markerCountOf(clip)): 1
   Expected (0)                  : 0
   Loc: [/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s9/tests/src/core/ControlWarpCommandsTest.cpp(404)]
PASS   : ControlWarpCommandsTest::cleanupTestCase()
Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted, 1402ms
********* Finished testing of ControlWarpCommandsTest *********


0% tests passed, 2 tests failed out of 2

Total Test time (real) =   3.69 sec

The following tests FAILED:
	 20 - ClipWarpPersistenceTest (Failed)
	 45 - ControlWarpCommandsTest (Failed)
Errors while running CTest

=== REVERT RUN (reset restored, same tree): bash-less rebuild of the 8 targets EXIT=0 (build/build-revert.md) ===
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-s9/build/tests
    Start  18: ClipSerialisationTest
1/8 Test  #18: ClipSerialisationTest ............   Passed    1.41 sec
    Start  19: WriteRefusalGateTest
2/8 Test  #19: WriteRefusalGateTest .............   Passed    0.50 sec
    Start  20: ClipWarpPersistenceTest
3/8 Test  #20: ClipWarpPersistenceTest ..........   Passed    2.20 sec
    Start  45: ControlWarpCommandsTest
4/8 Test  #45: ControlWarpCommandsTest ..........   Passed    1.42 sec
    Start 138: ProjectRevIdsTest
5/8 Test #138: ProjectRevIdsTest ................   Passed    1.60 sec
    Start 148: WarpMarkersTest
6/8 Test #148: WarpMarkersTest ..................   Passed    0.04 sec
    Start 155: SampleClipStretchTest
7/8 Test #155: SampleClipStretchTest ............   Passed    1.53 sec
    Start 156: SampleClipWarpTest
8/8 Test #156: SampleClipWarpTest ...............   Passed    1.60 sec

100% tests passed, 0 tests failed out of 8

Total Test time (real) =  10.31 sec
REVERT_CTEST_EXIT=0
