# Parent review and preserved scale transcripts — 2026-10-06

Source fix58cf719ea safely checks finite0..11 before narrowing; existing fractional rejection remains.
Parent independently reran ctest from build/tests after the committed fix: EXIT=0,1/1 executable passed.
Lane-authored RED/GREEN below are preserved raw build transcripts, distinct from parent rerun.
No full suite/gates or other platforms were run. Linux x86-64 exception check has no loud nonLinux skip;
its test structure needs a separate platform-aware case before lane acceptance. Source implementation is sound.

## luna-red-final-build.log
Original SHA256 `c3097801f1c4fa458f9b06b76e3db8ea56320c7d58d8ccb9d1f3c5d18a47a295`.

```text
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target weakjack
[  0%] Built target lua
[  0%] Built target zene_api_autogen
[  4%] Built target zene_api
[  4%] Built target lmmsobjs_autogen_timestamp_deps
[  4%] Automatic MOC and UIC for target lmmsobjs
[  4%] Built target lmmsobjs_autogen
[  4%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlScaleSupport.cpp.o
[ 95%] Built target lmmsobjs
[ 95%] Built target zene_autogen_timestamp_deps
[ 95%] Built target zene_autogen
[ 95%] Linking CXX executable ../zene
[100%] Built target zene
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target weakjack
[  0%] Built target lua
[  0%] Built target zene_api_autogen
[  4%] Built target zene_api
[  4%] Built target lmmsobjs_autogen_timestamp_deps
[  4%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target ControlNoteScaleVerbsTest_autogen_timestamp_deps
[100%] Automatic MOC for target ControlNoteScaleVerbsTest
[100%] Built target ControlNoteScaleVerbsTest_autogen
[100%] Building CXX object tests/CMakeFiles/ControlNoteScaleVerbsTest.dir/src/core/ControlNoteScaleVerbsTest.cpp.o
[100%] Linking CXX executable ControlNoteScaleVerbsTest
[100%] Built target ControlNoteScaleVerbsTest

```

## luna-red-final-ctest.log
Original SHA256 `dcf42f7fad83d2fb628ca7995f77a721bc985e90fd27b63cff9ff8335a81de2b`.

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-control/build/tests
    Start 45: ControlNoteScaleVerbsTest
1/1 Test #45: ControlNoteScaleVerbsTest ........***Failed    1.35 sec
********* Start testing of ControlNoteScaleVerbsTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 114 msecs.
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ControlNoteScaleVerbsTest::initTestCase()
PASS   : ControlNoteScaleVerbsTest::everyVerbIsRegisteredWithSchemasAndAContractRow()
PASS   : ControlNoteScaleVerbsTest::theSeedIsPersistedAndItsUndoRestoresIt()
PASS   : ControlNoteScaleVerbsTest::theRollIsRepeatableWithOneSeedAndDifferentWithAnother()
PASS   : ControlNoteScaleVerbsTest::theRollRefusesWhatItCannotMean()
PASS   : ControlNoteScaleVerbsTest::slideSetAndClearAreOneUndoableStepEach()
PASS   : ControlNoteScaleVerbsTest::clearingSlidesOnAClipWithoutAnyRecordsNoUndoStep()
PASS   : ControlNoteScaleVerbsTest::theThreeTransformsMoveNotesAndUndoPutsThemBack()
FAIL!  : ControlNoteScaleVerbsTest::malformedCustomScaleEntriesAreIgnored() Compared values are not the same
   Actual   (invalidExceptions): 1
   Expected (0)                : 0
   Loc: [../../tests/src/core/ControlNoteScaleVerbsTest.cpp(546)]
PASS   : ControlNoteScaleVerbsTest::theScaleVocabularyResolvesThroughTheEngine()
PASS   : ControlNoteScaleVerbsTest::theScaleContextIsReversibleAndSnapRefusesWithoutOne()
PASS   : ControlNoteScaleVerbsTest::snapMovesOnlyTheOutOfScaleNotesAndTheCheckpointTakesThemBack()
PASS   : ControlNoteScaleVerbsTest::theMpeSwitchFlipsAndUndoRestoresIt()
PASS   : ControlNoteScaleVerbsTest::theDetectedKeyVerbAdvertisesAnInverseThatRestoresTheContext()
PASS   : ControlNoteScaleVerbsTest::cleanupTestCase()
Totals: 14 passed, 1 failed, 0 skipped, 0 blacklisted, 1281ms
********* Finished testing of ControlNoteScaleVerbsTest *********


