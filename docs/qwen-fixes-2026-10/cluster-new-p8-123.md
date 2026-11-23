FIXED

# Cluster new-p8-123 - a mono device drops the right side of stereo hardware routes

## Decision: REAL

`HardwareOutputBus::add()` wrote a width-2 route's left and right samples into two adjacent planes
(`src/core/HardwareOutputBus.cpp:69-78`), while `monoSample()` read only the first plane:

- Before: `src/core/HardwareOutputBus.cpp:105` returned `side.data[frame]`, i.e. plane 0.
- The one-channel device feed reaches it at `include/AudioEngine.h:713-716`; with any route on the bus
  (`active()` true) it takes `monoSample()` instead of the master path.
- So a stereo direct out, insert send or cue on outputs 1/2 fed only its left sample to the mono device;
  the right sample stayed in plane 1 and was never read. The two bus-level tests that pinned the old
  behaviour were `tests/src/core/HardwareOutputBusTest.cpp:51` (`0.75` = plane 0 + master) and
  `tests/src/core/HardwareIoTestParts1.h:91` (`0.5` = channel 0 only).

## Fix at the cause

The bus now keeps a per-period mono downmix beside the planes, filled by the two writers that know a
route's width:

- `include/HardwareOutputBus.h:98-105`: `Side::mono`, one plane of `m_frames` (allocated once by
  `init()`, `src/core/HardwareOutputBus.cpp:38`; cleared by `swap()`, `:50`).
- `src/core/HardwareOutputBus.cpp:64-68`: `add()` adds `gain * 0.5 * (L + R)` to the downmix for both
  widths. A width-1 route's plane already holds that same value, so a mono route keeps its intended
  level; a width-2 route's right side is folded in rather than lost.
- `src/core/HardwareOutputBus.cpp:86-93`: `addPlane()` adds the plane it writes, so a surround side
  channel is not dropped from the mono output either.
- `src/core/HardwareOutputBus.cpp:114-120`: `monoSample()` returns the downmix plus the master averaged
  when the master is placed on the first channel, exactly as before. The dropped-route guard
  (`:61-63`) runs before the downmix is touched, so a route that does not fit contributes nothing.

This is route-aware: overlapping routes add their own downmix instead of making the reader average one
route's channel with another route's channel.

## Test matrix

All in `tests/src/core/HardwareOutputBusTest.cpp` unless stated.

| Slot | Path it covers | Expectation |
| --- | --- | --- |
| `monoOutputDownmixesBothSidesOfAStereoRoute` | normal path: stereo route on 1/2 | `0.5 * (0.25 + 0.75) = 0.5` |
| `monoOutputKeepsAMonoRouteAtItsLevel` | mono route is not halved again | `0.5 * (0.5 + 0.7) = 0.6` |
| `monoOutputFoldsEveryRoutedChannel` | routes beyond the first pair | `0.3 + 0.7 + 0.5 = 1.5` |
| `monoOutputAddsTheMasterOnlyOnItsPlacement` | master on 1/2 is averaged; master elsewhere is absent | `0.5 + 0.5 = 1.0`; routed away: `0.5` |
| `monoDownmixOfADroppedRouteStaysEmpty` | exhaustion: width-2 route at `MaxChannels - 1` | `0` |
| `monoDownmixClearsOnSwap` | reconfiguration: next period after `swap()` | `0.5`, then `0` |
| `outOfRangeFrameReadsAsSilence` (updated) | failure/boundary: frame outside the period | silence at frame 4 and -1 |
| `HardwareIoTest::outOfRangeFrameReadsAsSilence` (updated) | sibling end-to-end bus use | two mono routes fold to `0.75` |

The bus is runtime state, not saved data (no serialization in `HardwareOutputBus`), so no format test
applies. No supported-platform branch was added; the code is plain C++20.

