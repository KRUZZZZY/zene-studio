FIXED

# Cluster new-p10-256 - surround side channels bypassed the crossfader

## Decision: REAL

The finding is real, and the gap is one stage earlier than the patrol's line numbers suggest: the
crossfade never reached the side planes at all, because `applyCrossfade()` in `src/core/MixerStrip.cpp`
scaled only the stereo pair.

- The stereo pair's crossfade: `mixerstrip::applyCrossfade()`, pre-fix `src/core/MixerStrip.cpp:167-182`,
  multiplied `channel.m_buffer[f][0]` and `channel.m_buffer[f][1]` and nothing else. The pair is the only
  buffer it touched; `m_surround` is a separate side buffer (`include/MixerSurround.h:219-220` `plane()`
  reads `m_side`, `:245` the storage).
- The side planes' own stage: `surround::processChannel()` (`src/core/MixerSurroundMix.cpp:337-365`) applied
  the trim (`:341-342`), the VCA gain (`:342`) and the pan balance (`:345-364`) - and no crossfade. Its
  contract said so: `include/MixerSurround.h:58-59` (pre-fix) listed what the sides carry - "the channel's
  trim, VCA gain, its pan as a balance on each left/right pair, its fader and mute".
- The render order is fixed and both stages run on the channel's worker inside `MixerChannel::processInserts`:
  `mixerstrip::applyOutput()` at `src/core/Mixer.cpp:840` (which calls `applyCrossfade`), the pan stage at
  `:841-851`, then `surround::processChannel()` at `src/core/Mixer.cpp:855`. So the gap was structural:
  every channel stage that scales the pair has a side-plane counterpart (trim: `MixerStrip.cpp:55-62` for the
  pair, `MixerSurroundMix.cpp:341-342` for the sides; VCA: `Mixer.cpp:816-828` and
  `MixerSurroundMix.cpp:342`; pan: `Mixer.cpp:841-851` and `MixerSurroundMix.cpp:345-364`), and the
  crossfade had none.
- The side planes are read after that stage by everything the finding names: a receiver's worker
  (`surround::addSideChannels`, `src/core/Mixer.cpp:946`), the channel's direct out
  (`surround::directOut` -> `placeSides`, `src/core/HardwareIo.cpp:128`) and the master mix
  (`surround::masterOutput`, `src/core/Mixer.cpp:2568`). None of them applies the crossfade gain again.
- Intended contract: MX-172 is "a channel on the crossfader takes its side's gain"
  (`include/Mixer.h:149` the side model, `include/MixerOptions.h:62-65` the position, `-1` all side A ..
  `+1` all side B), and `include/MixerSurround.h:53-54` calls the side buffer "its other speakers" - the same
  channel output, not a tap before the strip.
- Measured, not argued: with a 5.1 bus on side A and the crossfader at 0.5, `test-1-red.log`
  (`EXIT=8`) reports `side A at half: output 3: 0.3, expected 0.15; output 4: 0.4, expected 0.2;
  output 5: 0.5, expected 0.25; output 6: 0.6, expected 0.3` - the pair halves and the centre, LFE and
  surrounds stay at full level, exactly the reported symptom.

## Fix at the cause

- `src/core/MixerStrip.cpp:183-192`: after the pair, `applyCrossfade()` scales the side planes of the same
  channel when it has any (`s.isSurround() && s.written()`), over `std::min(frames, s.frames())` frames and
  `surround::channelCount(s.format())` channels - the same bounds `processChannel()` uses. One gain, one
  stage, both halves of the channel's output. No new function, no declaration change, no header API change.
- `include/MixerSurround.h:59`: the class comment now lists the crossfader gain among what the sides pass by
  the devices with (comment only; no declaration, signature or type changed).
- The stage stays where it was: it runs before the pan stage, so a surround channel's sides get
  `crossfade * trim * VCA * pan` in the same order the pair gets them.
- Siblings checked and left consistent: `applyOutput()` has one product caller (`Mixer.cpp:840`); no other
  file applies the crossfade (`grep crossfader`/`applyCrossfade` -> `MixerStrip.cpp`, `MixerOptions.cpp`,
  `ControlCommandsMixerMeters.cpp` only). A frozen bus (`busfreeze::substitute`) skips `processInserts`
  altogether, pair and sides alike - unchanged. The master's sides (`masterOutput`, the export tap, the
  direct out) read the planes after this stage, so a crossfaded master fades its sides too, as its pair does.

## Test matrix

`tests/src/core/WideDiscreteBusTest.cpp` - the wide-bus test, which already owns a 128-output device, the
`period()` seam and `outputLevel()`/`outputsOf()`. `tests/src/core/SurroundTest.cpp` is the first home of
surround render slots but is at exactly 500 code lines (measured with `tests/file-length-gate.sh`'s own
counter, `file-length-0.log`: only 4 fork files exceed 500, SurroundTest is not one of them), so a code line
there fails gate 7 - the same reason cluster new-p14-187 moved its slots out of it.

