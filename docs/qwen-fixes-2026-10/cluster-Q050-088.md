IMPLEMENTED

Q050-088: IMPLEMENTED
Q050-108: IMPLEMENTED
Q050-262: IMPLEMENTED (already in the tree before this branch - no product change; existing tests re-run here)
Q050-339: IMPLEMENTED
Q050-340: IMPLEMENTED

Branch `feat/050-Q050-088-oc`, cut from `2da2ed356`. Round 1 (the round-0 runs never reached the model: rate limit).
Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-088.r1/logs` (build-N.log, test-N.log, gate-*.log).
Commits: e9439c942 (Q050-088), 79f8df64c (Q050-108), 2200698b2 (Q050-339), bad12210d (Q050-340 + the batch's shared
GUI test, socket transcript and manual text), 520c6ee29 (Q050-262 record), 843c14a1e (snapshot, canary, A16 histogram).
Feature records: `docs/050-features/Q050-088.md`, `Q050-108.md`, `Q050-262.md`, `Q050-339.md`, `Q050-340.md`.

Round-0 review points addressed: the Console-section view option, strip controls and per-channel agent command
(`mixer.console_strip`) are built; the glue compressor is an optional, default-off, per-instance Console stage with
save/load and undo; `DeviceDspTest` now `add_dependencies(... console)`; trim/polarity/EQ DSP and serialization were
kept as they were (the section writes the same models).

## Integration onto `050/m0-train-17` (round 5, 2026-10-27)

Accepted source `960bba5a3` applied by the driver onto the train tip `639e1d23e` (three-way). Logs:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-088.r5/logs`.

Conflicts and how each was resolved (no design change; the accepted design and its tests are unchanged):

