### gate 4 complexity --check (fork scope)
#TOTAL	6477	72
complexity-gate: 6477 functions in the fork scope; 72 exceed CCN 10

Functions over the CCN target (grandfathered if in the baseline):
  CCN      NLOC   LEN    function
  42       250    283    lmms::interchange::dawProjectXmlFromModel@156-438@src/core/DawProjectWrite.cpp
  41       144    144    lmms::interchange::parseDocument@74-217@src/core/DawProjectRead.cpp
  41       198    214    lmms::ExternalProcessStemSeparator::separate@238-451@src/core/ExternalProcessStemSeparator.cpp
  36       173    178    lmms::interchange::dawProjectZipRead@248-425@src/core/DawProjectZip.cpp
  33       133    153    lmms::interchange::applyDawProjectModel@344-496@src/core/DawProjectSession.cpp
  31       46     49     check_result@325-373@tests/stem_commands_lib.py
  30       87     111    lmms::detection::estimateTempo@236-346@src/core/ImportDetectionDsp.cpp
  28       139    162    lmms::clap::HostedPlugin::load@136-297@plugins/ClapEffect/ClapHost.cpp
  27       124    144    lmms::vst3::HostedPlugin::load@284-427@plugins/Vst3Effect/Vst3Host.cpp
  27       135    151    lmms::OnnxRuntimeStemSeparator::separate@91-241@src/core/OnnxRuntimeStemSeparator.cpp
  27       86     111    lmms::ScriptEngine::applyCommand@501-611@src/core/ScriptEngine.cpp
  26       97     98     main@399-496@tests/control-golden-audio.py
  25       52     62     check_id_contract@402-463@tests/control-stable-ids-slice2.py
  23       64     91     lmms::AudioPortsModel::updateDirectRouting@247-337@src/core/AudioPortsModel.cpp
  23       64     73     lmms::interchange::readdetail::parseClips@107-179@src/core/DawProjectReadTracks.cpp
  23       131    163    lmms::interchange::dawProjectModelFromSong@180-342@src/core/DawProjectSession.cpp
  23       70     86     lmms::detection::estimateKey@48-133@src/core/ImportDetectionKey.cpp
  22       86     104    lmms::wasm::renderOffline@241-344@src/wasm/WasmOfflineRender.cpp
  22       71     77     compare@212-288@tests/golden_audio_lib.py
  18       36     36     lmms::vst3::HostedPlugin::loadState@544-579@plugins/Vst3Effect/Vst3Host.cpp
  18       109    129    lmms::control::meterMeasureFile@125-253@src/core/ControlCommandsMeterFile.cpp
  18       102    121    main@72-192@tests/golden_audio_selftest.py
  17       37     39     lmms::followTargetScene@226-264@include/SessionFollow.h
  17       80     85     lmms::AudioBus::update@195-279@src/core/AudioBus.cpp
  17       156    170    lmms::registerApplyCommand@57-226@src/core/ControlCommandsDetectApply.cpp
  17       37     37     lmms::interchange::readdetail::parseLanes@184-220@src/core/DawProjectReadTracks.cpp
  17       46     72     compare_fingerprints@211-282@tests/golden_audio_record.py
  16       81     87     lmms::randomize@256-342@src/core/ControlCommandsNoteRandom.cpp
  16       36     38     check_framing_and_ids@292-329@tests/control-named-pipe-smoke.py
  15       58     64     main@20-83@plugins/RnnoiseDenoiser/testdata/rnn_harness.c
  15       64     80     lmms::AudioBus::sanitize@96-175@src/core/AudioBus.cpp
  15       9      9      lmms::interchange::DawProjectTrack::operator ==@157-165@src/core/DawProjectModel.cpp
  15       57     57     lmms::interchange::dawProjectModelDigest@201-257@src/core/DawProjectModel.cpp
  15       35     38     lmms::RoutingGraph::connect@90-127@src/core/RoutingGraph.cpp
  15       42     53     lmms::RoutingGraph::rebuildPlan@310-362@src/core/RoutingGraph.cpp
  15       75     85     lmms::wasm::WasmSandbox::loadModuleBytes@336-420@src/wasm/WasmSandbox.cpp
  15       14     16     check_transaction@119-134@tests/control-pitch-stretch-transcript.py
  15       67     80     main@139-218@tests/control-stem-commands.py
  14       80     98     lmms::vst3::HostedPlugin::prepare@581-678@plugins/Vst3Effect/Vst3Host.cpp
  14       62     83     lmms::vst3::HostedPlugin::Impl::runChunk@789-871@plugins/Vst3Effect/Vst3Host.cpp
  14       63     74     lmms::controlExportPresetFromBytes@222-295@src/core/ControlExportPresetSupport.cpp
  14       44     48     lmms::ScriptEngine::resolveProjectPath@769-816@src/core/ScriptEngine.cpp
  14       84     119    lmms::SessionScheduler::evaluateFollow@191-309@src/core/SessionFollow.cpp
  14       96     130    lmms::gui::PinConnector::paintEvent@184-313@src/gui/PinConnector.cpp
  14       39     55     build_fixture@124-178@tests/control-golden-audio.py
  14       37     38     bound_sweep@291-328@tests/control-golden-audio.py
  13       56     62     lmms::followActionFromJson@77-138@src/core/ControlCommandsSessionFollow.cpp
  13       47     51     lmms::control::readScaleRoot@108-158@src/core/ControlScaleSupport.cpp
  13       68     76     lmms::wasm::renderInline@70-145@src/wasm/WasmOfflineRender.cpp
  13       28     33     check_refusals@332-364@tests/control-named-pipe-smoke.py
  13       20     21     check_transactions@415-435@tests/control-render-presets.py
  12       31     31     lmms::pickFollowIndex@182-212@include/SessionFollow.h
  12       40     51     lmms::control::addressableParameterForModel@252-302@src/core/ControlAutomationSupport.cpp
  12       41     44     lmms::automationRampGet@227-270@src/core/ControlCommandsAutomationRamp.cpp
  12       48     49     lmms::sessionrecord::landPairEvents@91-139@src/core/ControlCommandsSessionRecordInternal.h
  12       81     88     lmms::handleRenderOffline@191-278@src/core/ControlCommandsWasmRender.cpp
  12       44     46     lmms::interchange::readDawProject@267-312@src/core/DawProjectRead.cpp
  12       96     102    lmms::interchange::dawProjectZipWrite@145-246@src/core/DawProjectZip.cpp
  12       35     37     lmms::detection::refinedPeriodHops@118-154@src/core/ImportDetectionDsp.cpp
  12       68     80     lmms::wasm::WasmWorker::processSlot@200-279@src/wasm/WasmWorker.cpp
  12       63     69     run@388-456@tests/control-named-pipe-smoke.py
  11       35     45     lmms::ScriptMemoryState::allocate@77-121@include/ScriptMemoryBudget.h
  11       22     22     lmms::clap::describe@56-77@plugins/ClapEffect/ClapHost.cpp
  11       35     41     lmms::AudioPortsModel::setAllChannelCounts@65-105@src/core/AudioPortsModel.cpp
  11       106    113    lmms::registerIdContractCommand@52-164@src/core/ControlCommandsIdContract.cpp
  11       69     84     lmms::renderSession@274-357@src/core/ControlCommandsProject.cpp
  11       74     84     lmms::registerFollowGetState@332-415@src/core/ControlCommandsSessionFollow.cpp
  11       124    133    lmms::registerRecordLand@79-211@src/core/ControlCommandsSessionRecordLand.cpp
  11       53     56     lmms::control::stemModelDownload@104-159@src/core/ControlStemModel.cpp
  11       71     80     lmms::StemModelStore::download@203-282@src/core/StemModelStore.cpp
  11       24     25     wav_facts@126-150@tests/stem_commands_lib.py
  11       40     42     check_model_store@400-441@tests/stem_commands_lib.py

