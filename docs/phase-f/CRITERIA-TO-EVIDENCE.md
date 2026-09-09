# Phase F — SPEC §8.2 criteria mapped to evidence (board task #592)

Clone: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-partd`,
branch `part-d-sidechain`. Build: `-DCMAKE_BUILD_TYPE=Debug -DWANT_QT6=ON`
(Qt 6.4.2, Ubuntu 24.04). Every command below was executed in this clone; `ctest` runs from
`build/tests` with `QT_QPA_PLATFORM=offscreen` (AGENTS.md rule 7). Raw logs: `/tmp/phasef-*.log`.

## 0. Verdict summary

| # | SPEC §8.2 criterion | Verdict |
|---|---|---|
| 1 | Dynamic channel allocation (not fixed 64) | **PROVEN** |
| 2 | Multi-channel plugin I/O (VST2) | **NOT PROVEN** — no VST2 plugin asset; SPEC test is Manual (smoke) |
| 3 | Native sidechain sends | **PROVEN** |
| 4 | Parallel effect buses | **PROVEN** |
| 5 | Backward-compatible project files | **PROVEN** (old → new); forward direction source-level only |
| 6 | PR mergable (not "too large to review") | **NOT PROVEN** — reviewer sign-off impossible (owner-gated, no push) |
| §8.3 | Performance regression gate (per-send, Decision D3) | **PROVEN** |

## 1. Dynamic channel allocation (not fixed 64) — PROVEN

SPEC test: *"Create 100+ mixer channels via API → verify no 64-artifact limit"*.

Command:

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ./PhaseFChannelScaleTest
```

Measured output (verbatim, `tests/src/core/PhaseFChannelScaleTest.cpp`):

```
PARTF_SCALE user_channels=128 num_channels=129 distinct_indices=128 distinct_names=128 master_receives=128 routes=128
PASS   : PhaseFChannelScaleTest::createsMoreThanOneHundredChannels()
PARTF_BOUNDARY feeds=5 expected=0.310000 measured=0.310000 channel_64_alive=1 channel_127_alive=1
PASS   : PhaseFChannelScaleTest::noFixedSixtyFourArtifact()
PARTF_HIGH_TO_MASTER source_channel=127 input=0.500000 master=0.500000
PASS   : PhaseFChannelScaleTest::channelAboveOneHundredCarriesAudioToMaster()
PARTF_HIGH_SEND from=128 to=126 input=0.500000 master=1.000000
PASS   : PhaseFChannelScaleTest::channelToChannelSendAboveOneHundred()
PARTF_HIGH_BUS bus=129 direct=0.350000 bus_gain=0.500000 bus_out=0.175000 master=0.525000
PASS   : PhaseFChannelScaleTest::parallelBusAboveOneHundred()
PARTF_HIGH_SIDECHAIN from=100 to=127 tap=prefader key=0.750000 master_period1=0.850000 master_period2=1.000000
PASS   : PhaseFChannelScaleTest::sidechainSendAboveOneHundred()
PARTF_GROWTH user_channels=200 num_channels=201 channel200_input=0.250000 master=0.250000
PASS   : PhaseFChannelScaleTest::growsBeyondOneHundredTwentyEight()
PARTF_STABILITY periods=10 expected=0.500000 ok=1
PASS   : PhaseFChannelScaleTest::repeatedPeriodsAreStable()
Totals: 10 passed, 0 failed, 0 skipped, 0 blacklisted, 1655ms
```

What each number proves:

- **128 user channels created through the real API** (`Mixer::createChannel()`), every channel
  with a distinct index and distinct name, the master receiving all 128 default sends, and 128
  entries in `Mixer::m_mixerRoutes` — no truncation at 64 or any other count.
- **Boundary test:** 5 channels (1, 32, 63, 64, 127) each fed 0.1; master measures exactly
  0.31 and both channel 64 and channel 127 carry audio (`alive=1`) — no fixed-count artifact at
  or above the 64 boundary.
- **Channel 127 → master carries audio:** input 0.5 → master 0.5.
- **Channel-to-channel send above 100:** 128 → 126, input 0.5 → master 1.0.
- **Bus above 100:** bus 129 sums 0.35, applies its 0.5 FX gain (0.175), master 0.525.
- **Sidechain above 100:** 100 → 127 pre-fader key = 0.75 exactly, key is inaudible
  (master 0.85 = 0.75 sender + 0.10 receiver; a leak would read 1.60).
