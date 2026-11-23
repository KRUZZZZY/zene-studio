FIXED

# Cluster new-p9-213 - LADSPA output ports can write past the stereo buffer

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-213`
(branch `bugs/qwen-new-p9-213`, tip `c6b40333d8` plus this change).

## Decision: REAL

The finding is real on this branch tip. Before the fix, `pluginInstantiation()`
made one processor per input channel and the merge loop then walked every
processor's output ports with a single unbounded channel counter:

- `plugins/LadspaEffect/LadspaEffect.cpp:334` (pre-fix) -
  `m_processors = DEFAULT_CHANNELS / effect_channels;`. A plugin with one audio
  input therefore gets `DEFAULT_CHANNELS / 1 = 2` processors.
- `plugins/LadspaEffect/LadspaEffect.cpp:226-254` (pre-fix) - the merge loop
  resets `channel = 0` and then, for every `BufferRate::ChannelOut` port of
  every processor, executes
  `inOut[frame][channel] = d * inOut[frame][channel] + w * pp->buffer[frame];`
  and `++channel`. There is no bound on `channel`.
- `plugins/LadspaEffect/LadspaEffect.cpp:156` (pre-fix) -
  `processImpl(InterleavedBufferView<float, 2> inOut)`: the view is exactly two
  channels wide (the type fixes the channel count at 2). A one-input/two-output
  plugin produces 2 x 2 = 4 channel writes, so the last processor writes
  `inOut[frame][2]` and `inOut[frame][3]` - one whole frame past the view.
- `plugins/LadspaEffect/LadspaEffect.cpp:361-385` (pre-fix) - the port
  classifier maps an audio output to `ChannelOut` when its name contains
  "OUT", so a valid plugin whose ports are named `Input`, `Output Left`,
  `Output Right` takes exactly that path. This is the fixture added here.
- No layout check existed: the only guard added earlier was the zero-input
  guard at `plugins/LadspaEffect/LadspaEffect.cpp:317-333` (cluster
  new-p15-94), which rejects `inputChannels == 0` and nothing else.

The consequence is a real buffer overrun, not a cosmetic one. On the
production path (`AudioPlugin::processAudioBuffer` -> `processImpl`) the
two-channel view is the core bus pair itself (`include/AudioPorts.h:828-891`,
in-place and unbuffered), so channel 2 of frame *f* is channel 0 of frame
*f+1*, and channels 2/3 of the last frame are past the bus. The RED run below
observed the write in a guarded allocation: the guard frame after the stereo
buffer was overwritten.

## The fix (at the cause)

`plugins/LadspaEffect/LadspaEffect.cpp`:

- `:59-77` - two predicates, `isChannelInputPort()` and
  `isChannelOutputPort()`, hold the name/direction rule the port classifier
  already used. The classifier (`:430-462`) now calls them, so the layout check
  and the mapping cannot drift apart.
- `:366-409` - after the processor count and port count are known, the mapped
  `ChannelIn`/`ChannelOut` ports are counted. If
  `processors * mappedInputs > DEFAULT_CHANNELS` or
  `processors * mappedOutputs > DEFAULT_CHANNELS`, the effect reports the
  layout and calls `setOkay(false)`, returning **before any port buffer is
  allocated**. The check is before that loop on purpose: the in-place sharing
  branch indexes its two-entry `inbuf` array by output channel
  (`LadspaEffect.cpp:439-447`), which a three-output layout could also overrun.
- Unsupported layouts are refused, as the finding allows ("reject unsupported
  input/output layouts"). Layouts the wrapper already handled keep their exact
  behaviour: mono 1-in/1-out (`2 x 1 = 2` channels), stereo 2-in/2-out
  (`1 x 2 = 2`), and a 2-in/1-out effect (`1 x 1 = 1`, channel 1 stays dry, as
  before). The rule counts only ports the merge loop actually writes, so a
  plugin whose outputs are named without "OUT" is not newly rejected.

No change to `processImpl`: the check runs in `pluginInstantiation()` (GUI /
load thread) only. Nothing is added to the audio thread - no allocation, free,
lock, wait or log. No control command, undo step or saved-data format changes.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp:2170`
(`ladspaEffectRejectsOutputChannelOverflow`), with the new
`mono_in_stereo_out` descriptor in
`tests/src/plugins/LadspaChannelCountFixture.cpp` and the fixture module added
to the test's `LADSPA_PATH` (`PluginPortsMigrationTest.cpp:1210-1221`) and its
CMake definition/dependency (`tests/CMakeLists.txt:9357-9365`).

The fixture is a valid hard-real-time, in-place-broken LADSPA transfer with one
audio input and two audio outputs named `Output Left`/`Output Right`. The test
constructs the real migrated `ladspaeffect` module, and:

1. if the effect is accepted (the pre-fix state) it drives the exact merge
   loop, `processImpl`, over a bus allocated with one extra stereo frame of
   guard storage and requires the guard to survive. `inOut[frame][2]` and
   `[3]` land in that guard frame, so the overrun is observed deterministically
   and inside the allocation (no ASan, no timing, no UB in the test itself);
