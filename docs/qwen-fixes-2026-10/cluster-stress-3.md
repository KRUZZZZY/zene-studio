FIXED

# Cluster stress-3 - plug-ins with mono, multichannel or side-chain bus layouts were refused with no reason

Branch `bugs/qwen-stress-3`, cut from `050/m0-train-19` (`5c1c6715d9`). Logs: run folder
`~/.local/share/zene-bunny-fix/qwen/runs/stress-3.r1/logs/` (file names in brackets).

## Verdict: REAL - two defects

**1. An odd channel count was handed to AudioPortsModel as the track channel count.** All four hosted-plug-in
hosts sized the track channels as `max(2, inputs, outputs)` (VST3 effect `plugins/Vst3Effect/Vst3Effect.cpp:105` and
CLAP effect `plugins/ClapEffect/ClapEffect.cpp:144` before the fix; VST3 instrument
`plugins/Vst3Instrument/Vst3Instrument.cpp:235` and CLAP instrument `plugins/ClapInstrument/ClapInstrument.cpp:105`
use `max(inputs, outputs)` with a floor of two on the output side). `AudioPortsModel::setTrackChannelCountImpl`
(`src/core/AudioPortsModel.cpp:237`) throws `std::invalid_argument("There must be an even number of track channels")`
for an odd count. `lmms_plugin_main` caught the throw and answered nullptr, so `controlInstantiateDevice`
(`src/core/ControlDeviceHosted.cpp:201` before the fix) refused with "the engine could not instantiate 'vst3effect'". Measured with
temporary prints in the VST3 entry point on the farm plug-ins [11-repro-dbg, 15-repro-dbg, 16-repro-dbg]:

| Plug-in (VST3) | inputs / outputs (buses) | track channels asked | outcome before the fix |
| --- | --- | --- | --- |
| LSP Mixer x4 Mono | 5 / 1 (5 / 1) | 5 | `There must be an even number of track channels` |
| LSP Mixer x8 Mono | 9 / 1 | 9 | same |
| LSP Mixer x16 Mono | 17 / 1 | 17 | same |
| LSP Oscilloscope x1 | 3 / 2 (2 / 1, side-chain at 2) | 3 | same |
| LSP Crossover Mono x8 | 1 / 9 (1 / 9) | 9 | same |