- **Growth past 128:** 200 user channels (201 total), channel 200 input 0.25 → master 0.25.
- **Stability:** 10 consecutive periods on channel 127 all render exactly 0.5 — no drift,
  no per-period accumulation, no crash.

Source-level corroboration (no fixed array, no cap):

```bash
grep -n "m_mixerChannels" include/Mixer.h          # 409: std::vector<MixerChannel*> m_mixerChannels
sed -n '533,540p' src/core/Mixer.cpp               # createChannel(): m_mixerChannels.push_back(...)
grep -n "MAX_CHANNEL\|MaxChannel" include/Mixer.h  # no matches
```

## 2. Multi-channel plugin I/O (VST2) — NOT PROVEN

SPEC §8.1 classifies the test as **Manual (smoke)**: *"Load Kontakt/EZDrummer → verify 8+ output
channels appear in PinConnector → route each output to separate mixer channel → verify distinct
audio per channel."*

Blocking evidence:

```bash
find . -iname "*kontakt*" -o -iname "*ezdrummer*" -o -iname "*.dll"   # (no output)
ls src/gui/PinConnectorView.cpp                                      # No such file or directory
grep -rn "multiOut\|MultiOut\|numOutputs" include/Mixer.h src/core/Mixer.cpp  # (no output)
```

- No VST2 plugin binary with multiple outputs exists on this machine; the SPEC test names two
  proprietary instruments (Kontakt, EZDrummer) that are not present.
- The host side *is* built (`build/plugins/libvestige.so`,
  `build/plugins/NativeLinuxRemoteVstPlugin64`), but there is no plugin to host.
- The PinConnector **view** (`src/gui/PinConnectorView.cpp`) is not part of the stacked branch
  set; only the core (`include/PinConnector.h`, `src/gui/PinConnector.cpp`) is present.
- No automated multi-out test exists in the clone (`grep -rln "multi-out\|VST2" tests/` → empty).

This criterion is therefore **unproven in this clone** and cannot be proven without a multi-out
VST2 instrument asset and the Phase C3 GUI half. It must not be claimed as verified.

## 3. Native sidechain sends — PROVEN

