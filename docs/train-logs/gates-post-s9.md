# Text-gate battery on tip 1253ca7d9 (post-s9, post-feedback-fix), 2026-09-22T19:29:25Z

file-length-gate: 707 fork-scope sources measured; 23 exceed 500 lines

  lines  file
  1174   src/core/ScriptBindings.cpp
  992    include/AudioPorts.h
  891    plugins/Vst3Effect/Vst3Host.cpp
  880    src/core/ScriptEngine.cpp
  732    include/ControlRegistryGroups.h
  597    src/wasm/WasmSandbox.cpp
  594    tests/zene-api-boundary.py
  574    tests/control-socket-path-safety.py
  573    src/core/ProjectContainerEntries.cpp
  568    tests/control-retro-capture.py
  549    tests/control-stable-ids-slice2.py
  549    src/core/AudioPortsModel.cpp
  544    include/ControlReversibility.h
  529    src/gui/PinConnector.cpp
  526    tests/control-undo-structural.py
  518    src/core/ControlReversibilityTablePassive.cpp
  517    tests/src/core/DataFileSaveIntegrityTest.cpp
  512    tests/control_socket_harness.py
  512    src/core/ControlCommandsNotes.cpp
  511    tests/src/tracks/SampleClipWindowTest.cpp

PASS (check mode: no regressions; baseline not written)
mavis-trash: moved to trash: '/tmp/tmp.N5vG5aOgRl'
file-length EXIT=0
#TOTAL	6709	72
complexity-gate: 6709 functions in the fork scope; 72 exceed CCN 10

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
  15       35     38     lmms::RoutingGraph::connect@125-162@src/core/RoutingGraph.cpp
  15       45     61     lmms::RoutingGraph::rebuildPlan@347-407@src/core/RoutingGraph.cpp
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
mavis-trash: moved to trash: '/tmp/tmp.LqwN0KYto5'
mavis-trash: moved to trash: '/tmp/tmp.LqwN0KYto5.over'
complexity EXIT=0
duplication-gate: scanning 708 fork sources (min-lines 25, min-tokens 120, threshold 5%)

PASS: duplicated lines 0.50% (budget 5%)
duplication EXIT=0

REPRODUCES: the entry list in all-sources.txt is the recipe's own output.
mavis-trash: moved to trash: '/tmp/tmp.3SwUoHZDCe'

scanned 1789 tracked source file(s) under src/, include/, plugins/, tests/, tools/, modules/;
  708 fork-sources entry(ies), 1110 all-sources (whole-tree),
  48 tools-sources (fork tooling), 0 stale entry(ies).