0% tests passed, 1 tests failed out of 1

Total Test time (real) =   1.36 sec

The following tests FAILED:
	 45 - ControlNoteScaleVerbsTest (Failed)
Errors while running CTest

```

## luna-green-final-build.log
Original SHA256 `71efa9bd3374c36dc3bdcc13b2b5edc1a1b3a142573c792354f9d18baf277c08`.

```text
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target ringbuffer
[  0%] Built target weakjack
[  0%] Built target lua
[  0%] Built target zene_api_autogen
[  4%] Built target zene_api
[  4%] Built target lmmsobjs_autogen_timestamp_deps
[  4%] Automatic MOC and UIC for target lmmsobjs
[  4%] Built target lmmsobjs_autogen
[  4%] Building CXX object src/CMakeFiles/lmmsobjs.dir/core/ControlScaleSupport.cpp.o
[ 95%] Built target lmmsobjs
[ 95%] Built target zene_autogen_timestamp_deps
[ 95%] Built target zene_autogen
[ 95%] Linking CXX executable ../zene
[100%] Built target zene
[  0%] Built target zene_api_autogen_timestamp_deps
[  0%] Built target weakjack
[  0%] Built target ringbuffer
[  0%] Built target lua
[  0%] Built target zene_api_autogen
[  4%] Built target zene_api
[  4%] Built target lmmsobjs_autogen_timestamp_deps
[  4%] Built target lmmsobjs_autogen
[100%] Built target lmmsobjs
[100%] Built target ControlNoteScaleVerbsTest_autogen_timestamp_deps
[100%] Built target ControlNoteScaleVerbsTest_autogen
[100%] Linking CXX executable ControlNoteScaleVerbsTest
[100%] Built target ControlNoteScaleVerbsTest

```

## luna-green-final-ctest.log
Original SHA256 `4faff8feeb9bd56bf1f65682f767ce8a6fc9bc91759858ff4e4839dd27b9bd02`.

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-control/build/tests
    Start 45: ControlNoteScaleVerbsTest
1/1 Test #45: ControlNoteScaleVerbsTest ........   Passed    1.34 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   1.35 sec

```

## luna-green-direct.log
Original SHA256 `faf18c9e2bfb03dec5f43116b38ec6c357bcdc4f17c2f979cf3f4b00f46541d5`.

```text
********* Start testing of ControlNoteScaleVerbsTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 114 msecs.
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() For details about not loaded plugins, please set
  environment variable "LMMS_LV2_DEBUG" to nonempty.
QDEBUG : ControlNoteScaleVerbsTest::initTestCase() Blocked Lv2 Plugins: 10 of 87 
  If you want to enable them (dangerous!), please set
  environment variable "LMMS_ENABLE_BLOCKED_PLUGINS" to nonempty.
PASS   : ControlNoteScaleVerbsTest::initTestCase()
PASS   : ControlNoteScaleVerbsTest::everyVerbIsRegisteredWithSchemasAndAContractRow()
PASS   : ControlNoteScaleVerbsTest::theSeedIsPersistedAndItsUndoRestoresIt()
PASS   : ControlNoteScaleVerbsTest::theRollIsRepeatableWithOneSeedAndDifferentWithAnother()
PASS   : ControlNoteScaleVerbsTest::theRollRefusesWhatItCannotMean()
PASS   : ControlNoteScaleVerbsTest::slideSetAndClearAreOneUndoableStepEach()
PASS   : ControlNoteScaleVerbsTest::clearingSlidesOnAClipWithoutAnyRecordsNoUndoStep()
PASS   : ControlNoteScaleVerbsTest::theThreeTransformsMoveNotesAndUndoPutsThemBack()
PASS   : ControlNoteScaleVerbsTest::malformedCustomScaleEntriesAreIgnored()
PASS   : ControlNoteScaleVerbsTest::theScaleVocabularyResolvesThroughTheEngine()
PASS   : ControlNoteScaleVerbsTest::theScaleContextIsReversibleAndSnapRefusesWithoutOne()
PASS   : ControlNoteScaleVerbsTest::snapMovesOnlyTheOutOfScaleNotesAndTheCheckpointTakesThemBack()
PASS   : ControlNoteScaleVerbsTest::theMpeSwitchFlipsAndUndoRestoresIt()
PASS   : ControlNoteScaleVerbsTest::theDetectedKeyVerbAdvertisesAnInverseThatRestoresTheContext()
PASS   : ControlNoteScaleVerbsTest::cleanupTestCase()
Totals: 15 passed, 0 failed, 0 skipped, 0 blacklisted, 1276ms
********* Finished testing of ControlNoteScaleVerbsTest *********

```