2. it requires `!effect->isOkay()` and `controls() == nullptr`, i.e. the
   layout is refused.

The test matrix:

| Path | Coverage |
|---|---|
| normal (accepted mono and stereo layouts) | `migratedPluginsPreserveBehaviour` renders the SWH mono `amp_1181` and stereo `dj_eq` through the migrated and the pre-migration reference modules and compares sample-exactly; it passes (log `test-9`). |
| failure (unsupported layout) | the new slot: refused before instantiation, and the pre-fix merge loop overwrites the guard (log `test-2`). |
| reconfiguration | `changeSampleRate()` returns at `LadspaEffect.cpp:135-140` while `m_controls == nullptr`, so a refused effect is never re-instantiated into a half state; `ladspaEffectSurvivesFailedInstantiationsDuringSampleRateChange` still passes (log `test-9`). |
| exhaustion | not applicable: this path has no pool, queue or bounded resource. |
| audio thread | the check is control-thread only; `processImpl` is unchanged, and the realtime-allocation watch used by the migration harness is unaffected (`test-9`). |
| platform | Linux built and run; the new slot uses the existing `testHostCanLoadPluginModules()` QSKIP on Windows (a Windows test host cannot load plugin modules - documented at `PluginPortsMigrationTest.cpp:670-686`). No platform-specific API was added. |

## RED / GREEN (step 4b), commands and EXIT codes

All logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-213.r1/logs/`.

Fix reverted (test kept) - red:

```
cd <worktree>
git diff -- plugins/LadspaEffect/LadspaEffect.cpp > .../logs/fix.patch   # EXIT=0
git apply -R .../logs/fix.patch                                          # EXIT=0
cmake --build build --target PluginPortsMigrationTest ladspaeffect -j4 \
  > .../logs/build-3-red.log 2>&1                                        # EXIT=0
cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsOutputChannelOverflow \
  > .../logs/test-2-red-slot.log 2>&1                                    # EXIT=1
```

`test-2-red-slot.log`: `FAIL! : ... 'storage[frames].left() == guardLeft &&
storage[frames].right() == guardRight' returned FALSE. (the LADSPA merge loop
wrote past the two-channel effect buffer)`; `Totals: 2 passed, 1 failed`.

Fix re-applied - green:

```
git apply .../logs/fix.patch                                             # EXIT=0
cmake --build build --target PluginPortsMigrationTest ladspaeffect -j4 \
  > .../logs/build-4-green.log 2>&1                                      # EXIT=0
cd build/tests && ./PluginPortsMigrationTest ladspaEffectRejectsOutputChannelOverflow \
  > .../logs/test-3-green-slot.log 2>&1                                  # EXIT=0
```

`test-3-green-slot.log`: `PASS ... ladspaEffectRejectsOutputChannelOverflow()`,
`Totals: 3 passed, 0 failed`, with the expected refusal reason
`"LADSPA plugin mono_in_stereo_out maps 1 input and 2 output channels per
processor; 2 processors do not fit the 2-channel effect bus"`.

Final LADSPA slots on the committed product code (log `test-9-final-ladspa-slots.log`,
EXIT=0): `8 passed, 0 failed` - process lock, zero-input rejection, the new
overflow rejection, both sample-rate re-instantiation slots, and
`migratedPluginsPreserveBehaviour`.

## Family run and the four unrelated failures

`ctest -R "^PluginPortsMigrationTest$" --output-on-failure` from `build/tests`
(log `test-7-plugin-ports.log`) exits 8: 93 of the suite's slots pass and four
fail. They are environmental and I proved they fail without this change:

- `malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange`, `slice8PluginsAreLiveAndExact` -
  `thirdparty/stk-rawwaves/` does not exist in this checkout
  (`ls .../thirdparty/stk-rawwaves` -> No such file or directory), and the log
  shows the STK `FileRead::open: could not open or find file
  .../thirdparty/stk-rawwaves/*.raw`.
- `nonRealtimeLadspaEffectsAreInvalid` - its fixture `decimator_1202.so` is not
  in any directory the manager scans: `find /usr/lib/ladspa /usr/lib64/ladspa
  /usr/local/lib/ladspa /usr/local/lib64/ladspa /Library/Audio/Plug-Ins/LADSPA
  -name 'decimator*'` printed nothing.

With the product fix reverted and rebuilt (log `build-7-prefix-env.log`, EXIT=0),
the same four slots still fail (log `test-8-prefix-env-slots.log`, EXIT=4), so
they are not collateral damage from this change.

