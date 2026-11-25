FIXED

# Cluster new-p14-187 - the wide surround master output bypassed the engine master gain

## Decision: REAL

A surround master's speakers after its pair were placed on the hardware outputs at the master fader
only, while the pair those speakers belong with carries the engine's master gain as well:

- Before the fix: `src/core/MixerSurroundMix.cpp:393` (this branch's pre-fix line) read
  `placeSides(Engine::audioEngine()->hardwareOutputs(), s, main.start, fader, n)` - `fader` is the
  master volume model read at `:383`, and nothing else is applied.
- The pair gets the gain afterwards, outside the mixer: `Mixer::masterMix()` calls
  `surround::masterOutput()` (`src/core/Mixer.cpp:2568`), then `AudioEngine::renderStageMix()` does
  `MixHelpers::multiply(m_outputBufferWrite.get(), m_masterGain, m_framesPerPeriod)`
  (`src/core/AudioEngine.cpp:1186`). That post-gain buffer frame is the `master` frame the device feed
  hands to the bus (`include/AudioEngine.h:681`, `:711-721`), and `HardwareOutputBus::sample()` adds it to
  the channels the master is placed on (`include/HardwareOutputBus.h:87-89`,
  `src/core/HardwareOutputBus.cpp:103-111`). No other stage multiplies the bus planes by the master gain -
  the bus is what the device is fed.
- So with the master on 1/2 and the engine master gain at 0.25, outputs 1/2 were a quarter of level and
  outputs 3..6 stayed at full level. Measured, not argued: `test-9-proof-red.log`
  (`output 3: 0.3 at unity, 0.3 at a quarter`, same for 4/5/6).
- The intended contract is written for the sibling path that already did it: the export tap applies the
  fader and then the master gain (`src/core/MixerSurroundMix.cpp:199-209`), and
  `docs/SURROUND-EXPORT.md:27` states the rule - "the master after its fader and the engine's master gain,
  the same gains the pair gets". `include/MixerSurround.h:278-280` says the master's side channels go
  "onto the hardware outputs after its pair", i.e. the same signal as the pair.

## Fix at the cause

- `src/core/MixerSurroundMix.cpp:159-167`: `placeSides()` takes a `scale` (default `1.f`) and multiplies it
  into the gain it hands to the bus, keeping the fader's sample-exact buffer as the per-frame factor.
- `src/core/MixerSurroundMix.cpp:396-402`: the master's wide branch passes `engine->masterGain()`, the same
  read `tapSides()` makes for the export at `:202`.
- `src/core/HardwareOutputBus.cpp:92`: `addPlane()` applies its scalar `gain` *and*, when given, the
  per-frame `gains[f]` (before, a table replaced the scalar). That is what lets one number - the master
  gain - ride on top of an automated master fader. Contract updated at
  `include/HardwareOutputBus.h:78-81`.
- `directOut()` (`src/core/MixerSurroundMix.cpp:429`) calls `placeSides()` without a scale, so a channel's
  direct out stays a pre-master tap. Covered by its own test slot.

## Test matrix

`tests/src/core/WideDiscreteBusTest.cpp` (the wide-bus test; own 128-output device, `period()` and
`outputLevel()` already there). `tests/src/core/SurroundTest.cpp` was the first home of these slots and
was reverted: it sat at exactly the 500 code-line limit (`tests/file-length-gate.sh`), and the gate
reported `REGRESSION: new file over 500 lines` (`gates-1.log`) until the slots moved.

| Slot | Path it covers | Expectation |
| --- | --- | --- |
| `theWideMasterOutputFollowsTheMasterGain` | normal path: a 5.1 master on 1/2, engine gain 1 then 0.25 | outputs 3-6 at `level * 0.25`; outputs 7/8 silent |
| `aChannelDirectOutIgnoresTheMasterGain` | sibling: a 5.1 channel's direct out on 1/2, gain 1 then 0.25 | unchanged (ratio 1) - the fix did not spread to direct outs |
| `theWideMasterMixAllocatesNothing` (existing) | audio thread: the wide master path | 0 allocations |
| `distinctChannelsCrossADiscrete64BusInOrder`, `wideDiscreteLayoutsSurviveASaveAndLoad` (existing) | routing and saved data | unchanged |
| `SurroundTest`, `HardwareOutputBusTest`, `SurroundExportTest`, `SurroundDevicePathTest`, `WidePinRoutingTest`, `HardwareIoTest`, `MixerListenTest`, `MixerStripTest` | family: the other bus, master and direct-out users | 9/9 pass |

Failure, exhaustion and reconfiguration: the same slot covers the failure state (gain at 0.25), the
silence beyond the master's six speakers (exhaustion of the layout) and a fresh mixer for every slot
(`clearedMixer()`, `mixer.clear()` + `createChannel()`), which is the reconfiguration path. No new
platform branch was added; the change is plain C++20 and both build configurations use the same code.

## Verification: commands and exit codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-187.r1/logs`. Builds ran from the
worktree root with at most `-j4`, tests from `build/tests`; `QT_QPA_PLATFORM=offscreen` is set by the
harness and `LMMS_PLUGIN_DIR` was never set.

| Stage | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| First RED build (test in `SurroundTest.cpp`) | `cmake --build build --target SurroundTest -j4` | 0 | `build-1-red.log` |
| First RED run | `ctest -R '^SurroundTest$' --output-on-failure` | 8 | `test-1-red.log`; `0.3 at unity, 0.3 at a quarter` |
| Green build with the fix | `cmake --build build --target zene SurroundTest HardwareOutputBusTest -j4` | 0 | `build-2-green.log` |
| Green run, first test version | `ctest -R '^(SurroundTest\|HardwareOutputBusTest)$' --output-on-failure` | 8 | `test-2-green.log` - the test read the bus one period late; corrected (see below) |
| Corrected slot, isolated | `./SurroundTest theWideMasterOutputFollowsTheMasterGain` | 0 | `test-4-unity.log` |
| Build the other family targets | `cmake --build build --target SurroundExportTest SurroundDevicePathTest WideDiscreteBusTest WidePinRoutingTest HardwareIoTest MixerListenTest MixerStripTest -j4` | 0 | `build-5-family.log` |
| Family with the fix (first home) | `ctest -R '^(SurroundTest\|HardwareOutputBusTest\|SurroundExportTest\|SurroundDevicePathTest\|WideDiscreteBusTest\|WidePinRoutingTest\|HardwareIoTest\|MixerListenTest\|MixerStripTest)$' --output-on-failure` | 0 | `test-6-green-family.log`; 9/9 |
| Gates, first home | `python3 .../feature_check.py <worktree>` | 1 | `gates-1.log`; file-length red: `SurroundTest.cpp (593)` code lines |
| Build after moving the slots | `cmake --build build --target WideDiscreteBusTest SurroundTest -j4` | 0 | `build-8-test-moved.log` |
| Both slots, moved and fixed | `ctest -R '^(WideDiscreteBusTest\|SurroundTest)$' --output-on-failure` | 0 | `test-8-green-moved.log`; 2/2 |
| **RED proof: reverse the product patch** | `git diff -- src/core/MixerSurroundMix.cpp src/core/HardwareOutputBus.cpp include/HardwareOutputBus.h > .../logs/fix.patch` then `git apply -R .../logs/fix-code.patch` | 0, 0 | `fix.patch`, `fix-code.patch`, `proof-3-reverse-code.log` |
| Rebuild without the fix | `cmake --build build --target zene WideDiscreteBusTest -j4` | 0 | `build-9-proof-red.log` |
| Test without the fix | `ctest -R '^WideDiscreteBusTest$' --output-on-failure` | 8 | `test-9-proof-red.log`; `theWideMasterOutputFollowsTheMasterGain` failed, 7 other slots passed |
| Restore the product patch | `git apply .../logs/fix-code.patch` | 0 | `proof-4-restore-code.log` |
| Rebuild with the fix | `cmake --build build --target zene WideDiscreteBusTest -j4` | 0 | `build-10-proof-green.log` |
| Test with the fix | `ctest -R '^WideDiscreteBusTest$' --output-on-failure` | 0 | `test-10-proof-green.log`; 1/1 |
| Final family rebuild | `cmake --build build --target SurroundTest HardwareOutputBusTest SurroundExportTest SurroundDevicePathTest WidePinRoutingTest HardwareIoTest MixerListenTest MixerStripTest -j4` | 0 | `build-11-family-final.log` |
| Final family run | `ctest -R '^(SurroundTest\|HardwareOutputBusTest\|SurroundExportTest\|SurroundDevicePathTest\|WideDiscreteBusTest\|WidePinRoutingTest\|HardwareIoTest\|MixerListenTest\|MixerStripTest)$' --output-on-failure` | 0 | `test-11-family-final.log`; 9/9 |
| Gates | `python3 .../feature_check.py <worktree>` | 0 | `gates-2.log`; `RESULT: PASS`, complexity and file-length "no new item in your files" |

`fix.patch` is the whole product diff, as step 4b asks. The reverse cycle above ran on
`fix-code.patch`, the same diff minus `include/HardwareOutputBus.h`, whose only change in this commit is
one documentation comment with no code: reverting it recompiles every target that includes the header
(a 300-file rebuild) and cannot change behaviour. The behaviour under test lives entirely in the two
`.cpp` files, and both rounds above are the same source tree otherwise.

## Earlier test correction (why the first green run failed)

The first version of `theWideMasterOutputFollowsTheMasterGain` measured the unity period's outputs after
the quiet period had already mixed and swapped the bus, so both readings came from the same period
(`test-2-green.log`: `0.075 at unity, 0.075 at a quarter`). The corrected slot reads the bus immediately
after each period (`outputsOf()`), which is the one-period contract the bus documents
(`include/HardwareOutputBus.h:47-49`). The RED run with the corrected slot shows the real difference
(`test-9-proof-red.log`: `0.3 at unity, 0.3 at a quarter`).

## Callers and siblings checked

- `placeSides()` callers: `masterOutput()` (`src/core/MixerSurroundMix.cpp:401`, now scaled) and
  `directOut()` (`:429`, default scale - unchanged by design, and asserted by its own slot).
- `HardwareOutputBus::addPlane()` callers: `placeSides()` only in product code; the three calls in
  `tests/src/core/HardwareOutputBusTest.cpp` (`:47`, `:48`, `:154`) pass a scalar and no table, so their
  behaviour is bit-identical and they pass (`test-11-family-final.log`).
- `tapSides()` (the export sibling) already applied the fader and the gain; untouched.
- `foldInto()` still writes pre-gain frames into `out`, which the engine scales afterwards; the stereo
  fold path was already right and is untouched.
- `masterPlaysWide()` unchanged: a master whose speakers do not fit the device, a mono device, an export
  and a listen replacing the main output all still take the fold path.

## Realtime

Nothing new on the audio thread: the render path gains one float multiply per side channel in
`placeSides()` and one per frame in `addPlane()`. No allocation, free, lock, wait, log or Qt call, and no
new model read - `masterGain()` is the same plain-float read `tapSides()` already makes
(`src/core/MixerSurroundMix.cpp:202`). `theWideMasterMixAllocatesNothing` still reports 0 allocations
(`test-11-family-final.log`). Local TSan does not run on this box; the race argument is that both
functions run only on the render thread, as their headers state, and the master gain is written by the
control thread exactly as before (no new writer, no new reader class).

## Collateral damage and registration

- No saved data changes: the bus and the master placement are runtime only; the layout save/load slots in
  `WideDiscreteBusTest` and `SurroundTest` pass.
- No command added or changed, so the ControlCommandsSnapshot canary does not move. The test uses two
  existing commands (`mixer.channel_format`, `mixer.hardware_output`).
- No declaration changed; the header edit is the `addPlane()` contract comment, and every target that
  includes it was rebuilt (`build-2-green.log`, `build-7-proof-green.log`, `build-10-proof-green.log`).
- All four changed files are already listed in `tests/fork-sources.txt`
  (`include/HardwareOutputBus.h:1172`, `src/core/HardwareOutputBus.cpp:2081`,
  `src/core/MixerSurroundMix.cpp:2132`, `tests/src/core/WideDiscreteBusTest.cpp:3063`), so no list edit was
  needed and no new product file was added.
- A master gain of 1 (the default) multiplies by 1: an old project renders exactly as before.

## Self-check

Answers to the six items the pre-submit check asks.

1. **The regression test FAILS with the product change reverted and PASSES with it.** Reverted with
   `git apply -R .../logs/fix-code.patch` (exit 0, `proof-3-reverse-code.log`), rebuilt with
   `cmake --build build --target zene WideDiscreteBusTest -j4` (exit 0, `build-9-proof-red.log`), then
   `ctest -R '^WideDiscreteBusTest$' --output-on-failure` **exited 8** (`test-9-proof-red.log`):
   `FAIL! : WideDiscreteBusTest::theWideMasterOutputFollowsTheMasterGain() ... (output 3: 0.3 at unity,
   0.3 at a quarter; ...)` with the other 7 slots passing. Restored with `git apply .../logs/fix-code.patch`
   (exit 0, `proof-4-restore-code.log`), rebuilt (exit 0, `build-10-proof-green.log`), and the same command
   **exited 0** (`test-10-proof-green.log`, 1/1). `fix.patch` holds the whole product diff; the reverse
   cycle ran on `fix-code.patch`, that diff minus the comment-only header edit, for the reason given under
   "Verification".
2. **Deterministic: no wall clock, sleep, thread or race.** The values are injected, not observed: the
   side planes hold a constant `0.1f * (c + 1)` per channel (`sidesPeriod()`), the engine gain is set to a
   constant 1.0 and then 0.25 through the seam `AudioEngine::setMasterGain()`, and the period is driven
   directly - `mixer.prepareMasterMix()`, `mixer.masterMix()`, `HardwareOutputBus::swap()` - exactly as the
   other slots of this test do. The only read is `HardwareOutputBus::sample()`. Two consecutive runs of
   the fixed tree printed the same constants (`test-8-green-moved.log`, `test-10-proof-green.log`), and every red run printed the
   same failure, `0.3 at unity, 0.3 at a quarter` (`test-1-red.log`, `test-7-proof-red.log`,
   `test-9-proof-red.log`).
3. **Fix at the cause and complete.** Every caller and sibling is listed under "Callers and siblings
   checked": `placeSides()` has two callers (the master's wide branch, now scaled, and `directOut()`,
   unscaled by design and asserted), `addPlane()` has `placeSides()` plus three scalar-only test calls that
   stay bit-identical, and `tapSides()`, `foldInto()` and `masterPlaysWide()` are untouched. The family's
   9 test binaries pass (`test-11-family-final.log`, exit 0).
