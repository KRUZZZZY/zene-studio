ALREADY-FIXED

Cluster `review-893a28421`, finding "Descriptor sorting loses original audio-port mapping"
(`plugins/ClapEffect/ClapBusMap.cpp`, historical line 0). Worktree
`kads-review-893a28421`, branch `bugs/qwen-review-893a28421`, base `fa4a5f2e7`.

## Verdict

The finding describes a real defect — `runChunk` filled `audio_inputs[p]` by the transport's
main-first layout position instead of the plug-in's own port index — but that defect is already
repaired on this branch, and the repair is covered by tests that I watched go RED and GREEN in
this session. No product change was needed; the only change in this commit is one assertion that
pins the conforming-plug-in case the finding asks about ("without changing conforming plugin
audio").

## Evidence (code on this branch)

- `plugins/ClapEffect/ClapBusMap.cpp:92` — `layout.inputPortIndices.push_back(input.pluginIndex);`
  each mapped input records its own position among the plug-in's input ports, independent of the
  main-first ordering the transport uses.
- `plugins/ClapEffect/ClapBusMap.cpp:85` — `layout.pluginInputPorts` counts every input port the
  plug-in reports (dropped and zero-channel ones included), so `audio_inputs` keeps its length.
- `plugins/ClapEffect/ClapHost.cpp:443` —
  `auto& buffer = inBuffers[static_cast<std::size_t>(layout.inputPortIndices[p])];`
  This is the exact line the finding says is wrong: the buffer is now written at the plug-in's
  index, not at the ordered-layout index `p`. The channel pointers still walk the transport order,
  so the main signal sits on the transport's first channels.
- `plugins/ClapEffect/ClapHostLifecycle.cpp:74` —
  `impl.inBuffers.assign(static_cast<std::size_t>(impl.layout.pluginInputPorts), clap_audio_buffer_t{});`
  sized on the main thread in `prepare()`, so a port the channel budget dropped keeps a zeroed
  buffer instead of shifting its neighbours.
- `plugins/ClapEffect/ClapHost.cpp:493` — `request.audio_inputs_count = inBuffers.size()`, i.e. one
  entry per plug-in input port.
- Commits that carry this on the branch (all ancestors of HEAD, checked with
  `git merge-base --is-ancestor`): `9a0d25c23` (index mapping), `ca43ed0c4` (runChunk placement
  test), `5659940b9` (output-side sibling), `57a4ca829` (unreadable port keeps its slot).

## The contract the finding asks about

`build/clap/include/clap/ext/audio-ports.h:27`: "Main port must be at index 0." The finding is
right that a conforming plug-in cannot report its side-chain before its main input. That makes the
main-first ordering step a no-op for conforming plug-ins and the index mapping the identity, so
conforming audio is unchanged by the repair:

- `tests/src/plugins/ClapBusMapTest.cpp:110-111` (`testSideChain`) asserts, for a conforming
  descriptor list (main at index 0, side-chain after it), that `pluginInputPorts == 2` and
  `inputPortIndices == {0, 1}` — the plug-in's index of each mapped port is its own position, so
  buffer placement and channel order are exactly what they were before the fix.
- The defect is only observable when the descriptor order is not conforming, or when an input port
  is unreadable. The fixture reaches those states through `CLAP_TEST_GAIN_LAYOUT`
  (`sidechain-first`, `unreadable-input`, `zero-output`); with the variable unset it behaves as
  before.

## Commands and exit codes (all run in this session)

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| build | `cmake --build build --target ClapBusMapTest ClapHostTest -j4` | `build-1.log` | 0 |
| green baseline | `cd build/tests && ctest -R '^(ClapBusMapTest\|ClapHostTest)$' --output-on-failure` | `test-1.log` | 0 |
| patch capture | `git diff -- plugins/ClapEffect/ClapBusMap.cpp plugins/ClapEffect/ClapHost.cpp > $L/revert-fix.patch` | `revert-fix.patch` | 0 |
| RED build | `cmake --build build --target ClapBusMapTest ClapHostTest -j4` | `build-8-red-patch.log` | 0 |
| RED run | `cd build/tests && ctest -R '^(ClapBusMapTest\|ClapHostTest)$' --output-on-failure` | `test-9-red-patch.log` | **8** |
| restore | `git apply -R $L/revert-fix.patch` | — | 0 |
| GREEN build | `cmake --build build --target ClapBusMapTest ClapHostTest -j4` | `build-9-green-patch.log` | 0 |
| GREEN run | `cd build/tests && ctest -R '^(ClapBusMapTest\|ClapHostTest\|ClapLoaderErrorTest\|ClapEffectIntegrationTest)$' --output-on-failure` | `test-10-green-patch.log` | **0** |
| family | `cd build/tests && ctest -R '^(ClapLoaderErrorTest\|ClapEffectIntegrationTest)$'` | `test-5-family.log` | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 |

`$L` = `/home/kruzzzzy/.config/kads/zene/state/runs/review-893a28421.r1/logs`.

RED output (revert of the two product lines above, test file kept):

```
FAIL!  : lmms::clap::ClapBusMapTest::testInputIndicesFollowThePluginsOwnPorts() Compared values are not the same
   Actual   (layout.inputPortIndices) : std::vector(0, 1)
FAIL!  : lmms::clap::ClapHostTest::testAnUnreadableInputPortKeepsItsNeighboursIndex() ... (the main input did not reach the plug-in's main port: the unreadable port shifted the indices)
FAIL!  : lmms::clap::ClapHostTest::testASideChainFirstPluginReadsItsOwnMainInput() ... (the plug-in's main input carried the side-chain: runChunk placed the buffers out of port order)
```

Two narrower single-line reverts were run as well, each RED EXIT=8:
`ClapHost.cpp:443` reverted to `inBuffers[p]` (log `test-2-red.log`) and `ClapBusMap.cpp:92`
reverted to the ordered position (log `test-7-red-map.log`). That pins both halves of the repair
independently.

Gates table (`gates-1.log`, unpiped exit 0):

```
PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace / no-tautology / doc-figures
PASS complexity (no new item in your files) / file-length (no new item in your files)
RESULT: PASS
```

## Test matrix

- Normal path (conforming plug-in, main at index 0): `ClapBusMapTest::testSideChain` — identity
  index mapping and unchanged channel order.
- Failure path (side-chain reported before main): `ClapBusMapTest::testSideChainReportedBeforeTheMainInput`,
  `ClapHostTest::testASideChainFirstPluginReadsItsOwnMainInput` (main at 0.5, side-chain at 0.25;
  a swapped buffer reads back 0.25).
- Unreadable/unusable port: `ClapHostTest::testAnUnreadableInputPortKeepsItsNeighboursIndex`.
- Exhaustion (channel budget, `MaxChannelsPerAudioBuffer`): `ClapBusMapTest::testChannelBudgetIsClamped`;
  zero-channel output port: `ClapHostTest::testAZeroChannelOutputKeepsTheIndexOfLaterPorts`.
- Reconfiguration: `inBuffers`/`outBuffers` are sized only in `prepare()` (main thread,
  `ClapHostLifecycle.cpp:74-75`); the whole `ClapHostTest` binary (prepare/activate/process/editor
  life cycle cases) passed, EXIT=0.
- Audio thread: `runChunk` only indexes vectors sized before activation — no allocation, lock,
  logging or Qt call is added or present on that path.
- Platform: the change is platform-neutral C++; `ClapHostTest`'s window-system branches ran on this
  box (offscreen).

## Self-check

- **Regression test fails with the product change reverted and passes with it.** Yes: RED EXIT=8
  (`test-9-red-patch.log`, plus the two single-line reverts `test-2-red.log`, `test-7-red-map.log`),
  GREEN EXIT=0 (`test-10-green-patch.log`).
- **The test is deterministic.** Yes: no timing, sleeps or threads. `ClapBusMapTest` is pure data
  (descriptor vectors in, layout out). `ClapHostTest` loads the local fixture
  `tests/data/clap-test-plugin`, selects the layout with the environment variable
  `CLAP_TEST_GAIN_LAYOUT`, and compares sample values (0.5 vs 0.25) — injected values, not a clock.
- **The fix is at the cause and complete; every caller and sibling checked.** The cause is the port
  index, and the three places that must agree all do: `ClapBusMap.cpp:85,92` (map), `ClapHost.cpp:443`
  (placement), `ClapHostLifecycle.cpp:74` (buffer array length). The only consumer of
  `inputPortIndices` is `ClapHost::runChunk`; `inputPortChannels` still drives channel placement and
  is unchanged. The output side has the same three-way shape (`outputPortIndices` at
  `ClapHost.cpp:463`, `pluginOutputPorts`, `ClapBusMap.cpp:111`) and is covered by its own test.
- **No collateral damage.** No product line changed in this commit, so old projects, saved state and
  the audio path are byte-for-byte what the branch already tested. For a conforming plug-in the
  mapping is the identity (`testSideChain`, added assertion) and `audio_inputs` keeps one entry per
  reported input port, which is CLAP's own length rule. No limits were added and no check rejects
  any caller.
- **ALREADY-FIXED code reference.** `plugins/ClapEffect/ClapHost.cpp:443` (the line the finding
  names as wrong, now indexing by the plug-in's own port), with `ClapBusMap.cpp:92`,
  `ClapHostLifecycle.cpp:74` and the SDK contract at `build/clap/include/clap/ext/audio-ports.h:27`.
- **Undo and control commands.** Untouched: no command, no undo record, no `ControlCommandsSnapshot`
  change.

## Other findings

- `build/tests` had no `ClapLoaderErrorTest`/`ClapEffectIntegrationTest` binaries in this worktree,
  so the first family run reported `Not Run` and EXIT=8 (`test-4-family.log`) although nothing had
  regressed; after `cmake --build build --target ClapLoaderErrorTest ClapEffectIntegrationTest -j4`
  (EXIT=0, `build-4-family.log`) the same run passed (EXIT=0, `test-5-family.log`). Unbuilt test
  binaries look like a regression on first read.