PASS: every tracked source in scope is registered (708 fork-NEW, 1110 inherited, 48 tooling).
fork-sources(G9) EXIT=0
changed file                                             verdict
Brewfile                                                 declared divergence -> #609 CI: brew "mda-lv2" so the macOS runners have the LV2 bundle PluginPortsMigrationTest renders by URI (3d87d33ca)
cmake/apple/background@2x.png                            declared divergence -> brand placeholders: DMG background (2x) was byte-identical to upstream's; replaced with a hand-authored placeholder card - see docs/BRAND-PLACEHOLDERS.md
cmake/apple/background.png                               declared divergence -> brand placeholders: DMG background was byte-identical to upstream's; replaced with a hand-authored placeholder card - see docs/BRAND-PLACEHOLDERS.md
cmake/apple/CMakeLists.txt                               build/config (allowed)
cmake/apple/icon.icns                                    declared divergence -> brand placeholders: bundle icon rebuilt from the hand-authored app icon as PNG chunks (ic11/ic12/ic07/ic13/ic09); upstream's legacy ic04-ic06 raw chunks are dropped, macOS rendering not verifiable on this Linux host - see docs/BRAND-PLACEHOLDERS.md
cmake/apple/MacDeployQt.cmake                            build/config (allowed)
cmake/apple/project.icns                                 declared divergence -> brand placeholders: bundle project-file icon rebuilt from the hand-authored document+note icon as PNG chunks; upstream's legacy ic04-ic06 raw chunks are dropped - see docs/BRAND-PLACEHOLDERS.md
cmake/apple/zene.plist.in                                declared divergence -> wave R rename (018d2041f): cmake/apple/lmms.plist.in -> cmake/apple/zene.plist.in -- the macOS bundle plist is renamed to match the renamed bundle and executable. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: macOS bundle identifier and CFBundle name/exe follow the renamed product
cmake/CMakeLists.txt                                     build/config (allowed)
cmake/install/CMakeLists.txt                             build/config (allowed)
cmake/install/excludelist-win                            declared divergence -> tag v0.2.1-alpha CI fix: the mingw64 packaging step fails hard on a system DLL that moves into the executable's import set with the VST3 host built (InstallDependencies.cmake:180 'Can't resolve dependency CRYPT32.dll' -> FATAL_ERROR). CRYPT32.dll is a Windows system library, exactly what this list exists to name, so it is added beside the other crypt-free system entries. The list is only read when cross-compiling for Windows (cmake/modules/InstallTargetDependencies.cmake:73 IGNORE_CASE IGNORE_LIBS_FILE), so no other platform's dependency resolution changes.
cmake/linux/apprun-hooks/carla-hook.sh                   declared divergence -> wave R rename (018d2041f): the comment describing the LD_PRELOAD target named lmms -> zene, the name the renamed target installs. Comment only.; wave R rename: AppRun hook resolves the renamed plugin directory
cmake/linux/apprun-hooks/jack-hook.sh                    declared divergence -> wave R rename (018d2041f): the AppImage JACK probe ldd'd $APPDIR/usr/bin/lmms; it now probes usr/bin/zene, the file the renamed target installs. Without this the hook would probe a path that no longer exists.; wave R rename: AppRun hook resolves the renamed binary/data paths
cmake/linux/apprun-hooks/README.md                       docs (allowed)
cmake/linux/CMakeLists.txt                               build/config (allowed)
cmake/linux/icons/128x128@2/apps/lmms.png                declared divergence -> wave R rename (018d2041f): cmake/linux/icons/128x128@2/apps/lmms.png -> cmake/linux/icons/128x128@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/128x128@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/128x128@2/apps/zene.png                declared divergence -> wave R rename (018d2041f): cmake/linux/icons/128x128@2/apps/lmms.png -> cmake/linux/icons/128x128@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/128x128@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/128x128@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/128x128/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/128x128/apps/lmms.png -> cmake/linux/icons/128x128/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/128x128/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/128x128/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/128x128/apps/lmms.png -> cmake/linux/icons/128x128/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/128x128/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/128x128/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/16x16@2/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/16x16@2/apps/lmms.png -> cmake/linux/icons/16x16@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/16x16@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/16x16@2/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/16x16@2/apps/lmms.png -> cmake/linux/icons/16x16@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/16x16@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/16x16@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/16x16/apps/lmms.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/16x16/apps/lmms.png -> cmake/linux/icons/16x16/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/16x16/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/16x16/apps/zene.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/16x16/apps/lmms.png -> cmake/linux/icons/16x16/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/16x16/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/16x16/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/24x24@2/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/24x24@2/apps/lmms.png -> cmake/linux/icons/24x24@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/24x24@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/24x24@2/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/24x24@2/apps/lmms.png -> cmake/linux/icons/24x24@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/24x24@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/24x24@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/24x24/apps/lmms.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/24x24/apps/lmms.png -> cmake/linux/icons/24x24/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/24x24/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/24x24/apps/zene.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/24x24/apps/lmms.png -> cmake/linux/icons/24x24/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/24x24/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/24x24/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/256x256/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/256x256/apps/lmms.png -> cmake/linux/icons/256x256/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/256x256/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/256x256/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/256x256/apps/lmms.png -> cmake/linux/icons/256x256/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/256x256/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/256x256/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/32x32@2/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/32x32@2/apps/lmms.png -> cmake/linux/icons/32x32@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/32x32@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/32x32@2/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/32x32@2/apps/lmms.png -> cmake/linux/icons/32x32@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/32x32@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/32x32@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/32x32/apps/lmms.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/32x32/apps/lmms.png -> cmake/linux/icons/32x32/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/32x32/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/32x32/apps/zene.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/32x32/apps/lmms.png -> cmake/linux/icons/32x32/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/32x32/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/32x32/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/48x48@2/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/48x48@2/apps/lmms.png -> cmake/linux/icons/48x48@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/48x48@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/48x48@2/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/48x48@2/apps/lmms.png -> cmake/linux/icons/48x48@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/48x48@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/48x48@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/48x48/apps/lmms.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/48x48/apps/lmms.png -> cmake/linux/icons/48x48/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/48x48/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/48x48/apps/zene.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/48x48/apps/lmms.png -> cmake/linux/icons/48x48/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/48x48/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/48x48/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/64x64@2/apps/lmms.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/64x64@2/apps/lmms.png -> cmake/linux/icons/64x64@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/64x64@2/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/64x64@2/apps/zene.png                  declared divergence -> wave R rename (018d2041f): cmake/linux/icons/64x64@2/apps/lmms.png -> cmake/linux/icons/64x64@2/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/64x64@2/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/64x64@2/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/64x64/apps/lmms.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/64x64/apps/lmms.png -> cmake/linux/icons/64x64/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/64x64/apps/zene.png (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/64x64/apps/zene.png                    declared divergence -> wave R rename (018d2041f): cmake/linux/icons/64x64/apps/lmms.png -> cmake/linux/icons/64x64/apps/zene.png -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/64x64/mimetypes/application-x-lmms-project.png declared divergence -> rename layer 2: this file was renamed to application-x-zene-project.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/64x64/mimetypes/application-x-zene-project.png declared divergence -> rename layer 2: upstream's application-x-lmms-project.png renamed so the raster icon name matches the MIME type zene.desktop and zene.xml declare (application/x-zene-project) at every size - the pixel bytes are unchanged by the rename; brand placeholders: icon pixels replaced with a hand-authored, licence-free document+note (was byte-identical to origin/master's copy); see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/scalable/apps/lmms.svg                 declared divergence -> wave R rename (018d2041f): cmake/linux/icons/scalable/apps/lmms.svg -> cmake/linux/icons/scalable/apps/zene.svg -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; brand placeholders: this file was renamed to cmake/linux/icons/scalable/apps/zene.svg (see its entry); the placeholder art is too dissimilar to upstream's for git to pair the rename, so the old path is declared here
cmake/linux/icons/scalable/apps/zene.svg                 declared divergence -> wave R rename (018d2041f): cmake/linux/icons/scalable/apps/lmms.svg -> cmake/linux/icons/scalable/apps/zene.svg -- the application icon is installed under the product's own name, so the LMMS-named icon file is replaced by its Zene Studio copy. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: application icon set renamed with the product (pixels unchanged); brand placeholders: pixels replaced with a hand-authored, licence-free music-note icon (wave R renamed this from upstream lmms.png; the pixels are no longer upstream's) - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/icons/scalable/mimetypes/application-x-lmms-project.svg declared divergence -> rename layer 1: this file was renamed to application-x-zene-project.svg (see its entry); the old path is declared because a rename deletes it from upstream's namespace
cmake/linux/icons/scalable/mimetypes/application-x-zene-project.svg declared divergence -> rename layer 1: file-association metadata and shipped content renamed with the product; brand placeholders: content replaced with a hand-authored, licence-free document+note icon; the upstream author's inkscape/sodipodi export metadata is gone - see docs/BRAND-PLACEHOLDERS.md
cmake/linux/LinuxDeploy.cmake                            build/config (allowed)
cmake/linux/lmms                                         declared divergence -> wave R rename (018d2041f): the lmms launcher script is deleted; cmake/linux/zene is its renamed replacement (it execs the renamed binary). The LMMS-named copy must go or the AppImage would install a second launcher pointing at a binary that no longer exists.; wave R rename: AppImage launcher script renamed cmake/linux/lmms -> cmake/linux/zene
cmake/linux/zene                                         declared divergence -> wave R rename (018d2041f): the launcher script renamed from cmake/linux/lmms -- it execs the renamed binary (zene), so the AppImage ships one correctly-named launcher.; wave R rename: AppImage launcher script renamed cmake/linux/lmms -> cmake/linux/zene
cmake/linux/zene.desktop                                 declared divergence -> wave R rename (018d2041f): cmake/linux/lmms.desktop -> cmake/linux/zene.desktop -- the desktop entry is renamed so the installed launcher presents itself as Zene Studio. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; rename layer 1: file-association metadata and shipped content renamed with the product
cmake/linux/zene.spec.in                                 declared divergence -> wave R rename (018d2041f): cmake/linux/lmms.spec.in -> cmake/linux/zene.spec.in -- the RPM spec is renamed so the source and binary package names carry the product name. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: legacy RPM template name/PROJECT_NAME follow the rename (body not repaired, pre-existing)
cmake/linux/zene.xml                                     declared divergence -> wave R rename (018d2041f): cmake/linux/lmms.xml -> cmake/linux/zene.xml -- the AppStream/AppData metadata file is renamed so software centres list Zene Studio. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; rename layer 1: file-association metadata and shipped content renamed with the product
CMakeLists.txt                                           build/config (allowed)
cmake/modules/BashCompletion.cmake                       build/config (allowed)
cmake/modules/BuildPlugin.cmake                          build/config (allowed)
cmake/modules/ClapHeaders.cmake                          build/config (allowed)
cmake/modules/DetectMachine.cmake                        build/config (allowed)
cmake/modules/FindWasmtime.cmake                         build/config (allowed)
cmake/modules/InstallHelpers.cmake                       build/config (allowed)
cmake/modules/PluginList.cmake                           build/config (allowed)
cmake/modules/VersionInfo.cmake                          build/config (allowed)
cmake/modules/Vst3Sdk.cmake                              build/config (allowed)
cmake/nsis/assets/Logo.png                               declared divergence -> brand placeholders: installer logo was byte-identical to upstream's; replaced with the hand-authored app icon - see docs/BRAND-PLACEHOLDERS.md
cmake/nsis/assets/SmallLogo.png                          declared divergence -> brand placeholders: installer small logo was byte-identical to upstream's; replaced with the hand-authored app icon - see docs/BRAND-PLACEHOLDERS.md
cmake/nsis/CMakeLists.txt                                build/config (allowed)
cmake/nsis/icon.ico                                      declared divergence -> brand placeholders: installer application icon rebuilt from the hand-authored app icon; same 8 sizes at 32bpp as upstream, entry byte counts identical - see docs/BRAND-PLACEHOLDERS.md
cmake/nsis/project.ico                                   declared divergence -> brand placeholders: installer project-file icon rebuilt from the hand-authored document+note icon; same 8 sizes at 32bpp as upstream - see docs/BRAND-PLACEHOLDERS.md
cmake/nsis/zene.exe.manifest                             declared divergence -> wave R rename (018d2041f): cmake/nsis/lmms.exe.manifest -> cmake/nsis/zene.exe.manifest -- the Windows installer resource/manifest is renamed so the installed program and its shortcuts carry the product name. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: Windows application manifest renamed with the executable
cmake/nsis/zene.rc.in                                    declared divergence -> wave R rename (018d2041f): cmake/nsis/lmms.rc.in -> cmake/nsis/zene.rc.in -- the Windows installer resource/manifest is renamed so the installed program and its shortcuts carry the product name. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: Windows resource FileDescription/ProductName name the renamed product
cmake/nsis/zene.VisualElementsManifest.xml               declared divergence -> wave R rename (018d2041f): cmake/nsis/lmms.VisualElementsManifest.xml -> cmake/nsis/zene.VisualElementsManifest.xml -- the Windows installer resource/manifest is renamed so the installed program and its shortcuts carry the product name. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: Windows VisualElements manifest renamed with the package
CONTRIBUTING.md                                          docs (allowed)
data/backgrounds/lmms_tile.png                           declared divergence -> rename layer 1: this file was renamed to zene_tile.png (see its entry); the old path is declared because a rename deletes it from upstream's namespace
data/backgrounds/newbg.png                               declared divergence -> brand placeholders: upstream artwork carrying the LMMS mark as a watermark, shipped byte-identical; replaced with a hand-authored placeholder card - see docs/BRAND-PLACEHOLDERS.md
data/backgrounds/vinnie.png                              declared divergence -> brand placeholders: upstream LMMS mascot artwork (the note character plus the LMMS wordmark) shipped byte-identical; replaced with a hand-authored placeholder card. Unreferenced in the tree but installed by data/backgrounds/CMakeLists.txt - see docs/BRAND-PLACEHOLDERS.md
data/backgrounds/zene_tile.png                           declared divergence -> rename layer 1: shipped background tile renamed zene_tile.png (pixels carry no branding; verified); brand placeholders: pixels replaced with a hand-authored, licence-free note tile (the upstream pixels carried the LMMS mark) - see docs/BRAND-PLACEHOLDERS.md
data/presets/AudioFileProcessor/Erazor.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/AudioFileProcessor/orion.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/AudioFileProcessor/SString.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/BitInvader/invaders_must_die.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/Clap dry.xpf                         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/Clap.xpf                             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/HihatClosed.xpf                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/HihatOpen.xpf                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/KickPower.xpf                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/Shaker.xpf                           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/SnareLong.xpf                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/SnareMarch.xpf                       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/TR909-RimShot.xpf                    declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Kicker/TrapKick.xpf                         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/LB302/AcidLead.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/LB302/AngryLead.xpf                         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/LB302/DroneArp.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Monstro/Growl.xpf                           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Monstro/HorrorLead.xpf                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Monstro/Phat.xpf                            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Monstro/ScaryBell.xpf                       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Nescaline/Chomp.xpf                         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Nescaline/Detune_lead.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Nescaline/Engine_overheats.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Nescaline/Fireball_flick.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Nescaline/Mega_weapon.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Organic/Pwnage.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Organic/Rubberband.xpf                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/Bass.xpf                                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/CheesyGuitar.xpf                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/Lead.xpf                                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/MadMind.xpf                             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/Overdrive.xpf                           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/SID/Pad.xpf                                 declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/AmazingBubbles.xpf         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/AnalogBell.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/AnalogDreamz.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Analogous.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/AnalogTimes.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/ArpeggioPing.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/BellArp.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Bell.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/BlandModBass.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/BrokenToy.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/CryingPads.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/DetunedGhost.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/DirtyReece.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/DistortedPMBass.xpf        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Drums_HardKick.xpf         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Drums_HihatC.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Drums_HihatO.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Drums_Kick.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Drums_Snare.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/DullBell.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/ElectricOboe.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Erazzor.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/FatCheese.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/FatPMArp.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/FatTB303Arp.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/FutureBass.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/FuzzyAnalogBass.xpf        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Garfunkel.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/GhostBoy.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Harmonium.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/HugeGrittyBass.xpf         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Jupiter.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/MoveYourBody.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/OldComputerGames.xpf       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PercussiveBass.xpf         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Play-some-rock.xpf         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PluckBass.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PMbass.xpf                 declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PMFMFTWbass.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PM-FMstring.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/PowerStrings.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Ravemania.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/ResoBass.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/ResonantPad.xpf            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Rough!.xpf                 declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/SawReso.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/SEGuitar.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/SquarePing.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/Supernova.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/SuperSawLead.xpf           declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/TheMaster.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/TINTNpad.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/TranceLead.xpf             declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/TripleOscillator/WarmStack.xpf              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Watsyn/Epic_lead.xpf                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Watsyn/Phase_bass.xpf                       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Watsyn/Pulse.xpf                            declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Accordion.xpf                     declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Ambition.xpf                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Baby Violin.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Bad Singer.xpf                    declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Cloud Bass.xpf                    declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Creature.xpf                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Dream.xpf                         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Electric Shock.xpf                declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Faded Colors - notes test.xpf     declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Faded Colors.xpf                  declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Fat Flute.xpf                     declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Frog.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Horn.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Low Battery.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Piano-Gong.xpf                    declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Rubber Bass.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Space Echoes.xpf                  declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Speaker Swapper.xpf               declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Toss.xpf                          declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Untuned Bell.xpf                  declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/Vibrato.xpf                       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/presets/Xpressive/X-Distorted.xpf                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Alf42red-Mauiwowi.mmpz               declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/AngryLlama-NewFangled.mmpz           declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Ashore.mmpz                          declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/CapDan/CapDan-ReggaetonTry.mmpz      declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/CapDan/CapDan-ReggaeTry.mmpz         declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/CapDan/CapDan-TwilightArea-OriginalByAlf42red.mmpz declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/CapDan/CapDan-ZeroSumGame-OriginalByZakarra.mmpz declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/DnB.mmpz                             declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/EsoXLB-CPU.mmpz                      declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Farbro-Tectonic.mmpz                 declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Greippi - Krem Kaakkuja (Second Flight Remix).mmpz declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Impulslogik-Zen.mmpz                 declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Jousboxx-BuzzerBeater.mmpz           declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Momo64-esp.mmpz                      declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Namitryus-K-Project.mmpz             declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Oglsdl-Dr8v2.mmpz                    declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Oglsdl-PpTrip.mmpz                   declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Popsip-Electric Dancer.mmpz          declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Root84-Initialize.mmpz               declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Saber-FinalStep.mmpz                 declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Settel-InnerRecreation.mmpz          declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Shovon-ProgressiveHousePluckDemo.mmpz declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Skiessi/Skiessi-C64.mmpz             declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Skiessi/Skiessi-Onion.mmpz           declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Skiessi/Skiessi-RandomProjectNumber14253.mmpz declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Skiessi/Skiessi-TurningPoint.mmpz    declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Socceroos-Progress.mmpz              declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/StrictProduction-DearJonDoe.mmp      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/TameAnderson-MakeMe.mmpz             declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Thaledric-Armageddon.mmpz            declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/Thomasso-AxeFromThe80s.mmpz          declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/TobyDox-Psycho.mmpz                  declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/demos/unfa-Spoken.mmpz                     declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/Crunk(Demo).mmp                   declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/DirtyLove.mmpz                    declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/Root84-TrancyLoop.mmpz            declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/Skiessi-222.mmpz                  declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/Surrender-Main.mmpz               declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/sv-DnB-Startup.mmpz               declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/shorties/sv-Trance-Startup.mmpz            declared divergence -> rename layer 3: shipped demo project written in the new format (zene-project root); ATTR-1 (2026-09-17): creator reverted to the upstream value from 4e677cb6c6ab through the [4-byte BE length][zlib] container round trip (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/AcousticDrumset.mpt              declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/ClubMix.mpt                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/CR8000.mpt                       declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/default.mpt                      declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/Empty.mpt                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/templates/TR808.mpt                        declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/projects/tutorials/editing_note_volumes.mmp         declared divergence -> rename layer 3: root element written in the fork's zene-project form; ATTR-1 (2026-09-13): creator reverted to the upstream value from 4e677cb6c6ab (owner decision, BACKLOG `ATTR-1`) -- the root-element rename is the only difference from upstream left in this file
data/scripts/create-pattern.lua                          declared divergence -> rename layer 1: shipped Lua example uses the zene API header and namespace
data/scripts/generative-bass.lua                         declared divergence -> rename layer 1: shipped Lua example uses the zene API header and namespace
data/scripts/hello.lua                                   declared divergence -> rename layer 1: shipped Lua example uses the zene API header and namespace
data/scripts/midi-router.lua                             declared divergence -> rename layer 1: shipped Lua example uses the zene API header and namespace
data/themes/default/lmms-plugin-logo.svg                 declared divergence -> rename layer 1: this file was renamed to zene-plugin-logo.svg (see its entry); the old path is declared because a rename deletes it from upstream's namespace
data/themes/default/splash.png                           declared divergence -> brand placeholders: the splash carried the LMMS wordmark and logo, byte-identical to upstream; replaced with a hand-authored placeholder splash that names itself a placeholder - see docs/BRAND-PLACEHOLDERS.md
data/themes/default/zene-plugin-logo.svg                 declared divergence -> rename layer 1: theme artwork renamed zene-plugin-logo.svg; dc:creator (CC0) retained; brand placeholders: art replaced with a hand-authored, licence-free music note; rename layer 1 renamed the file and its CC0 dc:creator (Rebecca Noel Ati) is retained in git history - see docs/BRAND-PLACEHOLDERS.md
doc/bash-completion/CMakeLists.txt                       build/config (allowed)
doc/bash-completion/zene                                 declared divergence -> wave R rename (018d2041f): doc/bash-completion/lmms -> doc/bash-completion/zene -- the bash completion file is renamed so it completes the renamed binary. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: bash completion renamed with the executable
doc/CMakeLists.txt                                       build/config (allowed)
doc/Doxyfile.in                                          declared divergence -> wave R rename (018d2041f): Doxygen PROJECT_NAME = LMMS -> Zene Studio, so generated API docs carry the product name.; wave R rename: generated documentation project name follows the rename
docs/706-logs/EVIDENCE.md                                docs (allowed)
docs/708-logs/EVIDENCE.md                                docs (allowed)
docs/709-logs/709-baseline-capture-final.md              docs (allowed)
docs/709-logs/709-baseline-capture.md                    docs (allowed)
docs/709-logs/709-baseline-capture-rerun.md              docs (allowed)
docs/709-logs/709-baseline-capture-stability.md          docs (allowed)
docs/709-logs/709-baseline-prechange.md                  docs (allowed)
docs/709-logs/709-baseline-prechange-values.md           docs (allowed)
docs/709-logs/709-baseline-rotate.md                     docs (allowed)
docs/709-logs/709-ctest-batch.md                         docs (allowed)
docs/709-logs/709-ctest-feedback2.md                     docs (allowed)
docs/709-logs/709-ctest-feedback3.md                     docs (allowed)
docs/709-logs/709-ctest-feedback.md                      docs (allowed)
docs/709-logs/709-ctest-golden-after-cmt.md              docs (allowed)
docs/709-logs/709-ctest-rerun5-after-plugins.md          docs (allowed)
docs/709-logs/709-localci-nobuild2.md                    docs (allowed)
docs/709-logs/709-localci-nobuild.md                     docs (allowed)
docs/709-logs/709-persistence.md                         docs (allowed)
docs/709-logs/709-snapshot-regen.md                      docs (allowed)
docs/709-logs/709-verify.md                              docs (allowed)
docs/A16-REVERSIBILITY.md                                docs (allowed)
docs/AGENT-SURFACE-TELEMETRY-FIX.md                      docs (allowed)
docs/arch5-logs/run1.md                                  docs (allowed)
docs/arch5-logs/run2.md                                  docs (allowed)
docs/AUDIT-FIX-PASS.md                                   docs (allowed)
docs/AUTO-MASTERING.md                                   docs (allowed)
docs/AUTOMATION-MODES.md                                 docs (allowed)
docs/automation-touch-race-logs/STACK-AND-REPRO-BEFORE.md docs (allowed)
docs/automation-touch-race-logs/VERIFICATION-AFTER.md    docs (allowed)
docs/AUTOSAVE-RECOVERY.md                                docs (allowed)
docs/BRAND-PLACEHOLDERS.md                               docs (allowed)
docs/CAPABILITIES-0.3.0.md                               docs (allowed)
docs/CAPABILITIES-0.3.0-VERIFICATION.md                  docs (allowed)
docs/CHORD-TRACK.md                                      docs (allowed)
docs/CI-TAG-FAILURES.md                                  docs (allowed)
docs/CLIP-CAPTURE-DESIGN.md                              docs (allowed)
docs/CLIP-SLICE0.md                                      docs (allowed)
docs/COMPING.md                                          docs (allowed)
docs/control-arm64-cluster-logs/EVIDENCE.md              docs (allowed)
docs/CONTROL-NAMED-PIPE.md                               docs (allowed)
docs/CONTROL-SOCKET-PATH-SAFETY.md                       docs (allowed)
docs/CONTROL-UNDO-CONNECTION-DROP.md                     docs (allowed)
docs/CONVENTIONS.md                                      docs (allowed)
docs/COVERAGE-GATE-GREEN.md                              docs (allowed)
docs/COVERAGE-MATRIX-2026-09-13.md                       docs (allowed)
docs/COVERAGE-REMEASURE-2026-09-14.md                    docs (allowed)
docs/COVERAGE-RUN.md                                     docs (allowed)
docs/CRASH-REPORTER.md                                   docs (allowed)
docs/DAWPROJECT-INTERCHANGE.md                           docs (allowed)
docs/DISARMED-AUDIT.md                                   docs (allowed)
docs/DOCS-NAMING.md                                      docs (allowed)
docs/EXPORT-SRC-DITHER.md                                docs (allowed)
docs/FEATURE-LIST-0.3.0.md                               docs (allowed)
docs/FINAL-DOC-EDITS.md                                  docs (allowed)
docs/GATE-HYGIENE.md                                     docs (allowed)
docs/GOLDEN-AUDIO.md                                     docs (allowed)
docs/GROOVE-POOL.md                                      docs (allowed)
docs/IMPORT-DETECTION.md                                 docs (allowed)
docs/INDEPENDENT-NOTES-READ.md                           docs (allowed)
docs/INSTRUMENT-HOSTING-SPEC.md                          docs (allowed)
docs/INSTRUMENT-VIEW-SAFETY.md                           docs (allowed)
docs/INTEGRATION-MERGES-2.md                             docs (allowed)
docs/INTEGRATION-MERGES-3A.md                            docs (allowed)
docs/INTEGRATION-MERGES-3B.md                            docs (allowed)
docs/INTEGRATION-MERGES-3C.md                            docs (allowed)
docs/INTEGRATION-MERGES-3D.md                            docs (allowed)
docs/INTEGRATION-MERGES-3E.md                            docs (allowed)
docs/INTEGRATION-MERGES-3F.md                            docs (allowed)
docs/INTEGRATION-MERGES.md                               docs (allowed)
docs/INTEGRATION-VERIFY.md                               docs (allowed)
docs/KNOWN-LIMITATIONS.md                                docs (allowed)
docs/KNOWN-LIMITATIONS-v0.1.0-alpha.md                   docs (allowed)
docs/LINKED-CLIPS.md                                     docs (allowed)
docs/LINK-SYNC.md                                        docs (allowed)
docs/LOCAL-CI.md                                         docs (allowed)
docs/LUA-API-STABILISATION.md                            docs (allowed)
docs/LUA-API-SURFACE.md                                  docs (allowed)
docs/LUA-COMPATIBILITY-POLICY.md                         docs (allowed)
docs/LUA-PACKAGE-FORMAT.md                               docs (allowed)
docs/LUA-SCRIPT-DEVICES-DESIGN.md                        docs (allowed)
docs/LUFS-METER.md                                       docs (allowed)
docs/LUFS-WIRING.md                                      docs (allowed)
docs/mc-logs/build-slice01.md                            docs (allowed)
docs/mc-logs/configure-slice01.md                        docs (allowed)
docs/mc-logs/ctest-slice01.md                            docs (allowed)
docs/mc-logs/gates-slice01-dev.md                        docs (allowed)
docs/mc-logs/live-test.md                                docs (allowed)
docs/mc-logs/mutation-gate-slice01.md                    docs (allowed)
docs/mc-logs/postcommit-static-gates.md                  docs (allowed)
docs/mc-logs/rt-safety-slice01.md                        docs (allowed)
docs/mc-logs/schedule-test.md                            docs (allowed)
docs/mc-logs/unit-test.md                                docs (allowed)
docs/METER-SURFACE.md                                    docs (allowed)
docs/MIDI-DEPTH.md                                       docs (allowed)
docs/MIDI-LEARN.md                                       docs (allowed)
docs/MIDI-LEARN-RACE.md                                  docs (allowed)
docs/MIDI-RETRO-CAPTURE-BOUNDS.md                        docs (allowed)
docs/MIDI-RETRO-CAPTURE.md                               docs (allowed)
docs/MIXER-CONCURRENCY-FIXES.md                          docs (allowed)
docs/MMPZ-GIT-DEPTH.md                                   docs (allowed)
docs/MODULATION.md                                       docs (allowed)
docs/MPE.md                                              docs (allowed)
docs/MSVC-APPLOCAL-LINK-RACE.md                          docs (allowed)
docs/MSVC-VST3-TEST-DLLIMPORT.md                         docs (allowed)
docs/OOP-HOSTING.md                                      docs (allowed)
docs/OUT-OF-PROCESS-BEYOND-ZYN.md                        docs (allowed)
docs/PATCHER-GRAPH.md                                    docs (allowed)
docs/phase-f/CRITERIA-TO-EVIDENCE.md                     docs (allowed)
docs/phase-f/PER-BRANCH-REVIEWABILITY.md                 docs (allowed)
docs/phase-f/RELEASE-NOTES-mixer-v2.md                   docs (allowed)
docs/PIPELINE-HARDENING.md                               docs (allowed)
docs/PITCH-STRETCH.md                                    docs (allowed)
docs/PITCH-STRETCH-TRANSCRIPT.md                         docs (allowed)
docs/PLUGIN-HOSTING-IN-RELEASE.md                        docs (allowed)
docs/PLUGIN-SCAN-CACHE.md                                docs (allowed)
docs/PLUGIN-SCAN-CACHE-RUNS.md                           docs (allowed)
docs/PR594-REBASE.md                                     docs (allowed)
docs/QDEBUG-CLASS-AND-MIME-RENAME.md                     docs (allowed)
docs/RACK-MACROS.md                                      docs (allowed)
docs/RACKS.md                                            docs (allowed)
docs/RECORDING-REALTIME-FIXES.md                         docs (allowed)
docs/RECORD-INPUTS.md                                    docs (allowed)
docs/RELEASE-DOCS-FINAL-PASS.md                          docs (allowed)
docs/RELEASE-NOTES-v0.1.0-alpha.md                       docs (allowed)
docs/RELEASE-NOTES-v0.2.1-alpha.md                       docs (allowed)
docs/RELEASE-NOTES-v0.3.0-alpha.md                       docs (allowed)
docs/RELEASE-PREP-0.2.0.md                               docs (allowed)
docs/RELEASING.md                                        docs (allowed)
docs/RENAME-COMPLETE.md                                  docs (allowed)
docs/RENDER-CHILD-WAIT.md                                docs (allowed)
docs/RENDER-DETERMINISM.md                               docs (allowed)
docs/reports/ARCH2-BOUNDARY-REPORT.md                    docs (allowed)
docs/reports/ARCH-7-LICENCE-ANSWER.md                    docs (allowed)
docs/reports/ARCH-7-VERDICT.md                           docs (allowed)
docs/reports/ATTR1-REVERT-REPORT.md                      docs (allowed)
docs/reports/CI-FIX6-REPORT.md                           docs (allowed)
docs/reports/CI-PREP-030-REPORT.md                       docs (allowed)
docs/reports/CI-PREP-030-TRANSCRIPT.md                   docs (allowed)
docs/reports/CLAP-INSTRUMENT-ROW79.md                    docs (allowed)
docs/reports/CMDN-REPORT.md                              docs (allowed)
docs/reports/CMDN-TRANSCRIPT.md                          docs (allowed)
docs/reports/COVERAGE-CLOSURE-2026-09-16.md              docs (allowed)
docs/reports/CRASHBOT-T3-PILOT-REPORT.md                 docs (allowed)
docs/reports/CRASHBOT-V0-REPORT.md                       docs (allowed)
docs/reports/DOCS-AGREE-REPORT.md                        docs (allowed)
docs/reports/FIXCLAP-REPORT.md                           docs (allowed)
docs/reports/FIXUP-ENGINE-2026-09-16.md                  docs (allowed)
docs/reports/FIXUP-HYGIENE-REPORT-2026-09-16.md          docs (allowed)
docs/reports/FIXUP-TESTS-2-REPORT.md                     docs (allowed)
docs/reports/LANE-STATE-A16-COUNT.md                     docs (allowed)
docs/reports/LANE-STATE-CLAP-WINDOWS.md                  docs (allowed)
docs/reports/LANE-STATE-CODE9-NAMED-PIPE.md              docs (allowed)
docs/reports/LANE-STATE-HOSTCHUNKING.md                  docs (allowed)
docs/reports/LANE-STATE-M1-DEMO.md                       docs (allowed)
docs/reports/LANE-STATE-MASTERING-SURFACE.md             docs (allowed)
docs/reports/LANE-STATE.md                               docs (allowed)
docs/reports/LANE-STATE-METER-SURFACE.md                 docs (allowed)
docs/reports/LANE-STATE-OUT-OF-PROCESS-ROW80.md          docs (allowed)
docs/reports/LANE-STATE-RECORD-INPUTS.md                 docs (allowed)
docs/reports/LANE-STATE-RENDER-PRESETS.md                docs (allowed)
docs/reports/LANE-STATE-VST3-INSTRUMENT-ROW78.md         docs (allowed)
docs/reports/MERGE-4-REPORT.md                           docs (allowed)
docs/reports/MERGE-5-REPORT.md                           docs (allowed)
docs/reports/MERGE-6-REPORT.md                           docs (allowed)
docs/reports/MERGE-7-REPORT.md                           docs (allowed)
docs/reports/MERGE-9-REPORT.md                           docs (allowed)
docs/reports/MERGE-TRAIN-030w10.md                       docs (allowed)
docs/reports/MERGE-TRAIN-030w11.md                       docs (allowed)
docs/reports/MERGE-TRAIN-030w4.md                        docs (allowed)
docs/reports/MERGE-TRAIN-030w5.md                        docs (allowed)
docs/reports/MERGE-TRAIN-030w9.md                        docs (allowed)
docs/reports/MERGE-TRAIN-030w9-PASS2.md                  docs (allowed)
docs/reports/MINGW-DISK-ROUND2.md                        docs (allowed)
docs/reports/PROOF-DEBT-030w9-REPORT.md                  docs (allowed)
docs/reports/README.md                                   docs (allowed)
docs/reports/REL2-REPORT.md                              docs (allowed)
docs/reports/REPO2-REPORT.md                             docs (allowed)
docs/reports/RT-SAFETY-REPORT.md                         docs (allowed)
docs/reports/SESSION-API-PROOF-2026-09-15.md             docs (allowed)
docs/reports/SESSION-API-PROOF-TRANSCRIPT-2026-09-15.md  docs (allowed)
docs/reports/SNAPSHOT-NOWASM-REPORT.md                   docs (allowed)
docs/reports/WPLAT-ARM64-REPORT.md                       docs (allowed)
docs/reports/WPLAT-LIN-REPORT.md                         docs (allowed)
docs/reports/WPLAT-MAC-REPORT.md                         docs (allowed)
docs/reports/WPLAT-WIN-REPORT.md                         docs (allowed)
docs/RETRO-AUDIO-CAPTURE.md                              docs (allowed)
docs/REVISION-TIMELINE.md                                docs (allowed)
docs/ROUTING-GRAPH-LIVE.md                               docs (allowed)
docs/RT-SAFETY-SWEEP.md                                  docs (allowed)
docs/s3-logs/s3-all-sources-reproduce.md                 docs (allowed)
docs/s3-logs/s3-ctest.md                                 docs (allowed)
docs/s3-logs/s3-gate4-complexity.md                      docs (allowed)
docs/s3-logs/s3-gate7-filelength.md                      docs (allowed)
docs/s3-logs/s3-gate8-duplication.md                     docs (allowed)
docs/s3-logs/s3-gate9-fork-sources.md                    docs (allowed)
docs/s3-logs/s3-merge-demo.md                            docs (allowed)
docs/s4-logs/00-df-before.md                             docs (allowed)
docs/s4-logs/01b-regen.md                                docs (allowed)
docs/s4-logs/01-configure.md                             docs (allowed)
docs/s4-logs/02b-rebuild-gate.md                         docs (allowed)
docs/s4-logs/02-build.md                                 docs (allowed)
docs/s4-logs/03-ctest.md                                 docs (allowed)
docs/s4-logs/04-gate4-complexity.md                      docs (allowed)
docs/s4-logs/05-gate7-filelength.md                      docs (allowed)
docs/s4-logs/06-gate8-duplication.md                     docs (allowed)
docs/s4-logs/07-negative-control-plant-FAIL.md           docs (allowed)
docs/s4-logs/08-negative-control-revert-PASS.md          docs (allowed)
docs/s4-logs/09-gate9-fork-sources.md                    docs (allowed)
docs/s5-logs/compile-DataFileSaveIntegrity.md            docs (allowed)
docs/s5-logs/compile-regression.md                       docs (allowed)
docs/s5-logs/configure.md                                docs (allowed)
docs/s5-logs/DataFileSaveIntegrityTest.md                docs (allowed)
docs/s5-logs/DataFileSaveIntegrityTest.verbose.md        docs (allowed)
docs/s5-logs/regression.md                               docs (allowed)
docs/s6-logs/build.md                                    docs (allowed)
docs/s6-logs/build-testfix.md                            docs (allowed)
docs/s6-logs/configure.md                                docs (allowed)
docs/s6-logs/ctest.md                                    docs (allowed)
docs/s6-logs/ctest-provenance.md                         docs (allowed)
docs/s6-logs/gates-complexity.md                         docs (allowed)
docs/s6-logs/gates-complexity-tools.md                   docs (allowed)
docs/s6-logs/gates-duplication.md                        docs (allowed)
docs/s6-logs/gates-duplication-tools.md                  docs (allowed)
docs/s6-logs/gates-evidence.md                           docs (allowed)
docs/s6-logs/gates-evidence-selftest.md                  docs (allowed)
docs/s6-logs/gates-file-length.md                        docs (allowed)
docs/s6-logs/gates-file-length-tools.md                  docs (allowed)
docs/s6-logs/gates-fork-sources.md                       docs (allowed)
docs/s6-logs/gates-fork-sources-post.md                  docs (allowed)
docs/s6-logs/gates-no-tautology.md                       docs (allowed)
docs/s6-logs/gates-rt-safety.md                          docs (allowed)
docs/s6-logs/gates-upstream.md                           docs (allowed)
docs/s6-logs/gates-upstream-post.md                      docs (allowed)
docs/s6-logs/README.md                                   docs (allowed)
docs/s6-logs/reconfigure.md                              docs (allowed)
docs/s9-logs/baseline-ctest.md                           docs (allowed)
docs/s9-logs/clipwarppersistence-slots.md                docs (allowed)
docs/s9-logs/configure-run.md                            docs (allowed)
docs/s9-logs/gates.md                                    docs (allowed)
docs/s9-logs/negative-control.md                         docs (allowed)
docs/s9-logs/postchange-ctest.md                         docs (allowed)
docs/s9-logs/S9-REPORT.md                                docs (allowed)
docs/s9-logs/sweep-ctest.md                              docs (allowed)
docs/SAMPLE-ACCURATE-AUTOMATION.md                       docs (allowed)
docs/SAVELOAD-INTEGRITY.md                               docs (allowed)
docs/SESSION-ARRANGEMENT-RECORD.md                       docs (allowed)
docs/SESSION-ARRANGEMENT-RECORD-MEASURED.md              docs (allowed)
docs/SESSION-SCHEDULER.md                                docs (allowed)
docs/SMF-INTERCHANGE.md                                  docs (allowed)
docs/specs/A16-STATUS-MEASURED.md                        docs (allowed)
docs/specs/AGENT-SURFACE-INVENTORY.md                    docs (allowed)
docs/specs/AGENT-TOOLING.md                              docs (allowed)
docs/specs/GIT-FRIENDLY-MMPZ.md                          docs (allowed)
docs/specs/NEURAL-AMP.md                                 docs (allowed)
docs/specs/PART-D-SIDECHAIN.md                           docs (allowed)
docs/specs/PATCHER-MVP.md                                docs (allowed)
docs/specs/README.md                                     docs (allowed)
docs/specs/RECORDING-PROTOTYPE.md                        docs (allowed)
docs/specs/SPEC-dynamic-routing.md                       docs (allowed)
docs/specs/SPEC-lua-api-v0.md                            docs (allowed)
docs/specs/SPEC-neural-amp.md                            docs (allowed)
docs/specs/SPEC-slide-notes.md                           docs (allowed)
docs/specs/SPEC-stable-ids.md                            docs (allowed)
docs/specs/SPEC-stem-split.md                            docs (allowed)
docs/specs/SPEC-two-track-recording.md                   docs (allowed)
docs/specs/SPEC-wasm-sandbox.md                          docs (allowed)
docs/specs/SPEC-zene-studio.md                           docs (allowed)
docs/specs/VST3-LICENSING.md                             docs (allowed)
docs/specs/WASM-SANDBOX.md                               docs (allowed)
docs/STATUS.md                                           docs (allowed)
docs/STEM-EXPORT.md                                      docs (allowed)
docs/TEARDOWN-ABORT-SWEEP.md                             docs (allowed)
docs/TELEMETRY-KILL-SWITCH.md                            docs (allowed)
docs/TELEMETRY-V1.md                                     docs (allowed)
docs/TEMPO-MAP.md                                        docs (allowed)
doc/STEM-SPLIT.md                                        docs (allowed)
docs/TEST-HYGIENE.md                                     docs (allowed)
docs/TOOLS-SCOPE-AND-GATE6-FIX.md                        docs (allowed)
docs/TRACK-FOLDER-DESIGN.md                              docs (allowed)
docs/train-logs/advertised-features-gate.sh.md           docs (allowed)
docs/train-logs/all-sources.md                           docs (allowed)
docs/train-logs/build-11lane.md                          docs (allowed)
docs/train-logs/build-merge.md                           docs (allowed)
docs/train-logs/complexity-gate.sh.md                    docs (allowed)
docs/train-logs/controlfeedback-fix.md                   docs (allowed)
docs/train-logs/ctest-11lane.md                          docs (allowed)
docs/train-logs/ctest-merged-tip.md                      docs (allowed)
docs/train-logs/duplication-gate.sh.md                   docs (allowed)
docs/train-logs/evidence-gate.sh.md                      docs (allowed)
docs/train-logs/file-length-gate.sh.md                   docs (allowed)
docs/train-logs/fork-sources-gate.sh.md                  docs (allowed)
docs/train-logs/gate6-final.md                           docs (allowed)
docs/train-logs/gate-file-length.md                      docs (allowed)
docs/train-logs/honesty.md                               docs (allowed)
docs/train-logs/no-upstream-regression-gate.sh.md        docs (allowed)
docs/train-logs/reanchor-datafilesaveintegrity.md        docs (allowed)
docs/train-logs/unregistered-tests-gate.sh.md            docs (allowed)
docs/train-logs/unreg.md                                 docs (allowed)
docs/ui-logs/040-ui-precond-all-sources.md               docs (allowed)
docs/ui-logs/040-ui-precond-build.md                     docs (allowed)
docs/ui-logs/040-ui-precond-configure.md                 docs (allowed)
docs/ui-logs/040-ui-precond-ctest.md                     docs (allowed)
docs/ui-logs/040-ui-precond-fork-sources.md              docs (allowed)
docs/ui-logs/040-ui-precond-gate4.md                     docs (allowed)
docs/ui-logs/040-ui-precond-gate7.md                     docs (allowed)
docs/ui-logs/040-ui-precond-gate8.md                     docs (allowed)
docs/ui-logs/040-ui-precond-gate9.md                     docs (allowed)
docs/ui-logs/040-ui-precond-gate9-postcommit.md          docs (allowed)
docs/ui-logs/040-ui-precond-grep-counts.md               docs (allowed)
docs/UNDO-BOUNDS.md                                      docs (allowed)
docs/UNDO-RELEASE-CONFIG.md                              docs (allowed)
docs/VCA-EDIT-GROUPS.md                                  docs (allowed)
docs/VCA-GROUPS.md                                       docs (allowed)
docs/VERIFICATION-DEBT-FIXES.md                          docs (allowed)
docs/VERSION-0.2.1-ALPHA.md                              docs (allowed)
docs/VERSIONING.md                                       docs (allowed)
docs/VST3-INSTRUMENT-FIXTURE.md                          docs (allowed)
docs/VST3-INSTRUMENT-HOSTING.md                          docs (allowed)
docs/WARP-COMMANDS-TRANSCRIPT.md                         docs (allowed)
docs/WARP.md                                             docs (allowed)
docs/WASM-EFFECT-ABI.md                                  docs (allowed)
docs/WAVE-R-RENAME.md                                    docs (allowed)
doc/zene.1                                               declared divergence -> wave R rename (018d2041f): doc/lmms.1 -> doc/zene.1 -- the man page is renamed so 'man zene' documents this product. Upstream's LMMS-named path is retired deliberately (a rebase onto upstream must not silently restore it).; wave R rename: man page renamed with the executable; NAME/SYNOPSIS follow
.github/ISSUE_TEMPLATE/alpha-feedback.yml                CI config (allowed, non-runtime)
.github/ISSUE_TEMPLATE/bug_report.yml                    CI config (allowed, non-runtime)
.github/ISSUE_TEMPLATE/feature_request.yml               CI config (allowed, non-runtime)
.github/workflows/build.yml                              CI config (allowed, non-runtime)
.github/workflows/checks.yml                             CI config (allowed, non-runtime)
.github/workflows/deps-ubuntu-24.04-gcc.txt              CI config (allowed, non-runtime)
.github/workflows/provision-plugin-hosting-deps.sh       CI config (allowed, non-runtime)
.github/workflows/quality-gates.yml                      CI config (allowed, non-runtime)
.github/workflows/release.yml                            CI config (allowed, non-runtime)
.gitignore                                               build/config (allowed)
include/Accessibility.h                                  fork-NEW (allowed)
include/AudioAlsa.h                                      declared divergence -> 0.3.0 recording engine surface (feature row 64, docs/RECORD-INPUTS.md): declares the CAPTURE side this playback-only backend never had - openCapture()/closeCapture()/captureLoop()/publishCaptured()/handleCaptureError() and their state (a second snd_pcm_t, its hw_params, the granted channel count, the FLOAT-or-S16_LE choice, three pre-allocated buffers, the cached bus pair, the thread and its two atomics). The playback declarations above are untouched, and the file's own contract is unchanged: the constructor still only opens the playback PCM and still reports success through the same flag.
include/AudioBuffer.h                                    declared divergence -> #608 memory correctness: the channel access buffer (pointer table) is process-local, never allocated from a shared memory resource (746f6bd0e)
include/AudioBusHandle.h                                 declared divergence -> #605 PDC: latency-reporting surface read by the mixer's compensation graph
include/AudioEngine.h                                    declared divergence -> #605 PDC: latency accessor surface used by the compensation graph; post-alpha/recording-realtime: capture staging ring (fixed capacity, pre-allocated) replacing the locking, doubling input buffer, and pushInputFrames() taking const frames as noexcept; #626: audioDevRequestName()/audioDevStartReason() so a device that failed to open can be named to an agent instead of a bare false (post-alpha/control-hardening); feature row 24 (meter surface): includes MasterLoudnessTap.h, declares masterLoudness() and holds this engine's one loudness tap (std::unique_ptr<MasterLoudnessTap>, constructed once, disarmed until a caller arms it). No existing declaration, value or call site changes; 0.3.0 recording engine surface (feature rows 14/16/64): declares the N-CHANNEL capture input path (pushInputFramesWide + the read-side inputWideBuffer/Frames/Channels/FramesStaged/FramesDropped + the drainWideInputStage hook), the AudioWideInputStage member those five accessors read, and the RetroAudioCapture member with its two accessors (retroCapture()). Additive by construction: with no capture backend the wide stage holds no frames, renderNextPeriod() takes the stereo path it always took, and the retro capture keeps one relaxed atomic load per period while disarmed.
include/AudioEngineWorkerThread.h                        declared divergence -> render determinism (render-determinism lane): public switch ProjectRenderer sets so an export's jobs run on one thread (docs/RENDER-DETERMINISM.md)
include/AudioFileWave.h                                  declared divergence -> 0.3.0 W7 export dither: declares the ExportDither member the WAV encoder applies before quantisation (and includes its header). Off by default via OutputSettings::dither(), so a default render's bytes are unchanged
include/AudioInputPath.h                                 fork-NEW (allowed)
include/AudioPlugin.h                                    fork-NEW (allowed)
include/AudioResampler.h                                 declared divergence -> 0.3.0 W7 export SRC quality+ratio convention: declares setMode()/modeForSrcQuality() so the render's chosen converter reaches this resampler, and documents the MEASURED ratio convention (output frames per input frame - the same convention libsamplerate's src_ratio uses). No existing call site changes behaviour: the default SrcQuality::Linear is the converter this class already used
include/AudioStretcher.h                                 fork-NEW (allowed)
include/AudioWideInputStage.h                            fork-NEW (allowed)
include/AutomatableModel.h                               declared divergence -> post-alpha/automation-modes: additive Read/Touch/Latch/Write state, touch-gesture timeout and trim offset on the model; all state is relaxed atomics so the render thread never locks on a mode change (new API in its own banner-marked block, no existing member or method touched); 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): declares publishAutomationRamp()/automationRamp() and the fixed-capacity AutomationRamp member the per-sample buffer is filled from; valueBuffer()'s existing paths, the model's footprint contract and every other declaration are untouched
include/AutomationClip.h                                 declared divergence -> 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): adds sampleAccurate()/setSampleAccurate(), writeBlockRamp() and the three private ramp helpers, plus the m_sampleAccurate flag and the two row-table declarations. The curve's node map, valueAt() and every other declaration are untouched
include/AutomationRamp.h                                 fork-NEW (allowed)
include/BounceInPlace.h                                  fork-NEW (allowed)
include/BrowserCatalog.h                                 fork-NEW (allowed)
include/BrowserPeakCache.h                               fork-NEW (allowed)
include/ChordDetect.h                                    fork-NEW (allowed)
include/ChordProgression.h                               fork-NEW (allowed)
include/ChordTrack.h                                     fork-NEW (allowed)
include/ChordVocabulary.h                                fork-NEW (allowed)
include/Clipboard.h                                      declared divergence -> rename layer 1: clipboard MIME types are application/x-zene-*
include/ClipEdits.h                                      fork-NEW (allowed)
include/Clip.h                                           declared divergence -> #611 Slice 0: freezes the clip->source mapping seam (sourceFrameAt/timelinePosAt) for #597; the base implementation is the tick identity, so every inherited clip behaves as before. fade/crossfade/clip-gain wave: the ClipEdits member and its accessors, and the saveClipEdits/loadClipEdits pair, all neutral by default so an unedited clip serialises and renders exactly as it did; comping (task #600): Clip gains laneIndex()/setLaneIndex() and the m_laneIndex member - the take-lane tag of a clip (docs/COMPING.md), written by that same saveClipEdits pair only when it is above 0 and reset to 0 by loadClipEdits when the attribute is absent, so an old project and a clip nobody assigned to a lane both serialise exactly as before; stable ids slice 2 (feature row 51): Clip declares the saveState/restoreState overrides that carry the stable id as an `id` ATTRIBUTE (written and read only in a document, never in a copy payload - ProjectIds::isDocumentElement), and they call JournallingObject's versions by name so the `<journallingObject id=N>` node every undo step names survives. 040/arch4-s9 (2026-09-22, ARCH-4 slice S9, SPEC-ARCH-4 census row 5 / migration upconversion row): the warp seam region only - `#include "WarpMarkers.h"` plus, immediately after timelinePosAt, the protected warp members (m_warp/m_tempoMode/m_sourceTempo/m_stretchMode) and the saveWarp/loadWarp declarations with their reopened access specifiers; `<warp>` is written and read by the base clip (Clip.cpp) instead of SampleClip now, mode/tempo/stretch/marker vocabulary unchanged, and nothing else in this header changed (S7 owns the rest of it).
include/ClipLinks.h                                      fork-NEW (allowed)
include/ConfigManager.h                                  declared divergence -> rename layer 2: ConfigMigration::adoptConfigFile/adoptWorkingDir declared; the config-file member is m_configFile (was named after the old product)
include/ControlAutomationSupport.h                       fork-NEW (allowed)
include/ControlBrowserSupport.h                          fork-NEW (allowed)
include/ControlChainPresetSupport.h                      fork-NEW (allowed)
include/ControlChordSupport.h                            fork-NEW (allowed)
include/ControlClipLinkSupport.h                         fork-NEW (allowed)
include/ControlCompSupport.h                             fork-NEW (allowed)
include/ControlDetectSupport.h                           fork-NEW (allowed)
include/ControlDeviceSupport.h                           fork-NEW (allowed)
include/ControlEdit.h                                    fork-NEW (allowed)
include/ControlExportPresetSupport.h                     fork-NEW (allowed)
include/ControlGrooveSupport.h                           fork-NEW (allowed)
include/ControllerConnection.h                           declared divergence -> feature row 19 (#651): adds static const ControllerConnectionVector& connections() - the read-only enumeration of the live connections the controller surface needs to save a mapping template from the project's bindings. Declared in the WIP commit cbb717660 and NOT declared in this ledger, which is the missing entry Gate 6 would have reported. Additive: one accessor over the existing static s_connections, no existing member, signature or behaviour changes.
include/ControllerSurface.h                              fork-NEW (allowed)
include/ControlMasteringSupport.h                        fork-NEW (allowed)
include/ControlMeterSupport.h                            fork-NEW (allowed)
include/ControlMixerSupport.h                            fork-NEW (allowed)
include/ControlModulationSupport.h                       fork-NEW (allowed)
include/ControlNoteShared.h                              fork-NEW (allowed)
include/ControlProjectAssets.h                           fork-NEW (allowed)
include/ControlRackSupport.h                             fork-NEW (allowed)
include/ControlRecordingSupport.h                        fork-NEW (allowed)
include/ControlRegistryGroups.h                          fork-NEW (allowed)
include/ControlRegistry.h                                fork-NEW (allowed)
include/ControlReversibility.h                           fork-NEW (allowed)
include/ControlScaleShared.h                             fork-NEW (allowed)
include/ControlServer.h                                  fork-NEW (allowed)
include/ControlStemSupport.h                             fork-NEW (allowed)
include/ControlStructuralSupport.h                       fork-NEW (allowed)
include/ControlUndoCoalescing.h                          fork-NEW (allowed)
include/ControlVocabulary.h                              fork-NEW (allowed)
include/ControlWarpSupport.h                             fork-NEW (allowed)
include/ControlWasmSupport.h                             fork-NEW (allowed)
include/CrashReporterFormat.h                            fork-NEW (allowed)
include/CrashReporter.h                                  fork-NEW (allowed)
include/DataFile.h                                       declared divergence -> ARCH-4 S2a (SPEC-ARCH-4 1.4): publishes setDocumentIndexEnabled(bool), the single-flag seam by which a caller says WHETHER the document warrants a <z:index> without also deciding WHEN it is built - the digests must be taken after DataFile::write()'s own cleanMetaNodes() prune. Adds one private member defaulting to false, so nothing changes for a document that never sets it. Held by tests/src/core/DocumentIndexTest.cpp (anUnclaimedSectionEarnsAnIndexAndNoUnclaimedSectionDoesNot).
include/DawProjectInterchange.h                          fork-NEW (allowed)
include/DocumentIndex.h                                  fork-NEW (allowed)
include/EffectChain.h                                    declared divergence -> #605 PDC: cached chain latency read by the mixer's PDC graph; #599 routing: the chain owns the RoutingGraph that renders its block (rebuildRoutingGraph/routingGraph); post-alpha/cmd-plugins: public const effects() view so plugin.unload / plugin.bypass resolve fx-<n> in processing order (QObject child order diverges after moveUp/moveDown); #655 patcher node-graph (feature row 69, lane 030/patcher-graph, 2026-09-15): the chain owns an AUTHORED wiring (include/PatchWiring.h) that rebuildRoutingGraph() re-applies on every rebuild, so a hand-wired edge survives the next plugin.load instead of being discarded by the derivation; patchNodeId() resolves the roles a wiring addresses against the current node set and setPatchWiring() publishes a new wiring off the audio thread under the engine's model-change guard. No behaviour change while no patch is set: the derived wiring is the same edge list, in the same node order, as before
include/Effect.h                                         declared divergence -> #605 PDC: latency accessor on the effect interface; post-alpha/cmd-plugins: public setEnabled() so plugin.bypass drives the same enabled model the rack's On/Off LED drives (SPEC A11); stable ids slice 2 (feature row 51): adds m_id, id() and setId() to the Effect base class so fx-<n> names the object, not its chain index; DOC-5 (2026-09-15): comment-only - a citation in the file names docs/specs/SPEC-dynamic-routing.md, the pinned snapshot in this repo
include/embed.h                                          declared divergence -> plugin-scan: PixmapLoader::xpm() accessor, so a cached descriptor's logo is only rebuilt when it is a pixmap name and not a compiled-in XPM (023770f7b)
include/Engine.h                                         declared divergence -> 0.3.0 W15 tempo map (D11): declares Engine::updateFramesPerTickForTempo(int bpm) - the tempo map's follower is its only caller and it calls from the AUDIO thread - and makes s_framesPerTick a std::atomic<float> so that one writer and the timing path's readers do not race. No existing declaration, value or call site changes; Engine::framesPerTick() returns the same float it always did.
include/ExportDither.h                                   fork-NEW (allowed)
include/ExportProjectDialog.h                            declared divergence -> #618 loudness wiring: the export dialog's "Loudness report (EBU R128)" checkbox and the result label that shows the report before the dialog closes
include/ExportRenderSettings.h                           fork-NEW (allowed)
include/fenv.h                                           declared divergence -> tag v0.2.0-alpha CI fix: the OS X feenableexcept/fedisableexcept polyfill poked fenv_t.__control/__mxcsr unconditionally, but Darwin declares fenv_t per architecture -- {unsigned long long __fpsr; unsigned long long __fpcr;} on arm64 (whose trap-enable bits are FE_ALL_EXCEPT << 8) versus the x87/SSE {__control,__status,__mxcsr} pair on x86 -- so the macos-arm64 job failed with 'no member named __control in fenv_t' (6 errors) as soon as the VST3 SDK's Apple framework headers included this shadowing header. The x86 branch is byte-identical to upstream; arm64 gets its own branch using the SDK's own __fpcr bit masks
include/FocusDesk.h                                      fork-NEW (allowed)
include/FocusDeskModules.h                               fork-NEW (allowed)
include/FocusDeskPane.h                                  fork-NEW (allowed)
include/GroovePool.h                                     fork-NEW (allowed)
include/GrooveTemplate.h                                 fork-NEW (allowed)
include/ImportDetectionDsp.h                             fork-NEW (allowed)
include/ImportDetection.h                                fork-NEW (allowed)
include/InputChannelRing.h                               fork-NEW (allowed)
include/InstrumentTrack.h                                declared divergence -> #601 MPE: the per-track MpeExpression input state, its accessor, and the live-note readback
include/LatencyCompensation.h                            fork-NEW (allowed)
include/LinkPeerTransport.h                              fork-NEW (allowed)
include/LinkSync.h                                       fork-NEW (allowed)
include/LoudnessReport.h                                 fork-NEW (allowed)
include/LufsMeter.h                                      fork-NEW (allowed)
include/MainWindow.h                                     declared divergence -> midi-learn feature (5d6ccdf1f): the Edit > MIDI Learn action's two slots and its QAction member; the slots only mirror MidiLearnGui's armed state; plugin-scan: updateToolsMenu() slot + populated flag, for building the Tools menu on first open instead of at start-up (023770f7b); rename layer 2: code comment names the new config file (~/.zenestudio.xml); retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): three declarations - the Edit > Arm MIDI Capture and Edit > Capture MIDI slots, the updateMidiRetroCaptureActions() menu-refresh slot and the two QAction members they need. The slots only invoke the registry commands the actions declare (midi.retro_capture_arm / midi.retro_capture_to_clip), so the menu item and the agent surface stay one implementation (SPEC A11/A15)
include/MasteringChain.h                                 fork-NEW (allowed)
include/MasteringJob.h                                   fork-NEW (allowed)
include/MasteringReport.h                                fork-NEW (allowed)
include/MasterLoudnessTap.h                              fork-NEW (allowed)
include/MidiAlsaSeq.h                                    declared divergence -> 0.3.0 MIDI controller auto-reconnection (feature-list row 18, OWNER-31 item 7): noticesPortChanges() overridden to true - this client re-reads the sequencer's client and port inventory every second and publishes the difference, and that poll is what makes a re-connection possible at all. One declaration; nothing else about the class changed.
include/MidiClient.h                                     declared divergence -> retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): one RetroMidiCapture member beside m_midiPorts plus a retroCapture() accessor, so both receive seams reach it through `this` and it dies with the client. Off by default and inert while disarmed (one relaxed atomic load per event); the ring is allocated once in the client constructor, off the MIDI and audio threads. Held by tests/src/core/MidiRetroCaptureTest.cpp.; 0.3.0 `clock.*` (030/midi-clock): midiParserData carries the three fields the system-common assembly needs (m_commonStatus / m_commonBytes / m_commonBuffer, initialised IN the struct because nothing zeroes this parser state) and MidiClientRaw declares beginSystemCommon() / consumeSystemCommon().; 0.3.0 MIDI controller auto-reconnection (feature-list row 18, OWNER-31 item 7): one MidiReconnect member beside m_retroCapture plus its accessor, the virtual noticesPortChanges() (false in the base - no class but MidiAlsaSeq answers true, so a backend whose port changes this build does not consume cannot be reported as one that has them), and a constructor that hands the member its own `this`, so the memory dies with the client exactly as the capture does.
include/MidiClip.h                                       declared divergence -> ARCH-4 S1b (SPEC-ARCH-4 1.6.3, the coercion Track::loadTrack's unrecognised-child branch performed): publishes static classNodeName() returning "midiclip" and makes nodeName() return it, mirroring the idiom AutomationClip.h already carries (include/AutomationClip.h:213, read at src/tracks/InstrumentTrack.cpp:1131). The clip loader now asks each Clip class for its own element name instead of the loader and Clip::saveState() each spelling the literal, so the two cannot drift; nodeName()'s value is unchanged. Held by tests/src/core/UnclaimedElementsTest.cpp.
include/MidiClock.h                                      fork-NEW (allowed)
include/MidiController.h                                 declared divergence -> midi-learn feature (5d6ccdf1f): public MidiPort accessor so a learn can configure a fresh binding and read it back (channel, controller number, subscribed ports)
include/MidiLearnGui.h                                   fork-NEW (allowed)
include/MidiLearn.h                                      fork-NEW (allowed)
include/MidiPort.h                                       declared divergence -> feature row 19 (#651): two relaxed-atomic output counters on MidiPort - offered and written - plus their accessors. MidiPort::processOutEvent is the ONLY place a control-change can leave a controller for the MIDI client, so the counts are taken there rather than self-reported by MidiController; the LED half of the feature is measured at that boundary because a headless test has no hardware to light. No signature, default, flag or behaviour on the existing path changes: two relaxed fetch_adds, no allocation and no lock, so the audio path stays realtime-safe.
include/MidiReconnect.h                                  fork-NEW (allowed)
include/Mixer.h                                          declared divergence -> #605 PDC: alignment points, per-edge compensation delays, channel latency API (9e12a68f5, 48fed8644); mixer concurrency audit D1: MixerChannel::m_muted is std::atomic (worker-read latch, post-alpha/mixer-concurrency); #599 racks: the channel owns its Rack (m_rack), whose chain 0 is the channel's own chain; board card #709 (040/feat-709, 2026-09-22): the cycle-permitted submode's declared API - MixerRoute's feedback flag plus its two pre-allocated snapshot buffers and accessors (setFeedback / writeFeedback / commitFeedback / clearFeedback / feedbackCommitted), MixerChannel::gatingReceives, Mixer::feedbackMode / setFeedbackMode and the control-thread m_feedbackMode flag. Declarations and inline accessors only; the PDC delay line, the mute latch and the VCA members are untouched
include/ModulationLayer.h                                fork-NEW (allowed)
include/MpeExpression.h                                  fork-NEW (allowed)
include/MultiTrackRecorder.h                             fork-NEW (allowed)
include/NamespaceRegistry.h                              fork-NEW (allowed)
include/Note.h                                           declared divergence -> post-alpha/midi-depth: note probability and velocity jitter (prob/veljit) on the note model, per the roadmap-gap register's shortest honest version of General MIDI depth; #601 MPE: per-note pitch/pressure/timbre expression fields with readback+edit accessors, serialized only when captured
include/NotePlayHandle.h                                 declared divergence -> #601 MPE: mpePitchRatio() - the pure-math frequency ratio updateFrequency() applies, unit-tested in MpeExpressionTest; #649 MPE playback: sendMpeExpressionMidi() - the note sends its captured channel pressure and CC74 to the instrument as MIDI events on the channel its own note-on took (its member channel when the note came from an MPE controller), so the two axes an MPE controller sends per note are APPLIED and not merely stored (proof: tests/src/core/MpePlaybackTest.cpp)
include/NoteRandom.h                                     fork-NEW (allowed)
include/NoteTransform.h                                  fork-NEW (allowed)
include/OutOfProcessHosting.h                            fork-NEW (allowed)
include/OutputSettings.h                                 declared divergence -> #618 loudness wiring: the "produce an EBU R128 loudness report" request carried by the render's output settings; defaults to false, so every pre-existing caller renders exactly as before; 0.3.0 W7 export-dither+SRC: carries the two render settings that outlive one OutputSettings - dither() (TPDF, OFF by default) and srcQuality() (default SrcQuality::Linear) - defaulted from ExportRenderSettings so a caller that does not mention them inherits the agent's choice; feature row 24 (meter surface): the loudness-report flag now defaults from ExportRenderSettings::loudnessReport(), the same rule dither() and srcQuality() follow, so an agent's export.set_loudness_report reaches the next render; m_loudnessReport moved after m_srcQuality so the constructor's initialiser list is in declaration order (this tree builds with -Werror)
include/PatchWiring.h                                    fork-NEW (allowed)
include/PatternClip.h                                    declared divergence -> ARCH-4 S1b (SPEC-ARCH-4 1.6.3, the coercion Track::loadTrack's unrecognised-child branch performed): publishes static classNodeName() returning "patternclip" and makes nodeName() return it, mirroring the idiom AutomationClip.h already carries (include/AutomationClip.h:213, read at src/tracks/InstrumentTrack.cpp:1131). The clip loader now asks each Clip class for its own element name instead of the loader and Clip::saveState() each spelling the literal, so the two cannot drift; nodeName()'s value is unchanged. Held by tests/src/core/UnclaimedElementsTest.cpp.
include/PatternTrack.h                                   declared divergence -> fix(control-undo): PatternTrack::patternIndex() reads the registry with s_infoMap.value(this) instead of s_infoMap[this] - a read must not register the caller, because the constructor derives the next pattern number from s_infoMap.size() (docs/CONTROL-UNDO-CONNECTION-DROP.md)
include/PluginBrowser.h                                  declared divergence -> plugin-scan: showEvent() declaration, for populating the plugin tree on first show instead of at construction (023770f7b)
include/PluginFactory.h                                  declared divergence -> plugin-scan: scan-cache/quarantine surface (ScanStats, scanCache(), scanReport()), a declared destructor for the host-owned cached-descriptor store, and the LMMS_TESTING instanceExists() hook (023770f7b)
include/PluginHostChunking.h                             fork-NEW (allowed)
include/PluginHostNotes.h                                fork-NEW (allowed)
include/PluginScanCache.h                                fork-NEW (allowed)
include/ProjectContainer.h                               fork-NEW (allowed)
include/ProjectIds.h                                     fork-NEW (allowed)
include/ProjectJournal.h                                 declared divergence -> #623 SPEC A16: additive checkpoint flavours - a COMPOSITE checkpoint (several objects restored in one pop, so track.set_solo is one Ctrl+Z) and an ACTION checkpoint (a recorded inverse operation, for a created/deleted object that has no live state to restore). The single-object path is an unchanged one-entry case (post-alpha/reversibility); #623 bounded undo (030/w11-undo-depth): the stack is now bounded TWO ways - a count cap (maxUndoStates(), default MAX_UNDO_STATES = 100) and a byte budget (maxUndoBytes(), default 16 MiB) - evicting FIFO with the newest step never dropped and every eviction counted; plus the coalescing primitive coalesceTopStepIntoPrevious() and the step serials a transaction record uses to detect that a bound has evicted the step it describes. The caps and the accounting are additive; no existing caller changes behaviour at the defaults; #664 structural undo (feature row 75): one more checkpoint FLAVOUR on the same class - addJournalStructure(), it is an action checkpoint PLUS the byte accounting an action checkpoint gets wrong for a structural payload (a captured track/device document is real bytes and was being charged as zero). Declared here because it is ProjectJournal's own member function; the implementation is the new fork TU src/core/ProjectJournalStructural.cpp, exactly as the bound lives in ProjectJournalBounds.cpp. No existing caller changes.
include/ProjectKey.h                                     fork-NEW (allowed)
include/ProjectRecovery.h                                fork-NEW (allowed)
include/ProjectRenderer.h                                declared divergence -> #618 loudness wiring: optional LoudnessReport on the render path (unique_ptr member + report accessors + loudnessReportReady signal); null and inert unless OutputSettings::loudnessReport(); auto-mastering (task #610): declares the static renderCount()/resetRenderCount() instrumentation (implemented in src/core/ProjectRenderer.cpp). Header-only addition -- no type, signature or default of any existing member changes.; 0.3.0 W7 export-dither+SRC: the renderer publishes the OutputSettings it was handed into ExportRenderSettings for the render's duration and holds the previous values to restore them (two members, declared after m_progress/m_abort so the initialiser order matches the declaration order)
include/ProjectRevisions.h                               fork-NEW (allowed)
include/ProvenanceSection.h                              fork-NEW (allowed)
include/Rack.h                                           fork-NEW (allowed)
include/RackMacros.h                                     fork-NEW (allowed)
include/RackNodes.h                                      fork-NEW (allowed)
include/RackZones.h                                      fork-NEW (allowed)
include/RecordingJournal.h                               fork-NEW (allowed)
include/RecordRingBuffer.h                               fork-NEW (allowed)
include/RemotePluginAudioPorts.h                         fork-NEW (allowed)
include/RemotePluginBase.h                               declared divergence -> #589 planar-ports migration: single-channel count ids retired in place (kept numeric to refuse a stale pre-#589 client loudly)
include/RemotePluginClient.h                             declared divergence -> #589 planar-ports migration: client process() over the host's planar shared block (const float* / float*)
include/RemotePlugin.h                                   declared divergence -> #589 planar-ports migration: sole ports-taking ctor, no-arg process(), planar updateAudioBuffer() layout contract; feature row 80 (030/out-of-process, board card #670): the client-exit path gains the crash accounting and two read-only accessors - hostProcessId()/hostClientExecutable() expose the pid and the client executable `oop.get_state` reports, and m_hostShutdownRequested marks the destructor's own deliberate shutdown so processFinished() records it as an exit and NOT as a crash (a mode switch that stops a healthy client must not look like a crash loop). Declarations and one bool only; the plugin's audio behaviour, its buffer layout and every existing member are untouched
include/RenderManager.h                                  declared divergence -> stem export (post-alpha/stem-export): StemExportOptions + exportStems()/stemFileName() added beside renderProject()/renderTracks(), whose behaviour is unchanged; #618 loudness wiring: forwards the finished report out of the renderer so the export dialog can show it; empty QString when no report was requested
include/RetroAudioCapture.h                              fork-NEW (allowed)
include/RetroAudioRing.h                                 fork-NEW (allowed)
include/RetroMidiCapture.h                               fork-NEW (allowed)
include/RetroMidiCaptureSettings.h                       fork-NEW (allowed)
include/RetroMidiClipWriter.h                            fork-NEW (allowed)
include/RetroMidiRing.h                                  fork-NEW (allowed)
include/RevisionTimeline.h                               fork-NEW (allowed)
include/RoutingChainNodes.h                              fork-NEW (allowed)
include/RoutingGraph.h                                   fork-NEW (allowed)
include/SafeStart.h                                      fork-NEW (allowed)
include/SampleClip.h                                     declared divergence -> #611 Slice 0: the authored SampleWindow member and its accessors; SampleClip's own header, no other clip type affected. #597 warp: the WarpMarkers member, the TempoMode/SourceTempo pair and clipFramesPerTick()/windowTicksFor()/rendersLinearly(), all additive to the frozen seam. fade/crossfade/clip-gain wave: no change here - the window and the warp map are untouched, and the fades ride on the base Clip; 0.3.0 row 30 pitch-preserving time-stretch: SampleClip gains the WarpStretchMode member (default Resample = the historical behaviour) and the 'stretch' attribute of the same <warp> element #597 already writes, so a clip that never asks serialises byte for byte as before; ARCH-4 S1b (SPEC-ARCH-4 1.6.3): publishes static classNodeName() returning "sampleclip" and makes nodeName() return it, mirroring AutomationClip.h, so the clip loader and Clip::saveState() cannot disagree about this class's element name. nodeName()'s value is unchanged and nothing else in the header moves. 040/arch4-s9 (2026-09-22, ARCH-4 slice S9, SPEC-ARCH-4 census row 5): the WarpTempoMode/WarpStretchMode enums moved to include/WarpMarkers.h and the four warp members (m_warp/m_tempoMode/m_sourceTempo/m_stretchMode) moved to Clip with their doc comments; the accessor API declared here is unchanged and now reads the members inherited from the base clip.
include/SampleFrameRingBuffer.h                          fork-NEW (allowed)
include/SamplePlayHandle.h                               declared divergence -> #611 Slice 0: the window snapshot member and the clip constructors that carry it; the preview/metronome constructors are unchanged. #597 warp: the warp snapshot (m_warp + the two rates + the timeline length) the handle renders at, defaulting to the natural rate so the preview/metronome constructors are unchanged. fade/crossfade/clip-gain wave: the ClipEdits snapshot and the envelope the handle applies per period in play(); the preview/metronome constructors keep the neutral default, so they are still unchanged; 0.3.0 row 30 pitch-preserving time-stretch: the handle snapshots the clip's stretch mode (m_preservePitch, false for every clip that did not ask) and owns the AudioStretcher it renders a rate change through; the preview/metronome constructors keep the resampler path
include/SampleRecordAccumulator.h                        fork-NEW (allowed)
include/SampleRecordHandle.h                             declared divergence -> post-alpha/recording-realtime D9c: the clip record path stages frames into a pre-allocated ring and assembles the take on the accumulator's own thread, replacing the per-period new SampleFrame[] and the QList of blocks
include/SampleTrack.h                                    declared divergence -> the folder-tracks feature (owner items 3+20+21) made Track::mixerChannelModel() VIRTUAL - see the include/Track.h entry above - so that routing mode can re-bind a child's own mixer channel through ONE virtual call instead of a dynamic_cast to the two track types that have a channel. This header's own definition of that accessor therefore gained the `override` keyword, so the compiler checks the signature against the base instead of the derived declaration silently HIDING it (a missing override would have made TrackFolder::routeChild() widen and set the range on the wrong model). ONE keyword added; the signature, the body and the return type are unchanged, so SampleTrack's channel routing is byte-for-byte the behaviour it had.
include/SampleWindow.h                                   fork-NEW (allowed)
include/ScriptApiVersion.h                               fork-NEW (allowed)
include/ScriptBindings.h                                 fork-NEW (allowed)
include/ScriptClock.h                                    fork-NEW (allowed)
include/ScriptConsole.h                                  fork-NEW (allowed)
include/ScriptDawBindings.h                              fork-NEW (allowed)
include/ScriptEngine.h                                   fork-NEW (allowed)
include/ScriptLuaQtTypes.h                               fork-NEW (allowed)
include/ScriptMemoryBudget.h                             fork-NEW (allowed)
include/ScriptPackage.h                                  fork-NEW (allowed)
include/SessionArrangementRecorder.h                     fork-NEW (allowed)
include/SessionFollow.h                                  fork-NEW (allowed)
include/SessionModel.h                                   fork-NEW (allowed)
include/SessionScheduler.h                               fork-NEW (allowed)
include/SmfInterchange.h                                 fork-NEW (allowed)
include/Song.h                                           declared divergence -> #594 Session View: gated WANT_SESSION_VIEW hook (SessionModel member + accessor behind #ifdef LMMS_HAVE_SESSION_VIEW); no API or behaviour change when the flag is off. Stem export (post-alpha/stem-export): setExportLengthOverrideBars()/setExportTailBars() accessors whose defaults (0/1) reproduce the historical export length exactly; post-alpha/midi-depth: midiSeed()/setMidiSeed() and m_midiSeed - the project seed the note probability and velocity jitter rolls use; save integrity (#594 follow-up): m_preservedSessionXml, the #else of that member, holds an unsupported <session> block so a build with the flag off can write it back instead of dropping it (docs/SAVELOAD-INTEGRITY.md); #594 Session View: gated WANT_SESSION_VIEW hook (SessionModel member + accessor behind #ifdef LMMS_HAVE_SESSION_VIEW); no API or behaviour change when the flag is off. #595: adds SessionScheduler member + accessor behind the same gate, so the audio path has somewhere to read the launch state from; #594 Session View: gated WANT_SESSION_VIEW hook (SessionModel member + accessor behind #ifdef LMMS_HAVE_SESSION_VIEW); no API or behaviour change when the flag is off. Stem export (post-alpha/stem-export): setExportLengthOverrideBars()/setExportTailBars() accessors whose defaults (0/1) reproduce the historical export length exactly; post-alpha/midi-depth: midiSeed()/setMidiSeed() and m_midiSeed - the project seed the note probability and velocity jitter rolls use; save integrity (#594 follow-up): m_preservedSessionXml, the #else of that member, holds an unsupported <session> block so a build with the flag off can write it back instead of dropping it (docs/SAVELOAD-INTEGRITY.md); #625 headless load: errors()/loadRefusal() accessors, so project.open answers with the per-item error list and a typed refusal instead of a dialog; guarded automation-restore keys (Darwin ctest cluster 2026-09-13): m_oldAutomatedValues holds QPointer keys, so Song::stop() cannot dereference a model destroyed between the automating frame and the restore (AutomationModesTest aborted with signal 11 at Song.cpp:778 on both macOS jobs); 0.3.0 W15 tempo map (D11): includes TempoMap.h and exposes tempoMap() (the TempoMapPublisher), tempoAtTick() and secondsAtTick(), plus the TempoMapPublisher member and the follower's applied-tempo memo. Additive only: with an empty or inactive map tempoAtTick() is the global tempo for every tick, so no existing reader changes behaviour (held by tests/src/core/TempoMapTest.cpp); 0.3.0 W18 modulation layer (#602, D11): includes ModulationLayer.h and exposes modulationLayer() (the ModulationLayerPublisher) plus the ModulationLayerPublisher member, and declares processModulation(). Additive only: with an empty layer processModulation() returns before copying the snapshot, so no existing reader changes behaviour (held by tests/src/core/ModulationLayerTest.cpp and ControlModulatorCommandsTest.cpp); 0.3.0 groove pool (docs/GROOVE-POOL.md): includes GroovePool.h, exposes groovePool() (the project's NAMED grooves - the timing and velocity feel of a note pattern) and holds the GroovePool member. Additive only: an empty pool is not persisted and is read by nobody but the groove.* commands, so no existing reader changes behaviour (held by tests/src/core/GrooveTemplateTest.cpp and tests/src/core/ControlGrooveCommandsTest.cpp); automation-touch-race: the static Song::forgetAutomatedModel() hook and its private s_automationCacheSong backing store - a destroyed AutomatableModel reports itself there, so the last-frame automated-value cache (walked by processAutomations() and stop()) can never end up naming a dead model (held by AutomationModesTest::testDestroyedControlIsNotDereferenced); 0.3.0 feature row 35 (chord track): includes include/ChordTrack.h and declares Song::chordTrack() (mutable and const) beside groovePool(), plus the m_chordTrack member. Nothing else in this header changes and no existing declaration moves: an EMPTY chord track is exactly the engine as it was before the feature and nothing here is consulted from a render path; import detection (feature row 34): declares the project's detected-key field - ProjectKey& projectKey() / const ProjectKey& projectKey(), one ProjectKey member and the include of include/ProjectKey.h - beside the groove pool's accessors, which is where every other piece of project-wide non-track state on this class lives. Nothing existing changes: no declaration, no default and no call site moves, and the member is inert until a detect.apply writes it (an empty ProjectKey persists nothing).; 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): declares buildAutomationRamps() - the once-per-block publisher of the per-sample ramps - beside processAutomations(), and its doc comment says why it is public (the allocation probe calls the same entry the engine calls); ARCH-4 S1b (SPEC-ARCH-4 1.6, unknown-field preservation): declares unclaimedElements() - the names of the <song> sections no reader in THIS build claimed, which is the report SPEC-ARCH-4 1.6.4 asks project.open to carry - and the m_unclaimedElements member behind it. Additive: a project this build understands completely captures nothing, so the report is empty and the file re-saves byte for byte. The one exclusion, stated on the accessor, is that a clip element is never "unclaimed". ARCH-4 S1c (SPEC-ARCH-4 1.6.2): unclaimedElements()'s doc comment now also names the third group it composes - the <trackcontainer> children this build could not CONSTRUCT, reported as `/song/trackcontainer/<name>[i]` - so the report's three path shapes are stated in one place. Comment only; no declaration, no default and no call site changes.
include/SrcQuality.h                                     fork-NEW (allowed)
include/StemSeparation/ExternalProcessStemSeparator.h    fork-NEW (allowed)
include/StemSeparation/OnnxRuntimeStemSeparator.h        fork-NEW (allowed)
include/StemSeparation/StemJobManager.h                  fork-NEW (allowed)
include/StemSeparation/StemModelStore.h                  fork-NEW (allowed)
include/StemSeparation/StemSeparator.h                   fork-NEW (allowed)
include/StemSeparation/StemTrackBuilder.h                fork-NEW (allowed)
include/StemSeparation/StemTypes.h                       fork-NEW (allowed)
include/StemSplitController.h                            fork-NEW (allowed)
include/TakeLane.h                                       fork-NEW (allowed)
include/TelemetryConsentDialog.h                         fork-NEW (allowed)
include/Telemetry.h                                      fork-NEW (allowed)
include/TelemetryNetworkTransport.h                      fork-NEW (allowed)
include/TempoMap.h                                       fork-NEW (allowed)
include/Timeline.h                                       declared divergence -> 0.3.0 punch in/out: declares the transport's punch region (punchBegin/punchEnd/punchEnabled, punchArmed(), punchCapturesAt() - the capture gate as a pure predicate - shouldPersistPunch(), setPunchRange/setPunchEnabled/clearPunch, the punchChanged() signal and the three members) and writes the region onto the <timeline> element through saveSettings/loadSettings, the two virtuals it already overrode. Written ONLY when the region is set or armed, and CLEARED by loadSettings when the attributes are absent, so a project that never punched re-saves byte for byte as it always did and a pre-punch journal checkpoint can still take the region back off. No existing declaration, attribute or value changes; no UI.
include/TrackContainer.h                                 declared divergence -> fix(control-undo): new signal TrackContainer::aboutToClearTracks(), emitted before a serialization restore deletes this container's tracks so its views can take themselves down first (docs/UNDO-RELEASE-CONFIG.md). ARCH-4 S1c (SPEC-ARCH-4 1.6.1/1.6.2): adds unclaimedChildren() - the <trackcontainer> children this build could not construct, the container's half of the same preservation Set on <song> (include/Song.h) and Track (include/Track.h) - and the m_unclaimedChildren member behind it, plus the include of UnclaimedElements.h. Additive: the list is empty for a document this build understands completely, so nothing reads it and no existing declaration moves. The file stays well under the 500-line limit (268 lines).
include/TrackContainerView.h                             declared divergence -> fix(control-undo): TrackContainerView::removeAllTrackViews() slot - answers aboutToClearTracks() by deleting this container's views while the tracks they view are still alive (docs/UNDO-RELEASE-CONFIG.md)
include/TrackFolder.h                                    fork-NEW (allowed)
include/Track.h                                          declared divergence -> #628 SPEC-stable-ids slice 1: Track gains id()/setId() and the m_id member - the number in trk-<n> is assigned at construction and kept until the track dies (SPEC-stable-ids.md 2.1); comping (task #600): Track gains takeLanes() and the m_takeLanes member - the take-lane container and the composite view of docs/COMPING.md, a CHILD RELATIONSHIP of the track and not a second track type (the takes stay in the track's own clip list, tagged by Clip::laneIndex), serialised by Track::saveTrack as one <takelanes> element and empty - writing nothing - on a track that never comped; freeze / bounce-in-place: Track gains the frozen-take state (FrozenTake, isFrozen()/frozenTake()/frozenAudioReady(), freezeTo()/unfreeze()/clearFrozenTake(), the protected playFrozenTake() the two concrete play() overloads call, and the m_frozen/m_frozenBuffer/m_frozenTakePlaying members) - a track's own output rendered to audio and played instead of its clips, which the track serialises as one <frozen> element and which cannot sound unless the state is ON the track; ARCH-4 S1b (SPEC-ARCH-4 1.6, unknown-child preservation on <track>): declares unclaimedChildren() - the children of this track that no reader claimed - and the m_unclaimedChildren member. Its doc comment states the ONE exclusion: a CLIP element is not among them, because the loader claims it as a real Clip (claimClipOrPreserveChild() in src/core/Track.cpp), so a track's own clips are never carried twice. Otherwise a comment-only change; the file stays under the 500-line limit.
include/TrackRecorder.h                                  fork-NEW (allowed)
include/UnattendedRun.h                                  fork-NEW (allowed)
include/UnclaimedElements.h                              fork-NEW (allowed)
include/VcaGroup.h                                       fork-NEW (allowed)
include/WarpMarkers.h                                    fork-NEW (allowed)
include/WcagContrast.h                                   fork-NEW (allowed)
include/zene/api/ControlApi.h                            fork-NEW (allowed)
INSTALL.txt                                              declared divergence -> wave R rename (018d2041f): the build instructions said the sources are built into 'LMMS' -> 'Zene Studio'. Documentation text only.; wave R rename: install instructions name the renamed executable and packages
plugins/Amplifier/Amplifier.cpp                          declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/BassBooster/BassBooster.cpp                      declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Bitcrush/Bitcrush.cpp                            declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/CarlaBase/Carla.cpp                              declared divergence -> rename layer 1: the Carla host UI name is "CarlaRack-Zene"/"CarlaPatchbay-Zene" (was the old product name), which a user sees in Carla's own window
plugins/ClapEffect/ClapBusMap.cpp                        fork-NEW (allowed)
plugins/ClapEffect/ClapBusMap.h                          fork-NEW (allowed)
plugins/ClapEffect/ClapEffectControlDialog.cpp           fork-NEW (allowed)
plugins/ClapEffect/ClapEffectControlDialog.h             fork-NEW (allowed)
plugins/ClapEffect/ClapEffectControls.cpp                fork-NEW (allowed)
plugins/ClapEffect/ClapEffectControls.h                  fork-NEW (allowed)
plugins/ClapEffect/ClapEffect.cpp                        fork-NEW (allowed)
plugins/ClapEffect/ClapEffect.h                          fork-NEW (allowed)
plugins/ClapEffect/ClapHost.cpp                          fork-NEW (allowed)
plugins/ClapEffect/ClapHost.h                            fork-NEW (allowed)
plugins/ClapEffect/ClapHostInternals.h                   fork-NEW (allowed)
plugins/ClapEffect/ClapHostLifecycle.cpp                 fork-NEW (allowed)
plugins/ClapEffect/ClapHostNotes.cpp                     fork-NEW (allowed)
plugins/ClapEffect/ClapHostParams.cpp                    fork-NEW (allowed)
plugins/ClapEffect/ClapLoader.cpp                        fork-NEW (allowed)
plugins/ClapEffect/ClapLoader.h                          fork-NEW (allowed)
plugins/ClapEffect/ClapNoteQueue.h                       fork-NEW (allowed)
plugins/ClapEffect/ClapParamDescriptor.h                 fork-NEW (allowed)
plugins/ClapEffect/ClapParameter.cpp                     fork-NEW (allowed)
plugins/ClapEffect/ClapParameter.h                       fork-NEW (allowed)
plugins/ClapEffect/ClapSubPluginFeatures.cpp             fork-NEW (allowed)
plugins/ClapEffect/ClapSubPluginFeatures.h               fork-NEW (allowed)
plugins/ClapEffect/CMakeLists.txt                        build/config (allowed)
plugins/ClapInstrument/ClapInstrument.cpp                fork-NEW (allowed)
plugins/ClapInstrument/ClapInstrument.h                  fork-NEW (allowed)
plugins/ClapInstrument/ClapInstrumentView.cpp            fork-NEW (allowed)
plugins/ClapInstrument/ClapInstrumentView.h              fork-NEW (allowed)
plugins/ClapInstrument/CMakeLists.txt                    build/config (allowed)
plugins/ClapInstrument/logo.png                          declared divergence -> feature row 79 (030/clap-instrument, board task #669, 2026-09-16): the new CLAP instrument module's own logo, the asset plugins/ClapInstrument/CMakeLists.txt installs and ClapInstrument.cpp loads through PluginPixmapLoader("logo") - every plugin module in this tree carries one (its bytes are those of plugins/ClapEffect/logo.png, the CLAP sibling, md5 a7c6655d2b304274af6687b9c8314e2c). It is a NEW path under plugins/, an upstream directory, so it is neither inherited nor derivable by either scope manifest: tests/fork-sources.txt's and tests/all-sources.txt's recipes admit source extensions only (.py .sh .cpp .c .h .hpp .cc .cxx), so a .png can never be derived and listing it would break their own REPRODUCES check - the same reason the brand-placeholder artwork is declared in this ledger rather than in a scope manifest (see the cmake/nsis/assets/Logo.png entry). No behaviour, no build flag, no option and no existing asset changes.
plugins/Compressor/Compressor.cpp                        declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/CrossoverEQ/CrossoverEQ.cpp                      declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Delay/DelayEffect.cpp                            declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Dispersion/Dispersion.cpp                        declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/DualFilter/DualFilter.cpp                        declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/DynamicsProcessor/DynamicsProcessor.cpp          declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Eq/EqEffect.cpp                                  declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Flanger/FlangerEffect.cpp                        declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/FrequencyShifter/FrequencyShifterEffect.cpp      declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/GigPlayer/CMakeLists.txt                         build/config (allowed)
plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/HydrogenImport/HydrogenImport.cpp                declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/LadspaEffect/LadspaEffect.cpp                    declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo; #625 headless load: the 'Can't get LADSPA descriptor function' and 'Plugin has no processor' boxes are not opened in an unattended run (they fire while a project that references the plugin loads); tag v0.2.1-alpha CI fix: the qWarning() << ... branch added above needs <QDebug> included by name -- <QtGlobal> only forward-declares the class, so the Qt5 jobs (linux-x86_64, linux-arm64) would have failed with 'invalid use of incomplete type class QDebug' at the next build. Same defect class as src/core/ConfigManager.cpp; see docs/QDEBUG-CLASS-AND-MIME-RENAME.md
plugins/LOMM/LOMM.cpp                                    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Lv2Effect/Lv2Effect.cpp                          declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/Lv2Instrument/Lv2Instrument.cpp                  declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/MidiExport/MidiExport.cpp                        declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/MidiImport/CMakeLists.txt                        build/config (allowed)
plugins/MidiImport/MidiImport.cpp                        declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo; #625 headless load: the 'Setup incomplete' boxes are not opened in an unattended run (the import path is reachable from --import at startup)
plugins/MultitapEcho/MultitapEcho.cpp                    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/NeuralAmp/CMakeLists.txt                         build/config (allowed)
plugins/NeuralAmp/LICENSE-NOTICE.md                      docs (allowed)
plugins/NeuralAmp/models/README.md                       docs (allowed)
plugins/NeuralAmp/NeuralAmpControlDialog.cpp             fork-NEW (allowed)
plugins/NeuralAmp/NeuralAmpControlDialog.h               fork-NEW (allowed)
plugins/NeuralAmp/NeuralAmpControls.cpp                  fork-NEW (allowed)
plugins/NeuralAmp/NeuralAmpControls.h                    fork-NEW (allowed)
plugins/NeuralAmp/NeuralAmpEffect.cpp                    fork-NEW (allowed)
plugins/NeuralAmp/NeuralAmpEffect.h                      fork-NEW (allowed)
plugins/NeuralAmp/tests/CMakeLists.txt                   build/config (allowed)
plugins/Oscilloscope/Oscilloscope.cpp                    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Oscilloscope/OscilloscopeGraph.cpp               declared divergence -> compile-only Qt6 fix: QMouseEvent::x()/y() via the repo's own lmms::position() adapter (2c77fd71a)
plugins/PeakControllerEffect/PeakControllerEffect.cpp    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/ReverbSC/ReverbSC.cpp                            declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/RnnoiseDenoiser/CMakeLists.txt                   build/config (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.cpp fork-NEW (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.h   fork-NEW (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.cpp      fork-NEW (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.h        fork-NEW (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp        fork-NEW (allowed)
plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h          fork-NEW (allowed)
plugins/Sfxr/Sfxr.cpp                                    declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/SlewDistortion/SlewDistortionControlDialog.cpp   declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/SlewDistortion/SlewDistortion.cpp                declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/SpectrumAnalyzer/Analyzer.cpp                    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/StereoEnhancer/StereoEnhancer.cpp                declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/StereoMatrix/StereoMatrix.cpp                    declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Vectorscope/Vectorscope.cpp                      declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Vestige/Vestige.cpp                              declared divergence -> #589 planar-ports migration: play() replaced by processImpl()/processLock(); ports controller passed to VstPlugin; rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/Vestige/Vestige.h                                declared divergence -> #589 planar-ports migration: VestigeInstrument is an AudioPlugin with dynamic planar ports
plugins/Vst3Effect/CMakeLists.txt                        build/config (allowed)
plugins/Vst3Effect/Vst3BusMap.cpp                        fork-NEW (allowed)
plugins/Vst3Effect/Vst3BusMap.h                          fork-NEW (allowed)
plugins/Vst3Effect/Vst3EffectControlDialog.cpp           fork-NEW (allowed)
plugins/Vst3Effect/Vst3EffectControlDialog.h             fork-NEW (allowed)
plugins/Vst3Effect/Vst3EffectControls.cpp                fork-NEW (allowed)
plugins/Vst3Effect/Vst3EffectControls.h                  fork-NEW (allowed)
plugins/Vst3Effect/Vst3Effect.cpp                        fork-NEW (allowed)
plugins/Vst3Effect/Vst3Effect.h                          fork-NEW (allowed)
plugins/Vst3Effect/Vst3Host.cpp                          fork-NEW (allowed)
plugins/Vst3Effect/Vst3Host.h                            fork-NEW (allowed)
plugins/Vst3Effect/Vst3MidiEvent.cpp                     fork-NEW (allowed)
plugins/Vst3Effect/Vst3MidiEvent.h                       fork-NEW (allowed)
plugins/Vst3Effect/Vst3MidiQueue.h                       fork-NEW (allowed)
plugins/Vst3Effect/Vst3ParamDescriptor.h                 fork-NEW (allowed)
plugins/Vst3Effect/Vst3Parameter.cpp                     fork-NEW (allowed)
plugins/Vst3Effect/Vst3Parameter.h                       fork-NEW (allowed)
plugins/Vst3Effect/Vst3SubPluginFeatures.cpp             fork-NEW (allowed)
plugins/Vst3Effect/Vst3SubPluginFeatures.h               fork-NEW (allowed)
plugins/Vst3Instrument/CMakeLists.txt                    build/config (allowed)
plugins/Vst3Instrument/logo.png                          fork-NEW (allowed)
plugins/Vst3Instrument/Vst3Instrument.cpp                fork-NEW (allowed)
plugins/Vst3Instrument/Vst3Instrument.h                  fork-NEW (allowed)
plugins/Vst3Instrument/Vst3InstrumentView.cpp            fork-NEW (allowed)
plugins/Vst3Instrument/Vst3InstrumentView.h              fork-NEW (allowed)
plugins/VstBase/RemoteVstPlugin.cpp                      declared divergence -> #589 planar-ports migration: remote VST host reads/writes planar planes at i * bufferSize()
plugins/VstBase/vst_base.cpp                             declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/VstBase/VstPlugin.cpp                            declared divergence -> #589 planar-ports migration: setSplittedChannels() gone; ports channel counts round-trip in load/saveSettings
plugins/VstBase/VstPlugin.h                              declared divergence -> #589 planar-ports migration: VstPlugin takes the ports controller instead of configuring split channels
plugins/VstEffect/VstEffectControlDialog.cpp             declared divergence -> rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/VstEffect/VstEffect.cpp                          declared divergence -> #589 planar-ports migration: no-arg process(), planar wet/dry mixing, lock moved to processLock/processUnlock; rename layer 1: user-visible plugin/module description string says Zene Studio; logo resource key zene-plugin-logo
plugins/VstEffect/VstEffect.h                            declared divergence -> #589 planar-ports migration: VstEffect is an AudioPlugin with dynamic planar ports
plugins/WasmEffect/WasmEffectControlDialog.cpp           fork-NEW (allowed)
plugins/WasmEffect/WasmEffectControlDialog.h             fork-NEW (allowed)
plugins/WasmEffect/WasmEffectControls.cpp                fork-NEW (allowed)
plugins/WasmEffect/WasmEffectControls.h                  fork-NEW (allowed)
plugins/WasmEffect/WasmEffect.cpp                        fork-NEW (allowed)
plugins/WasmEffect/WasmEffect.h                          fork-NEW (allowed)
plugins/WaveShaper/WaveShaper.cpp                        declared divergence -> rename layer 1: built-in plugin logo resource key is zene-plugin-logo (display only, no behavioural change)
plugins/Xpressive/CMakeLists.txt                         build/config (allowed)
plugins/ZynAddSubFx/LocalZynAddSubFx.cpp                 declared divergence -> #589 planar-ports migration: render straight into the planar port buffers, dropping the scratch copy
plugins/ZynAddSubFx/LocalZynAddSubFx.h                   declared divergence -> #589 planar-ports migration: processAudio(SampleFrame*) -> process(PlanarBufferView<float, 2>)
plugins/ZynAddSubFx/RemoteZynAddSubFx.cpp                declared divergence -> #589 planar-ports migration: client reports (0, 2) and renders into the two planar output planes
plugins/ZynAddSubFx/ZynAddSubFx.cpp                      declared divergence -> #589 planar-ports migration: play() -> processImpl(); setBufferType()/activate() on the local path; ports controller passed to the remote plugin; post-alpha/oop-hosting: the persisted toggle selects the client process or the in-process synth, and the Zyn window is only requested when the user asked for it; feature row 80 (030/out-of-process, board card #670): defines hostingProcessId() and setHostingMode(bool), and the instrument view's separateProcessToggled() now calls setHostingMode() instead of setting m_separateProcessModel and calling reloadPlugin() itself - the same two operations, in one place, so the interface and the control surface cannot drift. A project that never chooses the separate process behaves exactly as before
plugins/ZynAddSubFx/ZynAddSubFx.h                        declared divergence -> #589 planar-ports migration: instrument is a ConfigurableAudioPorts AudioPlugin (remote/local buffer switch); post-alpha/oop-hosting: per-instance `separateprocess` BoolModel + hostingState() for the run-in-a-separate-process toggle; feature row 80 (030/out-of-process, board card #670): declares two Q_INVOKABLE members - hostingProcessId() (the live client's pid, 0 when the in-process synth runs) and setHostingMode(bool) (the ONE settle point for the hosting choice, so the instrument view's checkbox, a project's `separateprocess` attribute and the new oop.set_mode all end in the same call). The existing declaration set, the class hierarchy and hostingState() are untouched
README.md                                                docs (allowed)
recording/DATA-FLOW.md                                   docs (allowed)
RESUME-NOTES.md                                          docs (allowed)
scripts/capabilities-dump.py                             fork-NEW (allowed)
SECURITY.md                                              docs (allowed)
src/3rdparty/CMakeLists.txt                              build/config (allowed)
src/CMakeLists.txt                                       build/config (allowed)
src/core/audio/AudioAlsa.cpp                             declared divergence -> 0.3.0 recording engine surface (feature row 64, docs/RECORD-INPUTS.md): the ALSA CAPTURE PATH - the first snd_pcm_readi this tree has ever had. A second PCM is opened for SND_PCM_STREAM_CAPTURE on its own thread (a blocking read on the playback thread would stall the render, and the capture->engine handover is the same pushInputFrames/pushInputFramesWide one the JACK and SDL backends use from their callbacks), with the channel count taken from the audioinput/channels configuration - the ARBITRARY INPUT COUNT - and FLOAT preferred to S16_LE. The loop waits with a bounded 200 ms timeout so the stop flag is honoured even on a silent device, allocates nothing, and publishes every block to BOTH input paths: the N-channel wide stage and the stereo bus carrying the configured channel pair. What the device granted is published through AudioInputPath so record.input_get_state can report 'no capture path' and 'the device refused' as the different facts they are. The playback path (open, hwparams, swparams, run loop, error recovery) is byte-identical apart from the start/stop hooks that bring the capture thread up and take it down.
src/core/audio/AudioFileFlac.cpp                         declared divergence -> rename layer 1: the software tag written into rendered audio files names Zene Studio
src/core/audio/AudioFileMP3.cpp                          declared divergence -> rename layer 1: the software tag written into rendered audio files names Zene Studio
src/core/audio/AudioFileOgg.cpp                          declared divergence -> rename layer 1: the software tag written into rendered audio files names Zene Studio
src/core/audio/AudioFileWave.cpp                         declared divergence -> rename layer 1: the software tag written into rendered audio files names Zene Studio; 0.3.0 W7 export-dither: writeBuffer draws the TPDF dither immediately before the quantiser (the 16-bit integer path and the 24-bit float path libsndfile quantises) when OutputSettings::dither() is on. The dither call is ABSENT when it is off, which is the default, so the bytes are unchanged
src/core/audio/AudioJack.cpp                             declared divergence -> rename layer 1: the host-visible audio/MIDI client name is Zene Studio (was the old product name)
src/core/audio/AudioPulseAudio.cpp                       declared divergence -> rename layer 1: the host-visible audio/MIDI client name is Zene Studio (was the old product name)
src/core/audio/AudioSoundIo.cpp                          declared divergence -> rename layer 1: the host-visible audio/MIDI client name is Zene Studio (was the old product name)
src/core/AudioBuffer.cpp                                 declared divergence -> #608 memory correctness: access buffer always from the default resource; allocationSize() no longer reserves it in the shared region (746f6bd0e)
src/core/AudioBusHandle.cpp                              declared divergence -> #605 PDC: delay each track output to its alignment point (9e12a68f5); mixer concurrency audit D6: the m_playHandles iteration takes m_playHandleLock, the lock its writers already hold (post-alpha/mixer-concurrency)
src/core/AudioEngine.cpp                                 declared divergence -> post-alpha/recording-realtime D9b/D9a: pushInputFrames() no longer takes m_changeMutex on the JACK/SDL capture thread and no longer doubles the input buffer; swapBuffers() drains the fixed-capacity staging ring instead; test hygiene (post-alpha/test-hygiene): ~AudioEngine joined each worker thread with a fixed wait(500), so a worker that had not finished winding down when the budget expired was left running, and the QThread objects are children of the engine (~QObject deletes them) - so the QThread was destroyed while its thread ran and Qt answered with qFatal("QThread: Destroyed while thread is still running")/SIGABRT inside Engine::destroy(), reported as a failure of whichever test was executing (measured: 13 of 64 PdcMixerTest runs aborted in cleanupTestCase(), exit 134, load average ~27, gdb showing exactly one worker of 19 still parked in QWaitCondition::wait()). The join is now unbounded and terminates because each worker re-checks m_quit inside the quit-recheck interval; the audio path is untouched. Held by tests/src/core/AudioEngineTeardownTest.cpp.; #626: record which configured audio device failed to open and why (the sentence control.ping and the typed 'requires' refusal hand to an agent) instead of reporting a bare false (post-alpha/control-hardening); feature row 24 (meter surface): constructs the one MasterLoudnessTap in the constructor (the engine's processing rate, stereo) and feeds it the period renderStageMix() has just mixed - one relaxed atomic load per audio period while the tap is disarmed, so an unarmed render is unchanged and the tap only reads; 0.3.0 recording engine surface (feature rows 14/16/64): constructs m_recorder with the CONFIGURED input channel count as each route's selectable width and MultiTrackRecorder::MaxRoutes as the route count (no device needed - it is a config read), allocates the AudioWideInputStage once in the constructor beside the stereo staging ring, drains it once per rendered period in swapBuffers(), feeds it and the retro window from renderNextPeriod()'s STAGE 4, and implements pushInputFramesWide()/the five wide-input accessors with the same contract pushInputFrames() documents (no lock, no allocation, no growth; a full stage drops and counts). The frame the ALSA capture thread used to leave empty is now filled when a capture device opens; nothing changes when none does.
src/core/AudioEngineWorkerThread.cpp                     declared divergence -> render determinism (render-determinism lane): storage for that switch plus the inline branch in startAndWaitForJobs(); test hygiene (post-alpha/test-hygiene): run() read m_quit at the top of the loop outside the mutex and then blocked in QWaitCondition::wait() with no deadline, so a worker preempted between the read and the wait missed the single wake that quit()+startAndWaitForJobs() issue and slept for good - that stranded worker is the one the old fixed wait(500) in ~AudioEngine gave up on, producing the abort above. The wait is now bounded by 100 ms so a lost wake-up delays the exit instead of stranding the thread; the rendering path still wakes every worker once per period, so nothing changes while the engine renders. Held by tests/src/core/AudioEngineTeardownTest.cpp.; render determinism (Darwin ctest cluster 2026-09-13): run() no longer drains the job queue while deterministicProcessing() is on, so a pool tick cannot take an offline render's jobs (RenderJobQueueTest::inlineModeNeverLetsThePoolTakeAJob failed on both macOS jobs and RenderJobQueueTest gained inlineModeSurvivesWorkerTicksWhileJobsAreQueued)
src/core/AudioInputPath.cpp                              fork-NEW (allowed)
src/core/audio/MultiTrackRecorder.cpp                    fork-NEW (allowed)
src/core/AudioPortsModel.cpp                             fork-NEW (allowed)
src/core/AudioResampler.cpp                              declared divergence -> 0.3.0 W7 export SRC quality+ratio convention: implements setMode()/modeForSrcQuality() and documents the convention at the single line where the engine's ratio crosses into libsamplerate's src_ratio. The value handed to libsamplerate is UNCHANGED; the claim that this line was inverted is refuted by measurement in tests/src/core/AudioResamplerRatioTest.cpp, and inverting it is what would have changed the pitch of every mismatched-rate project
src/core/AudioStretcher.cpp                              fork-NEW (allowed)
src/core/audio/TrackRecorder.cpp                         fork-NEW (allowed)
src/core/AudioWideInputStage.cpp                         fork-NEW (allowed)
src/core/AutomatableModel.cpp                            declared divergence -> post-alpha/automation-modes: the mode state machine, the transport run token, the monotone clock and the trim offset; appended in its own banner-marked block at the end of the file; automation-touch-race: the destructor reports itself to the song (Song::forgetAutomatedModel) so the song's last-frame automated-value cache drops a control that is destroyed while the transport runs, instead of dereferencing it in the next processAutomations()/stop() (held by AutomationModesTest::testDestroyedControlIsNotDereferenced); 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): valueBuffer() gains ONE branch - a published ramp fills the per-sample buffer from the curve - and the two accessors are defined below it; the controller path, the linked-model path and the old-new interpolation path are unchanged and remain the fallback for every block that publishes nothing
src/core/AutomationClip.cpp                              declared divergence -> 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): defines setSampleAccurate()/rampValueAt()/addRampKnot()/writeBlockRamp(), and saveSettings() writes <automationclip sample_accurate> ONLY when the flag is on while loadSettings() RESETS it on absence (which is what lets a checkpoint taken before the first edit take the flag back). The node parsing, the other attributes and valueAt() are untouched
src/core/AutomationRamp.cpp                              fork-NEW (allowed)
src/core/BounceInPlace.cpp                               fork-NEW (allowed)
src/core/BrowserCatalog.cpp                              fork-NEW (allowed)
src/core/BrowserMetadata.cpp                             fork-NEW (allowed)
src/core/BrowserPeakCache.cpp                            fork-NEW (allowed)
src/core/BrowserTagStore.cpp                             fork-NEW (allowed)
src/core/ChordDetect.cpp                                 fork-NEW (allowed)
src/core/ChordProgression.cpp                            fork-NEW (allowed)
src/core/ChordTrack.cpp                                  fork-NEW (allowed)
src/core/ChordVocabulary.cpp                             fork-NEW (allowed)
src/core/Clip.cpp                                        declared divergence -> #611 Slice 0 relocated the clip->source mapping here, so this file is already an inherited one this fork owns. fade/crossfade/clip-gain wave: saveClipEdits/loadClipEdits (attributes gain/fadein/fadeout/fadeinshape/fadeoutshape, each written ONLY when it differs from the neutral default) and the ClipEdits copy in the copy constructor, so a clone or a split carries its fades; comping (task #600): the same saveClipEdits/loadClipEdits pair is where a clip's take-lane tag is written (`lane`, only when above 0) and read back (reset to 0 when the attribute is absent - the reset is what makes the pre-assignment state reachable from a journal checkpoint), plus the m_laneIndex copy in the copy constructor; stable ids slice 2 (feature row 51): the Clip constructor allocates the id, saveState writes it as an `id` attribute on the clip's own element and restoreState takes the file's value (or keeps the constructor's, counted); the override calls JournallingObject::saveState/restoreState BY NAME - SerializingObject's drops the journallingObject child and takes the undo mechanism with it - and a clip built from a copy payload keeps its own id (rule R4/R5); 040/arch4-s4 (2026-09-22, ARCH-4 slice S4, SPEC-ARCH-4 1.7 Requirement 6): the lane/link reset-on-absence defaults are declared once in a file-local CLIP_RESTORE_SCHEMA and read through clipRestoreDefault() - same defaults as before ("0", "0"), same std::max clamp, comment consolidated onto the schema; the link comment keeps its SPEC-stable-ids R3 observe-rationale at the site. Proven by ControlLinkCommandsTest, TakeLaneCompTest and the registered write-refusal gate. 040/arch4-s9 (2026-09-22, ARCH-4 slice S9, SPEC-ARCH-4 census row 5): saveWarp/loadWarp - the `<warp>` child formerly written by SampleClip::saveSettings (498-527) and read by SampleClip::loadSettings (589-635) - called from saveState/restoreState; the reader runs right after loadSettings and UNCONDITIONALLY before the copy-payload guard, because every payload carried the child (a pasted/cloned clip keeps its warp), and reset-on-absence is kept in its else branch (SPEC-ARCH-4 1.7 R6 form for a child element); the copy constructor copies the four warp members. Output stays byte-identical: the child is insertBefore'd into the position saveSettings used to give it ([warp][journallingObject]), and the revision fingerprint still stamps last, after it. Negative control: neutering the else branch fails ClipWarpPersistenceTest::restoringAnElementWithoutAWarpUnwarpsTheClip and ControlWarpCommandsTest::markerEditsAreReversibleThroughTheJournal (measured, docs/s9-logs/negative-control.md).
src/core/ClipEdits.cpp                                   fork-NEW (allowed)
src/core/ClipLinks.cpp                                   fork-NEW (allowed)
src/core/CMakeLists.txt                                  build/config (allowed)
src/core/ConfigManager.cpp                               declared divergence -> wave R rename (018d2041f): the bundled data search path moved from share/lmms/ to share/zene/, and the build-tree discovery moved from the lmms_SOURCE_DIR/lmms_BINARY_DIR CMake cache entries to the zene_ spelling the renamed CMake project writes (the old lmms_ entry is still accepted, so an existing build tree keeps working).; wave R rename: runtime data/working paths derive from the renamed install layout; #625 headless load: the config parse-error and config-write-failure boxes are not opened in an unattended run (saveConfigFile is reachable from project.open via the recent-projects list); tag v0.2.0-alpha CI fix: saveConfigFile's unattended qWarning() branch needs <QDebug> included by name -- <QtGlobal> only forward-declares QDebug, so the linux-x86_64 and linux-arm64 jobs failed with 'invalid use of incomplete type class QDebug' (Qt5; Qt6 pulls QDebug in transitively through <QApplication>, which is why the same tree builds on a Qt6-only box)
src/core/ControlAutomationSupport.cpp                    fork-NEW (allowed)
src/core/ControlBrowserSupport.cpp                       fork-NEW (allowed)
src/core/ControlChainPresetSupport.cpp                   fork-NEW (allowed)
src/core/ControlChordSupport.cpp                         fork-NEW (allowed)
src/core/ControlCommandsArrangement.cpp                  fork-NEW (allowed)
src/core/ControlCommandsArrangementState.cpp             fork-NEW (allowed)
src/core/ControlCommandsAutomation.cpp                   fork-NEW (allowed)
src/core/ControlCommandsAutomationEdit.cpp               fork-NEW (allowed)
src/core/ControlCommandsAutomationRamp.cpp               fork-NEW (allowed)
src/core/ControlCommandsBrowser.cpp                      fork-NEW (allowed)
src/core/ControlCommandsBrowserTags.cpp                  fork-NEW (allowed)
src/core/ControlCommandsBus.cpp                          fork-NEW (allowed)
src/core/ControlCommandsChain.cpp                        fork-NEW (allowed)
src/core/ControlCommandsChainEdit.cpp                    fork-NEW (allowed)
src/core/ControlCommandsChord.cpp                        fork-NEW (allowed)
src/core/ControlCommandsChordEdit.cpp                    fork-NEW (allowed)
src/core/ControlCommandsChordWrite.cpp                   fork-NEW (allowed)
src/core/ControlCommandsClip.cpp                         fork-NEW (allowed)
src/core/ControlCommandsClipEdits.cpp                    fork-NEW (allowed)
src/core/ControlCommandsClipLink.cpp                     fork-NEW (allowed)
src/core/ControlCommandsClipLinkState.cpp                fork-NEW (allowed)
src/core/ControlCommandsClipTrim.cpp                     fork-NEW (allowed)
src/core/ControlCommandsClock.cpp                        fork-NEW (allowed)
src/core/ControlCommandsComp.cpp                         fork-NEW (allowed)
src/core/ControlCommandsCompEdits.cpp                    fork-NEW (allowed)
src/core/ControlCommandsControl.cpp                      fork-NEW (allowed)
src/core/ControlCommandsController.cpp                   fork-NEW (allowed)
src/core/ControlCommandsControllerTemplates.cpp          fork-NEW (allowed)
src/core/ControlCommandsCrashControl.cpp                 fork-NEW (allowed)
src/core/ControlCommandsCrash.cpp                        fork-NEW (allowed)
src/core/ControlCommandsDawProject.cpp                   fork-NEW (allowed)
src/core/ControlCommandsDetectApply.cpp                  fork-NEW (allowed)
src/core/ControlCommandsDetect.cpp                       fork-NEW (allowed)
src/core/ControlCommandsDevice.cpp                       fork-NEW (allowed)
src/core/ControlCommandsDsp.cpp                          fork-NEW (allowed)
src/core/ControlCommandsExport.cpp                       fork-NEW (allowed)
src/core/ControlCommandsExportPresets.cpp                fork-NEW (allowed)
src/core/ControlCommandsFeedback.cpp                     fork-NEW (allowed)
src/core/ControlCommandsFreeze.cpp                       fork-NEW (allowed)
src/core/ControlCommandsGroove.cpp                       fork-NEW (allowed)
src/core/ControlCommandsGrooveEdit.cpp                   fork-NEW (allowed)
src/core/ControlCommandsGroovePool.cpp                   fork-NEW (allowed)
src/core/ControlCommandsHostChunking.cpp                 fork-NEW (allowed)
src/core/ControlCommandsHostNotes.cpp                    fork-NEW (allowed)
src/core/ControlCommandsIdContract.cpp                   fork-NEW (allowed)
src/core/ControlCommandsInterchange.cpp                  fork-NEW (allowed)
src/core/ControlCommandsLink.cpp                         fork-NEW (allowed)
src/core/ControlCommandsLivecode.cpp                     fork-NEW (allowed)
src/core/ControlCommandsMastering.cpp                    fork-NEW (allowed)
src/core/ControlCommandsMasteringRun.cpp                 fork-NEW (allowed)
src/core/ControlCommandsMeter.cpp                        fork-NEW (allowed)
src/core/ControlCommandsMeterFile.cpp                    fork-NEW (allowed)
src/core/ControlCommandsMidi.cpp                         fork-NEW (allowed)
src/core/ControlCommandsMidiReconnect.cpp                fork-NEW (allowed)
src/core/ControlCommandsMidiReconnectEdit.cpp            fork-NEW (allowed)
src/core/ControlCommandsMidiReconnectShared.h            fork-NEW (allowed)
src/core/ControlCommandsMixer.cpp                        fork-NEW (allowed)
src/core/ControlCommandsMixerRoutes.cpp                  fork-NEW (allowed)
src/core/ControlCommandsModulator.cpp                    fork-NEW (allowed)
src/core/ControlCommandsModulatorRoutes.cpp              fork-NEW (allowed)
src/core/ControlCommandsNoteExpression.cpp               fork-NEW (allowed)
src/core/ControlCommandsNoteProbability.cpp              fork-NEW (allowed)
src/core/ControlCommandsNoteRandom.cpp                   fork-NEW (allowed)
src/core/ControlCommandsNotes.cpp                        fork-NEW (allowed)
src/core/ControlCommandsNoteSlide.cpp                    fork-NEW (allowed)
src/core/ControlCommandsNoteTransform.cpp                fork-NEW (allowed)
src/core/ControlCommandsOutOfProcess.cpp                 fork-NEW (allowed)
src/core/ControlCommandsOutOfProcessEdit.cpp             fork-NEW (allowed)
src/core/ControlCommandsOutOfProcessShared.h             fork-NEW (allowed)
src/core/ControlCommandsOutOfProcessSupport.cpp          fork-NEW (allowed)
src/core/ControlCommandsPatcher.cpp                      fork-NEW (allowed)
src/core/ControlCommandsPatcherEdit.cpp                  fork-NEW (allowed)
src/core/ControlCommandsPatcherShared.h                  fork-NEW (allowed)
src/core/ControlCommandsPdc.cpp                          fork-NEW (allowed)
src/core/ControlCommandsPlugin.cpp                       fork-NEW (allowed)
src/core/ControlCommandsPluginParams.cpp                 fork-NEW (allowed)
src/core/ControlCommandsPluginPreset.cpp                 fork-NEW (allowed)
src/core/ControlCommandsPluginScan.cpp                   fork-NEW (allowed)
src/core/ControlCommandsPluginScanEdit.cpp               fork-NEW (allowed)
src/core/ControlCommandsPluginScanShared.h               fork-NEW (allowed)
src/core/ControlCommandsPluginState.cpp                  fork-NEW (allowed)
src/core/ControlCommandsPorts.cpp                        fork-NEW (allowed)
src/core/ControlCommandsProjectArchive.cpp               fork-NEW (allowed)
src/core/ControlCommandsProject.cpp                      fork-NEW (allowed)
src/core/ControlCommandsProjectFiles.cpp                 fork-NEW (allowed)
src/core/ControlCommandsProjectMmpzGit.cpp               fork-NEW (allowed)
src/core/ControlCommandsPunch.cpp                        fork-NEW (allowed)
src/core/ControlCommandsRack.cpp                         fork-NEW (allowed)
src/core/ControlCommandsRackMacros.cpp                   fork-NEW (allowed)
src/core/ControlCommandsRackZones.cpp                    fork-NEW (allowed)
src/core/ControlCommandsRecording.cpp                    fork-NEW (allowed)
src/core/ControlCommandsRecordingInput.cpp               fork-NEW (allowed)
src/core/ControlCommandsRecordingRecovery.cpp            fork-NEW (allowed)
src/core/ControlCommandsRecordingRetro.cpp               fork-NEW (allowed)
src/core/ControlCommandsRecordingRoutes.cpp              fork-NEW (allowed)
src/core/ControlCommandsRenderStems.cpp                  fork-NEW (allowed)
src/core/ControlCommandsRevisions.cpp                    fork-NEW (allowed)
src/core/ControlCommandsRouting.cpp                      fork-NEW (allowed)
src/core/ControlCommandsSafeStart.cpp                    fork-NEW (allowed)
src/core/ControlCommandsSafeStartEdit.cpp                fork-NEW (allowed)
src/core/ControlCommandsSafeStartShared.h                fork-NEW (allowed)
src/core/ControlCommandsSample.cpp                       fork-NEW (allowed)
src/core/ControlCommandsSampleEdit.cpp                   fork-NEW (allowed)
src/core/ControlCommandsScale.cpp                        fork-NEW (allowed)
src/core/ControlCommandsScaleEdit.cpp                    fork-NEW (allowed)
src/core/ControlCommandsScript.cpp                       fork-NEW (allowed)
src/core/ControlCommandsSession.cpp                      fork-NEW (allowed)
src/core/ControlCommandsSessionFollow.cpp                fork-NEW (allowed)
src/core/ControlCommandsSessionLaunch.cpp                fork-NEW (allowed)
src/core/ControlCommandsSessionRecord.cpp                fork-NEW (allowed)
src/core/ControlCommandsSessionRecordInternal.h          fork-NEW (allowed)
src/core/ControlCommandsSessionRecordLand.cpp            fork-NEW (allowed)
src/core/ControlCommandsSessionShared.h                  fork-NEW (allowed)
src/core/ControlCommandsSettings.cpp                     fork-NEW (allowed)
src/core/ControlCommandsStemModel.cpp                    fork-NEW (allowed)
src/core/ControlCommandsStems.cpp                        fork-NEW (allowed)
src/core/ControlCommandsStructure.cpp                    fork-NEW (allowed)
src/core/ControlCommandsSurface.cpp                      fork-NEW (allowed)
src/core/ControlCommandsTelemetry.cpp                    fork-NEW (allowed)
src/core/ControlCommandsTrackFolder.cpp                  fork-NEW (allowed)
src/core/ControlCommandsTrackFolderSets.cpp              fork-NEW (allowed)
src/core/ControlCommandsTrackFolderShared.h              fork-NEW (allowed)
src/core/ControlCommandsTransport.cpp                    fork-NEW (allowed)
src/core/ControlCommandsTransportMap.cpp                 fork-NEW (allowed)
src/core/ControlCommandsUndo.cpp                         fork-NEW (allowed)
src/core/ControlCommandsVca.cpp                          fork-NEW (allowed)
src/core/ControlCommandsVcaEdit.cpp                      fork-NEW (allowed)
src/core/ControlCommandsVcaEditSet.cpp                   fork-NEW (allowed)
src/core/ControlCommandsVcaMix.cpp                       fork-NEW (allowed)
src/core/ControlCommandsVcaShared.h                      fork-NEW (allowed)
src/core/ControlCommandsWarp.cpp                         fork-NEW (allowed)
src/core/ControlCommandsWarpEdit.cpp                     fork-NEW (allowed)
src/core/ControlCommandsWasm.cpp                         fork-NEW (allowed)
src/core/ControlCommandsWasmEdit.cpp                     fork-NEW (allowed)
src/core/ControlCommandsWasmRender.cpp                   fork-NEW (allowed)
src/core/ControlCompSupport.cpp                          fork-NEW (allowed)
src/core/ControlDetectSupport.cpp                        fork-NEW (allowed)
src/core/ControlDeviceCatalogue.cpp                      fork-NEW (allowed)
src/core/ControlDeviceClap.cpp                           fork-NEW (allowed)
src/core/ControlDeviceHosted.cpp                         fork-NEW (allowed)
src/core/ControlDeviceState.cpp                          fork-NEW (allowed)
src/core/ControlDeviceSupport.cpp                        fork-NEW (allowed)
src/core/ControlDeviceVst3.cpp                           fork-NEW (allowed)
src/core/ControlEditSupport.cpp                          fork-NEW (allowed)
src/core/ControlExportPresetSupport.cpp                  fork-NEW (allowed)
src/core/ControlGrooveSupport.cpp                        fork-NEW (allowed)
src/core/ControllerSurface.cpp                           fork-NEW (allowed)
src/core/ControlMasteringSupport.cpp                     fork-NEW (allowed)
src/core/ControlMixerSupport.cpp                         fork-NEW (allowed)
src/core/ControlModulationSupport.cpp                    fork-NEW (allowed)
src/core/ControlNoteShared.cpp                           fork-NEW (allowed)
src/core/ControlProjectAssets.cpp                        fork-NEW (allowed)
src/core/ControlProjectAssetsRelink.cpp                  fork-NEW (allowed)
src/core/ControlProjectAssetsShared.h                    fork-NEW (allowed)
src/core/ControlRackSupport.cpp                          fork-NEW (allowed)
src/core/ControlRecordingSupport.cpp                     fork-NEW (allowed)
src/core/ControlRegistry.cpp                             fork-NEW (allowed)
src/core/ControlRegistryRegistrations.cpp                fork-NEW (allowed)
src/core/ControlReversibility.cpp                        fork-NEW (allowed)
src/core/ControlReversibilityTableAction.cpp             fork-NEW (allowed)
src/core/ControlReversibilityTableArchive.cpp            fork-NEW (allowed)
src/core/ControlReversibilityTableAutomationModes.cpp    fork-NEW (allowed)
src/core/ControlReversibilityTableAutomationRamp.cpp     fork-NEW (allowed)
src/core/ControlReversibilityTableChain.cpp              fork-NEW (allowed)
src/core/ControlReversibilityTableChord.cpp              fork-NEW (allowed)
src/core/ControlReversibilityTableClapInstrument.cpp     fork-NEW (allowed)
src/core/ControlReversibilityTableController.cpp         fork-NEW (allowed)
src/core/ControlReversibilityTable.cpp                   fork-NEW (allowed)
src/core/ControlReversibilityTableDawProject.cpp         fork-NEW (allowed)
src/core/ControlReversibilityTableDetect.cpp             fork-NEW (allowed)
src/core/ControlReversibilityTableExportPresets.cpp      fork-NEW (allowed)
src/core/ControlReversibilityTableHostChunking.cpp       fork-NEW (allowed)
src/core/ControlReversibilityTableInterchange.cpp        fork-NEW (allowed)
src/core/ControlReversibilityTableLivecode.cpp           fork-NEW (allowed)
src/core/ControlReversibilityTableLive.cpp               fork-NEW (allowed)
src/core/ControlReversibilityTableMastering.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableMeter.cpp              fork-NEW (allowed)
src/core/ControlReversibilityTableMidiReconnect.cpp      fork-NEW (allowed)
src/core/ControlReversibilityTableMmpzGit.cpp            fork-NEW (allowed)
src/core/ControlReversibilityTableNoteScale.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableOutOfProcess.cpp       fork-NEW (allowed)
src/core/ControlReversibilityTablePassive.cpp            fork-NEW (allowed)
src/core/ControlReversibilityTableRecording.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableRevisions.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableRouting.cpp            fork-NEW (allowed)
src/core/ControlReversibilityTableSafeStart.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableSample.cpp             fork-NEW (allowed)
src/core/ControlReversibilityTableScanAndCrash.cpp       fork-NEW (allowed)
src/core/ControlReversibilityTableSessionView.cpp        fork-NEW (allowed)
src/core/ControlReversibilityTableSnapshot.cpp           fork-NEW (allowed)
src/core/ControlReversibilityTableStems.cpp              fork-NEW (allowed)
src/core/ControlReversibilityTableStructure.cpp          fork-NEW (allowed)
src/core/ControlReversibilityTableTrackFolder.cpp        fork-NEW (allowed)
src/core/ControlReversibilityTableVca.cpp                fork-NEW (allowed)
src/core/ControlReversibilityTableVerbs.cpp              fork-NEW (allowed)
src/core/ControlReversibilityTableWasmRender.cpp         fork-NEW (allowed)
src/core/ControlScaleSupport.cpp                         fork-NEW (allowed)
src/core/ControlSchema.cpp                               fork-NEW (allowed)
src/core/ControlServer.cpp                               fork-NEW (allowed)
src/core/ControlServerSocket.cpp                         fork-NEW (allowed)
src/core/ControlServerWin32.cpp                          fork-NEW (allowed)
src/core/ControlSession.cpp                              fork-NEW (allowed)
src/core/ControlStemModel.cpp                            fork-NEW (allowed)
src/core/ControlStemSupport.cpp                          fork-NEW (allowed)
src/core/ControlStemWav.cpp                              fork-NEW (allowed)
src/core/ControlStructuralSupport.cpp                    fork-NEW (allowed)
src/core/ControlTransactions.cpp                         fork-NEW (allowed)
src/core/ControlUndoCoalescing.cpp                       fork-NEW (allowed)
src/core/ControlVocabulary.cpp                           fork-NEW (allowed)
src/core/ControlWasmSupport.cpp                          fork-NEW (allowed)
src/core/CrashReporter.cpp                               fork-NEW (allowed)
src/core/CrashReporterFormat.cpp                         fork-NEW (allowed)
src/core/CrashReporterWindows.cpp                        fork-NEW (allowed)
src/core/DataFile.cpp                                    declared divergence -> save integrity (D3): the four statements at the end of writeFile() no longer discard QFile::rename/QFile::remove results - a rename the filesystem refuses is reported through showError() and returns false, a backup that could not be made is reported on its own, and the previous project is moved back out of .bak when the final rename fails (upstream returned true unconditionally). docs/SAVELOAD-INTEGRITY.md; rename layer 1: the version-difference notice reports the name the file itself records; #625 headless load: the 'Could not open file', bundle write-failure, LADSPA upgrade and 'Error in file' boxes are not opened in an unattended run ARCH-4 S2a (SPEC-ARCH-4 1.4, the <z:index>): DataFile::write() builds and inserts the index after its own cleanMetaNodes() prune when the caller asked for one via the new setDocumentIndexEnabled() - off by default, so a DataFile nobody asked still writes exactly the bytes it wrote before. The ordering has to live here rather than in Song::saveProjectFile because only the writer knows when its prune has run; measured, the digests taken before it described a document the file never contained. docs not yet written; held by tests/src/core/DocumentIndexTest.cpp.
src/core/DawProjectModel.cpp                             fork-NEW (allowed)
src/core/DawProjectRead.cpp                              fork-NEW (allowed)
src/core/DawProjectReadShared.h                          fork-NEW (allowed)
src/core/DawProjectReadTracks.cpp                        fork-NEW (allowed)
src/core/DawProjectSession.cpp                           fork-NEW (allowed)
src/core/DawProjectWrite.cpp                             fork-NEW (allowed)
src/core/DawProjectZip.cpp                               fork-NEW (allowed)
src/core/DocumentIndex.cpp                               fork-NEW (allowed)
src/core/EffectChain.cpp                                 declared divergence -> #605 PDC: refresh cached chain latency when the chain changes (9e12a68f5, 48fed8644); mixer concurrency audit D5: moveUp/moveDown take the change mutex, as appendEffect/removeEffect/clear already did (post-alpha/mixer-concurrency); #605 PDC: refresh cached chain latency when the chain changes (9e12a68f5, 48fed8644); #599 routing: build the chain's RoutingGraph on every topology edit and render the block through it in processAudioBuffer(AudioBus&); #605 PDC: refresh cached chain latency when the chain changes (9e12a68f5, 48fed8644); mixer concurrency audit D5: moveUp/moveDown take the change mutex, as appendEffect/removeEffect/clear already did (post-alpha/mixer-concurrency); #599 routing: build the chain's RoutingGraph on every topology edit and render the block through it in processAudioBuffer(AudioBus&); #655 patcher node-graph (feature row 69, lane 030/patcher-graph, 2026-09-15): the chain owns an AUTHORED wiring (include/PatchWiring.h) that rebuildRoutingGraph() re-applies on every rebuild, so a hand-wired edge survives the next plugin.load instead of being discarded by the derivation; patchNodeId() resolves the roles a wiring addresses against the current node set and setPatchWiring() publishes a new wiring off the audio thread under the engine's model-change guard. No behaviour change while no patch is set: the derived wiring is the same edge list, in the same node order, as before
src/core/EffectChainPatcher.cpp                          fork-NEW (allowed)
src/core/Effect.cpp                                      declared divergence -> #605 PDC: propagate latency changes into the owning chain's accounting (9e12a68f5); stable ids slice 2 (feature row 51): adds ProjectIds::allocate() in the constructor, setId(), and writes/reads the id attribute in saveSettings/loadSettings; loadSettings takes the id only out of a DOCUMENT (ProjectIds::isDocumentElement), so restoring a <zenepluginstate> device state or preset into an effect cannot renumber the live instance it was applied to
src/core/Engine.cpp                                      declared divergence -> 0.3.0 W15 tempo map (D11): defines Engine::updateFramesPerTickForTempo() - Engine::updateFramesPerTick()'s expression with the map's tempo substituted for the global one - and stores s_framesPerTick atomically in both. The VALUE written for the same tempo is bit-identical to before, so an unmapped render is unchanged (TempoMapTest asserts the expression equality and the unmapped playhead advance).
src/core/ExportDither.cpp                                fork-NEW (allowed)
src/core/ExportRenderSettings.cpp                        fork-NEW (allowed)
src/core/ExternalProcessStemSeparator.cpp                fork-NEW (allowed)
src/core/GroovePool.cpp                                  fork-NEW (allowed)
src/core/GrooveTemplate.cpp                              fork-NEW (allowed)
src/core/ImportDetection.cpp                             fork-NEW (allowed)
src/core/ImportDetectionDsp.cpp                          fork-NEW (allowed)
src/core/ImportDetectionKey.cpp                          fork-NEW (allowed)
src/core/ImportDetectionSpectrum.cpp                     fork-NEW (allowed)
src/core/ImportDetectionSpectrum.h                       fork-NEW (allowed)
src/core/ImportFilter.cpp                                declared divergence -> wave R rename (018d2041f): the 'open this with' hint shown when no import filter matches named LMMS -> Zene Studio. User-visible string only.; wave R rename: user-visible import error text names the renamed product; #625 headless load: the import-path boxes are not opened in an unattended run (reachable with --import); tag v0.2.1-alpha CI fix: the qWarning() << ... branch added above needs <QDebug> included by name -- <QtGlobal> only forward-declares the class, so the Qt5 jobs (linux-x86_64, linux-arm64) would have failed with 'invalid use of incomplete type class QDebug' at the next build. Same defect class as src/core/ConfigManager.cpp; see docs/QDEBUG-CLASS-AND-MIME-RENAME.md
src/core/JournallingObject.cpp                           declared divergence -> 030/platform-defects: restoreState() looked for a child node named "journal", which NOTHING in the tree writes - the writer beside it has always emitted <journallingObject id=N> - so the journal id an object was saved with was never taken back, on any code path, since the fork. The effect on shipped behaviour, measured: undoing a freeze re-loads the Track (Track::loadTrack deletes every clip and re-creates it), so the journal id of a re-created clip changed and every clip checkpoint recorded before that undo named a dead object; ProjectJournal::undo() skipped such a step and unwound an OLDER one instead, so ONE control.undo took back SEVERAL edits (freeze transcript, before: one undo dropped the depth 6 -> 4 and the clip edit it was asked to restore did not come back, notes=[1, 0] with a clip gone). Fixed by reading the node the writer writes; changeID() is the pre-existing guard, so two objects still cannot share one id. Regression: tests/control-freeze-commands-transcript.py check_freeze_undo_restores_clip_edits; the mechanism is recorded in docs/UNDO-BOUNDS.md (the id a re-load must give back).; null-journal guard in the constructor (030/fixup-tests-2): Engine::projectJournal() is null before Engine::init() and after Engine::destroy(), and only the DESTRUCTOR guarded for that state - the constructor dereferenced it, so any JournallingObject built outside a live engine died inside ProjectJournal::allocID() (measured: SIGSEGV at lmms::fastRand(), address 0x8, reached from DummyPlugin's constructor through Plugin::instantiate() in SafeStartLoadPathTest, a binary that never runs Engine::init because it is about the load-time predicate alone). id() 0 means "no journal saw this object": every id allocID() hands out carries EO_ID_MSB, so 0 cannot be mistaken for a registered one. No behaviour change while a journal exists. Regression: SafeStartLoadPathTest::theLoadPathReallySkipsAThirdPartyInstance.
src/core/LatencyCompensation.cpp                         fork-NEW (allowed)
src/core/LinkSync.cpp                                    fork-NEW (allowed)
src/core/LinkSyncWire.cpp                                fork-NEW (allowed)
src/core/LinkUdpTransport.cpp                            fork-NEW (allowed)
src/core/LoudnessReport.cpp                              fork-NEW (allowed)
src/core/LufsMeter.cpp                                   fork-NEW (allowed)
src/core/main.cpp                                        declared divergence -> crash reporter: install()/beginSession()/endSession() around the process lifetime and setProjectPath() on every project load (start-up, render, --run-script, clean exit), plus the one-off pending-report offer (QMessageBox in GUI mode, stderr when core-only) and the Song::projectFileNameChanged follow (98a706dbe); Lua console: the --run-script action turns ScriptConsole off because stdout is that path's contract, otherwise every captured line would be printed twice by the new console stream (task #613); wave R rename (018d2041f): every user-visible name in main() moves to the product -- the version banner, the --help usage block ('Usage: zene', 'Start Zene Studio'), the root warning and the single-instance messages.; stem export (post-alpha/stem-export): headless `exportstems <project> -o <dir> [--tail-bars N]` action beside `render`/`rendertracks`; #618 loudness wiring: the CLI's --loudness-report option for `lmms render`, so a headless render can ask for the report (and its sidecar) as well; autosave recovery: the startup recovery prompt is gated by ProjectRecovery::decideRecovery() so a stale or other-project recover.mmp is not offered, and the dialog names the project it belongs to; regression test tests/src/core/ProjectRecoveryTest.cpp (post-alpha/autosave); wave R rename: --version banner, --help usage and root-user notice name the renamed product; crash reporter: install()/beginSession()/endSession() around the process lifetime and setProjectPath() on every project load (start-up, render, --run-script, clean exit), plus the one-off pending-report offer (QMessageBox in GUI mode, stderr when core-only) and the Song::projectFileNameChanged follow (98a706dbe); Lua console: the --run-script action turns ScriptConsole off because stdout is that path's contract, otherwise every captured line would be printed twice by the new console stream (task #613); wave R rename (018d2041f): every user-visible name in main() moves to the product -- the version banner, the --help usage block ('Usage: zene', 'Start Zene Studio'), the root warning and the single-instance messages; auto-mastering (task #610): a third render action 'master' beside render/rendertracks (offline -- one project render, N mastered candidates), its --help text, the -o-required guard and printMasteringReport(); the headless exit-code variable scriptExitCode is renamed headlessExitCode because it now carries this action's exit code too. No existing action's behaviour changes.; crash reporter: install()/beginSession()/endSession() around the process lifetime and setProjectPath() on every project load (start-up, render, --run-script, clean exit), plus the one-off pending-report offer (QMessageBox in GUI mode, stderr when core-only) and the Song::projectFileNameChanged follow (98a706dbe); Lua console: the --run-script action turns ScriptConsole off because stdout is that path's contract, otherwise every captured line would be printed twice by the new console stream (task #613); wave R rename (018d2041f): every user-visible name in main() moves to the product -- the version banner, the --help usage block ('Usage: zene', 'Start Zene Studio'), the root warning and the single-instance messages.; stem export (post-alpha/stem-export): headless `exportstems <project> -o <dir> [--tail-bars N]` action beside `render`/`rendertracks`; #618 loudness wiring: the CLI's --loudness-report option for `lmms render`, so a headless render can ask for the report (and its sidecar) as well; autosave recovery: the startup recovery prompt is gated by ProjectRecovery::decideRecovery() so a stale or other-project recover.mmp is not offered, and the dialog names the project it belongs to; regression test tests/src/core/ProjectRecoveryTest.cpp (post-alpha/autosave); wave R rename: --version banner, --help usage and root-user notice name the renamed product; #A11/A12: --control-socket flag starts the opt-in in-app JSON-RPC control server and documents it in --help (post-alpha/agent-control-surface); #626: apply a quit requested before readiness once the engine is ready, and unlink the control socket as soon as the event loop returns (post-alpha/control-hardening); #A11/A12: --control-socket flag starts the opt-in in-app JSON-RPC control server and documents it in --help; it also marks the process as agent-driven (ControlRegistry::setAgentInstance) so no modal dialog is opened, and the project-recovery prompt is answered without a dialog (#625) (post-alpha/agent-control-surface, post-alpha/headless-load); 0.3.0 W7 export-dither+SRC: two CLI flags, --dither and --src-quality <linear|sinc_fastest|sinc_medium|sinc_best>, parsed in stage 2 and set on both the render's OutputSettings and the process-wide ExportRenderSettings; the usage text names them; auto-mastering control surface (feature rows 25/72, lane 030/mastering-surface): the mastering run's report printer MOVED out of this file into src/core/MasteringReport.cpp, beside the JSON document the same run can write, so the inherited file's line ratchet is not grown by a second copy (behaviour unchanged: the CLI prints the same table); a new --report <path> option writes that JSON document, which is the machine-readable half the control surface's mastering.run reads back from the child process it starts; feature row 24 (meter surface): --loudness-report also sets ExportRenderSettings::setLoudnessReport(true), exactly as --dither does, so the CLI flag, export.get_settings and export.set_loudness_report cannot disagree about the current selection; render/export presets and selection-to-audio (030/render-presets, feature rows 70/71): three additions to the render/rendertracks/exportstems option block - `--bit-depth <16|24|32>` (the integer depths, so a stored export preset's depth can reach the render; --float keeps its own 32-bit meaning), `--range-start <ticks>` / `--range-end <ticks>` (a render RANGE: both required together, applied through the engine's OWN bounded render - Song::setRenderBetweenMarkers plus Timeline::setLoopPoints, the path the GUI's "render between loop markers" checkbox drives - so a ranged render is the same renderer over a span; no tail bar, no loop repetition, no second renderer), plus their --help lines and the pair/empty/negative-range refusals, which run before a project is loaded or a destination opened. With neither flag given the action is byte-for-byte what it was.; CODE-9 (030/code9-named-pipe, 2026-09-15, feature row 83): the --control-socket help text and the flag's parse comment now name the Windows transport (a named pipe, \\.\pipe\<name>) beside the AF_UNIX socket, because on Windows the flag no longer refuses. Strings and comments only: no control flow, no argument parsing and no behaviour changes.; safe-start mode (feature row 77, board task #666, lane 030/safe-start): safestart::install()/beginSession() beside the crash reporter's, at the early and the GUI startup point, safestart::endSession() beside every crashreporter::endSession() (render-exit, the recovery dialog's Exit, --import exit-after, the clean exit), announceSafeStart() - the one stderr line that OFFERS the normal start, because this release has no dialog for it (docs/KNOWN-LIMITATIONS.md carries the absence) - and safestart::setProjectPath() beside every crashreporter::setProjectPath() including the Song::projectFileNameChanged follow, so the marker names the project the session that died had open. No existing action's behaviour changes: with no crash marker on disk every one of these calls is a no-op; mmpz v2 container refusal (ARCH-4 S2c, commit 6d8c2ce70): the `dump`/`--dump`/`-d` and `compress`/`--compress` actions now ask projectcontainer::isContainer() about the file's OWN BYTES first and refuse a v2 container by name on stderr with EXIT_FAILURE, because qUncompress() answers a ZIP with an empty string - `--dump` printed that as a bare newline and returned EXIT_SUCCESS (the one combination a script cannot detect), and `--compress` is a writer, so it wrapped the ZIP inside v1's qCompress frame and produced a file that is neither format and that nothing in the tree reads back; regression test tests/mmpz-v2-container-refusal.py, whose two controls keep the shapes that are NOT v2 (a v1 `.mmpz` is still printed, an `.mmp` is still framed) so the refusal cannot pass by refusing everything; byte-exact stdout for those two actions (2026-09-21, CI run 35653413479, job msvc-x64): both call binaryStdout() before their first write, which puts stdout into binary mode, because the Windows CRT opens it in TEXT mode where every '\n' becomes "\r\n" - measured on that job, `--dump` printed 179 bytes where the same source prints 173 on POSIX and the frame `--compress` wrote failed zlib's own stream check ("invalid literal/lengths set"), so a v1 round trip was impossible on Windows while every POSIX job stayed green; on POSIX binaryStdout() is a no-op and nothing changes. No existing action's behaviour changes: a file that is not a v2 container, and a platform with no translation, take exactly the path they took before.
src/core/MasteringChain.cpp                              fork-NEW (allowed)
src/core/MasteringJob.cpp                                fork-NEW (allowed)
src/core/MasteringReport.cpp                             fork-NEW (allowed)
src/core/MasterLoudnessTap.cpp                           fork-NEW (allowed)
src/core/MidiClock.cpp                                   fork-NEW (allowed)
src/core/MidiClockState.cpp                              fork-NEW (allowed)
src/core/MidiClockTracker.cpp                            fork-NEW (allowed)
src/core/MidiLearn.cpp                                   fork-NEW (allowed)
src/core/midi/MidiAlsaSeq.cpp                            declared divergence -> midi-learn feature (5d6ccdf1f): SND_SEQ_EVENT_CONTROLLER feeds MidiLearn::handleMidiEvent() before the port mask, so a control can be bound while nothing listens on the channel; post-alpha/midi-race moved the bind itself to the GUI thread, leaving this site a lock-free record; rename layer 1: the host-visible audio/MIDI client name is Zene Studio (was the old product name); test hygiene (post-alpha/teardown-2): ~MidiAlsaSeq joined its polling thread with a fixed wait(EventPollTimeOut*2) = 500 ms, so when that budget expired the thread was still running and the QThread destructor met it - qFatal("QThread: Destroyed while thread is still running")/SIGABRT inside Engine::destroy(), reported as a failure of whichever test was closing the application (measured: 1 abort in a 3-run suite sweep under load, caught by tests/control-shutdown.py, its stack naming ~AudioEngine -> delete m_midiClient -> ~MidiAlsaSeq). The join is now unbounded; run() leaves its loop within one 250 ms poll interval of m_quit, and the sequencer handle is closed only after the thread has stopped. The render path and the MIDI data path are untouched. Held by tests/control-shutdown.py. 0.3.0 `clock.*` (030/midi-clock): processOutEvent() sends the clock family as SND_SEQ_EVENT_CLOCK/START/CONTINUE/STOP/SONGPOS/QFRAME delivered DIRECT rather than on the tick queue (a clock must not wait behind the note events the queue already holds), and run() dispatches the same family to MidiClock before the destination lookup because a clock pulse names no port of ours.; retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): run() records every received sequencer event once, above the destination-port `continue` and above the event-type switch, through the file-local retroCaptureEvent() mapping that mirrors the switch (the switch itself is unchanged); guarded by isArmed(), so inert while disarmed. Captures events addressed to ports this client does not know - the "played it and nothing was armed" case. Held by tests/src/core/MidiRetroCaptureTest.cpp (the element mapping) and the suite render comparison.; 0.3.0 MIDI controller auto-reconnection (feature-list row 18, OWNER-31 item 7): updatePortList() calls m_reconnect.reconcile() after publishing the changed port list, on EVERY poll rather than only on a change, so an assignment whose device went away and came back is re-established once the MidiPort slots have run, and the engine's record of a loss is never older than one second. One pass over the remembered assignments (there are none unless something was bound); the port enumeration and the emits above it are unchanged. Held by tests/src/core/MidiReconnectTest.cpp and tests/control-midi-reconnect.py.
src/core/midi/MidiClient.cpp                             declared divergence -> midi-learn feature (5d6ccdf1f): MidiClientRaw::processParsedEvent() feeds MidiLearn::handleMidiEvent() before the ports; the GUI-thread deferral (post-alpha/midi-race) lives inside MidiLearn, so this call site is unchanged by the fix 0.3.0 `clock.*` (030/midi-clock): parseData() routes the system real-time family (0xF8/0xFA/0xFB/0xFC) to MidiClock::handleInputMessage() and assembles the 0xF1/0xF2 payloads that were previously cancelled and dropped (a status byte still cancels an interrupted one); MidiClientRaw::processOutEvent() sends that family through the same sendByte() path a note takes instead of a qWarning per pulse.; retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): MidiClientRaw::processParsedEvent() records the parsed event into the client capture before the per-port loop, so an event no MidiPort is subscribed to is still recorded; guarded by isArmed() so the disarmed path is one relaxed atomic load and nothing else. Held by tests/src/core/MidiRetroCaptureTest.cpp.
src/core/midi/MidiController.cpp                         declared divergence -> feature row 19 (#651): the engine half of soft-takeover (the crossing gate in processInEvent) and of LED/feedback output (setFeedbackEnabled, feedbackByte, sendFeedback, the 'softtakeover'/'feedback' save/load attributes, and the out-event forward to the port). processOutEvent was an inline no-op in the header and now has its definition here. Also adds <algorithm> and <cmath>, which the WIP commit cbb717660 used (std::clamp, std::lround, std::abs) without including - a compile error, fixed here.
src/core/midi/MidiJack.cpp                               declared divergence -> rename layer 1: the host-visible audio/MIDI client name is Zene Studio (was the old product name); reads the legacy config attribute as a fallback
src/core/midi/MidiPort.cpp                               declared divergence -> 030/freeze-journal-fix (2026-09-14, the 0.3.0-alpha release gate): the constructor stops journalling the two DEVICE-state models it builds at :65-66 (m_readableModel/m_writableModel) - two setJournalling(false) calls plus their banner comment; no other line of the file changes and no behaviour of the port itself changes. WHY it is a divergence from upstream rather than a local fix to fork code: upstream's MidiPort journals both flags, and InstrumentTrack::autoAssignMidiDevice() (src/tracks/InstrumentTrack.cpp:1236, the raw-client branch) WRITES them - off and on around every save from saveTrackSpecificSettings's deliberate clear/restore (:1017/:1025, the reason a project does not record a transient auto-assignment), on every track construction and destruction (:114/:213) and from a piano-roll or piano-view click (src/gui/editors/PianoRoll.cpp:4299, src/gui/instrument/PianoView.cpp:686). Journalled, each write reached AutomatableModel::setValue -> addJournalCheckPoint (src/core/AutomatableModel.cpp:317) and pushed an undo step for an assignment nobody asked to undo. On the two Linux CI runners the MIDI client is the RAW MidiDummy fallback (no ALSA sequencer: src/core/AudioEngine.cpp:1036 with include/MidiClient.h:145, isRaw() true), so the save-time flip put ONE step on the stack for render.render, bounce.in_place and project.save - all three not_mutating (src/core/ControlReversibilityTableSnapshot.cpp:73 for project.save) - and ControlFreezeCommandsTranscript failed with the committed assertion verbatim: reproduced on this box with MIDIDEV=/dev/null (exit 1 before this change, exit 0 after) and 0 of 41 trace entries differing from the CI job; macOS (include/MidiApple.h:113) and Windows (include/MidiWinMM.h:113) clients are not raw, which is exactly why only the Linux jobs were red. Precedent in this tree for a model that is deliberately not journalled: src/gui/editors/SongEditor.cpp:250 (zooming), src/gui/editors/AutomationEditor.cpp:115 (tension), src/gui/widgets/AutomatableButton.cpp:220 (button-group members). NOTHING is dropped to get the zero: the flags are still written with the port by MidiPort::saveSettings (:197-198) and restored by loadSettings (:251-252), and a checkpoint taken on the port itself still carries them as attributes. Held by tests/src/core/UndoBoundsTest.cpp::aDeviceAssignmentIsNotAnUndoStep (the flag still changes, the depth does not move, the value still round-trips) and by the release-gate transcript tests/control-freeze-commands-transcript.py, which is the test that fails without it.; 0.3.0 MIDI controller auto-reconnection (feature-list row 18, OWNER-31 item 7): subscribeReadablePort()/subscribeWritablePort() record (or forget) the binding in the client's MidiReconnect memory by IDENTITY - the client and port NAME - instead of the volatile ALSA address, and ~MidiPort forgets every assignment of a port that is going away. The subscription itself is the same call with one record beside it, so a project that binds nothing behaves exactly as before. Held by tests/src/core/MidiReconnectTest.cpp and tests/control-midi-reconnect.py.
src/core/midi/MidiReconnect.cpp                          fork-NEW (allowed)
src/core/midi/MpeExpression.cpp                          fork-NEW (allowed)
src/core/Mixer.cpp                                       declared divergence -> #605 PDC: per-period latency recompute and summing-point alignment (9e12a68f5, 48fed8644); mixer concurrency audit D1 (mute latch pass split from the action pass, D2(ii)/(iii) muted senders clear their outgoing sidechain intermediates), D3 (createChannel/createBusChannel under the change mutex) and D4 (moveChannelLeft under the change mutex) (post-alpha/mixer-concurrency); #605 PDC: per-period latency recompute and summing-point alignment (9e12a68f5, 48fed8644); #599 racks: the channel renders its period through the rack's RoutingGraph when one is configured, saves/loads the channel's <rack> element, and drops the rack when the channel is cleared; #605 PDC: per-period latency recompute and summing-point alignment (9e12a68f5, 48fed8644); #622 VCA groups: one extra gain multiply on the post-FX buffer in doProcessing (guarded so unity gain takes the pre-#622 path unchanged), group gain publication, group index bookkeeping in deleteChannel/moveChannelLeft, mute/solo linking, <vcagroup> save/load, and m_muteBeforeSolo initialised (it was read by deactivateSolo() before ever being written); #605 PDC: per-period latency recompute and summing-point alignment (9e12a68f5, 48fed8644); mixer concurrency audit D1 (mute latch pass split from the action pass, D2(ii)/(iii) muted senders clear their outgoing sidechain intermediates), D3 (createChannel/createBusChannel under the change mutex) and D4 (moveChannelLeft under the change mutex) (post-alpha/mixer-concurrency); #599 racks: the channel renders its period through the rack's RoutingGraph when one is configured, saves/loads the channel's <rack> element, and drops the rack when the channel is cleared; OWNER-31 item 11 / 030/vca-editgroups (2026-09-14): a group's <vcagroup> element now also carries a `locked` attribute and one <edittrack track="N"/> child per edit-set track (the edit half of the phase-locked multitrack edit group), and the loader reads `locked` with a default of 1 so an element written before the edit half existed loads as the locked group it was. Save/load only - no audio-path change, and a pre-0.3.0 reader skips both exactly as it skips <vcagroup>; board card #709 (040/feat-709, 2026-09-22): the cycle-permitted signal-graph submode - processed() skips a feedback send in the dependency count, gatingReceives() replaces m_receives.size() in incrementDeps and in masterMix's initial queue, doProcessing pulls the committed one-period snapshot for a feedback send and publishes its own block to outgoing feedback sends, masterMix clears a muted sender's feedback snapshots (D2(iii) rule) and prepareMasterMix commits them at the same quiescent moment as the deferred sidechain commit, resolveLatency skips feedback edges and the per-edge delay loop fixes them at 0 frames (compensation suspended, feedback.* says so at the point of use), createRoute flags a loop-closing send ONLY while feedbackMode() is on, setFeedbackMode deletes the flagged sends on exit, and save/load carry the per-send feedback attribute (the persisted half of the mode). Behaviour-preserving by default: with the mode off every path is the old one, byte for byte
src/core/ModulationLayer.cpp                             fork-NEW (allowed)
src/core/NamespaceRegistry.cpp                           fork-NEW (allowed)
src/core/Note.cpp                                        declared divergence -> post-alpha/midi-depth: serialise/parse prob and veljit as optional attributes written only when set, and carry both through the copy constructor and operator=; #601 MPE: expression setters/clear, the three optional note attributes in save/loadSettings, copy + assignment carry them; stable ids slice 2 (feature row 51): the Note constructor allocates the id, saveSettings writes it on the <note> element, loadSettings takes the file's value only for a note that sits in a document (a payload's id names the note that was copied, which is still alive)
src/core/NotePlayHandle.cpp                              declared divergence -> #601 MPE: updateFrequency() multiplies the instrument frequency by the note's own bend ratio (no-op at zero, so untouched renders are bit-identical); #649 MPE playback: play() sends the note's captured pressure and CC74 with its note-on (sendMpeExpressionMidi(), the same InstrumentTrack::processOutEvent path the note-on uses), and a note that carries no expression sends neither event, so an untouched project reaches the instrument exactly as before
src/core/NoteRandom.cpp                                  fork-NEW (allowed)
src/core/NoteTransform.cpp                               fork-NEW (allowed)
src/core/OnnxRuntimeStemSeparator.cpp                    fork-NEW (allowed)
src/core/OutOfProcessHosting.cpp                         fork-NEW (allowed)
src/core/PatchWiring.cpp                                 fork-NEW (allowed)
src/core/PatternStore.cpp                                declared divergence -> fix(control-undo): updateComboBox() skips a pattern index whose PatternTrack::findPatternTrack() is nullptr. numOfPatterns() counts the Song's pattern tracks while findPatternTrack() searches PatternTrack's own registry, so the two can disagree; the disagreement is fixed at its source, and this guard is why the next one degrades to a missing combobox row instead of killing the process the clients are connected to (docs/CONTROL-UNDO-CONNECTION-DROP.md)
src/core/PeakController.cpp                              declared divergence -> #625 headless load: the 'Peak Controller Bug' box (raised from the project-load path) is not opened in an unattended run; tag v0.2.1-alpha CI fix: the qWarning() << ... branch added above needs <QDebug> included by name -- <QtGlobal> only forward-declares the class, so the Qt5 jobs (linux-x86_64, linux-arm64) would have failed with 'invalid use of incomplete type class QDebug' at the next build. Same defect class as src/core/ConfigManager.cpp; see docs/QDEBUG-CLASS-AND-MIME-RENAME.md
src/core/Plugin.cpp                                      declared divergence -> #625 headless load: the 'Plugin not found' and 'Error while loading plugin' boxes are not opened in an unattended run; tag v0.2.1-alpha CI fix: the qWarning() << ... branch added above needs <QDebug> included by name -- <QtGlobal> only forward-declares the class, so the Qt5 jobs (linux-x86_64, linux-arm64) would have failed with 'invalid use of incomplete type class QDebug' at the next build. Same defect class as src/core/ConfigManager.cpp; see docs/QDEBUG-CLASS-AND-MIME-RENAME.md; safe-start mode (feature row 77, board task #666, lane 030/safe-start): Plugin::instantiate() consults safestart::shouldSkipPluginInstance() after the factory lookup and hands back the engine's own DummyPlugin for a THIRD-PARTY module file while the session follows an unclean exit (the one funnel every instrument, effect, tool, import filter and exporter is created through, so 'skipped' cannot mean one thing for a track and another for an effect), and records the skip through safestart::noteSkippedInstance(). The predicate is false unless a crash marker was found at launch, and a plugin that resolved to no file at all (pi.isNull()) is not third-party, so a normal session's behaviour is unchanged.
src/core/PluginFactory.cpp                               declared divergence -> plugin-scan: fingerprint-keyed scan cache, quarantine filtering, and cache-served (lazily loaded) plugin entries in discoverPlugins() (023770f7b); wave R rename (018d2041f): the installed plugin search path ../lib/lmms -> ../lib/zene (it follows the renamed install location), the path comments in the same function, and the 'LMMS plugin %1 ...' warning string -> 'Zene Studio plugin %1 ...'.; wave R rename: plugin search path and warning text follow the renamed install layout; includes: <QDateTime> and <QFileInfo> by name -- recordFromDescriptor() calls QFileInfo::lastModified() and reads its QDateTime, and on the Qt5 jobs that came in as an incomplete type ("invalid use of incomplete type 'class QDateTime'", PluginFactory.cpp:117/146) once the build reached this translation unit
src/core/PluginHostChunking.cpp                          fork-NEW (allowed)
src/core/PluginHostNotes.cpp                             fork-NEW (allowed)
src/core/PluginScanCache.cpp                             fork-NEW (allowed)
src/core/ProjectContainer.cpp                            fork-NEW (allowed)
src/core/ProjectContainerEntries.cpp                     fork-NEW (allowed)
src/core/ProjectIds.cpp                                  fork-NEW (allowed)
src/core/ProjectJournalBounds.cpp                        fork-NEW (allowed)
src/core/ProjectJournal.cpp                              declared divergence -> #623 SPEC A16: implements the two flavours; undo()/redo() now restore a whole step and carry the reverse action, and a step with no reverse empties the redo stack rather than replaying an older entry (post-alpha/reversibility); #623 bounded undo (030/w11-undo-depth): every step measures its own serialised size at capture, trimUndoStack() enforces BOTH caps FIFO (and now runs on the redo path too, so a cap lowered while steps sit on the redo stack still holds), mergeCheckpointsFrom()/undo()/redo() maintain the byte accounting, and clearJournal() resets the counters without rewinding the serials. The new code is in src/core/ProjectJournalBounds.cpp; the changes here are the accounting only
src/core/ProjectJournalStructural.cpp                    fork-NEW (allowed)
src/core/ProjectKey.cpp                                  fork-NEW (allowed)
src/core/ProjectRecovery.cpp                             fork-NEW (allowed)
src/core/ProjectRenderer.cpp                             declared divergence -> #618 loudness wiring: the passive tap (measure each rendered block immediately before writeBuffer()), the sidecar write and the console line; the audio path is untouched and the tap does not exist unless the render asked for a report; render determinism (render-determinism lane): holds the switch for the duration of an export; live playback is untouched; auto-mastering (task #610): run() increments the new static render counter. Instrumentation only -- no render output depends on it; it is what lets MasteringJob prove N candidates cost one project render (MainWindow/RenderManager render paths call the same run()).; 0.3.0 W7 export-dither+SRC: the constructor publishes OutputSettings::{dither,srcQuality} process-wide and the destructor (formerly = default) restores the previous selection whatever route the renderer leaves by
src/core/ProjectRevisions.cpp                            fork-NEW (allowed)
src/core/ProvenanceSection.cpp                           fork-NEW (allowed)
src/core/Rack.cpp                                        fork-NEW (allowed)
src/core/RackMacros.cpp                                  fork-NEW (allowed)
src/core/RackNodes.cpp                                   fork-NEW (allowed)
src/core/RackZones.cpp                                   fork-NEW (allowed)
src/core/RecordingJournal.cpp                            fork-NEW (allowed)
src/core/RemotePlugin.cpp                                declared divergence -> #589 planar-ports migration: ports allocate the shared block; counts owned by the ports model; removed count ids refused loudly; feature row 80 (030/out-of-process, board card #670): init() records a client start and processFinished() records the exit under the client executable's name (oop::HostTracker), and the destructor sets m_hostShutdownRequested before its IdQuit/terminate/kill path so that exit is recorded as a shutdown. Notifications only: no new lock or allocation on any audio path, process() still zero-fills a failed slot exactly as before, and the pre-existing `Remote plugin crashed` log lines are unchanged
src/core/RenderManager.cpp                               declared divergence -> stem export (post-alpha/stem-export): exportStems() renders every stem to the whole-project length with a configurable tail and numbers the files in track order; renderProject()/renderTracks() unchanged (held by StemExportTest); #618 loudness wiring: keep the report alive past the renderer (which is destroyed on finish()) so the export dialog can show it
src/core/RetroAudioCapture.cpp                           fork-NEW (allowed)
src/core/RetroMidiCapture.cpp                            fork-NEW (allowed)
src/core/RetroMidiCaptureSettings.cpp                    fork-NEW (allowed)
src/core/RetroMidiClipWriter.cpp                         fork-NEW (allowed)
src/core/RevisionTimelineCompare.cpp                     fork-NEW (allowed)
src/core/RevisionTimeline.cpp                            fork-NEW (allowed)
src/core/RevisionTimelineGit.cpp                         fork-NEW (allowed)
src/core/RevisionTimelineGit.h                           fork-NEW (allowed)
src/core/RoutingChainNodes.cpp                           fork-NEW (allowed)
src/core/RoutingGraph.cpp                                fork-NEW (allowed)
src/core/SafeStart.cpp                                   fork-NEW (allowed)
src/core/SampleBuffer.cpp                                declared divergence -> #625 headless load: the 'Failed to load sample' boxes are not opened in an unattended run (they fire while a project loads)
src/core/SampleClip.cpp                                  declared divergence -> #611 Slices 0+1: the authored window (setter, mapping, window-based sampleLength) and its serialisation (srcin/srcout, written only for a trimmed clip); the legacy frame setters author the window instead of writing Sample directly, which is identical for an untrimmed clip. #597 warp: the mapping's warp branch, the tempo leader/follower rate, and the additive <warp> child element (written only when a clip carries markers or a source tempo); the empty-marker branch is the pre-warp arithmetic verbatim; 0.3.0 warp.* surface: SampleClip::loadSettings resets the warp map (markers, tempo mode, source tempo) when the clip carries no <warp> element, so the clip's own journal checkpoint is a true inverse of a warp edit - the checkpoint taken before the FIRST marker is a state with no element, and without the reset control.undo left the marker in place; 0.3.0 row 30 pitch-preserving time-stretch: SampleClip gains the WarpStretchMode member (default Resample = the historical behaviour) and the 'stretch' attribute of the same <warp> element #597 already writes, so a clip that never asks serialises byte for byte as before. 040/arch4-s9 (2026-09-22, ARCH-4 slice S9, SPEC-ARCH-4 census row 5): the additive `<warp>` save block and the reset-on-absence read block (and the four warp member initializers in the copy constructor) moved OUT of this file to src/core/Clip.cpp (Clip::saveWarp/loadWarp), so this row's reset-on-absence and 'stretch' claims now hold at Clip::loadWarp/saveWarp; the mapping's warp branch, the tempo-leader rate, setWarpMarkers/clearWarpMarkers and the accessors stay here, reading the members inherited from Clip.
src/core/Sample.cpp                                      declared divergence -> 0.3.0 W7 export SRC quality: Sample::play syncs its resampler's converter to the render's SrcQuality before setting the ratio. The comparison is false for the default (Linear), so the converter state is never re-created and the output is byte-identical unless a render asks for a different quality
src/core/SampleDecoder.cpp                               declared divergence -> import detection (feature row 34) found a real defect on the refusal path: decodeSampleDS() read Engine::audioEngine()->outputSampleRate() with no null check, so ANY file the decoders before it cannot read SIGSEGVs the process in a context that never brought the audio subsystem up (measured: the registered ImportDetectionTest, which analyses a path that does not exist, segfaulted at address 0x288 inside Engine::audioEngine()). The fix is one guard: with no engine there is no rate to synthesise a DS file at, so the decoder REFUSES (std::nullopt) instead of dereferencing null. It can only change behaviour in a process where the DS decoder could never have worked anyway; a normal session has an engine and is byte-for-byte unchanged.
src/core/SampleOperators.cpp                             fork-NEW (allowed)
src/core/SampleOperators.h                               fork-NEW (allowed)
src/core/SamplePlayHandle.cpp                            declared divergence -> #611 Slice 0: the handle snapshots its window at construction and totalFrames() reads the snapshot, not the sample's live frame fields (SampleTrack.cpp:126-127 used to move those under it). #597 warp: the resampler ratio comes from the warped rate per period (exactly 1.0 with no markers and no source tempo, the value the call already passed) and the length from the window's timeline span; 0.3.0 W7 SRC-convention: the comment above warpRatio() is corrected - libsamplerate's src_ratio is output/input (measured, tests/src/core/AudioResamplerRatioTest.cpp), so natural/rate is the direct ratio and not a reciprocal workaround for an inversion. The returned expression is UNCHANGED; 0.3.0 row 30 pitch-preserving time-stretch: SampleClip gains the WarpStretchMode member (default Resample = the historical behaviour) and the 'stretch' attribute of the same <warp> element #597 already writes, so a clip that never asks serialises byte for byte as before
src/core/SampleRecordHandle.cpp                          declared divergence -> post-alpha/recording-realtime D9c: writeBuffer() appends to the pre-allocated ring instead of allocating per period, and the destructor installs the take the drain thread already built instead of assembling it on the audio thread
src/core/ScriptApiVersion.cpp                            fork-NEW (allowed)
src/core/ScriptBindings.cpp                              fork-NEW (allowed)
src/core/ScriptClock.cpp                                 fork-NEW (allowed)
src/core/ScriptCommandQueue.cpp                          fork-NEW (allowed)
src/core/ScriptConsole.cpp                               fork-NEW (allowed)
src/core/ScriptDawBindings.cpp                           fork-NEW (allowed)
src/core/ScriptDawEdit.cpp                               fork-NEW (allowed)
src/core/ScriptDawEffects.cpp                            fork-NEW (allowed)
src/core/ScriptEngine.cpp                                fork-NEW (allowed)
src/core/ScriptPackage.cpp                               fork-NEW (allowed)
src/core/SessionClip.cpp                                 fork-NEW (allowed)
src/core/SessionFollow.cpp                               fork-NEW (allowed)
src/core/SessionModel.cpp                                fork-NEW (allowed)
src/core/SessionModelPrivate.h                           fork-NEW (allowed)
src/core/SessionScheduler.cpp                            fork-NEW (allowed)
src/core/SmfInterchange.cpp                              fork-NEW (allowed)
src/core/SmfInterchangeReader.cpp                        fork-NEW (allowed)
src/core/Song.cpp                                        declared divergence -> #594 Session View: gated <session> save/load branches behind #ifdef LMMS_HAVE_SESSION_VIEW; project IO is byte-identical when the flag is off (held by SessionModelTest::preSessionProjectLoadsAndSavesUnchanged). Stem export (post-alpha/stem-export): startExport() honours the render-length override and the configurable tail, both of which default to the historical length (held by StemExportTest::wholeProjectRenderIsUnchangedByAStemExport); post-alpha/automation-modes: publish the transport run once per rendered period (processNextBuffer), let a Touch/Latch/Write control record its moves (processAutomations), and apply the trim offset where the automation is read out; post-alpha/midi-depth: read and write the midiseed header attribute (only when non-zero) and reset it for a new project; save integrity: the failed-open branch restores m_loadingProject and journal journalling (D2) and the #else of the session branches preserves an unsupported <session> block verbatim instead of dropping it (docs/SAVELOAD-INTEGRITY.md); rename layer 1: user-visible label/dialog string says Zene Studio; #594 Session View: gated <session> save/load branches behind #ifdef LMMS_HAVE_SESSION_VIEW; project IO is byte-identical when the flag is off (held by SessionModelTest::preSessionProjectLoadsAndSavesUnchanged). #595: audio-thread session clock + launch drain at the top of processNextBuffer and the SPEC A1 track takeover in the per-tick track loop, both inside #ifdef LMMS_HAVE_SESSION_VIEW - a no-op with nothing launched (held by SessionSchedulerRenderTest, byte-identical renders); #594 Session View: gated <session> save/load branches behind #ifdef LMMS_HAVE_SESSION_VIEW; project IO is byte-identical when the flag is off (held by SessionModelTest::preSessionProjectLoadsAndSavesUnchanged). Stem export (post-alpha/stem-export): startExport() honours the render-length override and the configurable tail, both of which default to the historical length (held by StemExportTest::wholeProjectRenderIsUnchangedByAStemExport); post-alpha/automation-modes: publish the transport run once per rendered period (processNextBuffer), let a Touch/Latch/Write control record its moves (processAutomations), and apply the trim offset where the automation is read out; post-alpha/midi-depth: read and write the midiseed header attribute (only when non-zero) and reset it for a new project; save integrity: the failed-open branch restores m_loadingProject and journal journalling (D2) and the #else of the session branches preserves an unsupported <session> block verbatim instead of dropping it (docs/SAVELOAD-INTEGRITY.md); rename layer 1: user-visible label/dialog string says Zene Studio; #625 headless load: the 'Aborting project load' and 'LMMS Error report' boxes are not opened in an unattended run; the refusal reason and the per-item errors are recorded (post-alpha/headless-load); #628 SPEC-stable-ids slice 1: Song::loadProject resets and starts the project id counter, honours/repairs the root's next-id after the container walk and re-assigns a duplicate track id, and Song::saveProjectFile writes next-id on the root; guarded automation-restore keys (Darwin ctest cluster 2026-09-13): both restore loops skip a null (destroyed) key and the per-frame refresh copies into the guarded map instead of assigning the raw-pointer AutomatedValueMap; 0.3.0 W15 tempo map (D11): reads and writes the <tempo-map> element ONLY when TempoMap::shouldPersist() (active or non-empty), so a project that predates the feature re-saves byte-identically; clears the map in clearProject(); and calls followTempoMap() once per audio block in processNextBuffer(), which returns before touching anything unless the map is active. No other path is altered (held by tests/src/core/TempoMapTest.cpp); 0.3.0 W18 modulation layer (#602, D11): calls processModulation() once per audio block immediately after followTempoMap() (a no-op unless the layer holds a modulator); implements processModulation() (the seqlock snapshot plus the block's arithmetic); reads and writes the <modulation-layer> element ONLY when ModulationLayer::shouldPersist(), so a project that never used a modulator re-saves byte-identically; clears the layer in clearProject(); and restores the modulated parameters to their captured bases in stop(). No other path is altered (held by tests/src/core/ModulationLayerTest.cpp); 0.3.0 groove pool (docs/GROOVE-POOL.md): writes the <groove-pool> element ONLY when GroovePool::shouldPersist() (the pool holds a groove), so a project that never captured one re-saves byte-identically; clears the pool in clearProject(); and restores it from its own <element> in the load walk, where an ABSENT element takes the pool back to empty rather than leaving the previous project's grooves behind. No other path is altered (held by tests/src/core/ControlGrooveCommandsTest.cpp); automation-touch-race: Song::forgetAutomatedModel() and the private s_automationCacheSong it resolves the live song through (set by the constructor, cleared at the top of the destructor before any member dies), so the last-frame automated-value map cannot name a model that has been destroyed - it is walked by both processAutomations() and stop(); no behaviour change for a live model (held by AutomationModesTest::testDestroyedControlIsNotDereferenced) 0.3.0 `clock.*` (030/midi-clock): processNextBuffer() calls MidiClock::processAudioPeriod() once per audio period BEFORE its own transport gate, so the clock master emits START/STOP/CONTINUE/a Song Position Pointer and 24 pulses to the quarter note from the audio thread and the slave's measurement ages; one relaxed atomic load and a return when neither mode is on, so a session that does not use the clock runs the block it always ran (held by MidiClockTest and tests/control-clock-commands.py).; retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): processNextBuffer() publishes the per-period transport tick with one relaxed atomic store (RetroMidiCapture::publishTick) after the `if (!m_playing) return;` gate, so a raw MIDI client can stamp a captured event without reading live Song state off the MIDI thread (getPlayPos() returns a reference; Song.h holds no atomics). One store per rendered period, nothing published while stopped - the render path itself is untouched and the render record is re-verified in the same change. Held by tests/src/core/MidiRetroCaptureTest.cpp.; 0.3.0 feature row 35 (chord track): Song::save writes ONE <chord-track> element and ONLY when the track holds a chord (m_chordTrack.shouldPersist() - the rule TempoMap, ModulationLayer and GroovePool follow, so a project that never used a chord re-saves byte for byte), Song::loadProject reads that element (an absent one leaves the track EMPTY, because ChordTrack::loadSettings clears first), and Song::clearProject() clears the track. Three additive blocks; no existing behaviour, attribute, element or ordering changes; import detection (feature row 34): three hooks for the project's `<detected-key>` element, each the groove pool's own shape one element over - saveSettings writes it ONLY when shouldPersist() (so a project that never ran a detection is byte-identical), loadProject restores it from the element and CLEARS on absence (the reset-on-absence rule that makes the pre-first-detection state reachable from a journal checkpoint), and clearProject clears it so a new project never inherits the last one's key.; 0.3.0 row 9 sample-accurate automation (030/sample-accurate-automation, board task #646, docs/SAMPLE-ACCURATE-AUTOMATION.md): buildAutomationRamps() is defined beside processAutomations() and CALLED once per block from process() before the tick loop, so the whole block's curve reaches the parameters before the first sample of it renders. The tick loop, processAutomations() and every render stage are untouched; ARCH-4 S1b (SPEC-ARCH-4 1.6, unknown-section preservation in <song>): loadProject()'s <song> walk now reads "no reader claimed this element" as preservation. The dispatch was extracted into restoreNamedSection()/restoreGuiSection() - the branches that were inline, unchanged - and whatever the two decline is captured by captureUnclaimed() and put back by reemitUnclaimed() at the end of saveProjectFile(), the position the #else <session> path already writes at. restoreNamedSection() also CLAIMS, without re-reading, the element Engine::mixer()->nodeName(): loadProject() deliberately reads the mixer before this walk (its channels set the range the track walk needs), and without the claim the walk treated the mixer as an unknown section and re-emitted it beside the writer's own <mixer>, so the SECOND save of any project carried two of them (measured: SessionModelTest::preSessionProjectLoadsAndSavesUnchanged gained exactly one duplicate <mixer>, and the same second-save duplication failed StableTrackIdsTest, TempoMapPersistenceTest, ProjectOpenIntegrityTest and MmpzGitDepthTest). State is cleared in clearProject(), so a report can never describe the previous project; a project with nothing unclaimed re-saves byte for byte. ARCH-4 S1c (SPEC-ARCH-4 1.6.2): two changes. (1) unclaimedElements() also reports the <trackcontainer> children this build could not construct - a <track> whose `type` has no class here, or an element that is not a track at all - at `/song/trackcontainer/<name>[<child index>]`, the index being where the re-emitted element lands (after the tracks the writer emits). (2) Song::loadProject's PRE-COUNT walk is fixed: its `node = node.nextSibling()` advance sat INSIDE the `nodeName() == "track"` branch, so the first <trackcontainer> child that was not a <track> made `while( !node.isNull() )` never terminate - the loader spun at 100% CPU before the load proper began and no assertion could ever be reached. Measured 2026-09-21 on the S1c fixture (`<lanes>` inside <trackcontainer>): gdb on the running test put the main thread in Song::loadProject at the walk with the walk's own line as the top frame, 100% CPU, 66 s and counting. Latent rather than live only because every file this tree writes has <track> children alone; a newer writer's element or a hand-edited file triggers it. The fix moves the advance out of the branch, so a non-track child is now SKIPPED by the count (it was never counted) and preserved by the container walk. Held by tests/src/core/UnclaimedTrackTypeTest.cpp, whose second slot is that fixture: without the fix the slot cannot complete. ARCH-4 S2a (SPEC-ARCH-4 1.4, board card #734): saveProjectFile writes the document index LAST, after every section it writes and after the re-emitted unclaimed elements, so the digests describe the document as saved rather than as loaded. It is written under ONE condition - the S1 preserved set (unclaimedElements() or unclaimedChildren()) is non-empty, the same two answers the load report is built from - because an index on a document that carries none would change the bytes of every project 0.4.0 saves, which SPEC-ARCH-4 5.2 risk 2 calls a release invariant rather than a guideline. A project this build understands completely therefore re-saves with no <z:index> and no xmlns:z, which is the half the additive-identity pin verification/arch4-s2-additive-gate.py now falsifies externally (S0's oracle cannot see it). Held by tests/src/core/DocumentIndexTest.cpp, whose integration slot pins both halves. ARCH-4 S6 (SPEC-ARCH-4 1.9, branch 040/arch4-s6): three additive hooks - loadProject clears the in-memory provenance section just before the dispatch walk (reset-on-absence, beside clearVisibilitySets, so a file that never carried <z:provenance> still saves without one), restoreNamedSection CLAIMS <z:provenance> only when provenance::Section::load accepts its exact shape (v="1", well-formed <z:change> children; anything else stays preserved verbatim as unclaimed), and saveProjectFile writes the section only when a change was recorded (the additive rule), before the 1.6.1 unclaimed tail so re-emission stays last. Held by tests/src/core/ProvenanceSectionTest.cpp.
src/core/StemJobManager.cpp                              fork-NEW (allowed)
src/core/StemModelStore.cpp                              fork-NEW (allowed)
src/core/StemTrackBuilder.cpp                            fork-NEW (allowed)
src/core/TakeLane.cpp                                    fork-NEW (allowed)
src/core/Telemetry.cpp                                   fork-NEW (allowed)
src/core/TelemetryNetworkTransport.cpp                   fork-NEW (allowed)
src/core/TempoMap.cpp                                    fork-NEW (allowed)
src/core/Timeline.cpp                                    declared divergence -> 0.3.0 punch in/out: implements setPunchRange (normalised, as setLoopPoints is), setPunchEnabled, clearPunch, punchCapturesAt (armed && begin <= ticks < end), and the three guarded punch attributes in saveSettings plus the reset-on-absence branch in loadSettings. The existing loop/stopbehaviour attributes and the lp0pos*3 upgrade path are untouched, so a project without a punch region is byte-identical to before.
src/core/TrackContainer.cpp                              declared divergence -> #625 headless load: the application-modal 'Loading project...' progress window is not created in an unattended run (its Cancel button is the only way to stop a load); fix(control-undo): TrackContainer::loadSettings() emits aboutToClearTracks() before clearAllTracks() on a journal restore. The restore deleted tracks under live views; a view is deleted from a deferred event once its track dies, and its destructor reads that dead track - measured SIGSEGV in ~InstrumentTrackView() at a null base + 0x308 (docs/UNDO-RELEASE-CONFIG.md); #664 structural undo (feature row 75): moveTrack() JOURNALS the reorder it performs - the ONE place every reorder passes through (the product's drag and its arrow-key moves both arrive there via TrackContainerView::moveTrackView), which is row 75's 'several paths bypass addJournalCheckPoint'. A container's order is not a serialized property of any track, so the recorded step is an action pair holding the index the track came from (control::journalTrackMove). Two guards: only the SONG's container is journalled (a pattern track owns a nested container whose step would replay against the wrong list), and a move that IS a recorded step's replay is not recorded again (control::structuralReplayInProgress()), or the stack would grow while it unwinds. ARCH-4 S1c (SPEC-ARCH-4 1.6.1/1.6.2, unknown-type preservation): the container now keeps what its factory refuses. loadSettings() claims a child only when it IS a <track> AND Track::create() constructed one from it (the file-local claimTrackOrPreserveChild(), the shape Track.cpp's own claimClipOrPreserveChild() uses); anything else is captured verbatim by captureUnclaimed() and put back after the written tracks by saveSettings(), which is the position Song::saveProjectFile already gives the <song> sections nothing claimed. The name test is not decoration: Track::create() reads a MISSING `type` as 0, so without it every element a newer writer put inside <trackcontainer> became an Instrument track. m_unclaimedChildren is reset in clearAllTracks() - the one place the tracks it arrived with go, reached by Song::clearProject(), by a journal restore and by ~TrackContainer - because a child kept from the previous project would be written into the next one. No existing reader changes behaviour: a project whose every track was claimed captures nothing and re-saves byte for byte. Held by tests/src/core/UnclaimedTrackTypeTest.cpp (three slots: the unknown type round-trips and is reported, a non-track element is not coerced into a track, and a type this build CAN build is claimed and never reported).
src/core/TrackContainerFolders.cpp                       fork-NEW (allowed)
src/core/Track.cpp                                       declared divergence -> #628 SPEC-stable-ids slice 1: the Track constructor allocates the id, saveTrack writes it as an `id` ATTRIBUTE on the track's own element, loadTrack takes the file's value (or keeps the constructor's for a legacy element), all guarded so presetMode never writes one (SPEC-stable-ids.md 3.1/3.3); #623: initialise m_mutedBeforeSolo in the constructor. It was read uninitialised by saveTrack, which writes it to the project file as an int, so the SAME fresh track saved as mutedBeforeSolo=1 in one run and 48 in the next - measured by the intermittently failing StableTrackIdsTest and by the byte-determinism promise the serializer makes (post-alpha/reversibility); merge train 3F: the incoming unit's initialiser list placed m_mutedBeforeSolo before m_mutedModel while include/Track.h declares it after, which the release configuration's -Werror=reorder rejects (the merge's first build failure, and the file is byte-identical to the unit's own, so it is the unit's defect, not a merge artefact); the entry is moved to declaration order - C++ initialises in declaration order either way, so this is a no-op; comping (task #600): saveTrack writes the take lanes and the composite as one <takelanes> child element ONLY when the model is non-empty, and the element carries metadata=1 because loadTrack turns an unrecognised child of <track> into a real Clip (the trap SPEC-stable-ids.md 3.1 records for the track id); loadTrack clears the model and loads that element, so a track element without one leaves an empty model and the Track's own journal checkpoint inverts every comp.* mutation; freeze / bounce-in-place: saveTrack writes the take as four ATTRIBUTES on the track's own element (frozenAudio/frozenStart/frozenEnd/frozenMuted - attributes and not a child element, because loadTrack turns an unrecognised child of <track> into a real Clip, and a metadata-marked child cannot be used at all: DataFile::write's cleanMetaNodes() removes every marked element from a saved project, measured), loadTrack clears it on absence so a mid-project-format change cannot resurrect a take (the rule every checkpoint restore depends on), Track::length() floors on the take's end because the export path skips a muted clip and a region freeze mutes the clips inside it, and playFrozenTake() queues the take on its own mix-level bus handle from the audio thread while the buffer is loaded on the control thread; stable ids slice 2 (feature row 51): loadTrack refuses the id of a COPY payload (ProjectIds::isDocumentElement), so a track built by Track::clone() or dropped from a track drag gets a fresh trk-<n> instead of second one wearing the source's id; ARCH-4 S1b (SPEC-ARCH-4 1.6, unknown-child preservation on <track>): loadTrack's unrecognised-child branch is SPLIT, not deleted. A child whose nodeName() is one of the four Clip classes' classNodeName() - or one of the four pre-rename literals that DataFile::upgrade_bbTcoRename only rewrites when a file's version is below the upgrade table's size - is loaded as a real Clip by the new file-local claimClipOrPreserveChild(); anything else is captured and re-emitted on save. Measured, and NOT what the debt row assumed: that branch was also the CLIP LOADER, since createClip() + clip->restoreState() ran with no nodeName() comparison, so a <sampleclip> under a SampleTrack reached it too. Capturing clips as unknown children therefore emptied m_clips, TrackContainer::isEmpty() reported every project empty, and 25 subtests went red - 17 of them green again once the split landed. The muted / solo / metadata="1" guards are unchanged.; 040/arch4-s4 (2026-09-22, ARCH-4 slice S4, SPEC-ARCH-4 1.7 Requirement 6): reset-on-absence at the four checkpoint-restore sites (the frozen take, the take lanes, the folder/visible pair) is derived from a file-local TRACK_RESTORE_SCHEMA stated once instead of a comment per field; the folder/visible reads take their fallback from that table through trackRestoreDefault(), behaviour identical for every input (attribute(name, default) equals the old hasAttribute ternary: absent -> -1 / "1", present -> toInt as before). Comment consolidation plus read plumbing only; proven by the existing checkpoint-restore tests and the registered write-refusal gate.
src/core/UnattendedRun.cpp                               fork-NEW (allowed)
src/core/UnclaimedElements.cpp                           fork-NEW (allowed)
src/core/VcaGroup.cpp                                    fork-NEW (allowed)
src/gui/CMakeLists.txt                                   build/config (allowed)
src/gui/editors/PianoRoll.cpp                            declared divergence -> compile-only Qt6 fix: QMouseEvent::x()/y() via the repo's own lmms::position() adapter (2c77fd71a); #601 MPE: recording a note copies the expression captured on its own MIDI channel from the live note handle
src/gui/editors/TrackContainerView.cpp                   declared divergence -> fix(control-undo): TrackContainerView connects TrackContainer::aboutToClearTracks() to removeAllTrackViews() (direct connection: the tracks are deleted by the next statement) and implements it. This is the view-then-track order TrackContainerView::deleteTrackView() and Song::clearProject() already use (docs/UNDO-RELEASE-CONFIG.md)
src/gui/embed.cpp                                        declared divergence -> tag v0.2.1-alpha CI fix: embed.cpp streams qWarning() << ... in loadSvgPixmap() and the SVG loader and includes no header that defines QDebug, so the Qt5 jobs are expected to fail with 'invalid use of incomplete type class QDebug' exactly as src/core/ConfigManager.cpp did (docs/QDEBUG-CLASS-AND-MIME-RENAME.md). Adding the include is the file's only change; it is otherwise byte-identical to origin/master. Compile-only: no behaviour, no output, no ABI.
src/gui/FocusDesk.cpp                                    fork-NEW (allowed)
src/gui/FocusDeskModules.cpp                             fork-NEW (allowed)
src/gui/FocusDeskPane.cpp                                fork-NEW (allowed)
src/gui/FocusDeskPlacement.cpp                           fork-NEW (allowed)
src/gui/GuiApplication.cpp                               declared divergence -> wave R rename (018d2041f): the first-run working-directory prompt, its title and the surrounding comments named LMMS -> Zene Studio. Strings and comments only; the directory itself is unchanged.; wave R rename: working-directory prompt text names the renamed product; #625 headless load: the missing-working-directory prompt is answered by creating the directory when no human can click it (post-alpha/headless-load)
src/gui/instrument/InstrumentView.cpp                    declared divergence -> InstrumentView::setModel() guards the window-icon call on instrumentTrackWindow(): the view is built outside an InstrumentTrackWindow by every caller that is not InstrumentTrackWindow::updateInstrumentView(), and the unguarded call was a null-pointer dereference inside QWidget::setWindowIcon (docs/INSTRUMENT-VIEW-SAFETY.md; regression test testTheViewsEntryPointIsSafeOutsideAnInstrumentTrackWindow)
src/gui/LmmsStyle.cpp                                    declared divergence -> wave R rename (018d2041f): the theme-reload status message named LMMS -> Zene Studio. User-visible string only.; wave R rename: theme-reload notice text names the renamed product
src/gui/MainWindow.cpp                                   declared divergence -> compile-only CI fixes for Qt6/-Werror: missing <QDebug> (7cd9da2b1), QMenu::addAction deprecation (7f08809e4); plugin-scan: build the Tools menu on first open, so start-up never scans plugins (023770f7b); Lua console: runScript stops printing the captured log a second time, ScriptConsole streams it onto Qt's logging path (task #613); wave R rename (018d2041f): the window title and the audio-device failure message name Zene Studio, and the stale wiki link (lmms.sf.net/wiki) is replaced by the product's documentation URL.; plus the Edit > MIDI Learn menu action and its two slots, which handle only the action's tick and read the armed state from MidiLearnGui (5d6ccdf1f, post-alpha/midi-race); autosave identity sidecar written beside recover.mmp and its removal in sessionCleanup (post-alpha/autosave); compile-only CI fixes for Qt6/-Werror: missing <QDebug> (7cd9da2b1), QMenu::addAction deprecation (7f08809e4); #617 telemetry: a Help-menu action opens the "what we send" consent/preview dialog (telemetry-enabled builds only); compile-only CI fixes for Qt6/-Werror: missing <QDebug> (7cd9da2b1), QMenu::addAction deprecation (7f08809e4); plugin-scan: build the Tools menu on first open, so start-up never scans plugins (023770f7b); Lua console: runScript stops printing the captured log a second time, ScriptConsole streams it onto Qt's logging path (task #613); wave R rename (018d2041f): the window title and the audio-device failure message name Zene Studio, and the stale wiki link (lmms.sf.net/wiki) is replaced by the product's documentation URL.; plus the Edit > MIDI Learn menu action and its two slots, which handle only the action's tick and read the armed state from MidiLearnGui (5d6ccdf1f, post-alpha/midi-race); autosave identity sidecar written beside recover.mmp and its removal in sessionCleanup (post-alpha/autosave); #626: in a headless run skip the interactive setup/audio-failure dialogs that blocked before app->exec() (the failure is reported instead) and answer the "project was modified" quit prompt from the control surface's stated intent (post-alpha/control-hardening); #A11/A15: Edit->Undo/Redo declare the registry commands they implement (control.undo/control.redo, the same ProjectJournal the menu slot drives) so the agent_surface gate can see the menu item as reachable (post-alpha/agent-surface-gate); #625 headless load: the first-run setup dialog and the 'Audio device setup failed' box are not opened in an unattended run; #A11/A15: the Edit > MIDI Learn action declares the registry command midi.learn_toggle (QAction::data) and toggleMidiLearn() now invokes that command instead of arming MidiLearnGui itself, so the menu item and the agent surface are ONE implementation (post-alpha/agent-surface-onto-integration); #A11/A15 for the telemetry action too: Help > "Telemetry - what we send..." now declares telemetry.consent in the dynamic property "controlCommand" and its slot opens the consent screen through lmms::openTelemetryConsentScreen - the same function the registry handler calls - so that menu item and the agent surface are ONE implementation as well (post-alpha/foreign-merge agent-surface fix, docs/AGENT-SURFACE-TELEMETRY-FIX.md) telemetry consent action icon: embed::getIconPixmap("setup") named a resource that does not exist while its siblings are setup_general/setup_audio/setup_midi - a menu icon that would not load, found by the brand-resource sweep (1 NEW unresolved call site) and corrected to setup_general; auto-save recovery: a clean quit clears recover.mmp through QCoreApplication::aboutToQuit as well, because Qt5's QCoreApplication::quit() is only exit(0) (Qt 5.15 qcoreapplication.cpp) - it sends no QEvent::Quit, so QApplication::event() never closes the window and MainWindow::closeEvent, which owned that cleanup, never ran on a control-driven quit, leaving the crash marker a clean quit must drop (CI: ControlShutdown red on linux-x86_64, linux-arm64 and macos-arm64; tests/control-shutdown-logs/); retrospective MIDI capture (owner item 14, docs/MIDI-RETRO-CAPTURE.md): the Edit menu gains Arm MIDI Capture (checkable) and Capture MIDI beside Edit > MIDI Learn, each declaring its registry command in QAction::data and each slot invoking that same command (toggleMidiRetroCapture/captureMidiToClip/updateMidiRetroCaptureActions); finalize() applies the persisted midi/retrocapture key through applyPersistedRetroCaptureArm(), which is the first point where qApp exists and a MIDI client is open (RetroMidiCapture's constructor runs before main(), where a ConfigManager read is a null dereference); capture feedback is a transient status-bar line, and a refusal is a modal box ONLY when a human is present (isUnattendedRun() otherwise stderr), so a headless instance is never parked on a dialog; DOC-5 (2026-09-15): comment-only - the Lua spec citation names docs/specs/SPEC-lua-api-v0.md, the pinned snapshot in this repo
src/gui/MidiLearnGui.cpp                                 fork-NEW (allowed)
src/gui/MixerView.cpp                                    declared divergence -> bounds every index that crosses from this view's channel list to the MIXER's, which the ENGINE can make SHORTER behind the view's back (Mixer::deleteChannel is reached from TrackFolder::releaseRouting(), from mixer.remove_channel and from Mixer::clear() on a project load) while Mixer::mixerChannel() does not bounds-check at all, so a view index past the mixer's last channel was a wild MixerChannel* (measured: SIGSEGV inside QObject::disconnectImpl at the instance's own shutdown, exit code -11, in a session that had reopened a routing folder and switched it back to group mode; and with NO folder in the project at all, through mixer.add_channel + project.save + project.open + mixer.remove_channel + control.quit, also -11). Four ONE-LINE bounds - refreshDisplay()'s disconnect walk, deleteUnusedChannels()'s isChannelInUse() walk, updateFaders()'s peak walk and deleteChannel()'s own index guard - plus one note at the top of the file stating the rule; no other line changed. The file ends at exactly its recorded 592-line baseline: the note is paid for by collapsing this file's redundant runs of 3-5 blank lines down to the 2 it uses everywhere else (whitespace OUTSIDE functions only; no code removed). Proof: ctest ControlTrackFolderTranscript's control.quit check, which failed with -11 before this change and passes after, on one and the same scenario.
src/gui/modals/about_dialog.ui                           declared divergence -> wave R rename (018d2041f): the About box title, product line, tagline and wiki link move to Zene Studio (the link points at the product repo). UI strings only.; wave R rename: About dialog title/body/link name the renamed product
src/gui/modals/ControllerConnectionDialog.cpp            declared divergence -> rename layer 1: user-visible label/dialog string says Zene Studio
src/gui/modals/EffectSelectDialog.cpp                    declared divergence -> rename layer 1: the built-in plugin type column/button is labelled Zene Studio (label and filter string changed together)
src/gui/modals/ExportProjectDialog.cpp                   declared divergence -> #618 loudness wiring: the checkbox that asks for the report and the result label that shows the measured values when the render finishes (the dialog stays open instead of closing on a report)
src/gui/modals/SetupDialog.cpp                           declared divergence -> wave R rename (018d2041f): the Setup dialog's working-directory row and its file-picker caption named LMMS -> Zene Studio. User-visible strings only.; wave R rename: settings path labels follow the renamed product
src/gui/PinConnector.cpp                                 fork-NEW (allowed)
src/gui/PluginBrowser.cpp                                declared divergence -> plugin-scan: populate the plugin tree on first show, so start-up never scans plugins (023770f7b); rename layer 1: the built-in plugin tree root is labelled Zene Studio
src/gui/StemSplitController.cpp                          fork-NEW (allowed)
src/gui/SubWindow.cpp                                    declared divergence -> 040/focus-desk (UI plan §9.4 Direction B, 2026-09-21): isDetached() and setVisible() dereferenced widget() unconditionally, so the first hide() of a subwindow whose editor the Focus Desk had taken onto the desk segfaulted. Both now test the widget for null, which is the convention adjustTitleBar() and detach() in the same class already follow. No change when a widget is present. Regression test: tests/src/gui/FocusDeskPaneTest.cpp, claimingOutOfAProductSubWindowLeavesItSafeToHide.
src/gui/TelemetryConsentDialog.cpp                       fork-NEW (allowed)
src/gui/tracks/PatternTrackView.cpp                      declared divergence -> fix(control-undo): ~PatternTrackView() and close() read PatternTrack::s_infoMap with value() - they run from the destroyedTrack() signal after the track's own destructor erased its entry, so operator[] there manufactured the ghost entry that made the next track's number collide (docs/CONTROL-UNDO-CONNECTION-DROP.md)
src/gui/tracks/TrackOperationsWidget.cpp                 declared divergence -> #664 structural undo (feature row 75): the track delete button records its inverse BEFORE emitting trackRemovalScheduled, because the deletion is DEFERRED (that connection is a queued one) and ~Track destroys the track's clips before TrackContainer::removeTrack is reached - a checkpoint taken inside deleteTrackView would restore a track with an empty clip list. One implementation, control::journalTrackRemoval, shared with the control surface's track.remove, so a delete the user can undo and a delete an agent can undo are the same delete.
src/gui/widgets/Fader.cpp                                declared divergence -> post-alpha/automation-modes: the mixer fader opens, refreshes and closes a touch gesture so Touch/Latch are reachable from the control a mixing engineer actually rides
src/gui/widgets/TabWidget.cpp                            declared divergence -> UI precondition #1 accessibility+palette pass (040/ui-precond, 2026-09-22, SPEC-zene-ui-v0 §4.3/§5 item 1): the hard-coded .darker(132) multiply on the tab background is replaced by the palette's own Shadow-role lookup, so the widget follows the active theme's luminance instead of a constant; measured 13 -> 0 hard-coded QColor literals in LmmsStyle.cpp and .darker(132) -> 0 in this file. Behaviour: same construction, theme-derived shade only.
src/gui/widgets/ToolButton.cpp                           declared divergence -> precondition #1 accessibility (040/ui-precond, 2026-09-22): icon-only ToolButtons carried their label only in the tooltip, so a screen reader never heard it (the measured zero of setAccessibleName in SPEC-zene-ui-v0 §4.3) - a11y::announce(this, _tooltip) names every such button at construction, and setFocusPolicy(Qt::TabFocus) pins QToolButton's documented default so a Qt default change cannot make toolbars keyboard-unreachable (§5 item 1). add-only: one include, two calls in the pixmap constructor; no existing line altered.
src/lmmsconfig.h.in                                      declared divergence -> #594 Session View: #cmakedefine LMMS_HAVE_SESSION_VIEW plumbing for the WANT_SESSION_VIEW option; build configuration only, no runtime behaviour when the option is off. CORRECTED 2026-09-13 (0.3.0-alpha): the option's default is now ON, so "off" is no longer "the default configuration" - the reason is narrowed rather than left stale. The release-honesty row that has to move with it is tests/advertised-features.tsv's session-view row; #617 telemetry: the ZENE_TELEMETRY_ENABLED packager kill switch define (OFF removes the client and its networking code)
src/tracks/CMakeLists.txt                                build/config (allowed)
src/tracks/InstrumentTrack.cpp                           declared divergence -> post-alpha/midi-depth: skip a note that loses the seeded probability roll, and apply the seeded velocity jitter to the NotePlayHandle, at scheduling time in play(); #601 MPE: capture per-note expression from member channels in processInEvent (off by default) and stamp it on the note handles; freeze / bounce-in-place: play() returns the frozen take for a pass inside the take's window and schedules none of the instrument's notes - the whole of "the source is disabled" - leaving single-clip playback (_clip_num >= 0, the piano roll's own play mode) on the source; #649 MPE playback: loadMpeExpressionOntoChannelNotes() sends a live pressure/timbre update on the containing note handle to the instrument (NotePlayHandle::sendMpeExpressionMidi) when either of those two axes actually changed - the pitch axis already reached playback through setFrequencyUpdate()
src/tracks/MidiClip.cpp                                  declared divergence -> #645 linked / smart clips (feature row 6, docs/LINKED-CLIPS.md) - three additive changes, none of them a change of what an unlinked clip does: (1) the propagation hook - MidiClip::addNote and both MidiClip::removeNote overloads call ClipLinks::mirrorContent(this) when the clip is a member of a link group (linkId() > 0), which copies this clip's note list onto the group's other members; the call is a no-op for every unlinked clip, which is every clip in every project that does not use the feature, and the mirror itself is re-entrancy-guarded in src/core/ClipLinks.cpp so the members' own entry points cannot mirror back; (2) the clip's non-default attributes reach the element - MidiClip::exportToXML calls the already-inherited Clip::saveClipEdits(), and MidiClip::loadSettings calls Clip::loadClipEdits(), so a MIDI clip's `link` attribute (and the take lane / fades / gain that pair already carried) is written only when it is not the neutral default and read back with a reset-on-absence rule - an unlinked, unedited MIDI clip serialises exactly as it did before this change (invariant I9); (3) MidiClip::loadSettings takes a ClipLinks::MirroringSuspension for the length of the load, because a load pass re-creates each clip in turn and propagating there would write half-loaded content into siblings.
src/tracks/PatternTrack.cpp                              declared divergence -> fix(control-undo): the five registry reads that are not the constructor's insert (play() x2, saveTrackSpecificSettings' two reads, the destructor's own read) use s_infoMap.value(this); swapPatternTracks keeps operator[] because it assigns - it swaps two live tracks' numbers (docs/CONTROL-UNDO-CONNECTION-DROP.md)
src/tracks/SampleTrack.cpp                               declared divergence -> #611 Slice 0: play() no longer writes the clip's window; the pass derives the window from the transport position and hands it to the play handle as a snapshot (I1); freeze / bounce-in-place: the same substitution as InstrumentTrack::play, so a frozen SAMPLE track plays its render and schedules none of its clips
src/tracks/TrackFolder.cpp                               fork-NEW (allowed)
src/wasm/CMakeLists.txt                                  build/config (allowed)
src/wasm/WasmAbi.h                                       fork-NEW (allowed)
src/wasm/WasmOfflineRender.cpp                           fork-NEW (allowed)
src/wasm/WasmOfflineRender.h                             fork-NEW (allowed)
src/wasm/WasmSandbox.cpp                                 fork-NEW (allowed)
src/wasm/WasmSandbox.h                                   fork-NEW (allowed)
src/wasm/WasmSandboxHost.cpp                             fork-NEW (allowed)
src/wasm/WasmSandboxHost.h                               fork-NEW (allowed)
src/wasm/WasmSpscRingBuffer.h                            fork-NEW (allowed)
src/wasm/WasmWorker.cpp                                  fork-NEW (allowed)
src/wasm/WasmWorker.h                                    fork-NEW (allowed)
src/wasm/WasmWorkerPool.cpp                              fork-NEW (allowed)
src/wasm/WasmWorkerPool.h                                fork-NEW (allowed)
tests/advertised-features.tsv                            tests (allowed)
tests/agent-surface-allowlist.txt                        tests (allowed)
tests/agent-surface-baseline.txt                         tests (allowed)
tests/agent-surface-exempt.txt                           tests (allowed)
tests/agent-surface-gate.py                              tests (allowed)
tests/agent_surface_lib.py                               tests (allowed)
tests/agent-surface-negative-control.md                  tests (allowed)
tests/all-sources-reproduce.sh                           tests (allowed)
tests/all-sources.txt                                    tests (allowed)
tests/automation-modes-render-check.sh                   tests (allowed)
tests/brand-resource-sweep.py                            tests (allowed)
tests/CMakeLists.txt                                     tests (allowed)
tests/complexity-baseline-all.tsv                        tests (allowed)
tests/complexity-baseline-tools.tsv                      tests (allowed)
tests/complexity-baseline.tsv                            tests (allowed)
tests/complexity-gate.sh                                 tests (allowed)
tests/control-absent-socket.py                           tests (allowed)
tests/control-arm64-cluster-logs/big-reply-prefix-semantics.txt tests (allowed)
tests/control-arm64-cluster-logs/big-reply-with-fix.txt  tests (allowed)
tests/control-bus-commands.py                            tests (allowed)
tests/control-chain-presets.py                           tests (allowed)
tests/control-clock-commands.py                          tests (allowed)
tests/control-commands-snapshot.py                       tests (allowed)
tests/control-crash-reporter.py                          tests (allowed)
tests/control-detect-commands.py                         tests (allowed)
tests/control_detect_fixtures.py                         tests (allowed)
tests/control-export-settings.py                         tests (allowed)
tests/control-feedback-commands.py                       tests (allowed)
tests/control-freeze-commands-transcript.py              tests (allowed)
tests/control-golden-audio.py                            tests (allowed)
tests/control-groove-commands.py                         tests (allowed)
tests/control-headless-no-audio-device.py                tests (allowed)
tests/control-headless-project-open.py                   tests (allowed)
tests/control-headless-working-directory.py              tests (allowed)
tests/control_instance_diagnosis.py                      tests (allowed)
tests/control-link-sync.py                               tests (allowed)
tests/control-link-sync-transcript.txt                   tests (allowed)
tests/control-livecode-commands.py                       tests (allowed)
tests/control-mastering-commands.py                      tests (allowed)
tests/control-mcp-group-coverage.py                      tests (allowed)
tests/control-meter-commands.py                          tests (allowed)
tests/control-midi-reconnect.py                          tests (allowed)
tests/control-named-pipe-smoke.py                        tests (allowed)
tests/control-negative-control.py                        tests (allowed)
tests/control-no-audio-device.py                         tests (allowed)
tests/control-pdc-commands.py                            tests (allowed)
tests/control_pipe_client.py                             tests (allowed)
tests/control-pitch-stretch-transcript.py                tests (allowed)
tests/control-plugin-scan-commands.py                    tests (allowed)
tests/control-ports-commands.py                          tests (allowed)
tests/control-punch-transcript.py                        tests (allowed)
tests/control-readiness.py                               tests (allowed)
tests/control-recording-recovery.py                      tests (allowed)
tests/control-record-inputs.py                           tests (allowed)
tests/control-render-presets.py                          tests (allowed)
tests/control-retro-capture.py                           tests (allowed)
tests/control-reversibility-transcript.py                tests (allowed)
tests/control-routing-commands.py                        tests (allowed)
tests/control-session-api-proof.py                       tests (allowed)
tests/control-session-lifecycle-transcript.py            tests (allowed)
tests/control-session-m1.py                              tests (allowed)
tests/control-session-m1-transcript.txt                  tests (allowed)
tests/control-shutdown-logs/ci-test85-failure.txt        tests (allowed)
tests/control-shutdown-logs/ctest-92-tests.txt           tests (allowed)
tests/control-shutdown-logs/local-qt5-route-after-fix.txt tests (allowed)
tests/control-shutdown-logs/local-qt5-route-before-fix.txt tests (allowed)
tests/control-shutdown-logs/poll-bounds-proof.txt        tests (allowed)
tests/control-shutdown-logs/qt-quit-difference.txt       tests (allowed)
tests/control-shutdown-logs/run-all-gates.txt            tests (allowed)
tests/control-shutdown.py                                tests (allowed)
tests/control_socket_flows.py                            tests (allowed)
tests/control_socket_harness.py                          tests (allowed)
tests/control-socket-integration.py                      tests (allowed)
tests/control-socket-path-safety.py                      tests (allowed)
tests/control-stable-ids.py                              tests (allowed)
tests/control-stable-ids-slice2.py                       tests (allowed)
tests/control-stem-commands.py                           tests (allowed)
tests/control-stem-export-verb.py                        tests (allowed)
tests/control-track-folder.py                            tests (allowed)
tests/control-undo-structural.py                         tests (allowed)
tests/control-vca-commands.py                            tests (allowed)
tests/control_vendor_libs.py                             tests (allowed)
tests/control-warp-commands-transcript.py                tests (allowed)
tests/control-wasm-sandbox.py                            tests (allowed)
tests/coverage-baseline-all.tsv                          tests (allowed)
tests/coverage-baseline.tsv                              tests (allowed)
tests/coverage-entry-floor-exempt.txt                    tests (allowed)
tests/coverage-gate.sh                                   tests (allowed)
tests/data/agent-control-fixture.mmp                     tests (allowed)
tests/data/automation-audio-fixture.mmp                  tests (allowed)
tests/data/clap-test-plugin/clap-test-broken.c           tests (allowed)
tests/data/clap-test-plugin/clap-test-gain.c             tests (allowed)
tests/data/clap-test-plugin/clap-test-instrument.c       tests (allowed)
tests/data/clap-test-plugin/CMakeLists.txt               tests (allowed)
tests/data/clip-window/render-proof.sh                   tests (allowed)
tests/data/error-carrying-project.mmp                    tests (allowed)
tests/data/loudness/bs1770_reference.py                  tests (allowed)
tests/data/loudness/make-fixtures.py                     tests (allowed)
tests/data/loudness/passivity-check.py                   tests (allowed)
tests/data/loudness/render-evidence.sh                   tests (allowed)
tests/data/midi-depth/baseline.mmp                       tests (allowed)
tests/data/midi-depth/gen-fixtures.py                    tests (allowed)
tests/data/midi-depth/prob-all-one.mmp                   tests (allowed)
tests/data/midi-depth/prob-seed1.mmp                     tests (allowed)
tests/data/midi-depth/prob-seed1-vol.mmp                 tests (allowed)
tests/data/midi-depth/prob-seed2.mmp                     tests (allowed)
tests/data/midi-depth/render-proof.py                    tests (allowed)
tests/data/midi-depth/veljit-seed1.mmp                   tests (allowed)
tests/data/midi-depth/veljit-seed2.mmp                   tests (allowed)
tests/data/modulation-layer-fixture.mmp                  tests (allowed)
tests/data/mpe/mpe-expression.mmp                        tests (allowed)
tests/data/mpe/mpe-neutral.mmp                           tests (allowed)
tests/data/mpe/mpe-plain.mmp                             tests (allowed)
tests/data/oop-hosting/compare-renders.py                tests (allowed)
tests/data/oop-hosting/kill-loop-experiment.sh           tests (allowed)
tests/data/oop-hosting/observe-render.sh                 tests (allowed)
tests/data/oop-hosting/README.md                         tests (allowed)
tests/data/oop-hosting/zyn-separate-process-off.mmp      tests (allowed)
tests/data/oop-hosting/zyn-separate-process-on.mmp       tests (allowed)
tests/data/README-error-fixtures.md                      tests (allowed)
tests/data/vca-inject-group.py                           tests (allowed)
tests/data/vca-render-fixture.mmp                        tests (allowed)
tests/data/vst3-chunk-probe/CMakeLists.txt               tests (allowed)
tests/data/vst3-chunk-probe/Info.plist.in                tests (allowed)
tests/data/vst3-chunk-probe/vst3-chunk-probe.cpp         tests (allowed)
tests/data/vst3-test-effect/CMakeLists.txt               tests (allowed)
tests/data/vst3-test-effect/Info.plist.in                tests (allowed)
tests/data/vst3-test-instrument/CMakeLists.txt           tests (allowed)
tests/data/vst3-test-instrument/vst3-test-instrument.cpp tests (allowed)
tests/data/warp/render-proof.sh                          tests (allowed)
tests/data/wasm-effect-abi/probes/channels-clamped.wat   tests (allowed)
tests/data/wasm-effect-abi/probes/channels-function.wat  tests (allowed)
tests/data/wasm-effect-abi/probes/grow.wat               tests (allowed)
tests/data/wasm-effect-abi/probes/mutable-channels.wat   tests (allowed)
tests/data/wasm-effect-abi/softclip.wat                  tests (allowed)
tests/duplication-gate.sh                                tests (allowed)
tests/evidence/export-src-dither/transcript.txt          tests (allowed)
tests/evidence-gate-exempt.txt                           tests (allowed)
tests/evidence-gate.sh                                   tests (allowed)
tests/evidence/integration-0.2.1-wave2/REPORT.md         tests (allowed)
tests/evidence-manifest.tsv                              tests (allowed)
tests/evidence/smf-interchange/transcript.txt            tests (allowed)
tests/evidence/vst3-platform-fixtures/README.md          tests (allowed)
tests/evidence/wave3-remaining/cap-refusal-reader-proof.txt tests (allowed)
tests/evidence/wave3-remaining/DIAGNOSIS.md              tests (allowed)
tests/evidence/wave3-remaining/gates-exit.txt            tests (allowed)
tests/evidence/wave3-remaining/linux-arm64-103762607496.txt tests (allowed)
tests/evidence/wave3-remaining/macos-arm64-103762607454.txt tests (allowed)
tests/evidence/wave3-remaining/macos-x86_64-103762607462.txt tests (allowed)
tests/evidence/wave3-remaining/msvc-x64-103762607470.txt tests (allowed)
tests/evidence/wave3-remaining/pre-wave-macos-arm64-103724603529.txt tests (allowed)
tests/file-length-baseline-all.tsv                       tests (allowed)
tests/file-length-baseline-tools.tsv                     tests (allowed)
tests/file-length-baseline.tsv                           tests (allowed)
tests/file-length-exempt.txt                             tests (allowed)
tests/file-length-gate.sh                                tests (allowed)
tests/fork-sources-gate.sh                               tests (allowed)
tests/fork-sources.txt                                   tests (allowed)
tests/freeze_bounce_evidence.py                          tests (allowed)
tests/gate-base.txt                                      tests (allowed)
tests/golden_audio_lib.py                                tests (allowed)
tests/golden_audio_record.py                             tests (allowed)
tests/golden-audio-record.tsv                            tests (allowed)
tests/golden_audio_selftest.py                           tests (allowed)
tests/link_sync_evidence.py                              tests (allowed)
tests/lua-api-surface.py                                 tests (allowed)
tests/mastering_probe_lib.py                             tests (allowed)
tests/mcp_stdio_session.py                               tests (allowed)
tests/midi_reconnect_flows.py                            tests (allowed)
tests/midi_reconnect_probe.py                            tests (allowed)
tests/mixer-concurrency-tsan.supp                        tests (allowed)
tests/mmpz-v2-container-refusal.py                       tests (allowed)
tests/mutation-gate.sh                                   tests (allowed)
tests/no-tautology-gate.sh                               tests (allowed)
tests/no-upstream-regression-gate.sh                     tests (allowed)
tests/prove-clap-loader-unchanged.sh                     tests (allowed)
tests/prove-posix-unchanged.sh                           tests (allowed)
tests/QA-GATES.md                                        tests (allowed)
tests/reference/extract-reference-sources.sh             tests (allowed)
tests/reference/plugin_export.h                          tests (allowed)
tests/reference/routing-graph-live-render.raw            tests (allowed)
tests/release-ci-evidence-gate.sh                        tests (allowed)
tests/release-honesty-gate.sh                            tests (allowed)
tests/release-mcp-bridge-leg.sh                          tests (allowed)
tests/release-ref-fitness.sh                             tests (allowed)
tests/release-staging-path-gate.sh                       tests (allowed)
tests/release-version-gate.sh                            tests (allowed)
tests/render-software-tag.py                             tests (allowed)
tests/rt-safety-allowlist.txt                            tests (allowed)
tests/rt_safety_lib.py                                   tests (allowed)
tests/rt-safety-scope.txt                                tests (allowed)
tests/rt_safety_selftest.py                              tests (allowed)
tests/rt_safety_source.py                                tests (allowed)
tests/rt-safety-sweep.py                                 tests (allowed)
tests/run-all-gates.sh                                   tests (allowed)
tests/run-coverage.sh                                    tests (allowed)
tests/run-mixer-concurrency-tsan.sh                      tests (allowed)
tests/scripted/check-namespace                           tests (allowed)
tests/scripted/test-hygiene-under-load.sh                tests (allowed)
tests/session_api_proof_lib.py                           tests (allowed)
tests/session_api_proof_rows.py                          tests (allowed)
tests/src/core/AllocationProbe.h                         tests (allowed)
tests/src/core/AudioBufferTest.cpp                       tests (allowed)
tests/src/core/AudioBusHandleTest.cpp                    tests (allowed)
tests/src/core/AudioBusTest.cpp                          tests (allowed)
tests/src/core/AudioEngineTeardownTest.cpp               tests (allowed)
tests/src/core/AudioPortsModelTest.cpp                   tests (allowed)
tests/src/core/AudioResamplerRatioTest.cpp               tests (allowed)
tests/src/core/AudioStretcherTest.cpp                    tests (allowed)
tests/src/core/AutomationModesTest.cpp                   tests (allowed)
tests/src/core/BrowserCatalogTest.cpp                    tests (allowed)
tests/src/core/BrowserTestSupport.h                      tests (allowed)
tests/src/core/ChordDetectTest.cpp                       tests (allowed)
tests/src/core/ChordProgressionTest.cpp                  tests (allowed)
tests/src/core/ChordTestSupport.h                        tests (allowed)
tests/src/core/ChordTrackTest.cpp                        tests (allowed)
tests/src/core/ClipEditsTest.cpp                         tests (allowed)
tests/src/core/ClipFadesRenderTest.cpp                   tests (allowed)
tests/src/core/ClipLinkPersistenceTest.cpp               tests (allowed)
tests/src/core/ClipLinkTest.cpp                          tests (allowed)
tests/src/core/ClipLinkTestSupport.h                     tests (allowed)
tests/src/core/ClipSerialisationTest.cpp                 tests (allowed)
tests/src/core/ClipWarpPersistenceTest.cpp               tests (allowed)
tests/src/core/ConfigMigrationTest.cpp                   tests (allowed)
tests/src/core/ControlAutomationModesTest.cpp            tests (allowed)
tests/src/core/ControlAutomationScriptTest.cpp           tests (allowed)
tests/src/core/ControlBrowserCommandsTest.cpp            tests (allowed)
tests/src/core/ControlChainPresetTest.cpp                tests (allowed)
tests/src/core/ControlChordCommandsTest.cpp              tests (allowed)
tests/src/core/ControlChordWriteTest.cpp                 tests (allowed)
tests/src/core/ControlDeviceCatalogueTest.cpp            tests (allowed)
tests/src/core/ControlEditCommandsTest.cpp               tests (allowed)
tests/src/core/ControlGrooveCommandsTest.cpp             tests (allowed)
tests/src/core/ControllerSurfaceTest.cpp                 tests (allowed)
tests/src/core/ControlLinkCommandsTest.cpp               tests (allowed)
tests/src/core/ControlModulatorCommandsTest.cpp          tests (allowed)
tests/src/core/ControlNoteExpressionCommandsTest.cpp     tests (allowed)
tests/src/core/ControlNoteScaleVerbsTest.cpp             tests (allowed)
tests/src/core/ControlProjectArchiveTest.cpp             tests (allowed)
tests/src/core/ControlRegistryTest.cpp                   tests (allowed)
tests/src/core/ControlSampleOperatorTest.cpp             tests (allowed)
tests/src/core/ControlShutdownHookTest.cpp               tests (allowed)
tests/src/core/ControlSurfaceReferenceTest.cpp           tests (allowed)
tests/src/core/ControlTempoMapCommandsTest.cpp           tests (allowed)
tests/src/core/ControlVcaCommandsTest.cpp                tests (allowed)
tests/src/core/ControlVcaEditGroupsTest.cpp              tests (allowed)
tests/src/core/ControlVcaTestSupport.h                   tests (allowed)
tests/src/core/ControlVerbInverseTest.cpp                tests (allowed)
tests/src/core/ControlWarpCommandsTest.cpp               tests (allowed)
tests/src/core/CrashReporterArmTest.cpp                  tests (allowed)
tests/src/core/CrashReporterTest.cpp                     tests (allowed)
tests/src/core/DataFileFormatTest.cpp                    tests (allowed)
tests/src/core/DataFileSaveIntegrityTest.cpp             tests (allowed)
tests/src/core/DawProjectInterchangeRoundTripTest.cpp    tests (allowed)
tests/src/core/DocumentIndexTest.cpp                     tests (allowed)
tests/src/core/DocumentSectionsTest.cpp                  tests (allowed)
tests/src/core/ExportDitherTest.cpp                      tests (allowed)
tests/src/core/ExportWavDitherTest.cpp                   tests (allowed)
tests/src/core/GrooveTemplateTest.cpp                    tests (allowed)
tests/src/core/GrooveTestSupport.h                       tests (allowed)
tests/src/core/ImportDetectionTest.cpp                   tests (allowed)
tests/src/core/LoudnessReportTest.cpp                    tests (allowed)
tests/src/core/LufsMeterTest.cpp                         tests (allowed)
tests/src/core/MasteringTest.cpp                         tests (allowed)
tests/src/core/MasteringTestSupport.h                    tests (allowed)
tests/src/core/MeterTapTest.cpp                          tests (allowed)
tests/src/core/MidiClockTest.cpp                         tests (allowed)
tests/src/core/MidiLearnTest.cpp                         tests (allowed)
tests/src/core/MidiLearnThreadTest.cpp                   tests (allowed)
tests/src/core/MidiProbabilityPersistenceTest.cpp        tests (allowed)
tests/src/core/MidiReconnectTest.cpp                     tests (allowed)
tests/src/core/MidiRetroCaptureTest.cpp                  tests (allowed)
tests/src/core/MixerAbRegressionTest.cpp                 tests (allowed)
tests/src/core/MixerConcurrencyTest.cpp                  tests (allowed)
tests/src/core/MixerRoutingBackwardCompatTest.cpp        tests (allowed)
tests/src/core/ModulationLayerProjectRoundTripTest.cpp   tests (allowed)
tests/src/core/ModulationLayerTest.cpp                   tests (allowed)
tests/src/core/ModulationLayerValueTest.cpp              tests (allowed)
tests/src/core/ModulationTestSupport.h                   tests (allowed)
tests/src/core/MpeExpressionTest.cpp                     tests (allowed)
tests/src/core/MpeInputPathTest.cpp                      tests (allowed)
tests/src/core/MpeNoteStorageTest.cpp                    tests (allowed)
tests/src/core/MpePlaybackTest.cpp                       tests (allowed)
tests/src/core/MultiTrackRecorderTest.cpp                tests (allowed)
tests/src/core/NamespaceRegistryTest.cpp                 tests (allowed)
tests/src/core/NoteRandomTest.cpp                        tests (allowed)
tests/src/core/NoteTransformTest.cpp                     tests (allowed)
tests/src/core/OnnxRuntimeStemSeparatorTest.cpp          tests (allowed)
tests/src/core/OutOfProcessHostClientLoopTest.cpp        tests (allowed)
tests/src/core/OutOfProcessHostSupport.h                 tests (allowed)
tests/src/core/OutOfProcessHostTest.cpp                  tests (allowed)
tests/src/core/PartialLoadTest.cpp                       tests (allowed)
tests/src/core/PatcherCommandsTest.cpp                   tests (allowed)
tests/src/core/PdcMixerTest.cpp                          tests (allowed)
tests/src/core/PhaseDMixerTestSupport.h                  tests (allowed)
tests/src/core/PhaseDPerfBench.cpp                       tests (allowed)
tests/src/core/PhaseDSidechainTest.cpp                   tests (allowed)
tests/src/core/PhaseFChannelScaleTest.cpp                tests (allowed)
tests/src/core/PluginAudioPortsTest.cpp                  tests (allowed)
tests/src/core/PluginLogoResourceTest.cpp                tests (allowed)
tests/src/core/PluginScanCacheTest.cpp                   tests (allowed)
tests/src/core/ProjectContainerEntriesTest.cpp           tests (allowed)
tests/src/core/ProjectContainerTest.cpp                  tests (allowed)
tests/src/core/ProjectOpenIntegrityTest.cpp              tests (allowed)
tests/src/core/ProjectRecoveryTest.cpp                   tests (allowed)
tests/src/core/ProjectRevIdsTest.cpp                     tests (allowed)
tests/src/core/ProjectVersionTest.cpp                    tests (allowed)
tests/src/core/ProvenanceSectionTest.cpp                 tests (allowed)
tests/src/core/RackMacrosTest.cpp                        tests (allowed)
tests/src/core/RackTest.cpp                              tests (allowed)
tests/src/core/RackTestSupport.h                         tests (allowed)
tests/src/core/RackZonesTest.cpp                         tests (allowed)
tests/src/core/RecordClipTest.cpp                        tests (allowed)
tests/src/core/RecordingInputPathTest.cpp                tests (allowed)
tests/src/core/RecordingRealtimeTest.cpp                 tests (allowed)
tests/src/core/RecordRingBufferTest.cpp                  tests (allowed)
tests/src/core/RemotePluginAudioPortsTest.cpp            tests (allowed)
tests/src/core/RemotePluginClientE2ETest.cpp             tests (allowed)
tests/src/core/RenderJobQueueTest.cpp                    tests (allowed)
tests/src/core/RetroAudioCaptureTest.cpp                 tests (allowed)
tests/src/core/RetroMidiCaptureCommandsTest.cpp          tests (allowed)
tests/src/core/RetroMidiRingTest.cpp                     tests (allowed)
tests/src/core/ReversibilityContractTest.cpp             tests (allowed)
tests/src/core/ReversibilityTestSupport.h                tests (allowed)
tests/src/core/ReversibilityUndoTest.cpp                 tests (allowed)
tests/src/core/RevisionTimelineTest.cpp                  tests (allowed)
tests/src/core/RoutingGraphLiveTest.cpp                  tests (allowed)
tests/src/core/RoutingGraphScheduleTest.cpp              tests (allowed)
tests/src/core/RoutingGraphTest.cpp                      tests (allowed)
tests/src/core/SafeStartLoadPathTest.cpp                 tests (allowed)
tests/src/core/SafeStartTest.cpp                         tests (allowed)
tests/src/core/SampleAccurateAutomationTest.cpp          tests (allowed)
tests/src/core/ScriptBindingsTest.cpp                    tests (allowed)
tests/src/core/ScriptClockTest.cpp                       tests (allowed)
tests/src/core/ScriptDawBindingTest.cpp                  tests (allowed)
tests/src/core/ScriptEngineTest.cpp                      tests (allowed)
tests/src/core/ScriptMemoryBudgetTest.cpp                tests (allowed)
tests/src/core/ScriptStabilisationTest.cpp               tests (allowed)
tests/src/core/SessionArrangementRecordTest.cpp          tests (allowed)
tests/src/core/SessionFollowTest.cpp                     tests (allowed)
tests/src/core/SessionModelTest.cpp                      tests (allowed)
tests/src/core/SessionSchedulerRenderTest.cpp            tests (allowed)
tests/src/core/SessionSchedulerTest.cpp                  tests (allowed)
tests/src/core/SlideNotesTest.cpp                        tests (allowed)
tests/src/core/SmfInterchangeRoundTripTest.cpp           tests (allowed)
tests/src/core/SmfInterchangeTest.cpp                    tests (allowed)
tests/src/core/SmfInterchangeTestSupport.h               tests (allowed)
tests/src/core/StableTrackIdsTest.cpp                    tests (allowed)
tests/src/core/StemExportTest.cpp                        tests (allowed)
tests/src/core/StemExportTestSupport.h                   tests (allowed)
tests/src/core/StemJobManagerTest.cpp                    tests (allowed)
tests/src/core/StemModelStoreTest.cpp                    tests (allowed)
tests/src/core/StemSplitPipelineTest.cpp                 tests (allowed)
tests/src/core/TakeLaneCompTest.cpp                      tests (allowed)
tests/src/core/TakeLaneTest.cpp                          tests (allowed)
tests/src/core/TakeLaneTestSupport.h                     tests (allowed)
tests/src/core/TelemetryTest.cpp                         tests (allowed)
tests/src/core/TelemetryTransportTest.cpp                tests (allowed)
tests/src/core/TempoMapPersistenceTest.cpp               tests (allowed)
tests/src/core/TempoMapTest.cpp                          tests (allowed)
tests/src/core/TrackFolderTest.cpp                       tests (allowed)
tests/src/core/TwoTrackAlsaCaptureProbe.cpp              tests (allowed)
tests/src/core/TwoTrackRecordingHarness.cpp              tests (allowed)
tests/src/core/UnclaimedElementsTest.cpp                 tests (allowed)
tests/src/core/UnclaimedTrackTypeTest.cpp                tests (allowed)
tests/src/core/UndoBoundsTest.cpp                        tests (allowed)
tests/src/core/VcaGroupTest.cpp                          tests (allowed)
tests/src/core/WarpMarkersTest.cpp                       tests (allowed)
tests/src/core/WriteRefusalGateTest.cpp                  tests (allowed)
tests/src/gui/AccessibilityHelperTest.cpp                tests (allowed)
tests/src/gui/FocusDeskPaneTest.cpp                      tests (allowed)
tests/src/gui/FocusDeskTest.cpp                          tests (allowed)
tests/src/gui/MidiLearnGuiTest.cpp                       tests (allowed)
tests/src/gui/WcagContrastTest.cpp                       tests (allowed)
tests/src/plugins/AudioPluginTest.cpp                    tests (allowed)
tests/src/plugins/ClapBusMapTest.cpp                     tests (allowed)
tests/src/plugins/ClapEffectIntegrationTest.cpp          tests (allowed)
tests/src/plugins/ClapHostTest.cpp                       tests (allowed)
tests/src/plugins/ClapLoaderErrorTest.cpp                tests (allowed)
tests/src/plugins/FakeRemotePluginClient.cpp             tests (allowed)
tests/src/plugins/MpeTestConsumer.cpp                    tests (allowed)
tests/src/plugins/PluginPortsHarness.h                   tests (allowed)
tests/src/plugins/PluginPortsMigrationReference.cpp      tests (allowed)
tests/src/plugins/PluginPortsMigrationTest.cpp           tests (allowed)
tests/src/plugins/SyntheticAudioPlugin.cpp               tests (allowed)
tests/src/plugins/Vst3BusMapTest.cpp                     tests (allowed)
tests/src/plugins/Vst3ChunkProbeTest.cpp                 tests (allowed)
tests/src/plugins/Vst3EffectIntegrationTest.cpp          tests (allowed)
tests/src/plugins/Vst3HostTest.cpp                       tests (allowed)
tests/src/plugins/Vst3InstrumentFixtureProbe.cpp         tests (allowed)
tests/src/plugins/Vst3InstrumentIntegrationTest.cpp      tests (allowed)
tests/src/plugins/Vst3InstrumentTest.cpp                 tests (allowed)
tests/src/plugins/ZynSeparateProcessTest.cpp             tests (allowed)
tests/src/tracks/SampleClipStretchTest.cpp               tests (allowed)
tests/src/tracks/SampleClipWarpTest.cpp                  tests (allowed)
tests/src/tracks/SampleClipWindowTest.cpp                tests (allowed)
tests/src/wasm/WasmAbiConformanceTest.cpp                tests (allowed)
tests/src/wasm/WasmSandboxTest.cpp                       tests (allowed)
tests/src/wasm/WasmWorkerPoolTest.cpp                    tests (allowed)
tests/stem_commands_lib.py                               tests (allowed)
tests/telemetry-off-build.sh                             tests (allowed)
tests/test-package-upload-guard.sh                       tests (allowed)
tests/test-release-ref-fitness.sh                        tests (allowed)
tests/test-release-version-gate.sh                       tests (allowed)
tests/test-verification-debt.sh                          tests (allowed)
tests/tools-sources.txt                                  tests (allowed)
tests/unregistered-tests-gate.sh                         tests (allowed)
tests/upstream-modifications.txt                         tests (allowed)
tests/vst3-instrument-render-proof.sh                    tests (allowed)
tests/zene-api-boundary.py                               tests (allowed)
tools/auto-mastering-demo.py                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/brand/rasterise-placeholders.py                    fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/capture.py                                fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/pilot.py                                  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/pool.py                                   fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/README.md                                 docs (allowed)
tools/crashbot/runner.py                                 fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenario.py                               fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/arrange-undo-structural.json    fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/automation-basics-0001.json     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/browser-basics-0001.json        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/bus-basics-0001.json            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/chain-presets-0001.json         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/clip-edit-basics-0001.json      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/clip-links-0001.json            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/clock-link-midi-0001.json       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/comp-meter-pdc-0001.json        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/controller-templates-0001.json  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/control-surface-audit-0001.json fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/crash-reports-0001.json         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/dawproject-interchange-0001.json fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/detect-oop-wasm-0001.json       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/export-settings-0001.json       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/fixture-roundtrip-0001.json     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/freeze-region-bounce-0001.json  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/freeze-track-0001.json          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/groove-basics-0001.json         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/mixer-bus-churn-0001.json       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/mixer-routing-churn.json        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/modulator-basics-0001.json      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/note-edit-basics-0001.json      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/plugin-chain-basics-0001.json   fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/plugin-scan-cache-0001.json     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/project-revisions-0001.json     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/rack-basics-0001.json           fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/rendered-media-0001.json        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/render-render-0001.json         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/render-stems-0001.json          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/safestart-record-0001.json      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/scale-chord-0001.json           fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/script-run-error-0001.json      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/script-run-ok-0001.json         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/session-basics-0001.json        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/settings-telemetry-0001.json    fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/track-state-extra-0001.json     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/transport-play-churn-0001.json  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/transport-punch-gate.json       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/transport-tempo-map-0001.json   fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/undo-hammer-0001.json           fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/vca-basics-0001.json            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/warp-basics-0001.json           fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/scenarios/wasm-modules-0001.json          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/crashbot/selftest_reaper.py                        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/dawproject-a16-histogram.cpp                       fork tooling (allowed by construction: tools/ does not exist upstream)
tools/dawproject-proof.sh                                fork tooling (allowed by construction: tools/ does not exist upstream)
tools/dawproject-roundtrip-proof.cpp                     fork tooling (allowed by construction: tools/ does not exist upstream)
tools/doc5-citations.py                                  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/import-detection-proof.cpp                         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/local-ci.sh                                        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/make_stub_model.py                                 fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/call_tool.py                      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/.gitignore                        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/README.md                         docs (allowed)
tools/mcp-zene-control/server.py                         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/snapshot_commands.py              fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/data/agent-control-fixture.mmp fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/harness.py                  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/__init__.py                 fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/mcp_fixture.py              fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_declared_surface.py    fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_mcp_e2e.py             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_mcp_errors.py          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_offline_staleness.py   fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_units.py               fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/tests/test_wire_client.py         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/commands_snapshot.json fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/config.py            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/__init__.py          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/protocol.py          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/registry.py          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/server.py            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/staleness.py         fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mcp-zene-control/zene_control/tools.py             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/demo_check.py                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/demo_edits.py                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/depth-demo.sh                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/mmpz_git.py                               fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/render-recipe.sh                          fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/rev_report.py                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/run-demo.sh                               fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/s3-rev-demo.sh                            fork tooling (allowed by construction: tools/ does not exist upstream)
tools/mmpz-git/tests/test_mmpz_git.py                    fork tooling (allowed by construction: tools/ does not exist upstream)
tools/ncpu-shim.c                                        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/rack-render-fixture.py                             fork tooling (allowed by construction: tools/ does not exist upstream)
tools/rack-render-proof.py                               fork tooling (allowed by construction: tools/ does not exist upstream)
tools/render-determinism-compare.py                      fork tooling (allowed by construction: tools/ does not exist upstream)
tools/render-determinism-probe.sh                        fork tooling (allowed by construction: tools/ does not exist upstream)
tools/stem-export-demo.py                                fork tooling (allowed by construction: tools/ does not exist upstream)
tools/stem_split_cli.py                                  fork tooling (allowed by construction: tools/ does not exist upstream)
tools/wasm/wat2wasm.cpp                                  fork tooling (allowed by construction: tools/ does not exist upstream)
vcpkg.json                                               declared divergence -> wave R rename (018d2041f): the vcpkg manifest's "name" field lmms -> zene (the manifest's product identity, not a dependency).; wave R rename: vcpkg manifest name follows the CMake project rename
.yamllint                                                declared divergence -> wave R rename (018d2041f): the inline comment on the line-length rule still read 'be conforming to LMMS coding rules' -> 'Zene Studio coding rules'. Comment only; no rule, value or threshold changed.; wave R rename: yamllint ignore path follows the renamed desktop file (no runtime effect)

PASS: every change to upstream-inherited code since 01148947ea4d8bdb05c237942758d61acc867223 is declared
      (428 changed path(s) declared; the ledger holds 466 entries)
no-upstream-regression(G6) EXIT=0
=== evidence gate: what this tree commits, beyond code ===
cap      : 1048576 bytes per file (EVIDENCE_SIZE_CAP_BYTES)
exempt   : tests/evidence-gate-exempt.txt (4 entry(ies))
tree     : git ls-files


evidence-gate: 6908 file(s) scanned, 0 refused (cap 1048576 bytes, 4 exemption(s))
PASS: no committed evidence file types and nothing over the cap.
evidence(G11) EXIT=0
test source                                              verdict
tests/src/core/PhaseDPerfBench.cpp                       DECLARED not-built -> A CPU-cost benchmark (SPEC v1.2 decision D3: "<5% single-core CPU per active sidechain send"), measured with CLOCK_PROCESS_CPUTIME_ID. Not registered because a suite that runs while sibling builds compile on the same box measures the machine's noise, not the code's cost: this file's own header documents bracketed twin windows precisely because the machine is not quiet. Run it by hand: cmake --build build --target PhaseDPerfBench && build/tests/PhaseDPerfBench. Its numbers are the evidence in PART-D-SIDECHAIN.md.
tests/src/core/TwoTrackAlsaCaptureProbe.cpp              DECLARED not-built -> Not a QTest class: a standalone probe with its own main() that opens a real ALSA capture device (default hw:1,0) and measures capture-thread allocations. It cannot run on a machine with no capture hardware and is not a unit test, so it has no home in ctest. Run by hand: build/tests/TwoTrackAlsaCaptureProbe <device> <periods> <outdir>.

test sources scanned: 189 (registered: 187, declared-not-built: 2, helpers: 4)
PASS: every test source under tests/src/ is registered, or declared with a reason
unregistered EXIT=0