The focused LADSPA family is green: `ctest -R "^Ladspa"` from `build/tests`
(log `test-5-ladspa-family.log`, EXIT=0): `LadspaManagerTest`,
`LadspaChannelCountsTest`, `LadspaPortDialogTest`, `LadspaDescriptionTest`,
`LadspaGlobalLinkTest` - `100% tests passed out of 5`.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py
<worktree>` (log `feature-check-1.log`, unpiped `EXIT=0`):

```
PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
PASS no-tautology / doc-figures
PASS complexity      no new item in your files
PASS file-length     no new item in your files
RESULT: PASS
```

## Registration

`tests/upstream-modifications.txt` gains one line each for
`plugins/LadspaEffect/LadspaEffect.cpp`,
`tests/src/plugins/LadspaChannelCountFixture.cpp`,
`tests/src/plugins/PluginPortsMigrationTest.cpp` and `tests/CMakeLists.txt`,
each naming cluster new-p9-213 and its reason. `LadspaEffect.cpp` and the two
test sources were already registered in `tests/all-sources.txt`; no new product
file was added.

## Other findings (not changed here)

- `LadspaEffect::pluginDestruction()` deletes every `ChannelOut` port buffer
  when `m_inPlaceBroken` is true (`LadspaEffect.cpp:698-709` after this change),
  but `m_inPlaceBroken` can be set to true *after* an earlier output already
  aliased an input buffer (`LadspaEffect.cpp:439-448`). A port order with two
  outputs between the two inputs of an otherwise accepted 2-in/2-out in-place
  plugin would then delete the aliased buffer twice. The layouts this finding
  rejects can no longer reach it, and the usual input-then-output port order
  cannot flip the flag; the odd-order case is a separate latent defect.
- The port mapping is name-based: an audio output whose name does not contain
  "OUT" is classified `AudioRateOutput` and its signal is dropped
  (`LadspaEffect.cpp:430-462`). Pre-existing, unrelated to the overrun.
- `slice6PluginsAreLiveAndExact` also needs plugin modules this build tree does
  not produce yet (`lb302 render missing`); not related to this change.

## Self-check

Answers to `submit_check.py`'s ASK items, each with the evidence from this session.

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Logs `build-3-red.log` / `test-2-red-slot.log` and `build-4-green.log` /
  `test-3-green-slot.log`: after `git apply -R .../logs/fix.patch` (EXIT=0) and a
  rebuild (EXIT=0), `./PluginPortsMigrationTest ladspaEffectRejectsOutputChannelOverflow`
  exits 1 with `FAIL! ... 'storage[frames].left() == guardLeft &&
  storage[frames].right() == guardRight' returned FALSE`; after
  `git apply .../logs/fix.patch` (EXIT=0) and a rebuild (EXIT=0), the same
  command exits 0. The final run on the committed code is `test-9-final-ladspa-slots.log`
  (EXIT=0).
- **The test is deterministic: no wall-clock timing, sleeps or races.**
  It uses a fixed fixture descriptor, fixed guard sentinels (`12345.0f` /
  `-12345.0f`), a fixed 0.25/-0.25 bus, and the engine's own
  `framesPerPeriod()`; there is no thread, timer, sleep or random input. The
  only "seam" is the direct call to the exact merge loop `processImpl` that the
  production path calls, with one extra stereo frame allocated after the bus so
  the out-of-range write is observable inside the allocation.
- **The fix is at the cause and complete.**
  The overrun comes from the unbounded channel counter in the merge loop
  (`LadspaEffect.cpp:269`), fed by `m_processors = DEFAULT_CHANNELS /
  effect_channels`. The check counts the same `ChannelIn`/`ChannelOut` ports the
  loop walks, with the same predicates the classifier uses
  (`isChannelInputPort` / `isChannelOutputPort`, `LadspaEffect.cpp:59-77`), and
  refuses an overrun before any port buffer exists. Every caller of
  `pluginInstantiation()` is covered: the constructor and `changeSampleRate()`;
  the latter returns early when `m_controls == nullptr` (`LadspaEffect.cpp:135-140`),
  so a refused effect is never re-instantiated. The merge loop's only other
  consumer of the channel counter is the input loop, whose count is checked by
  the same rule. No sibling function maps ports.
- **No collateral damage.**
  The check is the exact set of channels the loop touches, so it cannot reject a
  layout that fit before: mono `2 x 1 = 2`, stereo `1 x 2 = 2`, and
  `2-in/1-out` `1 x 1 = 1` all stay accepted (the latter keeps its old dry
  channel 1). This is held by `migratedPluginsPreserveBehaviour`, which renders
  the SWH mono `amp_1181` and stereo `dj_eq` through the migrated module and the
  pre-migration reference module and compares sample-exactly - it passes
  (`test-9-final-ladspa-slots.log`). No saved-data format, project XML,
  control/undo or audio-thread path changed; `LadspaEffect.h` is untouched, so
  no other target needs a rebuild for a header change.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed; the verdict is FIXED and the
  pre-fix code path is cited in the Decision section.
- **Sibling commands (`app.*`, `arrangement.*`):** this change touches no
  command file and adds no control command (the ControlCommandsSnapshot canary
  is unchanged). `plugins/LadspaEffect/LadspaEffect.cpp` is only reachable
  through the effect's own `processImpl`/`pluginInstantiation` and has no
  command handler, so none of the listed commands is affected.
