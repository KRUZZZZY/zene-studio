<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, PART-D-SIDECHAIN.md
    sha256   : 8d8c7e75d97b962a6b4b399a7fefa5a1f6aaa3d17e2e657560291ec2e727421c
    bytes    : 21273
    why this file: the Phase D sidechain design; cited by the mixer A/B and sidechain tests as the document their evidence is pasted into
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# Part D — Sidechain sends & parallel buses (task #587)

**Status: six failing tests fixed, 12/12 green; D1 A-B gate re-confirmed; D3 per-send CPU gate measured PASS.**

| | |
|---|---|
| Worktree | `lmms-partd/` (branch `part-d-sidechain`, stacked on `part-c-plugin-migration`) |
| Base of this work | `61e4898a1` |
| Head | `7edfbbe45b214c1c1315685a1ed01698c1cd66be` |
| Spec | `mixer/SPEC-dynamic-routing.md` v1.2 §5.4 (sidechain strategy (a)), §5.5 (D1), §6 Phase D, §8.1 (tests), D3 decision |
| Mission | `lmms-mixer-routing-mission` |
| Push | **none** (local commits only, by instruction) |

---

## 1. Result at a glance

| Check | Before | After |
|---|---|---|
| `PhaseDSidechainTest` | **3 passed, 4 failed** | **7 passed, 0 failed** |
| `MixerRoutingBackwardCompatTest` | **3 passed, 2 failed** | **5 passed, 0 failed** |
| failing tests total | **6** | **0** |
| `MixerAbRegressionTest` (D1 A-B) | pass | **pass, hash `19846573…` matches reference** |
| full build (`make -C build -j4`) | — | **exit 0, 0 errors** |
| `ctest` (build/tests) | — | **100% tests passed, 0 failed out of 14** |
| D3 per-send CPU gate | — | **0.042% of one core per active sidechain send (threshold 5%)** |

Commit ledger (branch `part-d-sidechain`, oldest first):

```
fcde49d33f7b49e492ed162f4f4b2c9da3ca13a9  EffectChain: set Effect::m_parent when an effect is appended (Phase D keying)
df953f5133b7e9db48d24fba01a4dd092e1fd168  PhaseDMixerTestSupport: settle automation ramps in PeriodHarness
a212d249dcc3e379383be953b1cfbe882c39cfe6  tests: export lmmsobjs symbols from PhaseDSidechainTest
8dc0f6bc657adfc1dffba9a3caac8d7908062416  PhaseDSidechainTest: expect the Compressor's documented 0.999 output gain
a543ea33b71bde83fecf9f93830641ce421075dd  MixerRoutingBackwardCompatTest: emit routing rows unindented
4731e9ab862ba37afc762973d7a9acede3adc8c8  Phase D: defer cycle-closing sidechain sends instead of dropping them
7edfbbe45b214c1c1315685a1ed01698c1cd66be  tests: add the D3 per-send CPU bench (task #587)
```

---

## 2. The six failures, verbatim (before state)

`./build/tests/PhaseDSidechainTest` → `Totals: 3 passed, 4 failed, 0 skipped, 0 blacklisted, 1207ms`

```
FAIL!  : PhaseDSidechainTest::tapPointsDeliverDistinctSidechainSignals() 'probePreFx->sawSidechain()' returned FALSE. (pre-FX receiver saw no sidechain)
   Loc: [.../tests/src/core/PhaseDSidechainTest.cpp(178)]
FAIL!  : PhaseDSidechainTest::duckingFollowsTheSidechainKey() 'duck->keyedBlocks() > 0' returned FALSE. (ducker was not keyed by the key channel)
   Loc: [.../tests/src/core/PhaseDSidechainTest.cpp(241)]
FAIL!  : PhaseDSidechainTest::parallelBusSumsInputsThroughItsFxChain() 'nearValue(out[0][0], expectL)' returned FALSE. (bus L 1.125 != 0.375)
   Loc: [.../tests/src/core/PhaseDSidechainTest.cpp(291)]
FAIL!  : PhaseDSidechainTest::demoProjectBusWithNativeSidechainCompressor() 'compressor != nullptr' returned FALSE. (native Compressor plugin could not be instantiated)
   Loc: [.../tests/src/core/PhaseDSidechainTest.cpp(329)]
```

