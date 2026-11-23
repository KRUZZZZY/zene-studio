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
to output channel 1, and `in.bufferPtr(1)` is the side-chain signal handed to the plug-in by
`sidechainInputs()` (`include/SidechainInputs.h:45-58`). `passThrough()` for an unprepared plug-in had the same
fault. The stereo-pair floor is what makes the count wrong, exactly as the finding says.

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

## Evidence table

| Check | Exact command | Result |
|---|---|---|
| Refactor commit (rule moved, behaviour unchanged) | build `vst3effect clapeffect SidechainInputsTest` + `ctest -R SidechainInputsTest` (`build-1.log`, `test-1.log`) | build EXIT=0, test EXIT=0 (1/1) |
| RED - the new test on the buggy rule | `cmake --build build --target SidechainInputsTest -j4` (`build-2.log`), then `ctest -R SidechainInputsTest` (`test-2.log`) | build EXIT=0, test **EXIT=8**; `Actual (dryMixChannels(2, 2, 1)): 2`, `Expected (ch_cnt_t{1}): 1`; `Actual (dryMixChannels(2, 2, 0)): 2`, `Expected (ch_cnt_t{0}): 0` |
| GREEN - the fix | build `vst3effect clapeffect SidechainInputsTest` (`build-3.log`), then `ctest -R SidechainInputsTest` (`test-3.log`) | build EXIT=0, test **EXIT=0** (1/1) |
| 4b reverse - product change reverted | `git diff -- include/SidechainInputs.h > .../logs/fix.patch` then `git apply -R .../logs/fix.patch`; build `SidechainInputsTest` (`build-4.log`); `ctest -R SidechainInputsTest` (`test-4.log`) | diff EXIT=0, reverse EXIT=0, build EXIT=0, test **EXIT=8** (same two assertions) |
| 4b restore - product change reapplied | `git apply .../logs/fix.patch`; build `vst3effect clapeffect SidechainInputsTest` (`build-5.log`); `ctest -R SidechainInputsTest` (`test-5.log`) | apply EXIT=0, build EXIT=0, test **EXIT=0** (1/1) |
| Affected family | `ctest -R 'Sidechain\|Clap\|Vst3' --output-on-failure` (`test-8.log`) | **EXIT=0**, 100% tests passed out of 18 (PhaseDSidechainTest, SidechainInputsTest, ClapBusMapTest, ClapHostTest, ClapTestInstrument{State,EventCount,ActivationCounters}Test, ClapLoaderErrorTest, ClapEffectIntegrationTest, ClapInstrumentViewTest, Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3EditorTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest) |
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

`feature_check.py` ran once, EXIT=0 (`feature-1.log`, RESULT: PASS). `submit_check.py` ran as the completion
check; its table is in `.../logs/submit-1.log` and is answered under `## Self-check`.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: product hunk
   reversed (`fix.patch`, `git apply -R`, EXIT=0), `SidechainInputsTest` rebuilt (`build-4.log`, EXIT=0) and run
   (`test-4.log`) - **EXIT=8**, with `FAIL! : SidechainInputsTest::aMonoMainBusKeepsItsSidechainOutOfTheDryMix()
   Actual (dryMixChannels(2, 2, 1)): 2 / Expected (ch_cnt_t{1}): 1`. Restored: `git apply` (EXIT=0), rebuilt
   (`build-5.log`, EXIT=0), run (`test-5.log`) - **EXIT=0**, 1/1 passed. The refactor commit `4bbcea54b9` exists
   precisely so the reverse-patch step fails on an assertion rather than on a missing symbol: without the fix the
   test compiles and fails, which is what makes it a real RED.
2. **Deterministic.** The test calls one inline function with constant arguments; no clock, no `sleep`, no thread,
   no plug-in load, no file or environment access, so there is nothing to inject or synchronise. The seam is the
   shared function both effect hosts call, which is what the 4b reversal proves: rebuilding only changes that
   function's body.
3. **Fix at the cause and complete.** The dry-mix count is now the main input port's width in both callers
   (`passThrough()` and the processImpl mix loop) of both hosts: `Vst3Effect.cpp:189`/`:237` and
   `ClapEffect.cpp:190`/`:237`. The two `BusLayout::sideChainChannel` producers were checked and agree on the
   meaning of the value (`Vst3BusMap.cpp:47`, `ClapBusMap.cpp:116`): it is where the aux bus starts, so it is also
   the width of the main port in front of it. `ch_cnt_t` is the type that flows into `bufferPtr()`, and the result
   is still bounded by `inputs` and `outputs`, so no index can go out of range. The other effects that compute a
   dry-channel count were checked: `plugins/AuEffect/AuEffect.cpp:153` uses
   `std::min(in.channels(), out.channels())` and its files have no side-chain concept at all
   (`rg sidechain plugins/AuEffect` is empty), so it is not this defect and is left alone.
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

## Other findings

- The aux-only case (`sideChainChannel == 0`, a plug-in whose only active input bus is auxiliary) is fixed by the
  same change rather than by a separate one: it previously copied two channels of the key signal into the dry
  output through `passThrough()` as well. It is asserted in the same test slot.