The CLAP builds of the same LSP plug-ins and ZAM's ZamCompX2 / ZamGateX2 (stereo main + mono side-chain = 3
inputs) take the same path through `ClapEffect::applyLayout`. Every one of the 12 is a layout the hosts' bus mapping
(`Vst3Host::Impl::mapBuses`, `clap::mapPorts`) already handles; the refusal was only the track-channel arithmetic.
VST3 `setBusArrangements` / CLAP `audio-ports-config` negotiation is therefore not needed for these: the plug-ins'
default arrangements are hosted as they are (the main pair on track channels 0/1, extra and side-chain inputs
from the remaining track channels or the mixer's side-chain send, as before).

**2. The reason never reached plugin.load.** The hosts' reasons went to `qWarning()`/`qCritical()` only. In the
instance log a `fprintf(stderr, ...)` placed in the same catch block printed while the `qCritical()` line next to
it did not appear at all [15-repro-dbg] (why Qt dropped it - presumably its default handler logging to the
journal when stderr is not a terminal - is unverified). So the refusal named no reason, and Stochas (CLAP, a
note-only plug-in with no usable audio port) was refused as "could not instantiate 'clapeffect'" with nothing else.

## Fix

- `include/HostedTrackChannels.h` (new): `hostedTrackChannels(in, out)` = `max(2, in, out)` rounded up to whole
  L/R pairs. Used by all four hosts: `Vst3Effect.cpp:110`, `ClapEffect.cpp:149` (also the path `reprepare()` takes
  after a plug-in's layout rescan), `Vst3Instrument.cpp:236`, `ClapInstrument.cpp:106`. For every even count the
  value is the same as before; only the counts that threw change.
- `include/PluginInstantiationFailure.h` + `src/core/PluginInstantiationFailure.cpp` (new): a per-thread reason
  noted by a host and taken by the caller. `Vst3Effect` / `ClapEffect` note the load error (constructor) and a
  caught exception (`lmms_plugin_main`); `controlInstantiateDevice` (`ControlDeviceHosted.cpp:198-208`) discards a
  stale note before instantiating and appends the noted reason to the old sentence:
  `the engine could not instantiate 'clapeffect': <reason>`. No new command, no schema change.
- `plugins/ClapEffect/ClapHost.cpp:274-275`: the no-audio refusal names the channel counts and why:
  `'org.surge-synth-team.stochas' has no usable audio ports (0 input, 0 output channels; a note-only plug-in has
  none)` (same line count; the file stays at 507 lines).
  Hosting a CLAP note effect (Stochas) would need a MIDI-effect slot the chain does not have - out of scope for a
  bug fix, so it is refused with that reason, as the brief allows.

Fixtures (test side): `tests/data/clap-test-plugin/clap-test-gain.c` gains two `CLAP_TEST_GAIN_LAYOUT` modes,
`mono-sidechain` (stereo main + mono side-chain, 3 inputs) and `no-audio` (no audio port);
`tests/data/vst3-test-instrument` gains a third compile-time variant `vst3-test-three-channel-instrument`
(`SpeakerArr::k30Cine`, 3 output channels, its own class UID). Regression binary:
`tests/src/core/ControlHostedBusLayoutTest.cpp`.

## Evidence

Builds use `CCACHE_TEMPDIR=<run folder>/scratch/ccache-tmp (removed afterwards)` (the jail refuses ccache's default temp dir - see
Self-check). Test commands run from `build/tests`.

| Step | Command | Result |
| --- | --- | --- |
| Repro before the fix (worktree `zene`, farm plug-ins, read-only) | `python3 -I logs/scripts/repro.py <wt> <5 subjects>` | 5/5 refused, bare "could not instantiate" [05-repro-before] |
| GREEN | `cmake --build build --target zene ControlHostedBusLayoutTest -j4` EXIT=0 [23]; `ctest -R '^ControlHostedBusLayoutTest$' --output-on-failure` | EXIT=0, 1/1 [24]; binary 8 passed 0 skipped [25] |
| 4b revert | `git diff -- <7 product files> > logs/fix.patch && git apply -R logs/fix.patch` | EXIT=0 |
| RED | rebuild EXIT=0 [26-build-red-2, 28]; `ctest -R '^ControlHostedBusLayoutTest$' --output-on-failure` | **EXIT=8**, 6 failed / 3 passed [29-test-red-split]: mono side-chain `loaded.ok` FALSE ("could not instantiate 'clapeffect'"); no-audio message lacks the reason; `vst3effect refused a plug-in with a three-channel output`; `vst3instrument fell back to a placeholder`; farm LSP Mixer x4 Mono and ZamCompX2 refused |
| 4b re-apply | `git apply logs/fix.patch` EXIT=0; rebuild EXIT=0 [30-build-green-2]; same ctest | **EXIT=0**, 1/1 [31-test-green]; binary 9 passed, 0 failed, 0 skipped [32] |
| Final wording | After 4b the no-audio sentence was shortened to fit its two lines (`logs/fix-final.patch` is the committed product diff); rebuilt EXIT=0 [42]; `ctest -j2 -R '^(ControlHostedBusLayoutTest|ClapHostTest|ClapEffectIntegrationTest|ClapLoaderErrorTest|ClapTestInstrumentUnitTest|ClapInstrumentViewTest|ControlDeviceCatalogueTest)$'` | EXIT=0, 6/6 [43]; binary 9 passed, 0 skipped [44]. The RED above is unaffected: the reverted code has neither the counts nor "note-only" |
| Family | 22 binaries built EXIT=0 [34]; `ctest -j2 -R '^(Vst3HostTest|Vst3ChunkProbeTest|Vst3InstrumentTest|Vst3InstrumentIntegrationTest|Vst3EffectIntegrationTest|Vst3BusMapTest|ClapHostTest|ClapEffectIntegrationTest|ClapLoaderErrorTest|ClapBusMapTest|ClapTestInstrumentUnitTest|ClapInstrumentViewTest|ControlDeviceCatalogueTest|ControlDeviceVendorTest|ControlHostedDeviceCatalogueTest|ControlPluginBusesTest|UndoHonestyTest|ControlChainPresetTest|InstrumentLatencyPdcTest|InstrumentLayersCommandsTest|AudioPortsModelTest|SidechainInputsTest|ControlHostedBusLayoutTest)$'` | EXIT=8, 21/22 [35]: the one failure was `ControlDeviceCatalogueTest::aTruncatedPatchKeepsThePreviouslyLoadedSound` "Patman plugin is unavailable" (module not built in this targeted tree). After `--target patman` EXIT=0 [36]: `ctest -R '^ControlDeviceCatalogueTest$'` EXIT=0, 1/1 [37] -> 22/22 |
| Repro after the fix | `logs/scripts/repro.py` with all 13 subjects | 12 load; Stochas refused with the reason above [18-repro-after] |
| Audio after the fix | `logs/scripts/render.py`: TripleOscillator + the plug-in, one note, `render.render` | every subject renders finite audio, 0 non-finite samples; peaks L/R e.g. dry 0.074/0.074, Mixer x4 Mono 0.148/0.148 (it sums the track's L and R), Crossover Mono x8 0.086/0.060, ZamCompX2 0.074/0.074 [20-render-after] |
| Gates | `python3 logs/scripts/feature_check_logs.py <wt>` (see Self-check) | EXIT=0, 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length "no new item in your files") [41-feature-check, 45-feature-check-final] |

Files committed: `include/HostedTrackChannels.h`, `include/PluginInstantiationFailure.h`,
`src/core/PluginInstantiationFailure.cpp`, `src/core/CMakeLists.txt`, `src/core/ControlDeviceHosted.cpp`,
`plugins/Vst3Effect/Vst3Effect.cpp`, `plugins/Vst3Instrument/Vst3Instrument.cpp`,
`plugins/ClapEffect/ClapEffect.cpp`, `plugins/ClapEffect/ClapHost.cpp`, `plugins/ClapInstrument/ClapInstrument.cpp`,
`tests/src/core/ControlHostedBusLayoutTest.cpp`, `tests/CMakeLists.txt`, `tests/data/clap-test-plugin/clap-test-gain.c`,
`tests/data/vst3-test-instrument/CMakeLists.txt`, `tests/data/vst3-test-instrument/vst3-test-instrument.cpp`,
`tests/fork-sources.txt`, `tests/upstream-modifications.txt`, this report.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted with
  `git apply -R logs/fix.patch` (the 7 tracked product files; the two new headers stay so the test compiles):
  `ctest -R '^ControlHostedBusLayoutTest$'` EXIT=8, 6 of the 6 behavioural cases fail [29-test-red-split]. Re-applied
  with `git apply logs/fix.patch`: EXIT=0, 9/9 pass, none skipped [31, 32]. The arithmetic case
  `trackChannelsAreWholePairs` passes both ways by construction (it tests the new header, which the revert keeps).
- **Deterministic.** No timing, sleeps or threads: in-tree fixtures selected by an environment variable
  (`CLAP_TEST_GAIN_LAYOUT=mono-sidechain|no-audio`, set before `plugin.load`, unset after) and a compile-time VST3
  variant; one `EffectChain::processAudioBuffer` block of constant input (0.5 / -0.25) with gain 0.5 set through
  `plugin.param_set`, compared to 1e-6. The two farm cases (`thirdPartySubjectsLoad`) only run where
  `~/.cache/zene-plugin-farm` holds the package and QSKIP with the missing path otherwise; the fixture cases always run.
- **At the cause and complete.** Every `setAllChannelCounts` caller (`rg setAllChannelCounts src plugins`: the four
  hosts only) now uses the one helper, including `ClapEffect::applyLayout`'s second caller `reprepare()`. The reason
  channel is consumed at the single place the refusal is built (`controlInstantiateDevice`); its other callers (chain
  presets, structural re-instantiation) get the longer message through the same `ControlResult`. Instrument entry
  points return their object even on a failed load, so they note nothing (no consumer); their sizing is fixed. Limit:
  the CLAP instrument's odd-count path has no fixture of its own - it is the same one-line substitution, covered by
  the helper's case; the VST3 instrument's is covered by `vst3InstrumentHostsAThreeChannelOutput`.
- **No collateral damage.** `hostedTrackChannels` equals the old value for every even count, so every plug-in that
  loaded before is sized exactly as before (family 22/22). Only counts that threw change, so no saved project held a
  hosted instance with such a layout - a saved one loaded as a placeholder and now loads the real plug-in. No new
  limit: a plug-in past 256 track channels still hits AudioPortsModel's own bound, now refused with that reason. The
  refusal keeps its old sentence as a prefix. Realtime: nothing changed on the audio thread (constructors,
  `applyLayout` under `requestChangeInModel`, the control handler). No new command; snapshot canary untouched.
- **Verdict is not ALREADY-FIXED / FALSE-POSITIVE** - reproduced on this branch's tip [05-repro-before].
- **Environment notes.** ccache's default temp dir is outside the write-jail ("Permission denied" [03]), so builds
  set `CCACHE_TEMPDIR` to the run folder. `feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`,
  also outside the jail (PermissionError [38]); the gates were run from an exact copy whose only change is that
  log directory (and `ROOT` pinned to the original folder) - `diff` shows the two lines [40]. `submit_check.py`
  calls the original script, so its "gates" row cannot pass inside this jail; the loop's own run of it can.
