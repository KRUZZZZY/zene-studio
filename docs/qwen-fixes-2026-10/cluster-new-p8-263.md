FIXED

## Verdict and evidence

**REAL.** The finding is that a hosted effect's wet/dry mix counts a side-chain channel as dry audio when the
side-chain starts at flattened channel 1, so a mono effect's second output is mixed with the key signal as its
dry source.

The rule named in the finding (`plugins/Vst3Effect/Vst3Effect.cpp:151-154` in the patrol's numbering) is
`dryChannels()`, which by this branch lived in the anonymous namespace at
`plugins/Vst3Effect/Vst3Effect.cpp:186-191` and, character for character, at
`plugins/ClapEffect/ClapEffect.cpp:186-191`:

```cpp
const auto main = sideChainChannel < 0 ? inputs : static_cast<ch_cnt_t>(std::max(2, sideChainChannel));
return std::min({inputs, outputs, main});
```

`sideChainChannel` is the flattened input index the auxiliary bus starts at, i.e. the width of the main input
port in front of it (`plugins/Vst3Effect/Vst3BusMap.cpp:47` - `layout.sideChainChannel = layout.inputs`;
`plugins/ClapEffect/ClapBusMap.cpp:116` - `layout.sideChainChannel = layout.hasSideChain ? mainChannels : -1`).
With a mono main port and the key at channel 1, `std::max(2, 1)` = 2, so `min({2, outputs, 2})` = 2: the mix loop
(`Vst3Effect.cpp:236-247`, `ClapEffect.cpp:236-245`) then applies `dry * in.bufferPtr(1) + wet * out.bufferPtr(1)`
to output channel 1 - a dry contribution from the key channel, which the third reviewer point asked to be stated
precisely. Two different pointers are involved and the report must not run them together: `Vst3Effect.cpp:227-230`
hands the *remapped* array to the plug-in, so with a mixer sidechain send on that channel the plug-in's channel 1
hears `Effect::sidechainBuffer()`; `Vst3Effect.cpp:241` reads the *original* input view for the dry audio, i.e. the
track's own channel-1 content. Either way channel 1 belongs to the key bus, not the main bus, so neither pointer is
dry signal for an output. `passThrough()` for an unprepared plug-in had the same fault. The stereo-pair floor is
what makes the count wrong, exactly as the finding says.

The CLAP effect carried the identical copy of the rule, so the fix covers both hosts.

## Test matrix

| Path | Case | Covered by |
|---|---|---|
| normal | stereo main bus, stereo key at channel 2 | `dryMixChannels(4, 2, 2) == 2` |
| normal | no key at all | `dryMixChannels(2, 2, -1) == 2`, `(1, 2, -1) == 1` |
| **failure (the bug)** | mono main bus, key at channel 1, two outputs | `dryMixChannels(2, 2, 1) == 1` |
| **failure (the bug)** | wider key, same mono main bus | `dryMixChannels(3, 2, 1) == 1` |
| reconfiguration | aux-only input port (key at channel 0) | `dryMixChannels(2, 2, 0) == 0` |
| exhaustion / bounds | output port narrower than the main bus | `dryMixChannels(4, 1, 2) == 1` |
| round 6 widening kept | six main channels, no key | `dryMixChannels(6, 6, -1) == 6` |
| **hosted VST3 audio (round 2)** | the reported layout end to end: mono main input, key at flattened channel 1, two outputs, half-dry mix, distinct samples per channel | `Vst3EffectIntegrationTest::testASidechainChannelIsNotMixedIntoTheSecondOutput`: output 0 = 0.375 (mixed), output 1 = -0.125 (the plug-in's wet value only; a key-as-dry mix would give -0.1875) |
| hosted VST3 audio, plug-in cannot run | `passThrough()` uses the same rule | the count assertions in `SidechainInputsTest` (`dryMixChannels(2, 2, 1) == 1`) |
| audio thread | the rule is inline arithmetic on its arguments; no allocation, free, lock, wait, log or Qt call, and the callers' loops are unchanged | review of `include/SidechainInputs.h:59-71` |
| platform | header-only inline in C++20, no Qt, no POSIX path, no `std::filesystem`; built on this Linux x86-64 tree, and the same construct compiles the VST3 and CLAP plug-in modules | `build-1.log`, `build-3.log`, `build-5.log` |

The regression target exists and is registered: `tests/src/core/SidechainInputsTest.cpp` (`tests/CMakeLists.txt:960`,
`ctest` test #305), the same file that already owns the E9 side-chain-input contract. The two effect modules
(`vst3effect`, `clapeffect`) and their family tests are configured targets in `build/`.

The fix moves the rule to `include/SidechainInputs.h` as `dryMixChannels()` so both hosts share one function and
one test. `Vst3Effect.cpp:189` and `:237`, `ClapEffect.cpp:190` and `:237` call it; the duplicate is gone. The
change is `mainInputChannels < 0 ? inputs : mainInputChannels` - no stereo floor - so the main port's own width
bounds the dry mix and no side-chain channel is ever a dry source.

The regression test is `SidechainInputsTest::aMonoMainBusKeepsItsSidechainOutOfTheDryMix` (with
`theDryMixCoversTheMainInputBusOnly` pinning the rest of the rule) in `tests/src/core/SidechainInputsTest.cpp:63-84`.
Both slots are one `QCOMPARE` per case with constant arguments: no clock, no sleep, no thread, no plugin load.

## Round 2: the hosted VST3 audio check the reviewer asked for

Round 1 proved the rule with integer counts only; the second output's audio was never measured on the hosted VST3
path. That is now `Vst3EffectIntegrationTest::testASidechainChannelIsNotMixedIntoTheSecondOutput`
(`tests/src/plugins/Vst3EffectIntegrationTest.cpp:219-289`), which drives the real effect in an `EffectChain` on an
`AudioBus`, with the layout the finding reports:

- layout `setBusLayoutForTest({inputBusChannels {1,1}, outputBusChannels {2}, inputs 2, outputs 2, hasSideChain,
  sideChainChannel 1})` - a mono main input bus, the key bus at flattened channel 1, two outputs;
- distinct samples per channel: channel 0 = 0.5 (main), channel 1 = -0.25 (key);
- nonzero dry level: wet = dry = 0.5, set through the effect's own saved `wet` attribute
  (`Effect::saveGenericSettings` + `Effect::loadSettings`, `src/core/Effect.cpp:114`, `:186`), and the
  plug-in's `Gain` parameter at 0.5, so the plug-in's output is a known half of its input;
- assertions against the known wet result: output 0 = `0.5 * 0.5 + 0.5 * 0.25` = 0.375, output 1 = the wet value
  `0.5 * -0.25` = -0.125. Output 1 is past the main input bus, so the mix leaves it fully wet. A dry contribution
  from channel 1 would make it `0.5 * -0.25 + 0.5 * -0.125` = -0.1875, which the 1e-6 tolerance separates.

Measured with the fix in place (`test-2.log`): `out[0]=0.375000000 out[1]=-0.125000000 (a key-as-dry mix would
give out[1]=-0.187500000)`. Measured with the behavioural hunk reversed (`test-3.log`): output 1 is **-0.1875** and
the test fails.

One product change made this reachable, and it is test-only: `Vst3Effect::setBusLayoutForTest()` plus a private
`busLayout()` accessor and two members, all inside `#ifdef LMMS_TESTING`
(`plugins/Vst3Effect/Vst3Effect.h:83-91`, `:107-116`, members `:126-127`), and the four layout reads in `processImpl()` now go through
`busLayout()` (`Vst3Effect.cpp:205`, `:214`, `:229`, `:237`). Without an injected layout `busLayout()` returns
`m_plugin.busLayout()`, so production behaviour is unchanged - the structural risk is that the SDK's test effect
("AGain Sample Accurate") declares one stereo port pair and no auxiliary input bus, so a small end-to-end run can
only reach the reported layout by stating it, exactly as the bus map would for a plug-in that has one. This follows
the file's existing `setReprepareTestHook` precedent (same `LMMS_TESTING` guard); `busLayout()` costs a flag read and
a reference on the audio thread, no allocation. The CLAP host is unaffected by the seam - it has no such hook and
still takes the fixed rule from the shared header.

## Evidence table

| Check | Exact command | Result |
|---|---|---|
| Refactor commit (rule moved, behaviour unchanged) | build `vst3effect clapeffect SidechainInputsTest` + `ctest -R SidechainInputsTest` (`build-1.log`, `test-1.log`) | build EXIT=0, test EXIT=0 (1/1) |
| RED - the new test on the buggy rule | `cmake --build build --target SidechainInputsTest -j4` (`build-2.log`), then `ctest -R SidechainInputsTest` (`test-2.log`) | build EXIT=0, test **EXIT=8**; `Actual (dryMixChannels(2, 2, 1)): 2`, `Expected (ch_cnt_t{1}): 1`; `Actual (dryMixChannels(2, 2, 0)): 2`, `Expected (ch_cnt_t{0}): 0` |
| GREEN - the fix | build `vst3effect clapeffect SidechainInputsTest` (`build-3.log`), then `ctest -R SidechainInputsTest` (`test-3.log`) | build EXIT=0, test **EXIT=0** (1/1) |
| 4b reverse - product change reverted | `git diff -- include/SidechainInputs.h > .../logs/fix.patch` then `git apply -R .../logs/fix.patch`; build `SidechainInputsTest` (`build-4.log`); `ctest -R SidechainInputsTest` (`test-4.log`) | diff EXIT=0, reverse EXIT=0, build EXIT=0, test **EXIT=8** (same two assertions) |
| 4b restore - product change reapplied | `git apply .../logs/fix.patch`; build `vst3effect clapeffect SidechainInputsTest` (`build-5.log`); `ctest -R SidechainInputsTest` (`test-5.log`) | apply EXIT=0, build EXIT=0, test **EXIT=0** (1/1) |
| Affected family | `ctest -R 'Sidechain\|Clap\|Vst3' --output-on-failure` (`test-8.log`) | **EXIT=0**, 100% tests passed out of 18 (PhaseDSidechainTest, SidechainInputsTest, ClapBusMapTest, ClapHostTest, ClapTestInstrument{State,EventCount,ActivationCounters}Test, ClapLoaderErrorTest, ClapEffectIntegrationTest, ClapInstrumentViewTest, Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3EditorTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest) |
| Round 2 GREEN - hosted VST3 audio with the fix | build `vst3effect Vst3EffectIntegrationTest SidechainInputsTest` (`r2/build-1.log`); `ctest -R Vst3EffectIntegrationTest` (`r2/test-1.log`) and the binary's own slot run (`r2/test-2.log`) | build EXIT=0, ctest **EXIT=0**; `out[0]=0.375000000 out[1]=-0.125000000 (a key-as-dry mix would give out[1]=-0.187500000)` |
| Round 2 RED - behavioural hunk reversed | `git show a8a70c462c -- include/SidechainInputs.h > .../r2/logs/fix.patch` (EXIT=0) then `git apply -R .../r2/logs/fix.patch` (EXIT=0); build (`r2/build-2.log`); `ctest -R Vst3EffectIntegrationTest` (`r2/test-3.log`) | build EXIT=0, test **EXIT=8**: `FAIL! ... testASidechainChannelIsNotMixedIntoTheSecondOutput() ... (frame 0: output 1 is -0.1875, expected the wet value -0.125 - the side-chain channel was mixed into it as dry audio ...)`, `Totals: 8 passed, 1 failed` |
| Round 2 RESTORE | `git apply .../r2/logs/fix.patch` (EXIT=0); build (`r2/build-3.log`); `ctest -R Vst3EffectIntegrationTest` (`r2/test-4.log`) | EXIT=0, all 9 slots pass |
| Round 2 family + rebuild | build `zene Vst3EditorTest Vst3ChunkProbeTest Vst3HostTest` (`r2/build-4.log`); `ctest -R 'Sidechain|Clap|Vst3'` (`r2/test-5.log`) | build EXIT=0, **EXIT=0**, 100% tests passed out of 18 |
| Round 2 gates | `python3 .../feature_check.py <worktree>` (`r2/feature-1.log`) | **EXIT=0**, RESULT: PASS, no new complexity or file-length item |
| Product binary still builds | `cmake --build build --target zene -j4` (`build-8.log`) | EXIT=0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`feature-1.log`) | **EXIT=0**; PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"); RESULT: PASS |

## Commands and exit codes

All build commands ran from the worktree root:

- `cmake --build build --target vst3effect clapeffect SidechainInputsTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r1/logs/build-1.log 2>&1; echo EXIT=$?` - EXIT=0
- `cmake --build build --target SidechainInputsTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` - EXIT=0
- `cmake --build build --target vst3effect clapeffect SidechainInputsTest -j4 > .../logs/build-3.log 2>&1; echo EXIT=$?` - EXIT=0
- `cmake --build build --target SidechainInputsTest -j4 > .../logs/build-4.log 2>&1; echo EXIT=$?` - EXIT=0 (with the product change reverted)
- `cmake --build build --target vst3effect clapeffect SidechainInputsTest -j4 > .../logs/build-5.log 2>&1; echo EXIT=$?` - EXIT=0 (product change reapplied)
- `cmake --build build --target PhaseDSidechainTest ClapBusMapTest ClapHostTest ClapEffectIntegrationTest Vst3BusMapTest Vst3HostTest Vst3EffectIntegrationTest -j4 > .../logs/build-6.log 2>&1; echo EXIT=$?` - EXIT=0
- `cmake --build build --target ClapInstrumentViewTest ClapLoaderErrorTest ClapTestInstrumentUnitTest Vst3ChunkProbeTest Vst3EditorTest Vst3InstrumentFixtureProbe Vst3InstrumentIntegrationTest Vst3InstrumentTest -j4 > .../logs/build-7.log 2>&1; echo EXIT=$?` - EXIT=0
- `cmake --build build --target zene -j4 > .../logs/build-8.log 2>&1; echo EXIT=$?` - EXIT=0

All `ctest` commands ran from `build/tests`:

- `ctest -R SidechainInputsTest --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` - EXIT=0 (before the test was added)
- `ctest -R SidechainInputsTest --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` - **EXIT=8**, intended RED
- `ctest -R SidechainInputsTest --output-on-failure > .../logs/test-3.log 2>&1; echo EXIT=$?` - EXIT=0, GREEN
- `ctest -R SidechainInputsTest --output-on-failure > .../logs/test-4.log 2>&1; echo EXIT=$?` - **EXIT=8**, 4b reverse
- `ctest -R SidechainInputsTest --output-on-failure > .../logs/test-5.log 2>&1; echo EXIT=$?` - EXIT=0, 4b restore
- `ctest -R Sidechain --output-on-failure > .../logs/test-6.log 2>&1; echo EXIT=$?` - EXIT=8 before `PhaseDSidechainTest` was built (`Not Run`); the binary was then built (`build-6.log`, EXIT=0) and the full family run in `test-8.log` passes
- `ctest -R 'Sidechain|Clap|Vst3' --output-on-failure > .../logs/test-7.log 2>&1; echo EXIT=$?` - EXIT=8, ten tests `Not Run` because their binaries were not built in this tree yet; built in `build-6.log`/`build-7.log` (both EXIT=0)
- `ctest -R 'Sidechain|Clap|Vst3' --output-on-failure > .../logs/test-8.log 2>&1; echo EXIT=$?` - **EXIT=0**, 18/18 passed

The 4b proof used exactly the required commands:

- `git diff -- include/SidechainInputs.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r1/logs/fix.patch` - EXIT=0 (29 lines, the one rule)
- `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r1/logs/fix.patch` - EXIT=0, then build EXIT=0 and test **EXIT=8**
- `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r1/logs/fix.patch` - EXIT=0, then build EXIT=0 and test **EXIT=0**

The same two-phase proof was then run once more as a single invocation per phase, so the manifest's commands
are commands that were executed verbatim from the committed tree (`build-9.log`, `test-9.log`, `build-10.log`,
`test-10.log`; the worktree was clean before and after, `git status --porcelain` empty):

- RED: `cd <worktree> && git apply -R .../logs/fix.patch; echo REVERSE_EXIT=$?; cmake --build build --target SidechainInputsTest -j4 > .../logs/build-9.log 2>&1; echo BUILD_EXIT=$?; cd build/tests && ctest -R SidechainInputsTest --output-on-failure > .../logs/test-9.log 2>&1; echo TEST_EXIT=$?` - REVERSE_EXIT=0, BUILD_EXIT=0, **TEST_EXIT=8** (same two assertions).
- GREEN: `cd <worktree> && git apply .../logs/fix.patch; echo APPLY_EXIT=$?; cmake --build build --target vst3effect clapeffect SidechainInputsTest -j4 > .../logs/build-10.log 2>&1; echo BUILD_EXIT=$?; cd build/tests && ctest -R SidechainInputsTest --output-on-failure > .../logs/test-10.log 2>&1; echo TEST_EXIT=$?` - APPLY_EXIT=0, BUILD_EXIT=0, **TEST_EXIT=0** (1/1 passed).

`feature_check.py` ran once, EXIT=0 (`feature-1.log`, RESULT: PASS). `submit_check.py` ran as the completion
check; its table is in `.../logs/submit-1.log` and is answered under `## Self-check`.

### Round 2 (log folder `.../new-p8-263.r2/logs`)

- `cmake --build build --target vst3effect Vst3EffectIntegrationTest SidechainInputsTest -j4 > .../r2/logs/build-1.log 2>&1; echo EXIT=$?` - EXIT=0
- `ctest -R Vst3EffectIntegrationTest --output-on-failure > .../r2/logs/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) - EXIT=0
- `./Vst3EffectIntegrationTest testASidechainChannelIsNotMixedIntoTheSecondOutput > .../r2/logs/test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) - EXIT=0, the slot's own output line quoted above
- `git show a8a70c462c -- include/SidechainInputs.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r2/logs/fix.patch; echo EXIT=$?` - EXIT=0 (49 lines)
- `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r2/logs/fix.patch; echo EXIT=$?` - EXIT=0
- `cmake --build build --target vst3effect Vst3EffectIntegrationTest SidechainInputsTest -j4 > .../r2/logs/build-2.log 2>&1; echo EXIT=$?` - EXIT=0
- `ctest -R Vst3EffectIntegrationTest --output-on-failure > .../r2/logs/test-3.log 2>&1; echo EXIT=$?` - **EXIT=8**, the round-2 RED
- `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p8-263.r2/logs/fix.patch; echo EXIT=$?` - EXIT=0
- `cmake --build build --target vst3effect Vst3EffectIntegrationTest SidechainInputsTest -j4 > .../r2/logs/build-3.log 2>&1; echo EXIT=$?` - EXIT=0
- `ctest -R Vst3EffectIntegrationTest --output-on-failure > .../r2/logs/test-4.log 2>&1; echo EXIT=$?` - EXIT=0, the round-2 GREEN
- `cmake --build build --target zene Vst3EditorTest Vst3ChunkProbeTest Vst3HostTest -j4 > .../r2/logs/build-4.log 2>&1; echo EXIT=$?` - EXIT=0
- `ctest -R 'Sidechain|Clap|Vst3' --output-on-failure > .../r2/logs/test-5.log 2>&1; echo EXIT=$?` - EXIT=0, 18/18
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../r2/logs/feature-1.log 2>&1; echo EXIT=$?` - EXIT=0, RESULT: PASS

## Self-check

1. **The regression tests FAIL with the product change reverted and PASS with it.**
   - Count rule (`SidechainInputsTest`), round 1: product hunk reversed (`r1/fix.patch`, `git apply -R`, EXIT=0),
     rebuilt (`r1/build-4.log`, EXIT=0), run (`r1/test-4.log`) - **EXIT=8**, `FAIL! : ...aMonoMainBusKeepsItsSidechainOutOfTheDryMix()
     Actual (dryMixChannels(2, 2, 1)): 2 / Expected (ch_cnt_t{1}): 1`; restored (`git apply`, EXIT=0,
     `r1/build-5.log`, EXIT=0, `r1/test-5.log`) - **EXIT=0**. The refactor commit `4bbcea54b9` exists so the
     reverse-patch step fails on an assertion rather than on a missing symbol.
   - Hosted VST3 audio (`Vst3EffectIntegrationTest::testASidechainChannelIsNotMixedIntoTheSecondOutput`), round 2:
     behavioural hunk reversed with `git show a8a70c462c -- include/SidechainInputs.h` (EXIT=0) + `git apply -R`
     (EXIT=0), rebuilt (`r2/build-2.log`, EXIT=0), run (`r2/test-3.log`) - **EXIT=8**,
     `frame 0: output 1 is -0.1875, expected the wet value -0.125 - the side-chain channel was mixed into it as dry
     audio`, `Totals: 8 passed, 1 failed`; restored (`git apply`, EXIT=0, `r2/build-3.log`, EXIT=0,
     `r2/test-4.log`) - **EXIT=0**. The seam added in round 2 is not part of the behavioural hunk: reversing it
     would only stop the test compiling, so the proof reverses the rule alone, which is the code the assertion is
     about.
2. **Deterministic.** The count test calls one inline function with constant arguments; no clock, no `sleep`, no
   thread, no plug-in load, no file or environment access. The hosted test feeds fixed samples (0.5 on channel 0,
   -0.25 on channel 1) into one `EffectChain::processAudioBuffer()` call on an `AudioBus` and compares against
   arithmetic expectations within 1e-6; no timing, no sleeps, no threads (unlike the re-prepare test next to it,
   which needs one), no rendered file, no environment. Its one non-determinism risk is the plug-in's own `Gain`
   parameter, which the test sets itself (`modelForParam(1)->setValue(0.5f)`) rather than reading a default. The
   layout it drives is stated through `setBusLayoutForTest()`; that is the only injected value, and it is the value
   under test.
3. **Fix at the cause and complete.** The dry-mix count is now the main input port's width in both callers
   (`passThrough()` and the processImpl mix loop) of both hosts: `Vst3Effect.cpp:189`/`:237` and
   `ClapEffect.cpp:190`/`:237`. The two `BusLayout::sideChainChannel` producers were checked and agree on the
   meaning of the value (`Vst3BusMap.cpp:47`, `ClapBusMap.cpp:116`): it is where the aux bus starts, so it is also
   the width of the main port in front of it. `ch_cnt_t` is the type that flows into `bufferPtr()`, and the result
   is still bounded by `inputs` and `outputs`, so no index can go out of range. The other effects that compute a
   dry-channel count were checked: `plugins/AuEffect/AuEffect.cpp:153` uses
   `std::min(in.channels(), out.channels())` and its files have no side-chain concept at all
   (`rg sidechain plugins/AuEffect` is empty), so it is not this defect and is left alone.
   Round 2 added one product-level seam so the reported layout can be driven through the hosted path:
   `Vst3Effect::setBusLayoutForTest()` and a private `busLayout()` accessor, both inside `#ifdef LMMS_TESTING`, and
   the four layout reads in `processImpl()` now go through that accessor (`Vst3Effect.h:83-91`, `:107-116`,
   `:126-127`; `Vst3Effect.cpp:205`, `:214`, `:229`, `:237`). With no injected layout it returns
   `m_plugin.busLayout()`, so the module and every non-injecting user behave exactly as before; the CLAP sibling
   needs no change and still takes the fixed rule from the shared header. `busLayout()` is a flag read plus a
   reference on the audio thread - no allocation, no lock, no logging.
4. **No collateral damage.** Nothing here is saved: no `.mmp` field, no state chunk, no command, no undo step, so
   old projects load and render as before. The only behaviour that changes is the one the finding names -
   `sideChainChannel` 0 (an aux-only input port: nothing in the port is the main bus, so nothing is dry) and 1 (a
   mono main port: only channel 0 is dry). Round 6's widening (main-bus channels past the second are mixed, not
   left fully wet) is preserved and asserted (`dryMixChannels(6, 6, -1) == 6`). No new limit or check is introduced
   that another caller could exceed: `std::min({inputs, outputs, main})` is the same clamp as before.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE**, so this item does not apply; the pre-fix expression
   and its effect on output channel 1 are quoted above and were reproduced by the run in `test-2.log`.