## Verification: commands and exit codes

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-123.r1/logs`. Builds ran from the
worktree root, tests from `build/tests`, with `QT_QPA_PLATFORM=offscreen` and no `LMMS_PLUGIN_DIR`.

| Stage | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| RED build, old product code + new tests | `cmake --build build --target HardwareOutputBusTest -j4` | 0 | `build-1-red.log` |
| RED test | `ctest -R '^HardwareOutputBusTest$' --output-on-failure` | 8 | `test-1-red.log`; 4 slots failed (right side, all channels, master, updated expectation) |
| GREEN build | `cmake --build build --target zene HardwareOutputBusTest HardwareIoTest -j4` | 0 | `build-2-green.log` |
| GREEN test | `ctest -R '^(HardwareOutputBusTest\|HardwareIoTest)$' --output-on-failure` | 0 | `test-2-green.log`; 2/2 passed |
| Family test attempt before those binaries existed | `ctest -R '^(HardwareIoTest\|HardwareOutputBusTest\|SurroundTest\|SurroundDevicePathTest\|WideDiscreteBusTest\|WidePinRoutingTest\|MixerInsertLatencyTest\|SurroundExportTest)$' --output-on-failure` | 8 | `test-3-family.log` (`Unable to find executable` for six targets) |
| Build the six missing test targets | `cmake --build build --target SurroundTest SurroundExportTest MixerInsertLatencyTest WideDiscreteBusTest WidePinRoutingTest SurroundDevicePathTest -j4` | 0 | `build-3-family.log` |
| Family after those targets were built | same ctest command | 0 | `test-4-family.log`; 8/8 passed |
| **Final RED proof**: reverse product patch | `git diff -- include/HardwareOutputBus.h src/core/HardwareOutputBus.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | `proof-3-reverse.log` |
| Rebuild without the fix | `cmake --build build --target zene HardwareOutputBusTest HardwareIoTest -j4` | 0 | `build-7-proof-red.log` |
| Test without the fix | `ctest -R '^(HardwareOutputBusTest\|HardwareIoTest)$' --output-on-failure` | 8 | `test-8-proof-red.log`; 5 `HardwareOutputBusTest` slots and 1 `HardwareIoTest` slot failed |
| Restore product patch | `git apply .../logs/fix.patch` | 0 | `proof-4-restore.log` |
| Rebuild with the fix | `cmake --build build --target zene HardwareOutputBusTest HardwareIoTest -j4` | 0 | `build-8-proof-green.log` |
| Test with the fix | `ctest -R '^(HardwareOutputBusTest\|HardwareIoTest)$' --output-on-failure` | 0 | `test-9-proof-green.log`; 2/2 passed |
| Final affected-family rerun | `ctest -R '^(HardwareIoTest\|HardwareOutputBusTest\|SurroundTest\|SurroundDevicePathTest\|WideDiscreteBusTest\|WidePinRoutingTest\|MixerInsertLatencyTest\|SurroundExportTest)$' --output-on-failure` | 0 | `test-10-family-final.log`; 8/8 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p8-123` | 0 | `gates-1.log`; RESULT: PASS, both complexity and file-length "no new item in your files" |

Earlier proof rounds (`proof-1-reverse.log`, `test-5-proof-red.log`, `proof-2-restore.log`,
`test-6-proof-green.log`) were run before the last test slot was added; the final round above supersedes
them with the committed test file.

## Callers and siblings checked

- `add()` callers: insert send (`src/core/HardwareIo.cpp:121`), direct out (`:129`), cue (`:278`); all
  pass width 1 or 2, which `add()` maps to one or two planes. Nothing else calls it (`rg -n "\.add\("` over
  `src/`/`include/` found only these and the test).
- `addPlane()` caller: `placeSides()` (`src/core/MixerSurroundMix.cpp:162`). It now contributes to the
  downmix; on a one-channel device `deviceCarries()` (`:427-432`) prevents surround side planes, so this
  is a consistency guard rather than a behaviour change on the mono path.
- `monoSample()` caller: `include/AudioEngine.h:715`. `sample()` callers (`include/AudioEngine.h:720`,
  `src/core/AudioEngine.cpp:2221`) are untouched.
- `init()` caller: `src/core/AudioEngine.cpp:549`, still the only allocation site, off the render thread.

## Realtime

`add()`, `addPlane()`, `swap()` and `monoSample()` only touch storage `init()` allocated. The new
`std::fill` over `side.mono` is a bounded loop over `m_frames`, like the existing plane clear; there is
no allocation, free, lock, wait, I/O or log. Local TSan does not run on this box; the race argument is
that only the render thread calls these methods, as the class contract states
(`include/HardwareOutputBus.h:47-49`).

## Collateral damage and registration

- The bus is not saved; old projects and project XML load unchanged. The only saved data is the routing,
  which is untouched.
- `MaxChannels` and every route bound are unchanged; a route that fits the bus still fits the downmix.
- Changed files are already in `tests/fork-sources.txt` (`include/HardwareOutputBus.h`,
  `src/core/HardwareOutputBus.cpp`, `tests/src/core/HardwareOutputBusTest.cpp`); no new product file, so
  `fork-sources.txt` needed no edit. `tests/src/core/HardwareIoTestParts1.h` is a test header already in
  the tree.
- No command was added or changed, so the ControlCommandsSnapshot canary does not move.

## Self-check

1. **Regression test fails with the product change reverted and passes restored.** Reverted product
   hunks: `test-8-proof-red.log` exit 8 (`monoOutputDownmixesBothSidesOfAStereoRoute`:
   `'qAbs(outputs.monoSample(f, master) - 0.5f) < 1e-5f' returned FALSE`). Restored:
   `test-9-proof-green.log` exit 0. The reverse and restore `git apply` commands both exited 0
   (`proof-3-reverse.log`, `proof-4-restore.log`).
2. **Deterministic.** The test injects constants into a bus of four frames and calls `swap()` directly;
   there is no clock, sleep, thread or timing. The same inputs produced the same five failing slots in
   both red runs (`test-5-proof-red.log`, `test-8-proof-red.log`).
3. **At the cause and complete.** The cause was a reader that had no route information, so the writer
   now records the downmix where the width is known; `add`, `addPlane`, `swap`, `init` and `monoSample`
   are consistent, and every caller is listed above. The family tests
   (`HardwareIoTest`, `SurroundTest`, `WideDiscreteBusTest`, `WidePinRoutingTest`, `SurroundExportTest`,
   `SurroundDevicePathTest`, `MixerInsertLatencyTest`) all still pass (`test-10-family-final.log`, exit 0).
4. **No collateral damage.** The bus is runtime-only; no project data or command surface changed. The
   only numeric change is for a one-channel device that has routes on the bus, which is the defect.
   Route limits (`MaxChannels`, the drop guard) are unchanged.
5. **Verdict FIXED**, so no ALREADY-FIXED/FALSE-POSITIVE contract citation is needed; the REAL evidence
   is `src/core/HardwareOutputBus.cpp:105` (old `side.data[frame]`) versus the writer at `:69-78` that
   put the right side in a second plane.

## Other findings

None. The six "Not Run" entries in `test-3-family.log` were missing test binaries in this build tree, not
product failures; building the six targets (`build-3-family.log`, exit 0) made the same command pass
(`test-4-family.log`, exit 0).