Commands and measured results:

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ./PhaseDSidechainTest
# Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 1488ms
#   tapPointsDeliverDistinctSidechainSignals  (pre-FX / pre-fader / post-fader / post-fader-no-gain)
#   duckingFollowsTheSidechainKey             (real compressor, 48 kHz, gain reduction follows the key)
#   parallelBusSumsInputsThroughItsFxChain
#   demoProjectBusWithNativeSidechainCompressor
#   cycleFormingSidechainSendIsRejected
QT_QPA_PLATFORM=offscreen ./PhaseFChannelScaleTest
# PARTF_HIGH_SIDECHAIN from=100 to=127 tap=prefader key=0.750000 master_period1=0.850000 master_period2=1.000000
```

The Phase F slot proves sidechain sends work between channels numbered above 100 and that the
key signal is delivered to the receiver's FX chain without leaking into the audible mix.

## 4. Parallel effect buses — PROVEN

```bash
QT_QPA_PLATFORM=offscreen ./PhaseDSidechainTest   # parallelBusSumsInputsThroughItsFxChain PASS (7/7)
QT_QPA_PLATFORM=offscreen ./PhaseFChannelScaleTest
# PARTF_HIGH_BUS bus=129 direct=0.350000 bus_gain=0.500000 bus_out=0.175000 master=0.525000
```

A bus at index 129 sums its sources pre-fader (0.35), applies its own FX gain (×0.5 → 0.175),
and reaches the master (0.175 + 0.35 direct = 0.525) — buses are not capped at 64 either.

## 5. Backward-compatible project files — PROVEN (old → new)

```bash
QT_QPA_PLATFORM=offscreen ./MixerRoutingBackwardCompatTest  # Totals: 5 passed, 0 failed
QT_QPA_PLATFORM=offscreen ./ProjectVersionTest              # Totals: 3 passed, 0 failed
QT_QPA_PLATFORM=offscreen ./MixerAbRegressionTest           # Totals: 3 passed, 0 failed
```

- `legacyProjectLoadsWithIdenticalRouting` — a legacy fixture loads with an exact routing table,
  renders identically to a programmatically built graph, and re-saves **without** v2 elements.
- `phaseDProjectRoundTripsThroughSaveLoad` — a v2 graph (bus + pre-fader + sidechain) round-trips.
- `unknownLegacyEffectIsPreservedAsDummy` — unknown effect types survive as dummies.
- `MixerAbRegressionTest::renderIsByteIdenticalToReference` — the mixer render path is
  byte-identical to the pre-change reference.
- `ProjectVersionTest` — the project-version comparison used by the upgrade chain still passes.

The reverse direction (new file → old LMMS build) has no automated test; it is documented as a
source-level reading in `docs/phase-f/RELEASE-NOTES-mixer-v2.md` §4.

## 6. PR mergable (not "too large to review") — NOT PROVEN

Evidence that exists locally: `docs/phase-f/PER-BRANCH-REVIEWABILITY.md` (per-branch and
per-commit diffstats + INTEGRATION.md submission order). Measured finding: **no branch in the
stacked set is under the 800-insertion review budget** (`MASTER-PLAN.md` §4.1 line 192); the
reviewable units are individual commits and the documented peel plans.

The criterion itself is *"Confirmed by reviewers (sakertooth, JohannesLorenz)"*. No PR may be
opened by this program (AGENTS.md rule 1: owner-gated, never push), so no reviewer has seen the
branches and the criterion **cannot be marked proven** from inside this clone. The diffstat
finding (over-budget branches) is itself part of the honest answer and must go to the owner.

## 7. SPEC §8.3 performance gate (per-send, Decision D3) — PROVEN

```bash
QT_QPA_PLATFORM=offscreen ./PhaseDPerfBench
```

```
D3_BENCH sample_rate=48000 frames_per_period=256 periods_per_window=2500 windows_per_rep=4 reps=5 channels=34 sends=32 base_twin_spread_us=3.96
D3_PER_SEND regular_ns=22060.8 sidechain_ns=10810.3
D3_GATE per_send_pct_of_core=0.2027% threshold=5% verdict=PASS
D3_GATE sidechain_pct_of_regular_send=49.0% (context)
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 30002ms
```

Per the owner's Decision D3 the gate is *"<5% single-core CPU per ACTIVE sidechain send"*:
measured **0.2027%** — PASS. (The bench is built but is not registered as a ctest test; it is run
directly as above. This is a §8.3 note, not a §8.2 criterion.)

## 8. Does the AudioBus silence bug affect criterion 1?

**No, as measured — but it was the same class of defect, and it is now fixed.**

- The bug: `AudioBus::silenceAllChannels()` stepped `tc += 2` over `m_channelPairs`, which is a
  count of *pairs*, so only every second pair was zeroed on a bus with more than one pair.
- Blast radius: every mixer-channel bus is constructed with exactly **one** pair —
  `src/core/Mixer.cpp:118`: `m_bus( &m_buffer, 1, Engine::audioEngine()->framesPerPeriod() )`.
  With one pair the old and new loops are equivalent, so the 100+/200-channel measurements in §1
  are unaffected by the bug.
- It is nonetheless a fixed-count artifact of the kind §8.2 asks to rule out, in the bus-silence
  path (it would bite multi-pair buses, e.g. future multi-out plugin I/O). Fixed in its own commit
  `9327c350f` with regression `AudioBusTest::SilenceAllChannels`
  (before: `FAIL! pair 1 frame 0 left channel not silenced: 2`, exit 1;
  after: `Totals: 3 passed, 0 failed`, exit 0).
- The §8.2 criterion itself concerns *channel allocation*, which is `std::vector`-backed and
  uncapped (`include/Mixer.h:409`, `src/core/Mixer.cpp:533`), with no `MAX_CHANNEL` constant.

## 9. Full-suite confirmation

```bash
cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
# 100% tests passed, 0 tests failed out of 16
# Total Test time (real) =  16.92 sec
```

16/16 including `AudioBusTest`, `MixerAbRegressionTest`, `MixerRoutingBackwardCompatTest`,
`PhaseDSidechainTest`, `PhaseFChannelScaleTest`, `ProjectVersionTest`.