6. **Realtime / undo / commands.** `dryMixChannels()` is `inline` arithmetic over two integers and a channel count,
   called from `processImpl()`/`passThrough()` on the audio thread: no allocation, free, lock, wait, log, signal or
   Qt call. No control command was added, so the `ControlCommandsSnapshot` canary does not move, and no undo step
   is involved.
7. **Sibling commands** (`app.*`, `arrangement.*`, ... in the pre-submit list): this commit changes no command file
   and no command handler - its diff is `include/SidechainInputs.h`, `plugins/Vst3Effect/Vst3Effect.cpp`,
   `plugins/ClapEffect/ClapEffect.cpp`, `tests/src/core/SidechainInputsTest.cpp` and this report. The sibling list
   comes from other lanes' `ControlCommands*` commits inside the shared `base..HEAD` range the check compares
   against (`050/m0-train-35`); none of them touch the wet/dry mix, so none is affected. No new command id exists
   in the diff, and the `ControlCommandsSnapshot` canary is untouched.

## Completion check

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p8-263 > .../logs/submit-1.log 2>&1; echo EXIT=$?`
- **EXIT=0**, `RESULT: PASS - submit`: commits on the branch PASS, working tree clean PASS, report PASS, tests
  added or changed PASS, new test files registered PASS, gates PASS, no FAIL item. The six ASK items are answered
  above. The check was rerun after the report's addendum commits (`submit-2.log`) with the same result, EXIT=0.

## Other findings

- The aux-only case (`sideChainChannel == 0`, a plug-in whose only active input bus is auxiliary) is fixed by the
  same change rather than by a separate one: it previously copied two channels of the key signal into the dry
  output through `passThrough()` as well. It is asserted in the same test slot.