| Slot | Path it covers | Expectation |
| --- | --- | --- |
| `theCrossfaderReachesTheSideChannels` | normal: the channel on neither side, position 0.5 | outputs 1-6 at full level |
| | the bug: side A, position 0.5 | all six at half (pre-fix: 3-6 at full) |
| | far end: side A, position -0.5 | all six at full |
| | other side: side B, position 0.5 | unchanged |
| | exhaustion: side A, position +1 (crossfaded away) | all six silent |
| | reconfiguration: 5.1 -> discrete-6 on side A at 0.5 | all six at half |
| `theCrossfadedMixAllocatesNothing` | audio thread: `masterMix` with a crossfaded 5.1 bus | 0 allocations, centre at half |
| existing slots (`distinctChannelsCrossADiscrete64BusInOrder`, `theWideMasterOutputFollowsTheMasterGain`, `aChannelDirectOutIgnoresTheMasterGain`, save/load, format read-back) | the shipped behaviour of this code | unchanged |

The test helper `sidesPeriod()` was split into `fillSides()` + `period()` (`WideDiscreteBusTest.cpp:145-159`)
so the allocation slot can write the planes inside the piece it measures; the two slots that used it behave
identically. Determinism: the slot drives the existing `period()` seam (`bus().swap()`,
`prepareMasterMix()`, `masterMix()`, `AutomatableModel::incrementPeriodCounter()`) and reads the bus in the
same period; the crossfader is a `std::atomic<float>` read by value (`include/MixerOptions.h:64-65`); the
inputs are exact constants written through `Mixer::mixToChannel`; the master is moved to outputs 127/128 so
nothing else reaches outputs 1-6. No timers, sleeps, threads or wall-clock reads.

## Commands and exit codes

All logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-256.r1/logs`.

| # | Command | Log | Exit |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target WideDiscreteBusTest -j4` (test added, product unchanged) | `build-1-red.log` | 0 |
| 2 | `cd build/tests && ctest -R WideDiscreteBusTest --output-on-failure` | `test-1-red.log` | **8** |
| 3 | `cmake --build build --target zene WideDiscreteBusTest -j4` (fix in) | `build-2-green.log` | 0 |
| 4 | `ctest -R WideDiscreteBusTest --output-on-failure` | `test-2-green.log` | **8** (see below) |
| 5 | `cmake --build build --target WideDiscreteBusTest -j4` (test's exhaustion case corrected) | `build-3-green.log` | 0 |
| 6 | `ctest -R WideDiscreteBusTest --output-on-failure` | `test-3-green.log` | 0 |
| 7 | `git diff -- src/core/MixerStrip.cpp > logs/fix.patch` | - | 0 |
| 8 | `git apply -R logs/fix.patch` | - | 0 |
| 9 | `cmake --build build --target WideDiscreteBusTest -j4` | `build-4-reverted.log` | 0 |
| 10 | `ctest -R WideDiscreteBusTest --output-on-failure` | `test-4-reverted-red.log` | **8** |
| 11 | `git apply logs/fix.patch` | - | 0 |
| 12 | `cmake --build build --target WideDiscreteBusTest -j4` | `build-5-restored.log` | 0 |
| 13 | `ctest -R WideDiscreteBusTest --output-on-failure` | `test-5-restored-green.log` | 0 |
| 14 | `cmake --build build --target SurroundTest SurroundExportTest SurroundDevicePathTest MixerStripTest MixerToolsTest HardwareIoTest MixerListenTest ConsoleStripTest WidePinRoutingTest MixerChannelPanTest -j4` | `build-6-family.log` | 0 |
| 15 | `ctest -R 'SurroundTest\|SurroundExportTest\|SurroundDevicePathTest\|MixerStripTest\|MixerToolsTest\|HardwareIoTest\|MixerListenTest\|ConsoleStripTest\|WidePinRoutingTest\|MixerChannelPanTest\|WideDiscreteBusTest' --output-on-failure` | `test-6-family.log` | 0 (11/11) |
| 16 | `cmake --build build --target zene -j4` | `build-7-zene.log` | 0 |
| 17 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 |

Step 4 needs a word: the fix was already in and the sides already faded (outputs 3-6 dropped to half), but
my first version of the slot asserted that position -1 silences side A. `MixerOptions::gains(PanLaw::Balance,
-1)` gives side A 1.0 and side B 0.0 (`src/core/MixerOptions.cpp:66-67`; `include/MixerOptions.h:63` says `-1`
is all side A), so the *test* was wrong, not the fix. The slot now asserts position `+1` for "crossfaded
away" and side B at `0.5` for the untouched side; step 6 passes and step 10 is the proof it still fails
without the product change.

Step 10's failure, quoted from `test-4-reverted-red.log`:

```
FAIL!  : WideDiscreteBusTest::theCrossfaderReachesTheSideChannels() 'seen.isEmpty()' returned FALSE. (side A at half: output 3: 0.3, expected 0.15; output 4: 0.4, expected 0.2; output 5: 0.5, expected 0.25; output 6: 0.6, expected 0.3)
FAIL!  : WideDiscreteBusTest::theCrossfadedMixAllocatesNothing() 'std::abs(centre - masterSideLevel(2) * 0.5f) < 1e-5f' returned FALSE. (0.3)
Totals: 7 passed, 2 failed, 0 skipped, 0 blacklisted
```

`fix.patch` holds only `src/core/MixerStrip.cpp`: that is the whole behavioural change. The
`include/MixerSurround.h` hunk is one comment line, and it was kept out of the patch so the reverse run
recompiles one file instead of the 137 translation units that include the header (`dryrun.log`).

Gate table, quoted from `gates-1.log`:

```
feature-check kads-new-p10-256 (base 3436a808b, 3 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files

- `src/core/MixerStrip.cpp` (product fix)
- `include/MixerSurround.h` (comment: the sides' gains now include the crossfader)
- `tests/src/core/WideDiscreteBusTest.cpp` (the two slots, one helper split, header comment)

All three are already in `tests/fork-sources.txt` (`:2142`, `:1265`, `:3045`), so no registration line is
needed and none was added.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `git apply -R logs/fix.patch` exit 0, rebuild `build-4-reverted.log` exit 0, run
  `test-4-reverted-red.log` exit 8 (`theCrossfaderReachesTheSideChannels` and
  `theCrossfadedMixAllocatesNothing` fail on outputs 3-6 / the centre). Restored: `git apply logs/fix.patch`
  exit 0, rebuild `build-5-restored.log` exit 0, run `test-5-restored-green.log` exit 0.
- **The test is deterministic.** It uses the file's existing `period()` seam (bus swap, `prepareMasterMix`,
  `masterMix`, `incrementPeriodCounter`), exact constants fed through `Mixer::mixToChannel`, and the
  crossfader's atomic value (`include/MixerOptions.h:64`). No sleeps, time reads, threads or ordering races;
  the test drives the periods itself on a device that never starts (`WideOutputs`).
- **The fix is at the cause and complete.** Every stage that scales the pair had a side-plane counterpart
  except the crossfade (trim, VCA, pan listed under Decision); the sides now take the same one gain, at the
  same point in the chain, for named and discrete layouts alike. `applyOutput()`'s only product caller is
  `Mixer.cpp:840`; `applyCrossfade()` has no other caller; `grep applyCrossfade` finds `MixerStrip.cpp` only
  behind that call.
- **No collateral damage.** The crossfade was already a no-op for a channel on neither side
  (`src/core/MixerStrip.cpp:172`) and at full gain (`:177`) - both returns happen before the new block, so
  every project that never touched the crossfader renders exactly as before. No attribute was added or read
  (`mixerstrip::save`/`load` untouched; the `xfade` read stays at `src/core/MixerStrip.cpp:400`); no limit or
  check was added; `frames` longer than the side buffer are capped exactly as `processChannel()` caps them
  (`src/core/MixerSurroundMix.cpp:348`), so no caller is rejected.
- **Realtime.** The new lines run on the channel's worker (`Mixer.cpp:840`): one relaxed atomic already read
  by the stage, `s.frames()`, `surround::channelCount()` (a format switch, `src/core/MixerSurround.cpp:290`),
  pointer arithmetic and multiplies. No allocation, free, lock, wait, log or Qt call; measured by
  `theCrossfadedMixAllocatesNothing` (0 allocations over `masterMix` with a crossfaded 5.1 bus).
- **Undo and commands.** No command, model write or undo step was added: the test drives the existing
  `mixer.crossfader` command, and `MixerToolsTest::settingCrossfaderPositionAndSideUsesOneUndoStep` still
  passes (step 15).
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that
  shows it.** Not applicable - the verdict is FIXED (the finding is REAL). The evidence that it was real
  before the change is under Decision above: `src/core/MixerStrip.cpp:167-182` (pre-fix, the pair only),
  `src/core/MixerSurroundMix.cpp:337-365` (trim, VCA and pan, no crossfade) and the measured failure in
  `test-1-red.log` / `test-4-reverted-red.log`.
- **Sibling commands:** no command file changed, so the sibling set is empty.
- **Portability.** C++20 standard library only (`std::min`, `<algorithm>` already included); no Qt API, no
  platform path, nothing Qt-6-only. Hosted Qt 5.15 builds of this file compile unchanged (the test file uses
  only QtTest/`QStringLiteral`/`QJsonObject`, as it did before).
- **Old projects.** Save and load are untouched; a project's `xfade` attribute is read as before
  (`src/core/MixerStrip.cpp:391`) and a channel without one stays off the crossfader.

## Other findings

- The patrol's location (`src/core/MixerSurroundMix.cpp:234-245`) now falls in `addSideChannels()`, which
  only moves planes; the missing gain was in `MixerStrip.cpp`. The finding text was right about the cause,
  the file pointer was stale.
- `tests/src/core/SurroundTest.cpp` sits at exactly 500 code lines, so any surround-render slot added there
  trips `file-length-gate.sh`. That is a lane-level nuisance (two clusters have now worked around it), not a
  defect fixed here.
- `MixerStrip.cpp`'s EQ, polarity, mono and swap stages still apply to the pair only. That is documented as
  intended (`src/core/MixerSurroundMix.cpp:36-39`), so it was left alone.
