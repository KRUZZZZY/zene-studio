IMPLEMENTED

Q050-169 Correct hosted-instrument PDC documentation: `docs/KNOWN-LIMITATIONS.md` no longer
denies instrument latency compensation next to the supported-host section. The page now says
in that bullet and in "Hosted instrument latency" that the native CLAP, VST3 and LV2 hosts are
the supported instrument hosts for delay compensation, that built-in instruments are 0, and
that out-of-process, VST2 (Vestige) and Carla are unsupported *for delay compensation, not for
hosting* (they play, they report nothing); the polling, 16384-frame clamp, automation and
live-monitoring limits are kept and pinned in the registered test that measures the behaviour.
Docs plus test slots; no product code, no new command, no format change.

Round 2 (rework after the round-1 REWORK verdict): added three BEHAVIOURAL cases to
`InstrumentLatencyPdcTest` (over-capacity clamp, automation exclusion, low-latency/monitoring)
measured on rendered audio against a real latent instrument, reconciled `README.md`'s denial to
match the owning `KNOWN-LIMITATIONS.md` change, and pinned each limit to the case that measures
it. See the round-2 rows below and `docs/050-features/Q050-169.md` § Rework log.

## Acceptance table (exact commands and EXIT codes seen)

| Criterion | Command (dir stated; unpiped; EXIT recorded) | Result |
|---|---|---|
| Build the proving targets (train-5 env: `unset LMMS_PLUGIN_DIR`, `QT_QPA_PLATFORM=offscreen`, ccache + `PKG_CONFIG_PATH=$HOME/.local/zene-deps/root/usr/lib/pkgconfig` + `LD_LIBRARY_PATH` exports) | `cmake --build build --target InstrumentLatencyPdcTest PdcMixerTest W3EngineTest -- -j4 > <logs>/build-1.log 2>&1; echo EXIT=$?` (worktree root) | EXIT=0 |
| Same, the CLAP host module the latency test loads (without it its four CLAP cases QSKIP "this build has no clapinstrument module or CLAP fixture") | `cmake --build build --target clapinstrument -- -j4 > <logs>/build-2-clapinstrument.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Rebuild the test with the new documentation slot, and again after its claim list settled | `cmake --build build --target InstrumentLatencyPdcTest -- -j4 > <logs>/build-3-test-with-doc-slot.log 2>&1; echo EXIT=$?` then `… > <logs>/build-5-test-final.log 2>&1; echo EXIT=$?` | EXIT=0, EXIT=0 |
| Multi-out sentence the old line also denied | `cmake --build build --target Vst3BusMapTest ControlPluginBusesTest -- -j4 > <logs>/build-4-busmap-pluginbuses.log 2>&1; echo EXIT=$?` | EXIT=0 |
| AC1 blanket denial gone; AC2 support matrix named; AC3 the four limit statements present — all asserted by the new slot | `ctest -R "InstrumentLatencyPdcTest\|PdcMixerTest\|W3EngineTest" --output-on-failure > <logs>/test-13-final-ctest.log 2>&1; echo EXIT=$?` (from `build/tests`, final binary) — 3/3 passed: InstrumentLatencyPdcTest 3.32 s, PdcMixerTest 1.19 s, W3EngineTest 1.27 s | EXIT=0 |
| Slot-level detail: 11/11 slots PASS, **0 QSKIP** (CLAP, VST3, LV2 all measured) | `./build/tests/InstrumentLatencyPdcTest > <logs>/test-10-final-slots.log 2>&1; echo EXIT=$?` (worktree root, `QT_QPA_PLATFORM=offscreen`) | EXIT=0 |
| AC1 red run — the check is not vacuous: put the old sentence back (`instrument latency compensation is PRESENT, not absent` → `no instrument latency compensation`) and the slot fails on that assertion; the page was then restored byte-for-byte (`cmp` of the two saved copies) | `./build/tests/InstrumentLatencyPdcTest > <logs>/test-11-negative-control-final.log 2>&1; echo EXIT=$?` → `FAIL! ... theLimitationsPageStatesWhichHostsReportLatency() '!flat.contains("no instrument latency compensation")' returned FALSE`, then `… > <logs>/test-12-restored-final.log 2>&1; echo EXIT=$?` | EXIT=1 (expected), restored run EXIT=0 |
| AC2 behaviour cross-check: rendered alignment for exactly the three named hosts, and poll pickup of a latency change (evidence line `latency 113 reported by the poll: yes` ×3, latent onset 257 frames late, reference delayed by 257, both onsets equal) | same `InstrumentLatencyPdcTest` run (`<logs>/test-10-final-slots.log`, cases `clapInstrumentLatencyAlignsTheOtherTracks`, `clapLatencyChangeIsPickedUp`, `vst3…`, `lv2…`) | EXIT=0 |
| AC3 clamp agreement (16384-frame delay-line capacity) | same ctest run as above — `PdcMixerTest` row: `PdcMixerTest::latencyAboveTheCapIsClampedAndDiagnosed` (`tests/src/core/PdcMixerTest.cpp:486`, assertion `:535`), `Passed 1.19 sec` in `<logs>/test-13-final-ctest.log` | EXIT=0 |
| AC3 automation agreement (compensation counts chain effects and the channel alignment, never an instrument) | same ctest run as above — `W3EngineTest` row: `W3EngineTest::automationReadsWhereTheLatentAudioIs` (`tests/src/core/W3EngineTest.cpp:171`, `upstreamFrames` 4100 = 100 alignment + 4000 device), `Passed 1.27 sec` in `<logs>/test-13-final-ctest.log` | EXIT=0 |
| Multi-out sentence proof | `ctest -R "Vst3BusMapTest\|ControlPluginBusesTest" --output-on-failure > <logs>/test-9-busmap-pluginbuses.log 2>&1; echo EXIT=$?` — 2/2 passed | EXIT=0 |
| Wording claim check independent of the C++ slot (13 claim strings + the denial-absence check, all 14 printed OK) | `{ python3 - <<'PY'; echo "EXIT=$?"; } > <logs>/check-1-doc-claims.log` over `docs/KNOWN-LIMITATIONS.md` (`re.sub(r"\s+"," ",doc)` then the 13 strings, including this lane's own `"instrument latency compensation is PRESENT, not absent"`, then the absence check) | EXIT=0 |
| AC3 polling/automation/monitoring figures read from source, not assumed | `grep -n "setInterval(100)" plugins/ClapInstrument/ClapInstrument.cpp plugins/Vst3Instrument/Vst3Instrument.cpp` → :157, :175; `grep -n "setInterval(500)" plugins/Lv2Instrument/Lv2Instrument.cpp` → :118; `MaxFrames = 1 << 14` `include/LatencyCompensation.h:57`, applied `src/core/LatencyCompensation.cpp:52`, warned only `src/core/EffectChain.cpp:104`; `src/core/AutomationLatency.cpp:184`; `src/core/ControlCommandsLowLatency.cpp:117-129` walks `chain->effects()` only; `reportLatency` callers = `plugins/ClapInstrument/ClapInstrument.cpp:235`, `plugins/Vst3Instrument/Vst3Instrument.cpp:267`, `plugins/Lv2Instrument/Lv2Instrument.cpp:154` (built-ins never call it → 0, `src/core/Instrument.cpp:83-93`) | seen, matches the page |
| complexity gate (run on the exact pre-commit tree) | `{ bash tests/complexity-gate.sh --check; echo "EXIT=$?"; } > <logs>/gate-40-complexity-precommit.log` | EXIT=1, pre-existing train-5 red; regression listing byte-identical to the pre-change `<logs>/gate-1-baseline-complexity.log` (`diff` empty); no file of this lane listed (new slot CCN 3) |
| file-length gate (run on the exact pre-commit tree) | `{ bash tests/file-length-gate.sh --check; echo "EXIT=$?"; } > <logs>/gate-41-filelength-precommit.log` | EXIT=1, pre-existing train-5 red; listing byte-identical to `<logs>/gate-2-baseline-filelength.log` (`diff` empty); `tests/src/core/InstrumentLatencyPdcTest.cpp` not listed (348 code lines / 500) |
| fork-sources gate | `{ bash tests/fork-sources-gate.sh; echo "EXIT=$?"; } > <logs>/gate-42-fork-precommit.log` | EXIT=0 |
| all-sources reproduce | `{ bash tests/all-sources-reproduce.sh; echo "EXIT=$?"; } > <logs>/gate-43-allsources-precommit.log` | EXIT=0 |
| no-upstream-regression | `{ bash tests/no-upstream-regression-gate.sh; echo "EXIT=$?"; } > <logs>/gate-44-noupstream-precommit.log` | EXIT=0 (docs and tests are allowed; no inherited source changed) |
| namespace check | `{ tests/scripted/check-namespace; echo "EXIT=$?"; } > <logs>/gate-45-namespace-precommit.log` | EXIT=0 |
| no-tautology | `{ bash tests/no-tautology-gate.sh; echo "EXIT=$?"; } > <logs>/gate-46-tautology-precommit.log` | EXIT=0 |
| canary figures | `{ python3 tests/doc-figures-gate.py; echo "EXIT=$?"; } > <logs>/gate-47-docfigures-precommit.log` | EXIT=0 (no command added, snapshot and A16 histogram untouched) |

`<logs>` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-169.r1/logs`
(`gate-1..7-*-baseline-*` are the pre-change runs of the same seven gates; the edits made
after the `gate-40..47` runs are inside these two report files only, and none of the eight
gates measures a `docs/**` file).