4. **No collateral damage.** The default master gain of 1 multiplies by 1, so a project that never lowered
   it renders exactly as before; no limit or check was added, so no caller can be newly rejected. Nothing
   in the bus or the placement is saved (both are per-period runtime state), and the save/load slots
   (`wideDiscreteLayoutsSurviveASaveAndLoad`, `everythingSurvivesASaveAndLoad`) pass
   (`test-11-family-final.log`, exit 0).
5. **ALREADY-FIXED / FALSE-POSITIVE:** not this verdict. The verdict is FIXED; the REAL evidence is the
   pre-fix `src/core/MixerSurroundMix.cpp:393` against `src/core/AudioEngine.cpp:1186` and the device feed
   at `include/AudioEngine.h:711-721`, measured in `test-9-proof-red.log`.
6. **Sibling commands in the command files I changed:** none - this commit changes no command file.
   `git show --name-only --format= HEAD` lists exactly `docs/qwen-fixes-2026-10/cluster-new-p14-187.md`,
   `include/HardwareOutputBus.h`, `src/core/HardwareOutputBus.cpp`, `src/core/MixerSurroundMix.cpp` and
   `tests/src/core/WideDiscreteBusTest.cpp`. The sibling list the check prints (`app.*`, `arrangement.*`, ...)
   comes from the branch's whole diff since its base - other clusters' committed work - not from this fix.
   No command schema, registry entry or snapshot file is touched here, so `app.about`, `arrangement.fit_tracks`
   and the rest keep their behaviour, and the ControlCommandsSnapshot canary does not move.

Realtime and registration, for completeness: the render path gains one float multiply per side channel
(`placeSides()`) and one per frame (`addPlane()`), with no allocation, free, lock, wait, log or Qt call,
and `theWideMasterMixAllocatesNothing` still reports 0 allocations. All four changed source files are
already in `tests/fork-sources.txt`, so no list edit was needed. Gates: `gates-2.log`, exit 0, `RESULT:
PASS`.

Pre-submit completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p14-187 new-p14-187`
exited 0 with `RESULT: PASS - submit` (`submit-check-1.log`).

## Other findings

- `tests/src/core/SurroundTest.cpp` sits at exactly 500 code lines, so it has no headroom for another
  slot; the file-length ratchet will reject any further growth there. Not fixed here.
- The wide master path's cue sends and direct outs are pre-master-gain by design (they are taps of a
  channel, not of the master mix), so they were deliberately left alone; only the master's own speakers
  follow the master gain.