| File | Resolution |
|---|---|
| `src/core/InputMeter.cpp` | Both kept: the train's peak-watch atomics (Q050-100) and its `LMMS_TESTING` observation hook beside the batch's per-reader per-channel atomics (Q050-340). All raises still go through the train's compare/exchange `raise`. |
| `data/manual/index.html` | The train's longer mixer paragraph (keyboard navigation) kept, followed by the batch's three paragraphs. |
| `README.md` | Canary taken from the live re-snapshot: 867 ids / 76 groups (train 863 + the batch's 4). |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | Train text kept; the batch's A16 paragraph re-stated on the train and the histogram block set to the measured `rows=876 true_inverse=432 snapshot=42 irreversible=25 not_mutating=377` (train 872/430/42/25/375 + 2 true_inverse + 2 not_mutating). |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | Union (train entries, then the batch's). |
| `tests/all-sources.txt` | Re-derived from the recipe in its header (3,291 entries = train 3,273 + the batch's 18 new sources). |
| `commands_snapshot.json` | Regenerated from a live instance with `snapshot_live.py` (867 commands, wasm.* out). |

One demonstrated regression from the merge, fixed: `src/gui/shell/ShellCommandMenus.cpp` reached 503 code lines
(train additions such as Q050-087's Chain Canvas + the batch's 4 lines), failing the file-length gate. The channel's
Strip submenu body moved unchanged (same entries, order, labels and commands) into `menuparts::stripEntries` in
`src/gui/shell/ShellMenuEntries.cpp` (declared in `include/ShellMenuParts.h`). MixerStripFeaturesTest, which clicks the
Strip menu's Meter Mode and Channel Delay entries, and DevicePanelTest pass on it (test-4.log).

Evidence on the integrated tree (every command unpiped, exit codes as seen):

| Step | Command / log | Exit |
|---|---|---|
| Build zene, console, frequencyshifter and 23 test targets | `cmake --build build --target ... -- -j4` (build-2.log; build-1.log stopped at "No rule to make target ConsoleStripTest" only because the in-build reconfigure created the targets; zene itself built) | 0 |
| The batch's own tests | `ctest -R '^(ConsoleStripTest\|StripMeterModeTest\|TrackOffsetTest\|InputChannelMeterTest\|MixerStripFeaturesTest\|ControlMixingStripFeatures)$'` (test-1.log, 6/6) | 0 |
| Neighbours | InputMeterTest, DeviceDspTest, MixerStripTest, MixerToolsTest, MixerModesTest, MixerMeteringTest, MixerChannelPanTest, PdcMixerTest, AudioBusHandleTest, CapturePublisherTest, SurroundTest, MixingRoutingTest, MixerWindowsTest, ReversibilityContractTest, LufsMeterTest, MixerRoutingBackwardCompatTest, ControlMixingRouting (test-2.log, 17/17) | 0 |
| Snapshot and A16 | `snapshot_live.py` (snapshot-1.log); `ctest -R '^(ControlCommandsSnapshot\|ReversibilityContractTest)$'` (test-3.log); `theTableHistogramIsTheDocumentedOne` PASS | 0 / 0 |
| After the menu move | rebuild (build-3.log 0); `ctest` MixerStripFeaturesTest, DevicePanelTest, MixerWindowsTest, MixerModesTest, MixerMeteringTest, ControlMixingStripFeatures, ControlCommandsSnapshot (test-4.log, 7/7) | 0 |
| Gates | feature_check (a copy writing its logs under the run folder, since the original writes outside the write area: PermissionError, gate-feature-check-1.log): all 8 PASS (gate-feature-check-4.log) | 0 |

Semantic differences from the accepted patch: none in product behaviour. The only code change beyond conflict
resolution is the Strip-submenu move above (no entry, order or command changed). The train's InputMeter now also
feeds the peak watcher; the batch's channel meters are separate atomics and do not interact with it.

Failure paths and platform hazards re-checked for the merged tree: input metering (`AudioEngine::renderNextPeriod`
still calls `inputmeter::measure` then the batch's channel measure - fixed atomics, no allocation/lock); the PDC
graph (`Mixer::resolveLatency` / `updateLatencyCompensation` hunks applied cleanly onto the train; PdcMixerTest,
AudioBusHandleTest pass); project load (MixerRoutingBackwardCompatTest passes); command registry (four new ids unique,
snapshot test passes). Portability: no new API was introduced by the integration; only this Linux/GCC/Qt 6 box built
it, hosted CI was not run.

## Acceptance (commands run from `build/tests`, `ctest -R ... --output-on-failure`)

| Unit | # | Criterion | Test | Exit |
|---|---|---|---|---|
| Q050-088 | 1 | window.mixer_view toggles the console strip section; the saved view restores it | MixerStripFeaturesTest::theConsoleSectionFollowsTheViewMenu; ControlMixingStripFeatures | 0 (test-7.log) |
| Q050-088 | 2 | Strip controls set/read back trim, polarity, EQ, drive, glue threshold/ratio/makeup via UI and agent | ConsoleStripTest::theSectionSetsAndReadsOneChannel; MixerStripFeaturesTest::theConsoleSectionEditsItsChannel; ControlMixingStripFeatures | 0 (test-7.log) |
| Q050-088 | 3 | Deterministic renders per signal property; save/load and undo restore | ConsoleStripTest (render, undo, save/load, refusal, older-state, allocation slots) | 0 (test-7.log) |
| Q050-108 | 1 | Per-strip mode set/read back by agent and strip UI incl. VU and LUFS/TP | StripMeterModeTest::eachStripKeepsItsOwnMode, theModeIsSavedWithTheChannel; MixerStripFeaturesTest::theStripMenuSetsThatStripsMeterMode; ControlMixingStripFeatures | 0 (test-7.log) |
| Q050-108 | 2 | 300 ms VU vs reference; calibrated per-channel LUFS/true peak | StripMeterModeTest::theVuIntegratesOver300msToTheReference, eachStripReadsItsOwnLoudness, theStripMetersAllocateNothing | 0 (test-7.log) |
| Q050-108 | 3 | Docs separate per-strip modes from the master-only shell readout | docs/KNOWN-LIMITATIONS.md, docs/METER-SURFACE.md §6, manual; doc-figures gate | 0 (gate-doc-figures.log) |
| Q050-262 | 1 | Surround send uses the destination's panner; LFE/divergence on the agent surface | SurroundTest::theSurroundPannerExplainsWhichSourcesItAffects; MixerWindowsTest::theSurroundPannerPlacesTheChannelByMouseKeysAndSliders | 0 (test-8.log) |
| Q050-262 | 2 | Multichannel render: front/rear and LFE placement; stereo send balance unchanged | SurroundTest::aPannedChannelLandsOnItsSpeakers, theDownmixFollowsBs775; MixingRoutingTest::aSendHasItsOwnPan | 0 (test-8.log) |
| Q050-339 | 1 | Agent sets/reads offsets on audio tracks, instrument tracks, channels | TrackOffsetTest::theAgentSetsAndReadsEachTarget, aRefusedOffsetChangesNothing; MixerStripFeaturesTest::theDelayEntriesShowAndSetTheOffset; ControlMixingStripFeatures | 0 (test-7.log) |
| Q050-339 | 2 | Impulse render verifies the sample offset on each target | TrackOffsetTest::aTrackImpulseMovesByItsOffset (sample/instrument x +37/+301/-45), aChannelImpulseMovesByItsOffset (incl. on top of a 100-sample latent device) | 0 (test-7.log) |
| Q050-339 | 3 | Save/load keeps values; undo restores | TrackOffsetTest::aSavedProjectKeepsTheOffsets, oneUndoRestoresThePreviousOffset | 0 (test-7.log) |
| Q050-340 | 1 | Two inputs at different levels show independent UI meters | MixerStripFeaturesTest::theInputMetersWindowShowsEachInput; InputChannelMeterTest::everyCaptureChannelReadsItsOwnLevel | 0 (test-7.log) |
| Q050-340 | 2 | Armed-track meters follow their input; silence without signal | InputChannelMeterTest::anArmedTrackFollowsItsSelectedInput; the GUI test's armed meter and its fall to 0 | 0 (test-7.log) |
| Q050-340 | 3 | Agent-readable, audio-thread safe | meter.inputs in InputChannelMeterTest / ControlMixingStripFeatures; theTwoReadersAreIndependent; measuringAllocatesNothing | 0 (test-7.log) |

Direct binary runs (no skips): ConsoleStripTest 10 passed / 0 skipped, StripMeterModeTest 7/0, TrackOffsetTest 13/0,
InputChannelMeterTest 6/0, MixerStripFeaturesTest 7/0 (the console module was loaded, nothing skipped).

Neighbour regression run on the final tree (test-8.log, exit 0, 19/19): DeviceDspTest, MixerStripTest, MixerToolsTest,
MixerModesTest, MixerMeteringTest, MixerChannelPanTest, PdcMixerTest, AudioBusHandleTest, InputMeterTest,
CapturePublisherTest, SurroundTest, MixingRoutingTest, MixerWindowsTest, ReversibilityContractTest, UndoBoundsTest,
LufsMeterTest, MixerRoutingBackwardCompatTest, ControlMixingRouting, ControlCommandsSnapshot.
DeviceDspTest also needs the `frequencyshifter` module (a pre-existing dependency its CMake entry does not declare; I
built it rather than change that unrelated test).

## Round 2 (review: merge the train; pre-submit: file-length)

- **Merge of `050/m0-train-15` NOT done - refused by the lane's git guard.** `git merge --no-edit 050/m0-train-15`
  exited 97: "bunny-guard: 'git merge ...' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12)"
  (`~/.local/share/zene-bunny-fix/bunny/shims/git:7` denies merge/pull/cherry-pick/rebase). I did not bypass the
  shim. The listed conflicts (README.md, data/manual/index.html, docs/RELEASE-NOTES-v0.3.0-alpha.md,
  include/InputMeter.h, tests/all-sources.txt, tests/fork-sources.txt, tests/upstream-modifications.txt,
  commands_snapshot.json) are still unresolved and need a merge by whoever is allowed to merge into this branch. The
  registries and snapshot conflicts are derived files (re-derive / re-snapshot after the merge); the train also adds a
  "Chain Canvas..." entry to ShellCommandMenus.cpp (Q050-087) that this branch lacks, which merges cleanly next to the
  change below.
- **file-length FAIL fixed.** The Q050-339 delay entries moved out of `src/gui/shell/ShellCommandMenus.cpp` into
  `menuparts::trackDelayEntries` / `menuparts::channelDelayEntries` in `src/gui/shell/ShellMenuEntries.cpp`
  (declared in `include/ShellMenuParts.h`), with the same labels and the same `pdc.set_offset` command. The
  `tests/file-length-gate.sh --check` output no longer lists any shell file. Built `zene` + `MixerStripFeaturesTest`
  (r2 logs/build-2.log, exit 0); `ctest -R ^MixerStripFeaturesTest$` exit 0 (r2 logs/test-1.log). That test clicks
  the Track Delay / Channel Delay entries, so it covers the moved code.
- Gates re-run by hand (feature_check.py and submit_check.py's gate row still fail here with PermissionError writing
  `../zene-feat-q050-088-feature-check/`, outside my write area): fork-sources 0, all-sources-reproduce 0,
  no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0; complexity --check 1 and
  file-length --check 1 as on the train, with no ShellCommandMenus/ShellMenuEntries/ShellMenuParts item listed.
- ccache needed `CCACHE_TEMPDIR=/tmp/ccache-q088`: its default temp dir is outside the write area.

## Self-check

Round 5 (integration):
- **Fails without / passes with, on train 17.** `tests/control-mixing-strip-features.py` run against the train-17
  binary `zene-cachyos/build/zene` (HEAD `639e1d23e`, built 2026-10-26; used read-only): exit 1 (test-5-base.log:
  "no command 'mixer.console_strip'", "no command 'pdc.set_offset'", "no command 'meter.inputs'", "no command
  'window.input_meters'"). The same script against this branch's `build/zene`: exit 0 (test-6-with.log, "every check
  held"). The C++ tests were not re-built against reverted product code on train 17; their round-1 proof (they do not
  compile without ConsoleStrip.h / StripMeters.h / AudioBusHandle::offsetFrames / InputReadings.h /
  ConsoleStripSection.h, none of which exist on the train) is carried forward below.
- **Criteria proven by tests I ran:** the acceptance table's tests all re-ran on the integrated tree: test-1.log (6/6),
  test-2.log (17/17), test-3.log (2/2), test-4.log (7/7), all exit 0.
- **Undo / reversibility, realtime, old projects, portability:** unchanged from the accepted patch (answers below);
  the integration added no command, audio-path code or format. ReversibilityContractTest (exit 0) confirms the four
  rows on the train; PdcMixerTest, AudioBusHandleTest, InputMeterTest, MixerRoutingBackwardCompatTest exit 0.
- **Gates:** `submit_check.py` reports its gates row FAIL because `feature_check.py` raises PermissionError creating
  `../zene-integrate-Q050-088-639e1d23e4-feature-check` (outside the write area; gate-feature-check-1.log). The same
  script, copied to /tmp with only its log directory pointed into the run folder, reports all 8 gates PASS
  (gate-feature-check-4.log, exit 0).

Earlier rounds:

- **Each new test fails without the change, passes with it.** Every product file under include/, src/, plugins/ was
  restored to `2da2ed356` (new product files removed), tests kept: `zene` + `console` built (build-13.log, exit 0);
  ConsoleStripTest, StripMeterModeTest, TrackOffsetTest, InputChannelMeterTest, MixerStripFeaturesTest each failed to
  build (build-14-*.log, exit 2: missing ConsoleStrip.h / StripMeters.h / AudioBusHandle::offsetFrames /
  InputReadings.h / ConsoleStripSection.h); ControlMixingStripFeatures against the base binary failed at runtime
  (test-6.log, ctest exit 8: "no command 'mixer.console_strip'" etc.). Restored with `git checkout HEAD -- include src
  plugins`, rebuilt (build-15.log, exit 0), all six passed (test-7.log, exit 0). The fail-without proof is mostly a
  compile failure, not an assertion failure; the transcript is the runtime one.
- **Every criterion proven by a test I ran:** see the table (test-7.log / test-8.log exit 0).
- **One undo step per edit; honest classes:** `mixer.console_strip` true_inverse (coalesces on `channel`: a knob drag is
  one step - MixerStripFeaturesTest checks depth+1), `pdc.set_offset` true_inverse, `mixer.set_strip` stays
  true_inverse with `meter_mode`, `meter.inputs` and `window.input_meters` not_mutating, `window.mixer_view` stays
  not_mutating. Refusals record no step (ConsoleStripTest::aRefusedCallChangesNothing, TrackOffsetTest). A value the
  models round back to their old value records nothing. ReversibilityContractTest exit 0 with the histogram re-taken.
- **Realtime.** Audio-path additions: `ConsoleEffect::processImpl`/`glue` (arithmetic on fixed members, two exp()
  per block), `stripmeters::measure/measureSilence` (fixed filter state; `StripLoudness::feed` copies into a fixed
  256-frame scratch and runs LufsMeter, allocation-free; its one-off `reset()` zeroes ~150 KB of fixed arrays on the
  worker when a strip enters lufs), `inputmeter::measureChannels/measureStereoChannels` (relaxed atomic maxima,
  2x128 fixed), the PDC offset arithmetic in `AudioBusHandle::doProcessing` and `Mixer::resolveLatency` (atomic loads).
  The loudness meter is allocated on the control thread (`stripmeters::setMode`) and published release/acquire; it is
  never freed while the channel lives. Allocation probes: ConsoleStripTest::theGlueAllocatesNothingWhileProcessing,
  StripMeterModeTest::theStripMetersAllocateNothing, InputChannelMeterTest::measuringAllocatesNothing - all 0. No
  locks, I/O or signals added on those paths. TSan does not run on this box: the races were reasoned through (worker
  writes / interface reads are relaxed atomics; m_vuState/m_vuRunning are worker-only; a muted channel is measured by
  masterMix and an unmuted one by its worker, never both).
- **Old projects.** Every new stage is skipped at its default and every new attribute is written only off its default
  (`metermode`, `offset`, `offsetframes`, the Console's `glue*`); absent ones load as defaults
  (ConsoleStripTest::anOlderConsoleLoadsWithTheGlueOff is bit-identical; TrackOffsetTest counts the written attributes;
  PdcMixerTest and MixerRoutingBackwardCompatTest pass unchanged). New data round-trips (save/open tests per unit).
- **Portability.** No Qt6-only API (the QCheckBox guard already existed); `indexOf` results cast to int (Qt 6
  qsizetype); the polarity sign is a QChar code point, not a source-charset literal; no banned identifiers
  (`interface` renamed in a test), no std::filesystem, no POSIX calls. Built and tested only on this box (Qt 6, GCC).
- **Gates** (feature_check.py could not run here: it writes ../zene-feat-q050-088-feature-check, outside my write area,
  PermissionError; I ran each gate directly): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0,
  check-namespace 0, no-tautology 0, doc-figures 0; complexity and file-length --check exit 1 as on the train, with no
  item new compared with the base listing (`gate-baselines/2da2ed356….json`) in a file this branch changed (four new
  over-target functions were refactored below the target).
- **Round 2:** the round-1 answers above still hold; round 2 only moved two menu helpers to another file (no
  behaviour, command, audio-path or format change) and re-ran the GUI test that drives them (exit 0). The train merge
  the review asked for was refused by the git guard (see Round 2).
- **Not done / limits:** intermediate commits were split by script and not each built (only the final tree was built
  and tested); hosted CI not run; the strip loudness meter keeps the engine rate it was built at; a track offset does
  not move a hosted instrument's extra routed output buses; Q050-262 has no new code or test (already implemented).