## luna-complexity-check.log
Original SHA256 `fa1ee6bf1dc8b5f14a474f8f3095dd26f6142a5ba361e53874b5a4cc7c4921f0`.

```text
#TOTAL	14005	68
complexity-gate: 14005 functions in the fork scope; 68 exceed CCN 10

Functions over the CCN target (grandfathered if in the baseline):
  CCN      NLOC   LEN    function
  39       187    203    lmms::ExternalProcessStemSeparator::separate@258-460@src/core/ExternalProcessStemSeparator.cpp
  37       125    130    lmms::interchange::parseDocument@182-311@src/core/DawProjectRead.cpp
  33       160    166    lmms::interchange::dawProjectZipRead@372-537@src/core/DawProjectZip.cpp
  32       109    138    lmms::interchange::applyDawProjectModel@469-606@src/core/DawProjectSession.cpp
  32       212    248    lmms::interchange::dawProjectXmlFromModel@322-569@src/core/DawProjectWrite.cpp
  31       46     49     check_result@328-376@tests/stem_commands_lib.py
  27       86     111    lmms::ScriptEngine::applyCommand@501-611@src/core/ScriptEngine.cpp
  26       97     98     main@399-496@tests/control-golden-audio.py
  25       129    144    lmms::OnnxRuntimeStemSeparator::separate@145-288@src/core/OnnxRuntimeStemSeparator.cpp
  25       52     62     check_id_contract@402-463@tests/control-stable-ids-slice2.py
  23       65     96     lmms::AudioPortsModel::updateDirectRouting@251-346@src/core/AudioPortsModel.cpp
  23       64     73     lmms::interchange::readdetail::parseClips@107-179@src/core/DawProjectReadTracks.cpp
  23       133    168    lmms::interchange::dawProjectModelFromSong@231-398@src/core/DawProjectSession.cpp
  23       70     88     lmms::detection::estimateKey@48-135@src/core/ImportDetectionKey.cpp
  22       86     106    lmms::vst3::HostedPlugin::load@200-305@plugins/Vst3Effect/Vst3Host.cpp
  22       86     104    lmms::wasm::renderOffline@251-354@src/wasm/WasmOfflineRender.cpp
  22       71     77     compare@212-288@tests/golden_audio_lib.py
  18       36     36     lmms::vst3::HostedPlugin::loadState@424-459@plugins/Vst3Effect/Vst3Host.cpp
  18       111    133    lmms::control::meterMeasureFile@140-272@src/core/ControlCommandsMeterFile.cpp
  18       102    121    main@72-192@tests/golden_audio_selftest.py
  17       37     39     lmms::followTargetScene@226-264@include/SessionFollow.h
  17       84     108    lmms::clap::HostedPlugin::load@178-285@plugins/ClapEffect/ClapHost.cpp
  17       80     85     lmms::AudioBus::update@195-279@src/core/AudioBus.cpp
  17       156    170    lmms::registerApplyCommand@57-226@src/core/ControlCommandsDetectApply.cpp
  17       46     72     compare_fingerprints@211-282@tests/golden_audio_record.py
  16       36     38     check_framing_and_ids@292-329@tests/control-named-pipe-smoke.py
  15       64     80     lmms::AudioBus::sanitize@96-175@src/core/AudioBus.cpp
  15       9      9      lmms::interchange::DawProjectTrack::operator ==@223-231@src/core/DawProjectModel.cpp
  15       20     20     lmms::interchange::readdetail::parseLanes@206-225@src/core/DawProjectReadTracks.cpp
  15       35     38     lmms::RoutingGraph::connect@144-181@src/core/RoutingGraph.cpp
  15       45     61     lmms::RoutingGraph::rebuildPlan@360-420@src/core/RoutingGraph.cpp
  15       75     85     lmms::wasm::WasmSandbox::loadModuleBytes@336-420@src/wasm/WasmSandbox.cpp
  15       14     16     check_transaction@119-134@tests/control-pitch-stretch-transcript.py
  15       68     83     main@141-223@tests/control-stem-commands.py
  14       43     50     main@51-100@plugins/RnnoiseDenoiser/testdata/rnn_harness.c
  14       67     87     lmms::vst3::HostedPlugin::prepare@461-547@plugins/Vst3Effect/Vst3Host.cpp
  14       63     74     lmms::controlExportPresetFromBytes@224-297@src/core/ControlExportPresetSupport.cpp
  14       58     58     lmms::interchange::dawProjectModelDigest@301-358@src/core/DawProjectModel.cpp
  14       44     48     lmms::ScriptEngine::resolveProjectPath@769-816@src/core/ScriptEngine.cpp
  14       96     130    lmms::gui::PinConnector::paintEvent@184-313@src/gui/PinConnector.cpp
  14       39     55     build_fixture@124-178@tests/control-golden-audio.py
  14       37     38     bound_sweep@291-328@tests/control-golden-audio.py
  13       56     62     lmms::followActionFromJson@80-141@src/core/ControlCommandsSessionFollow.cpp
  13       47     53     lmms::control::readScaleRoot@150-202@src/core/ControlScaleSupport.cpp
  13       68     76     lmms::wasm::renderInline@70-145@src/wasm/WasmOfflineRender.cpp
  13       28     33     check_refusals@332-364@tests/control-named-pipe-smoke.py
  13       20     21     check_transactions@415-435@tests/control-render-presets.py
  12       31     31     lmms::pickFollowIndex@182-212@include/SessionFollow.h
  12       40     51     lmms::control::addressableParameterForModel@255-305@src/core/ControlAutomationSupport.cpp
  12       41     44     lmms::automationRampGet@238-281@src/core/ControlCommandsAutomationRamp.cpp
  12       66     78     lmms::randomize@299-376@src/core/ControlCommandsNoteRandom.cpp
  12       48     49     lmms::sessionrecord::landPairEvents@91-139@src/core/ControlCommandsSessionRecordInternal.h
  12       81     88     lmms::handleRenderOffline@191-278@src/core/ControlCommandsWasmRender.cpp
  12       44     46     lmms::interchange::readDawProject@361-406@src/core/DawProjectRead.cpp
  12       87     96     lmms::interchange::dawProjectZipWrite@275-370@src/core/DawProjectZip.cpp
  12       35     37     lmms::detection::refinedPeriodHops@118-154@src/core/ImportDetectionDsp.cpp
  12       41     55     lmms::detection::estimateTempo@347-401@src/core/ImportDetectionDsp.cpp
  12       68     80     lmms::wasm::WasmWorker::processSlot@200-279@src/wasm/WasmWorker.cpp
  12       63     69     run@388-456@tests/control-named-pipe-smoke.py
  11       35     45     lmms::ScriptMemoryState::allocate@77-121@include/ScriptMemoryBudget.h
  11       22     22     lmms::clap::describe@56-77@plugins/ClapEffect/ClapHost.cpp
  11       35     41     lmms::AudioPortsModel::setAllChannelCounts@65-105@src/core/AudioPortsModel.cpp
  11       106    113    lmms::registerIdContractCommand@52-164@src/core/ControlCommandsIdContract.cpp
  11       69     84     lmms::renderSession@310-393@src/core/ControlCommandsProject.cpp
  11       81     91     lmms::registerFollowGetState@428-518@src/core/ControlCommandsSessionFollow.cpp
  11       124    133    lmms::registerRecordLand@79-211@src/core/ControlCommandsSessionRecordLand.cpp
  11       72     110    lmms::SessionScheduler::evaluateFollow@243-352@src/core/SessionFollow.cpp
  11       24     25     wav_facts@126-150@tests/stem_commands_lib.py

Nesting depth (lizard cannot threshold it — reported for manual review):
include/SessionFollow.h: lmms::pickFollowIndex has 31 NLOC, 12 CCN, 199 token, 2 PARAM, 31 length, 0 ND
include/SessionFollow.h: lmms::followTargetScene has 37 NLOC, 17 CCN, 271 token, 5 PARAM, 39 length, 0 ND
plugins/ClapEffect/ClapHost.cpp: lmms::clap::HostedPlugin::load has 84 NLOC, 17 CCN, 712 token, 3 PARAM, 108 length, 0 ND
plugins/RnnoiseDenoiser/testdata/rnn_harness.c: main has 43 NLOC, 14 CCN, 465 token, 2 PARAM, 50 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::load has 86 NLOC, 22 CCN, 1001 token, 3 PARAM, 106 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::loadState has 36 NLOC, 18 CCN, 288 token, 2 PARAM, 36 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::prepare has 67 NLOC, 14 CCN, 819 token, 3 PARAM, 87 length, 0 ND
src/core/AudioBus.cpp: lmms::AudioBus::sanitize has 64 NLOC, 15 CCN, 401 token, 1 PARAM, 80 length, 0 ND
src/core/AudioBus.cpp: lmms::AudioBus::update has 80 NLOC, 17 CCN, 468 token, 2 PARAM, 85 length, 0 ND
src/core/AudioPortsModel.cpp: lmms::AudioPortsModel::updateDirectRouting has 65 NLOC, 23 CCN, 373 token, 0 PARAM, 96 length, 0 ND
src/core/ControlAutomationSupport.cpp: lmms::control::addressableParameterForModel has 40 NLOC, 12 CCN, 237 token, 2 PARAM, 51 length, 0 ND
src/core/ControlCommandsAutomationRamp.cpp: lmms::automationRampGet has 41 NLOC, 12 CCN, 345 token, 1 PARAM, 44 length, 0 ND
src/core/ControlCommandsDetectApply.cpp: lmms::registerApplyCommand has 156 NLOC, 17 CCN, 1150 token, 1 PARAM, 170 length, 0 ND
src/core/ControlCommandsMeterFile.cpp: lmms::control::meterMeasureFile has 111 NLOC, 18 CCN, 997 token, 1 PARAM, 133 length, 0 ND
src/core/ControlCommandsNoteRandom.cpp: lmms::randomize has 66 NLOC, 12 CCN, 659 token, 1 PARAM, 78 length, 0 ND
src/core/ControlCommandsSessionFollow.cpp: lmms::followActionFromJson has 56 NLOC, 13 CCN, 396 token, 4 PARAM, 62 length, 0 ND
src/core/ControlCommandsSessionRecordInternal.h: lmms::sessionrecord::landPairEvents has 48 NLOC, 12 CCN, 336 token, 8 PARAM, 49 length, 0 ND
src/core/ControlCommandsWasmRender.cpp: lmms::handleRenderOffline has 81 NLOC, 12 CCN, 850 token, 1 PARAM, 88 length, 0 ND
src/core/ControlExportPresetSupport.cpp: lmms::controlExportPresetFromBytes has 63 NLOC, 14 CCN, 559 token, 4 PARAM, 74 length, 0 ND
src/core/ControlScaleSupport.cpp: lmms::control::readScaleRoot has 47 NLOC, 13 CCN, 457 token, 4 PARAM, 53 length, 0 ND

improved: lmms::vst3::HostedPlugin::Impl::runChunk@plugins/Vst3Effect/Vst3Host.cpp is no longer over CCN 10 (baseline entry is dead weight; remove it deliberately)
PASS (check mode: no regressions; baseline not written)

```

## luna-file-length-check.log
Original SHA256 `92e53797bf8665909cd427baf06feeb2a441d3160e0beb9d2256963a6116724c`.

```text
file-length-gate: 1510 fork-scope sources measured; 5 exceed 500 code lines

  lines  file
  928    src/core/ScriptBindings.cpp
  662    src/core/ScriptEngine.cpp
  644    include/AudioPorts.h
  613    plugins/Vst3Effect/Vst3Host.cpp
  512    src/wasm/WasmSandbox.cpp

PASS (check mode: no regressions; baseline not written)

```

## luna-parent-scale-2026-10-06.log
Original SHA256 `5d5dbcf92cffcdc86f215c97b2bc0174a8bc1e9062ce5aab64920f0c0a1c796a`.

```text
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-bunny-control/build/tests
    Start 45: ControlNoteScaleVerbsTest
1/1 Test #45: ControlNoteScaleVerbsTest ........   Passed    1.34 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   1.36 sec

```
