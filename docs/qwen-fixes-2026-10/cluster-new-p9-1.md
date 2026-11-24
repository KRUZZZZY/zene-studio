ALREADY-FIXED

# cluster new-p9-1 - a mono sidechain can leak into the dry signal

Verdict: **ALREADY-FIXED on this branch** by commit `a8a70c462c` ("fix(cluster new-p8-263): a hosted
effect's dry mix stops at its main input bus"). This job adds the missing end-to-end regression test
for the finding's exact configuration (mono main port + mono sidechain) and nothing else; no product
file is changed, because the product behaviour is already correct.

## Evidence

The finding names `plugins/ClapEffect/ClapEffect.cpp:187` with the pre-fix expression
`std::max(2, sideChainChannel)`. Line 187 is now the `passThrough()` helper, and it no longer contains
that expression:

- `plugins/ClapEffect/ClapEffect.cpp:190` - `const auto dry = dryMixChannels(in.channels(), out.channels(), sideChainChannel);`
  (the plug-in-error / not-prepared path).
- `plugins/ClapEffect/ClapEffect.cpp:237` - the same rule for the wet/dry mix loop, so a fully dry
  mix covers the main input bus only.
- `include/SidechainInputs.h:68-72` - the rule itself:

  ```cpp
  inline auto dryMixChannels(ch_cnt_t inputs, ch_cnt_t outputs, int mainInputChannels) -> ch_cnt_t
  {
      const auto main = mainInputChannels < 0 ? inputs : static_cast<ch_cnt_t>(mainInputChannels);
      return std::min({inputs, outputs, main});
  }
  ```

  `mainInputChannels` is `BusLayout::sideChainChannel`, i.e. where the auxiliary bus starts. With a
  mono main port and a mono key that is 1, so the mix covers one channel and channel 1 - the key - is
  never an output's dry source. The pre-fix `std::max(2, 1)` = 2 covered the key; the RED run below
  shows exactly that (`dryMixChannels(2, 2, 1)` returning 2).

Producers of the field (checked, both agree):

- `plugins/ClapEffect/ClapBusMap.cpp:115-116` - `layout.hasSideChain = mainChannels < layout.inputs;`
  / `layout.sideChainChannel = layout.hasSideChain ? mainChannels : -1;`
- `plugins/Vst3Effect/Vst3BusMap.cpp:43-47` - set to `layout.inputs` when the aux bus is seen, and
  left `-1` otherwise.

Consumers (every sibling checked, all use the one rule):

- `plugins/ClapEffect/ClapEffect.cpp:190` (pass-through) and `:237` (mix)
- `plugins/Vst3Effect/Vst3Effect.cpp:203` (pass-through), `:219` and `:228` (the two error paths),
  `:251` (mix)

`grep std::max(2` over the worktree finds no sidechain use of the buggy expression left; the only
survivors are unrelated (`Vibed/VibratingString.cpp:46`, `RenderManager.cpp:293`, the vendored Calf
sources).

Test coverage that already existed (both pass, see `test-4.log`):

- `tests/src/core/SidechainInputsTest.cpp:67-72` - `aMonoMainBusKeepsItsSidechainOutOfTheDryMix`
- `tests/src/plugins/Vst3EffectIntegrationTest.cpp:252-257` - a stereo-output effect at a half-dry
  mix whose second output must carry the wet value, not the key.

## The regression test added here

`tests/src/plugins/ClapBusMapTest.cpp`: `testAMonoMainPortKeepsItsKeyOutOfTheDryMix`. It drives the
finding's configuration through **both** halves of the contract - the real producer (`mapPorts`, the
function that decides where the key starts) and the consumer (`dryMixChannels`):

```cpp
const auto layout = mapPorts({
    makePort(PortDirection::Input, 1, true),  // the main input, one channel
    makePort(PortDirection::Input, 1),        // the side-chain, one channel
    makePort(PortDirection::Output, 2, true),
});
QCOMPARE(layout.sideChainChannel, 1);  // the key starts right after the mono main channel
QCOMPARE(dryMixChannels(layout.inputs, layout.outputs, layout.sideChainChannel), ch_cnt_t{1});
QCOMPARE(dryMixChannels(layout.inputs, layout.outputs, -1), ch_cnt_t{2}); // no key: the whole port
```

`ClapBusMapTest` had no include path to the generated `lmms_export.h` / `lmmsconfig.h`, which
`SidechainInputs.h` reaches through `AudioBuffer.h`; `tests/CMakeLists.txt` now lists
`${CMAKE_BINARY_DIR}/src` and `${CMAKE_BINARY_DIR}/include` for that target (the same two lines the
neighbouring `WasmWorkerPoolTest` target already carries). Nothing else in the build changes. The
first build without them failed (`build-5.log`, `fatal error: lmms_export.h: No such file or
directory`, EXIT=2) and the build with them succeeded (`build-6.log`, EXIT=0).