## Round 2 (rework) — behavioural coverage + README, exact commands and EXIT codes

The round-1 verdict was REWORK for (a) criterion 3 pinned only as text and (b) `README.md`
still denying. Both are closed; the three new cases and the README check all run in the one
registered `InstrumentLatencyPdcTest` binary.

| What was added / fixed | Command (dir; unpiped) | Result |
|---|---|---|
| Over-capacity CLAMP, behavioural: fixture reports 17000 (> MaxFrames 16384); reference delayed by only 16384, onsets 16384 apart with the clamped-away 616 between them, `totalLatencyFrames()==16384`, `pdc.report` `delay_line_clamped` true, **no** capacity warning | in `InstrumentLatencyPdcTest::instrumentLatencyAboveTheCapacityIsClampedAndDiagnosed` (`tests/src/core/InstrumentLatencyPdcTest.cpp:561-587`), fixture max raised to 32768 (`tests/data/clap-test-plugin/clap-test-instrument.c:68-73`) | PASS (`<logs>/test-21-rework-final2.log`) |
| AUTOMATION exclusion, behavioural: clip on the latent instrument's own parameter and on an effect of its track's own chain both stamped 0; the channel fader stamps 257 (the contrast) | `InstrumentLatencyPdcTest::automationCompensationDoesNotCountTheInstrumentsLatency` (`:596-629`) | PASS |
| LIVE MONITORING / low-latency, behavioural: `transport.low_latency` (1 ms, `scope=all`) stops a latent EFFECT (`dontRun` true) but the instrument is not bypassed — its reference stays delayed by 257, total unchanged; off restores the effect | `InstrumentLatencyPdcTest::lowLatencyModeDoesNotBypassALatentInstrument` (`:637-666`). First run failed: 50 ms threshold (2205 frames) sat above the 257-frame effect so it was not stopped (`<logs>/test-15-rework-latency.log`, FAIL); corrected to 1 ms (~44 frames) | PASS (`<logs>/test-21-rework-final2.log`) |
| README reconciled + pinned: denial gone, `"reports its latency"` present; every README claim verified | `PluginNativeEditorTest` + `ClapHostTest` (editor cases), `ControlPluginBusesTest` + `Vst3BusMapTest` (multi-out), `InstrumentLatencyPdcTest` (latency) — all in `<logs>/test-22-rework-full-suite.log` | EXIT=0, 7/7 |
| Full unit suite on the final binary | `ctest -R "InstrumentLatencyPdcTest\|PdcMixerTest\|W3EngineTest\|PluginNativeEditorTest\|ClapHostTest\|ControlPluginBusesTest\|Vst3BusMapTest" --output-on-failure > <logs>/test-22-rework-full-suite.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0, 7/7 |
| Latency test, all slots | `./build/tests/InstrumentLatencyPdcTest > <logs>/test-21-rework-final2.log 2>&1; echo EXIT=$?` — **14/14 PASS, 0 QSKIP** | EXIT=0 |
| Transient regression-sweep failures were module availability, not this lane | `ControlDeviceCatalogueTest`/`InstrumentLayersCommandsTest` need `patman`/`tripleoscillator`; after `cmake --build build --target patman tripleoscillator` (`<logs>/build-9-rework-modules.log`) they pass (`<logs>/test-18-rework-modules.log`) | EXIT=0, 2/2 |
| Round-2 gates (exact pre-commit tree) | `{ <gate>; echo "EXIT=$?"; } > <logs>/gate-r2-<n>-<name>.log` — file-length & complexity `exit 1` (pre-existing train-5 red; both regression listings byte-identical to baseline, no lane file listed, test file 470/500 lines, new slots CCN ≤ 4); fork-sources, all-sources, no-upstream, namespace, tautology, doc-figures all `exit 0` | as stated |

## What was built (file:line)

- `docs/KNOWN-LIMITATIONS.md:214-221` — supported-host bullet: blanket denial removed;
  "instrument latency compensation is PRESENT, not absent" with the three reporting hosts,
  built-ins 0, the three non-reporting paths, cross-references to "Hosted instrument
  latency" and "F2, instruments"; still missing narrowed to preset lists + out-of-process.
- `docs/KNOWN-LIMITATIONS.md:1018-1022` — support line inside "Hosted instrument latency":
  CLAP/VST3/LV2 = supported instrument hosts for delay compensation; built-ins 0;
  out-of-process, VST2 (Vestige), Carla = unsupported for delay compensation, not for
  hosting. Poll, clamp, automation, rack-zone and monitoring limits unchanged at
  `:1012-1030`.
- `tests/src/core/InstrumentLatencyPdcTest.cpp:413-457` — `theLimitationsPageStatesWhichHostsReportLatency`
  (reads `docs/KNOWN-LIMITATIONS.md` **and** `README.md` at run time, `QSKIP` if unreachable, a string
  list that also pins the three case names that measure the clamp/automation/monitoring limits);
  header note updated; includes extended (`AutomatableModel.h`, `AutomationClip.h`,
  `AutomationLatency.h`, `AutomationTrack.h`, `Effect.h`, `EffectChain.h`, `LatencyCompensation.h`,
  `Track.h`, `PhaseDMixerTestSupport.h`).
- `tests/src/core/InstrumentLatencyPdcTest.cpp:561-587` — `instrumentLatencyAboveTheCapacityIsClampedAndDiagnosed`
  (over-capacity clamp, behavioural; `CapacityWarningGuard` `:152-160`, `OverCapacityLatency` `:122`).
- `tests/src/core/InstrumentLatencyPdcTest.cpp:596-629` — `automationCompensationDoesNotCountTheInstrumentsLatency`
  (automation exclusion, behavioural; `automationClipOn` helper `:367`).
- `tests/src/core/InstrumentLatencyPdcTest.cpp:637-666` — `lowLatencyModeDoesNotBypassALatentInstrument`
  (live monitoring / low-latency, behavioural).
- `tests/data/clap-test-plugin/clap-test-instrument.c:68-73` — `TEST_INSTRUMENT_MAX_LATENCY`
  16384 → 32768 (dated comment), so the over-capacity clamp can be measured.
- `README.md:62-84,144` — VST3/CLAP hosting paragraphs and the "No patcher GUI" bullet reconciled to
  the owning `KNOWN-LIMITATIONS.md` change (editor opens, multi-out routable, latency reported,
  still-missing narrowed); mirrors Q050-003's accepted wording for a clean merge.
- `docs/KNOWN-LIMITATIONS.md:1014-1036` — the clamp, automation and live-monitoring bullets each
  cite the case that measures them.

## New commands and reversibility

None — no command registered, changed or removed; no GUI action; nothing to undo.

## Format changes

None — no project-format path touched; old projects load and render as before.

## Known limits / not claimed

- `README.md`'s correction mirrors Q050-003's accepted wording (`e0fafd17d`/`76d4dc73b`, in
  `050/m0-train-6`, not ancestors of this train-5 base). This lane's rewrite of the overlapping
  KNOWN-LIMITATIONS and README hunks will meet Q050-003's at merge; both sides say the same
  thing, so the resolution is mechanical.
- `InstrumentLatencyPdcTest` now proves the clamp, the automation exclusion and the low-latency
  (nothing-bypasses-the-instrument) half of the monitoring limit behaviourally. It still does
  not exercise live playing as a real-time MIDI input path — that half of the monitoring limit
  is the fixture's own 257-frame onset shift. The rack-zone limit is `RackZoneFeedRunTest`'s.
- Local TSan does not run on this box (thread-creation crash, train-5 note); no product code
  changed, so no new race surface exists.

## Files committed

- `README.md`
- `docs/KNOWN-LIMITATIONS.md`
- `tests/data/clap-test-plugin/clap-test-instrument.c`
- `tests/src/core/InstrumentLatencyPdcTest.cpp`
- `docs/050-features/Q050-169.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-169.md` (this file)