`./build/tests/MixerRoutingBackwardCompatTest` → `Totals: 3 passed, 2 failed, 0 skipped, 0 blacklisted, 1211ms`

```
FAIL!  : MixerRoutingBackwardCompatTest::legacyProjectLoadsWithIdenticalRouting() 'loadedTable.contains("ch1 send->0 amount=0.75 prefader=0")' returned FALSE. (  ch1 send->0 amount=0.75 prefader=0
   Loc: [.../tests/src/core/MixerRoutingBackwardCompatTest.cpp(192)]
FAIL!  : MixerRoutingBackwardCompatTest::phaseDProjectRoundTripsThroughSaveLoad() 'mixer->channelSidechainSend(bus, 2) != nullptr' returned FALSE. (sidechain send bus->2 did not survive)
   Loc: [.../tests/src/core/MixerRoutingBackwardCompatTest.cpp(314)]
```

---

## 3. Fixes, one class at a time

### 3.1 Fix #1 — sidechain keying / tap-points (2 failures)

**Root cause.** `Effect::m_parent` was assigned only inside `Effect::instantiate()`. Effects appended to an existing chain at project-load time went through `EffectChain::appendEffect()`, which never set the parent — so an effect that needs to find its own chain (the Compressor's sidechain key lookup) found nothing. The receiver consequently "saw no sidechain" and the ducker was never keyed.

**Fix.** `EffectChain::appendEffect()` now sets the effect's parent chain (`include/Effect.h` +8, `src/core/EffectChain.cpp` +4).
**Commit.** `fcde49d33f7b49e492ed162f4f4b2c9da3ca13a9`

Both keying failures disappear in the next full run (PhaseD 3→6 passed after #1+#2):

```
PASS   : PhaseDSidechainTest::tapPointsDeliverDistinctSidechainSignals()
PASS   : PhaseDSidechainTest::duckingFollowsTheSidechainKey()
```

### 3.2 Fix #2 — parallel-bus summing (`bus L 1.125 != 0.375`)

**Root cause.** Not a mixer bug. The test harness renders synchronously and therefore never advances the engine's periodic automation counter, so the sends' amount `ValueBuffer`s were still ramping 0→1 when the test read the bus output. The bus was summing mid-ramp send amounts (`1.125`), not the settled values.

**Fix.** `PeriodHarness` now settles all automation value ramps before rendering (`tests/src/core/PhaseDMixerTestSupport.h` +32).
**Commit.** `df953f5133b7e9db48d24fba01a4dd092e1fd168`

```
PASS   : PhaseDSidechainTest::parallelBusSumsInputsThroughItsFxChain()
```

After #1+#2: `Totals: 6 passed, 1 failed` (only the compressor demo left).

### 3.3 Fix #3 — native Compressor plugin link (`undefined symbol _ZTIN4lmms16AutomatableModelE`)

**Root cause.** The Compressor is a MODULE plugin dlopened by the test. The test executable did not export the `lmmsobjs` symbols, so the plugin's undefined reference to `lmms::AutomatableModel`'s typeinfo could not resolve at `dlopen` time.

**Fix.** `set_target_properties(PhaseDSidechainTest PROPERTIES ENABLE_EXPORTS ON)` — the same pattern already used by `src/CMakeLists.txt:203` and the Part C plugin-dlopen tests (`tests/CMakeLists.txt`).
**Commit.** `a212d249dcc3e379383be953b1cfbe882c39cfe6`

After this the compressor instantiates and the demo test fails on its *last* assertion instead:

```
FAIL!  : PhaseDSidechainTest::demoProjectBusWithNativeSidechainCompressor() 'nearValue(static_cast<float>(openLevel), program, 1.0e-5f)' returned FALSE. (bus is not transparent without a key: 0.02997 != 0.03)
```

### 3.4 Fix #4 — the demo test's `0.03` expectation is provably wrong (test corrected, not the code)

The Compressor's `calcOutGain()` hard-codes a `* 0.999` output gain (upstream, Lost Robot, 2021-03-10), so a 0.03 program level passes through as `0.029970`. The test asserted `0.03` with a `1e-5` tolerance. The implementation is correct and upstream-compatible; the expectation was wrong, so the **test** was corrected to the documented `0.03 * 0.999`, with a citation comment.
**Commit.** `8dc0f6bc657adfc1dffba9a3caac8d7908062416`

`PhaseDSidechainTest` is now fully green:

```
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 1214ms
```

### 3.5 Fix #5 — legacy routing table row not found

**Root cause.** The test emitted routing rows with a leading indent while `QStringList::contains()` matched the exact unindented row, so a *correct* legacy load (`ch1 send->0 amount=0.75 prefader=0`) was reported missing.

**Fix.** Emit routing rows unindented (`tests/src/core/MixerRoutingBackwardCompatTest.cpp`, 2 rows).
**Commit.** `a543ea33b71bde83fecf9f93830641ce421075dd`

```
PASS   : MixerRoutingBackwardCompatTest::legacyProjectLoadsWithIdenticalRouting()
LEGACY_RENDER expected=0.560000 measured=0.560000002
LEGACY_EQUIVALENCE routes_identical=1 audio_identical=1 frames=256
```

### 3.6 Fix #6 — save/load round-trip: `sidechain send bus->2 did not survive`

**Root cause.** `createSidechainSend()` refused any route that closed a cycle, because a sidechain send is also a scheduling edge and a cycle would deadlock the dependency counter. The Phase D demo project closes a cycle *through a regular send* (`bus -> 2` audio, `2 -> bus` sidechain), so the sidechain send was silently dropped at load time and the round-trip assertion failed.

**Fix (spec-derived, §5.2 / §5.4(a)).** A sidechain send is observation-only and must never create a circular wait. Split the cycle rule in two:
- a cycle made of **sidechain sends alone** has no regular send to anchor ordering → still refused (`checkSidechainCycle`, returns `nullptr`);
- a cycle through **at least one regular send** is accepted as **deferred**: the route does not gate its receiver and the receiver reads the *previous period's* committed tap (`MixerSidechainRoute::m_committed`, pre-allocated `framesPerPeriod()×2`; published by `Mixer::prepareMasterMix()` on the render path via `commitIntermediate()`, which is a `std::copy` between pre-allocated buffers).

Files: `include/Mixer.h` (+33), `src/core/Mixer.cpp` (+124/−14) — `deferred()` accessor, `gatingSidechainReceives()`, deferred-aware `processed()` / `incrementDeps()` / leaf detection / `sumSidechainInputs()` / `clearIntermediate()`, `checkSidechainCycle()`, `checkInfiniteLoop()` traversal restricted to gating edges.
**Commit.** `4731e9ab862ba37afc762973d7a9acede3adc8c8`

```
PASS   : MixerRoutingBackwardCompatTest::phaseDProjectRoundTripsThroughSaveLoad()
PHASED_ROUNDTRIP routes_identical=1 audio_identical=1 xml_bytes=973
```

A non-deferred edge here would deadlock the dependency counter, so this test passing is itself evidence the deferral is active.

Final state:

```
PhaseDSidechainTest              Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 1203ms
MixerRoutingBackwardCompatTest   Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 1203ms
```

---

## 4. D1 A-B gate re-confirmation

Command: `./build/tests/MixerAbRegressionTest` (exit 0)

```
PASS   : MixerAbRegressionTest::initTestCase()
AB_EVIDENCE render bytes=65536 sha256=19846573f5c71ef1222133bb33f2c7173df9b89715c3731751876c87d2c7f324
AB_EVIDENCE reference bytes=65536 sha256=19846573f5c71ef1222133bb33f2c7173df9b89715c3731751876c87d2c7f324
PASS   : MixerAbRegressionTest::renderIsByteIdenticalToReference()
PASS   : MixerAbRegressionTest::cleanupTestCase()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 1200ms
```

Render and committed reference both hash `19846573f5c71ef1222133bb33f2c7173df9b89715c3731751876c87d2c7f324` — **identical to the required reference**. The D1 byte-exactness gate still passes after all six fixes.

---

## 5. D3 per-send CPU gate

**Metric (spec v1.2, decision D3).** *"<5% single-core CPU per ACTIVE sidechain send, measured against the baseline cost of a regular send, at 48 kHz / 256-frame buffer."* (§8.3 also states the intent: *"<5% increase in CPU time vs Phase C baseline (no sidechain)"*.)

**Harness.** `tests/src/core/PhaseDPerfBench.cpp` (commit `7edfbbe45`), deliberately **not** registered with ctest (timing-sensitive). Run: `./build/tests/PhaseDPerfBench`.

**Method.**
- One process, one mixer, one graph: master + 32 sender channels + 1 bus (bus→master always present). Channel count, feeds and processing are identical in every state.
- Per repetition, four measured windows in this order: **base** (no sends) → **regular** (32 audio sends `ch(i)→master`) → **base twin** (sends deleted again) → **sidechain** (32 sidechain sends `ch(i)→bus`). The two base windows bracket the measurement, so machine drift cancels in the deltas; `base_twin_spread_us` is printed as the noise floor.
- Each window = 200 warm-up + 2500 synchronous periods; automation ramps settled by `PeriodHarness` before each window; every channel is fed a fresh non-silent period so no silence short-circuit can hide the send work.
- CPU = `CLOCK_PROCESS_CPUTIME_ID` delta over the window — total CPU across the mixer's worker threads, i.e. core-seconds ("single-core CPU").
- Per-send marginal cost = `(state − base_mean) / 32 / periods`. 5 repetitions per invocation, 3 invocations = **15 repetitions**.

**Numbers (15 interleaved reps, 48 kHz / 256 frames, period budget 5.333 ms).**

```
reps=15
sidechain_ns: min=-805.6 median=2248.7 max=4051.9 mean=2272.7     (per active send, per period)
regular_ns:   min=3.9    median=1582.9 max=3575.5                  (per regular send, per period)
per_send_pct_of_core(median)=0.0422%   threshold=5%
per_send_pct_of_core(worst_rep)=0.0760%
margin_vs_gate=119x
```

Per-invocation verdict lines (the harness prints its own gate check):

```
run A: D3_PER_SEND regular_ns=1582.9 sidechain_ns=3051.9 | D3_GATE per_send_pct_of_core=0.0572% threshold=5% verdict=PASS
run B: D3_PER_SEND regular_ns=864.0  sidechain_ns=1400.8 | D3_GATE per_send_pct_of_core=0.0263% threshold=5% verdict=PASS
run C: D3_PER_SEND regular_ns=1660.3 sidechain_ns=2526.0 | D3_GATE per_send_pct_of_core=0.0474% threshold=5% verdict=PASS
```

**Verdict.**
- **PASS** on the per-send-vs-single-core reading: one active sidechain send costs **0.042% of one core** per period (median; worst single rep 0.076%) — **~119× under the 5% threshold**. The verdict is insensitive to measurement noise: even the worst rep is 66× under.
- **Context / honesty note.** A sidechain send costs **~1.5–1.9× a regular send** (median ratio across invocations: 152%, 162%, 193%), *not* 5% of one. This is structural: strategy (a) writes the tap (a copy) and then sums it post-hoc (an add pass), versus the regular send's single multiply-accumulate pass — ~2× is the floor for any copy+sum design that keeps the real-time rule. If the gate is instead read as "sidechain cost < 5% of a regular send's cost", it does **not** pass and cannot be met without abandoning the per-sender intermediate buffer design; state that explicitly rather than bending the metric.
- **Noise floor.** On this shared box (loadavg 5–6 during the runs) the base-twin spread was 8.5–112 µs/period, so the absolute ns figure carries roughly ±50% uncertainty; the gate verdict does not depend on it.

---

## 6. Demo evidence (ducking, bus summing, XML)

From the green runs (`PhaseDSidechainTest`, `MixerRoutingBackwardCompatTest`):

```
DUCKING target=0.5000 key=0.5000 open=0.500000 ducked=0.100000 ratio=0.200000(expect 0.200000) drop_db=-13.98 key_peak=0.5000 keyed_blocks=1
PARALLEL_BUS bus=4 inA=0.5000 inB=0.2500 gainL=0.50 gainR=0.25 outL=0.375000(expect 0.375000) outR=0.187500(expect 0.187500)
DEMO_BUS bus=5 bass=0.0200 pad=0.0100 kick=0.5000 program=0.0300 open=0.029970 ducked=0.006496 ratio=0.2168 drop_db=-13.28 effect=compressor peakcontroller_present=0 effects_in_project=1
LEGACY_LOADED channels=4 routes=4 sidechain=0
LEGACY_RENDER expected=0.560000 measured=0.560000002
LEGACY_EQUIVALENCE routes_identical=1 audio_identical=1 frames=256
PHASED_ROUNDTRIP routes_identical=1 audio_identical=1 xml_bytes=973
```

- **Ducking**: a 0.5 key gates the target to 0.1 (−13.98 dB); the demo bus program material drops from 0.029970 to 0.006496 (−13.28 dB) under a 0.5 kick key, keyed by the native Compressor plugin.
- **Bus summing**: two parallel inputs (0.5 at gain 0.5, 0.25 at gain 0.25) sum to 0.375 L / 0.1875 R through the bus FX chain.
- **XML assertions**: Phase D project round-trips with identical routing and identical rendered audio (`xml_bytes=973`); the synthetic legacy project loads with identical routing table and identical audio (0.560000002 vs 0.56, within float tolerance).

---

## 7. Full build + ctest baseline

`make -C build -j4` → **exit 0**, `grep -c "error:"` = 0.

`cd build/tests && ctest --output-on-failure` → exit 0:

```
 1/14 Test  #1: ArrayVectorTest ..................   Passed    0.02 sec
 2/14 Test  #2: AudioBufferTest ..................   Passed    0.02 sec
 3/14 Test  #3: AudioBusHandleTest ...............   Passed    1.22 sec
 4/14 Test  #4: AudioPortsTest ...................   Passed    1.23 sec
 5/14 Test  #5: AutomatableModelTest .............   Passed    1.22 sec
 6/14 Test  #6: MathTest .........................   Passed    0.02 sec
 7/14 Test  #7: MixerAbRegressionTest ............   Passed    1.24 sec
 8/14 Test  #8: MixerRoutingBackwardCompatTest ...   Passed    1.22 sec
 9/14 Test  #9: PhaseDSidechainTest ..............   Passed    1.22 sec
10/14 Test #10: ProjectVersionTest ...............   Passed    0.02 sec
11/14 Test #11: RelativePathsTest ................   Passed    0.02 sec
12/14 Test #12: TimelineTest .....................   Passed    1.24 sec
13/14 Test #13: AutomationTrackTest ..............   Passed    1.23 sec
14/14 Test #14: PluginPortsMigrationTest .........   Passed    2.57 sec

100% tests passed, 0 tests failed out of 14
```

The pre-existing baseline is unchanged and still passes.

---

## 8. Audio-thread audit (allocation/locking on the process path)

**Method.** `git diff 61e4898a1..HEAD -- include/ src/ plugins/` (production code only; the other commits are tests), then grep every added line for allocation, locking and logging primitives:

```
git diff 61e4898a1..HEAD -- include/ src/ plugins/ | grep -E "^\+" | \
  grep -nE "new |delete |malloc|calloc|realloc|free\(|\.resize|\.reserve|push_back|emplace|QMutex|\.lock\(|std::mutex|QSemaphore|QWaitCondition|qDebug|printf|QString\(|AudioBuffer\("
```

**Result — exactly one hit in the entire production diff:**

```
116:+	auto route = new MixerSidechainRoute(from, to, amount, mode, deferred);
```

That is `Mixer::createSidechainSend()`, a **control-thread** function (wrapped in `Engine::audioEngine()->requestChangeInModel()`; route vectors are only mutated there). No mutex, semaphore, wait-condition, lock, `qDebug`/`printf`, or `QString` construction was added anywhere.

Process-path additions, reviewed line by line:

| Added code | Where it runs | Allocation / locking |
|---|---|---|
| `MixerSidechainRoute::commitIntermediate()` | `Mixer::prepareMasterMix()` → audio render path (`AudioEngine::renderStageNoteSetup`, `AudioEngine.cpp:238`), before the period's workers start | `std::copy` between `m_intermediate`/`m_committed`, both allocated once in the route ctor (`framesPerPeriod()×2`); `updateSilenceFlags` is a bitmask write. **None.** |
| `sumSidechainInputs()` deferred branch | worker thread | picks `committed()` vs `intermediate()`; existing sum loop unchanged; `clearIntermediate()` skipped for deferred routes. **None.** |
| `processed()` / `incrementDeps()` / `masterMix` leaf test | worker / audio thread | skips deferred routes, `gatingSidechainReceives()` counts an existing vector. **None.** |
| `checkSidechainCycle()` / `checkInfiniteLoop()` changes | control thread (`createSidechainSend`) | recursion over existing vectors. **None.** |

**Residual costs (bounded, no alloc/lock):** `prepareMasterMix()` now iterates all sidechain routes once per period and copies one pre-allocated buffer per *deferred* route. Ordering between control-thread route mutation and the render path relies on the existing `requestChangeInModel()` protocol, not on new locks.

---

## 9. Not verified

- **No real-time device run.** All evidence comes from synchronous headless renders on the dummy audio device; no interactive LMMS session, no audible check, no xrun/soak test through a real ALSA/JACK device.
- **Deferred-route semantics are only covered by one topology.** The mixed cycle (sidechain + regular send) is exercised via the `bus→2` round-trip; the audible "one period late" behaviour under live UI editing, and other cycle topologies, are untested. Pure-sidechain cycles are refused by design (test `cycleFormingSidechainSendIsRejected` covers the refusal, not the deferral).
- **D3 precision.** The per-send figure carries ~±50% uncertainty on this loaded box (base-twin spread 8.5–112 µs/period). The verdict (0.042% vs 5%) is robust; the ns figure is not tight. The extra per-period copy cost of *deferred* routes was not measured separately.
- **No memory/thread sanitisers.** No valgrind, ASan or TSan run; the real-time audit is a static grep + code review, not dynamic proof.
- **Save/load coverage is narrow.** Round-trip is verified with a synthetic, test-authored XML project (973 bytes) via the harness; no real-world `.mmp`/`.mmpz` files, no forward/backward compatibility with other LMMS builds, no verification that older LMMS versions handle the new deferred sidechain attribute.
- **Legacy fixture is synthetic.** The "legacy project" is test-authored XML, not a real-world project file.
- **Scope of regression testing.** Only the 14 ctest entries were run; GUI paths, VST/plugin scanning, automation of sidechain send amounts, and undo/redo of sidechain send creation/deletion were not exercised.
- **Plugin link fix scope.** `ENABLE_EXPORTS` was verified for this test's dlopen on this Linux/GCC build only; other platforms/linkers are unverified.

---

## 10. Reproduction

```
cd lmms-partd
make -C build -j4                                                   # exit 0
./build/tests/PhaseDSidechainTest                                   # 7 passed, 0 failed
./build/tests/MixerRoutingBackwardCompatTest                        # 5 passed, 0 failed
./build/tests/MixerAbRegressionTest                                 # sha256 19846573…
./build/tests/PhaseDPerfBench                                       # D3 gate: 0.042% of a core, PASS
cd build/tests && ctest --output-on-failure                         # 14/14 passed
```

No push was performed. Branch `part-d-sidechain` head `7edfbbe45b214c1c1315685a1ed01698c1cd66be`.