## Test matrix

| path | how it is covered | result |
| --- | --- | --- |
| normal | mono main port + mono key + stereo out: `sideChainChannel` 1, dry mix 1 channel (`testAMonoMainPortKeepsItsKeyOutOfTheDryMix`) | PASS (`test-7.log`) |
| normal, no key | the same port with `sideChainChannel` -1 covers the whole input port (same test, second assert) | PASS |
| failure path | plug-in error / not prepared goes through `passThrough()`, which uses the same rule (`ClapEffect.cpp:190`, `:219`, `:228`); `ClapEffectIntegrationTest::testAProcessingErrorFallsBackToDryAudio` and `Vst3EffectIntegrationTest` pass | PASS (`test-7.log`) |
| exhaustion | a key wider than the channel budget: `ClapBusMapTest::testChannelBudgetIsClamped` and the mono-key cases stay green; `dryMixChannels` is bounded by `min(inputs, outputs, main)` | PASS (`test-7.log`) |
| reconfiguration | `sideChainChannel` is re-read every block from `m_plugin.busLayout()`, so a re-prepared layout takes effect on the next period; `ClapEffectIntegrationTest` re-prepare tests pass | PASS (`test-7.log`) |
| audio thread | the rule is integer arithmetic (`std::min` over its three arguments); no allocation, lock, I/O or logging was added anywhere by this job - the commit touches no audio-path file | by inspection of `include/SidechainInputs.h:68-72` |
| platforms | the test uses QtTest and plain C++ only (no Qt-6-only API, no absolute POSIX path); the rule is a `constexpr`-friendly inline function in a portable header | by inspection |

## Commands and exit codes (all logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-1.r1/logs`)