Nesting depth (lizard cannot threshold it — reported for manual review):
include/SessionFollow.h: lmms::pickFollowIndex has 31 NLOC, 12 CCN, 199 token, 2 PARAM, 31 length, 0 ND
include/SessionFollow.h: lmms::followTargetScene has 37 NLOC, 17 CCN, 271 token, 5 PARAM, 39 length, 0 ND
plugins/ClapEffect/ClapHost.cpp: lmms::clap::HostedPlugin::load has 139 NLOC, 28 CCN, 1306 token, 3 PARAM, 162 length, 0 ND
plugins/RnnoiseDenoiser/testdata/rnn_harness.c: main has 58 NLOC, 15 CCN, 575 token, 2 PARAM, 64 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::load has 124 NLOC, 27 CCN, 1216 token, 3 PARAM, 144 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::loadState has 36 NLOC, 18 CCN, 288 token, 2 PARAM, 36 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::prepare has 80 NLOC, 14 CCN, 789 token, 3 PARAM, 98 length, 0 ND
plugins/Vst3Effect/Vst3Host.cpp: lmms::vst3::HostedPlugin::Impl::runChunk has 62 NLOC, 14 CCN, 469 token, 8 PARAM, 83 length, 0 ND
src/core/AudioBus.cpp: lmms::AudioBus::sanitize has 64 NLOC, 15 CCN, 401 token, 1 PARAM, 80 length, 0 ND
src/core/AudioBus.cpp: lmms::AudioBus::update has 80 NLOC, 17 CCN, 468 token, 2 PARAM, 85 length, 0 ND
src/core/AudioPortsModel.cpp: lmms::AudioPortsModel::updateDirectRouting has 64 NLOC, 23 CCN, 369 token, 0 PARAM, 91 length, 0 ND
src/core/ControlAutomationSupport.cpp: lmms::control::addressableParameterForModel has 40 NLOC, 12 CCN, 237 token, 2 PARAM, 51 length, 0 ND
src/core/ControlCommandsAutomationRamp.cpp: lmms::automationRampGet has 41 NLOC, 12 CCN, 345 token, 1 PARAM, 44 length, 0 ND
src/core/ControlCommandsDetectApply.cpp: lmms::registerApplyCommand has 156 NLOC, 17 CCN, 1150 token, 1 PARAM, 170 length, 0 ND
src/core/ControlCommandsMeterFile.cpp: lmms::control::meterMeasureFile has 109 NLOC, 18 CCN, 981 token, 1 PARAM, 129 length, 0 ND
src/core/ControlCommandsNoteRandom.cpp: lmms::randomize has 81 NLOC, 16 CCN, 850 token, 1 PARAM, 87 length, 0 ND
src/core/ControlCommandsSessionFollow.cpp: lmms::followActionFromJson has 56 NLOC, 13 CCN, 396 token, 4 PARAM, 62 length, 0 ND
src/core/ControlCommandsSessionRecordInternal.h: lmms::sessionrecord::landPairEvents has 48 NLOC, 12 CCN, 336 token, 8 PARAM, 49 length, 0 ND
src/core/ControlCommandsWasmRender.cpp: lmms::handleRenderOffline has 81 NLOC, 12 CCN, 850 token, 1 PARAM, 88 length, 0 ND
src/core/ControlExportPresetSupport.cpp: lmms::controlExportPresetFromBytes has 63 NLOC, 14 CCN, 559 token, 4 PARAM, 74 length, 0 ND