| step | command | EXIT |
| --- | --- | --- |
| fix commit on this branch | `git merge-base --is-ancestor a8a70c462c HEAD` | 0 |
| build at HEAD | `cmake --build build --target SidechainInputsTest -j4 > logs/build-1.log 2>&1` | 0 (`build-1.exit`) |
| family at HEAD (three targets not yet built) | `cd build/tests && ctest -R 'SidechainInputsTest|ClapEffectIntegrationTest|Vst3EffectIntegrationTest|ClapHostTest' --output-on-failure > logs/test-1.log 2>&1` | 8 - SidechainInputsTest passed, the other three `Not Run` |
| build the siblings | `cmake --build build --target ClapHostTest ClapEffectIntegrationTest Vst3EffectIntegrationTest -j4 > logs/build-2.log 2>&1` | 0 (`build-2.exit`) |
| family at HEAD | `cd build/tests && ctest -R '...' --output-on-failure > logs/test-2.log 2>&1` | 0 (4/4) |
| RED, existing test | `git checkout -- include/SidechainInputs.h` after restoring `std::max(2, mainInputChannels)`; `cmake --build build --target SidechainInputsTest -j4 > logs/build-3.log 2>&1` | 0 (`build-3.exit`) |
| RED, existing test | `cd build/tests && ctest -R 'SidechainInputsTest' --output-on-failure > logs/test-3.log 2>&1` | **8** - `FAIL! : ...aMonoMainBusKeepsItsSidechainOutOfTheDryMix() Actual (dryMixChannels(2, 2, 1)): 2 Expected: 1` |
| GREEN again | `git checkout -- include/SidechainInputs.h`; `cmake --build build --target zene SidechainInputsTest ClapHostTest ClapEffectIntegrationTest Vst3EffectIntegrationTest -j4 > logs/build-4.log 2>&1`; `cd build/tests && ctest -R '...' > logs/test-4.log 2>&1` | 0; 0 (4/4) |
| new test, first build | `cmake --build build --target ClapBusMapTest -j4 > logs/build-5.log 2>&1` | 2 - `fatal error: lmms_export.h: No such file or directory` |
| new test, with include dirs | `cmake --build build --target ClapBusMapTest -j4 > logs/build-6.log 2>&1`; `cd build/tests && ctest -R 'ClapBusMapTest|SidechainInputsTest' > logs/test-5.log 2>&1` | 0 (`build-6.exit`); 0 (`test-5.exit`) |
| 4b, product change reversed | `git show a8a70c462c -- include/SidechainInputs.h > logs/fix.patch` | 0 (49 lines) |
| 4b, reverse apply | `git apply -R logs/fix.patch` | 0 |
| 4b, RED | `cmake --build build --target ClapBusMapTest SidechainInputsTest -j4 > logs/build-7.log 2>&1` | 0 (`build-7.exit`) |
| 4b, RED | `cd build/tests && ctest -R 'ClapBusMapTest|SidechainInputsTest' > logs/test-6.log 2>&1` | **8** - `FAIL! : ClapBusMapTest::testAMonoMainPortKeepsItsKeyOutOfTheDryMix() Actual (dryMixChannels(layout.inputs, layout.outputs, layout.sideChainChannel)): 2 Expected: 1` (and the two older SidechainInputsTest failures) |
| 4b, apply forward | `git apply logs/fix.patch` | 0 |
| 4b, GREEN | `cmake --build build --target zene ClapBusMapTest SidechainInputsTest ClapHostTest ClapEffectIntegrationTest Vst3EffectIntegrationTest -j4 > logs/build-8.log 2>&1` | 0 (`build-8.exit`) |
| 4b, GREEN | `cd build/tests && ctest -R 'ClapBusMapTest|SidechainInputsTest|ClapHostTest|ClapEffectIntegrationTest|Vst3EffectIntegrationTest' --output-on-failure > logs/test-7.log 2>&1` | 0 (5/5, `test-7.exit`) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-2.log 2>&1` | 0 (`gates-2.exit`), `RESULT: PASS`, 8/8 PASS |

`fix.patch` is the product hunk only (`include/SidechainInputs.h`), taken from the commit that fixed
it, because this job's own diff contains no product file. `git diff -- include/SidechainInputs.h`
after the reverse apply showed the restored `std::max(2, mainInputChannels)` rule and its stale
comment, which is what the RED runs exercised.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   Reversed: `git apply -R logs/fix.patch` EXIT=0, build `build-7.exit` EXIT=0, test `test-6.exit`
   EXIT=**8** with `testAMonoMainPortKeepsItsKeyOutOfTheDryMix` reporting dry mix 2 instead of 1.
   Restored: `git apply logs/fix.patch` EXIT=0, build `build-8.exit` EXIT=0, test `test-7.exit`
   EXIT=0 (5/5). The older `SidechainInputsTest::aMonoMainBusKeepsItsSidechainOutOfTheDryMix` behaves
   the same (`test-3.log` EXIT=8 without the rule, `test-4.log` EXIT=0 with it).
2. **Deterministic.** The test is integer arithmetic over `mapPorts` of three literal port
   descriptors and `dryMixChannels` of three integers: no timing, no sleeps, no threads, no injected
   clock, no plug-in load. No state is carried between the assertions.
3. **At the cause and complete.** The cause was one expression in the one function both hosts call;
   it now lives in `include/SidechainInputs.h:68-72` and its four call sites in `Vst3Effect.cpp`
   plus two in `ClapEffect.cpp` were each read this session and all pass
   `busLayout().sideChainChannel`. Both producers of that field were read
   (`ClapBusMap.cpp:115-116`, `Vst3BusMap.cpp:43-47`); the CLAP one reports `-1` with no key, the
   VST3 one leaves the `-1` default. The new test now pins producer and consumer together so a future
   change to either half breaks it.
4. **No collateral damage.** This commit changes no product file at all: `git diff --stat` is
   `tests/CMakeLists.txt` (two include directories for one test target) and
   `tests/src/plugins/ClapBusMapTest.cpp` (one test). No limit or check was added to product code, so
   no other caller can be rejected; saved projects, effect attributes and the wet/dry semantics are
   untouched; the mid-dry behaviour other tests assert (`ClapEffectIntegrationTest`,
   `Vst3EffectIntegrationTest`) still passes (`test-7.log`).
5. **ALREADY-FIXED evidence.** `include/SidechainInputs.h:68-72` (the rule),
   `plugins/ClapEffect/ClapEffect.cpp:190` and `:237` (its consumers in the file the finding names,
   whose line 187 no longer holds `std::max(2, sideChainChannel)`), fix commit `a8a70c462c` an
   ancestor of HEAD (`git merge-base --is-ancestor` EXIT=0), and the RED/GREEN runs above showing the
   old expression reproduces the reported leak.
6. **Audio-thread.** This job adds nothing that runs on the audio thread: its diff is a test and a
   CMake include path. The rule itself (`include/SidechainInputs.h:68-72`) stays branch-free integer
   arithmetic and allocates nothing; the test calls it once on the test thread.
7. **Sibling commands.** No `ControlCommands*`, `ControlReversibilityTable` or `ControlRegistry` file
   is touched, no command is added, so the command snapshot canary is unmoved.
8. **Undo.** No undo path, model setter or command handler is touched: nothing to make one honest
   step.

## Other findings

- The `ClapEffectIntegrationTest` fixture (`tests/data/clap-test-plugin/clap-test-gain.c`) has no
  layout with a mono main input port (`mono-sidechain` is a stereo main plus a mono key), so the
  finding's configuration cannot be driven end to end through a real plug-in today. The unit level
  (`ClapBusMapTest` + `SidechainInputsTest`) is where it is pinned. Adding such a fixture layout is a
  separate, larger change and was not made here.