PASS (check mode: no regressions; baseline not written)
mavis-trash: moved to trash: '/tmp/tmp.d3QmAik7tT'
mavis-trash: moved to trash: '/tmp/tmp.d3QmAik7tT.over'
G4_FORK_EXIT=0

### gate 4 complexity --check --scope tools
complexity-gate: tools scope (fork-owned developer tooling; its own baseline, separate from the product ratchets)
#TOTAL	727	13
complexity-gate: 727 functions in the tools scope; 13 exceed CCN 10

Functions over the CCN target (grandfathered if in the baseline):
  CCN      NLOC   LEN    function
  47       136    140    cmd_audible_diff@1578-1717@tools/mmpz-git/mmpz_git.py
  34       54     64     diff_dom@410-473@tools/mmpz-git/mmpz_git.py
  30       109    114    main@73-186@tools/mmpz-git/demo_edits.py
  28       56     64     serialize@191-254@tools/mmpz-git/mmpz_git.py
  20       43     44     main@25-68@tools/mmpz-git/demo_check.py
  20       37     40     canonicalize_dom.walk@296-335@tools/mmpz-git/mmpz_git.py
  19       41     42     read_wav@69-110@tools/stem_split_cli.py
  16       37     37     save_node@9-45@tools/mmpz-git/scratch/proto.py
  13       24     25     marker_records@1286-1310@tools/mmpz-git/mmpz_git.py
  13       70     80     main@226-305@tools/stem_split_cli.py
  12       26     26     cmd_diff@476-501@tools/mmpz-git/mmpz_git.py
  11       40     55     _conflict_comment@838-892@tools/mmpz-git/mmpz_git.py
  11       28     29     wav_mono@1407-1435@tools/mmpz-git/mmpz_git.py

Nesting depth (lizard cannot threshold it — reported for manual review):
tools/mmpz-git/demo_check.py: main has 43 NLOC, 20 CCN, 325 token, 1 PARAM, 44 length, 0 ND
tools/mmpz-git/demo_edits.py: main has 109 NLOC, 30 CCN, 1124 token, 0 PARAM, 114 length, 0 ND
tools/mmpz-git/mmpz_git.py: serialize has 56 NLOC, 28 CCN, 498 token, 4 PARAM, 64 length, 0 ND
tools/mmpz-git/mmpz_git.py: canonicalize_dom.walk has 37 NLOC, 20 CCN, 218 token, 1 PARAM, 40 length, 0 ND
tools/mmpz-git/mmpz_git.py: diff_dom has 54 NLOC, 34 CCN, 611 token, 5 PARAM, 64 length, 0 ND
tools/mmpz-git/mmpz_git.py: cmd_diff has 26 NLOC, 12 CCN, 237 token, 1 PARAM, 26 length, 0 ND
tools/mmpz-git/mmpz_git.py: marker_records has 24 NLOC, 13 CCN, 223 token, 1 PARAM, 25 length, 0 ND
tools/mmpz-git/mmpz_git.py: cmd_audible_diff has 136 NLOC, 47 CCN, 1194 token, 1 PARAM, 140 length, 0 ND
tools/mmpz-git/scratch/proto.py: save_node has 37 NLOC, 16 CCN, 329 token, 3 PARAM, 37 length, 0 ND
tools/stem_split_cli.py: read_wav has 41 NLOC, 19 CCN, 440 token, 1 PARAM, 42 length, 0 ND
tools/stem_split_cli.py: main has 70 NLOC, 13 CCN, 567 token, 0 PARAM, 80 length, 0 ND

PASS (check mode: no regressions; baseline not written)
mavis-trash: moved to trash: '/tmp/tmp.yTAguyXKYB'
mavis-trash: moved to trash: '/tmp/tmp.yTAguyXKYB.over'
G4_TOOLS_EXIT=0
