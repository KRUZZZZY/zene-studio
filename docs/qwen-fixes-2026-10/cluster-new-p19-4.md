FALSE-POSITIVE

# Cluster new-p19-4 - layer mix updates do not reach the audio thread as shared state

## Verdict

The finding (historical `src/core/ControlCommandsRackLayers.cpp:438`) says the `rack.layer_set`
handler changes mute, solo and level "immediately" while a sounding note is processed, with no
audio-safe handoff. On this branch there is one: the handler changes control-thread state and then
publishes each layer's resulting gain into that layer's audio bus as a `std::atomic<float>`. The audio
thread never reads the layer mix; it reads the published gain once per period while it applies volume
and panning to the whole bus buffer, so a note already sounding follows the change at its next period.
No product code changed. The commit adds a regression pin, `aLayerMixIsPublishedAsItsBusGain`, to the
existing layer publish test, plus this report.

Base: `fa4a5f2e7` (the gate base `feature_check.py` reports). Branch: `bugs/qwen-new-p19-4`.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| The handler changes the mix through `InstrumentLayers::setMix`, not by touching audio state | `src/core/ControlCommandsRackLayers.cpp:433` (`setLayerMix`), `:439-440` (`before = layers.mix(index)`, `layers.setMix(index, mixArgs(...))`) |
| `setMix` stores the mix in the control-thread array, then publishes | `src/core/InstrumentLayers.cpp:387-395` (write `m_mix[index]`, then `publishMix()`) |
| The mix becomes a per-layer bus gain (0 when muted, or not soloed while any layer is soloed) | `src/core/InstrumentLayers.cpp:398-408` (`publishMix`, `track->audioBusHandle()->setGain(silent ? 0.0f : mix.level)` at `:407`) |
| The published value is atomic, not the shared array | `include/AudioBusHandle.h:152-153` (`setGain`/`gain` store/load `m_gain`), `:240` (`std::atomic<float> m_gain = 1.0f`) |
| The audio thread reads that atomic once per period and scales the whole bus buffer with it | `src/core/AudioBusHandle.cpp:181-196` (`applyVolumeAndPanning` loads `m_gain` at `:187` and multiplies every frame), called on the render path at `:249` after the play handles (already-sounding notes included) are mixed into the buffer (`:236-247`) |
| The mix array itself is control-thread only; nothing on the render path reads it | `include/InstrumentLayers.h:202` (`//! Control thread only: each layer's mix`); `rg -n "m_mix" src` lists only `InstrumentLayers.cpp` control-thread functions (`setMix`, `publishMix`, save/load, add/remove) |
| The advertised contract is exactly what the publish does | `src/core/ControlCommandsRackLayers.cpp:449-451` the recorded inverse "re-publishes every layer's gain"; `:461-465` the description says the change is applied at once, to the notes already sounding too |
| The GUI path the existing test drives is the registered command, not a direct model write | `src/gui/LayerRackPanel.cpp:126-130` (`setMix` -> `ControlRegistry::instance()->invoke("rack.layer_set", ...)`), `:200-206` (buttons emit it) |
| Existing test end to end: mute, solo, level, and real rendered audio | `tests/src/gui/InstrumentSetupUiTest.cpp:381` (`layerRackMixesOpensAndRemovesALayer`): `:404-407` mute click -> `bus->gain() == 0`; `:409` undo -> `1`; `:410-414` solo -> own instrument gain `0`, layer `1`; `:416-418` level 50 % -> `bus->gain() == 0.5`; `:420` the layer's bus renders sound; `:421-422` muting it renders peak `0` |

There is therefore no shared `Mix` state for the audio thread to race with. The only cross-thread
state is one relaxed `std::atomic<float>` per bus - the fix the finding asks for, already in place.

### Why the finding looked real

`setLayerMix` (`ControlCommandsRackLayers.cpp:433-451`) does look like a plain model write: the
audio-safe step is one function deeper, in `InstrumentLayers::setMix` -> `publishMix`. The finding's
own suggested fix is the code that was there when the layer mix was added (`2fc82e9c9b`,
`feat(f2-ui)`, whose message says "Published as each layer bus's gain (AudioBusHandle::setGain, one
std::atomic<float> read once a period in applyVolumeAndPanning ... sounding notes included)"). That
commit predates this finding, so the defect never existed on this branch.

## Regression pin

`tests/src/core/InstrumentLayerZonePublishTest.cpp:231`,
`aLayerMixIsPublishedAsItsBusGain` (the file is an existing fork-new test file, already listed in
`tests/fork-sources.txt`; only the `AudioBusHandle.h` include and this slot were added). It drives the
finding's own command path: `rack.layer_set` mutes layer-0 (the track's own instrument, so the slot
runs with or without a loadable instrument), then asserts the bus gain - the value the render thread
loads - is `0`, the undo publishes `1` back, and a save/reload keeps the mix and gain in step. That
gain is what `AudioBusHandle::applyVolumeAndPanning` multiplies a sounding note's buffer by.

This is a contract pin, not a RED/GREEN pair for a committed product fix: there is no product change,
so there is no fix to revert. To prove the pin actually catches the defect the finding describes, I
ran it once against a temporary mutation of the product that removes the handoff the finding says is
missing (`publishMix()` deleted from `setMix`, nothing else). It failed on the first published-gain
assertion; restored, it passes. The mutation was never committed: `git diff --stat -- src include`
is empty after the restore (run 10 below). See "Mutation proof" for the commands and exits.

Test matrix:

- Normal path: `rack.layer_set` `muted`/`solo` -> published bus gain changes; a real rendered note
  follows it (existing GUI test, `test-9` below).
- Failure path: NaN / out-of-range level is refused before it reaches a gain
  (`tests/src/core/InstrumentLayerZonePublishTest.cpp:200-223`, `layerLevelRefusesNonFinite`).
- Exhaustion path: the mix array is fixed at `MaxLayers + 1`; `setMix` refuses an out-of-range index
  (`src/core/InstrumentLayers.cpp:390`). Unchanged.
- Reconfiguration: add/remove/insert move the mix with the layer and call `publishMix`
  (`src/core/InstrumentLayers.cpp:235-240`, `:255-258`, `:275-278`). Unchanged.
- Audio-thread constraints: the render path only loads an atomic (`AudioBusHandle.cpp:187`); no
  allocation, lock, wait, log or signal. The new test's work is on the control thread.
- Platform: `std::atomic<float>` and Qt only, no platform-specific API. No build change.

## Mutation proof (the test catches the finding's defect)

The mutated line is the one the finding says is missing; the committed tree is restored afterwards.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| mutate | `cp src/core/InstrumentLayers.cpp $TMPDIR/InstrumentLayers.cpp.orig` then delete `publishMix();` from `setMix` (Python one-liner, asserts exactly one match) | 0 | - |
| build | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p19-4 cmake --build build --target InstrumentLayerZonePublishTest -j4 > <LOG>/build-3-mutation.log 2>&1` | 0 | `build-3-mutation.log` |
| RED | `cd build/tests && ./InstrumentLayerZonePublishTest aLayerMixIsPublishedAsItsBusGain > <LOG>/test-5-mutation-red.log 2>&1` | **1** | `test-5-mutation-red.log`: `FAIL! ... Compared floats are not the same`; `Totals: 2 passed, 1 failed` |
| restore | `cp $TMPDIR/InstrumentLayers.cpp.orig src/core/InstrumentLayers.cpp && git diff --stat -- src include` | 0 | empty diff |
| rebuild | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p19-4 cmake --build build --target InstrumentLayerZonePublishTest -j4 > <LOG>/build-4-restored.log 2>&1` | 0 | `build-4-restored.log` |
| GREEN | `cd build/tests && ./InstrumentLayerZonePublishTest aLayerMixIsPublishedAsItsBusGain > <LOG>/test-6-green-restored.log 2>&1` | **0** | `test-6-green-restored.log`: `PASS ... aLayerMixIsPublishedAsItsBusGain`, `Totals: 3 passed, 0 failed` |

The RED failure is the muted bus still reading its old unity gain, i.e. the state the finding
describes (mix changed, no published handoff). The GREEN run is the branch's real code.

## Commands and results

All builds used `-j4`. `<LOG>` is
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p19-4.r1/logs`.

| # | Command | EXIT | Log / result |
| --- | --- | --- | --- |
| 1 | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p19-4 cmake --build build --target InstrumentLayersCommandsTest InstrumentSetupUiTest -j4 > <LOG>/build-1.log 2>&1` | 0 | `build-1.log` |
| 2 | `cd build/tests && ctest -R '^(InstrumentSetupUiTest\|InstrumentLayersCommandsTest)$' --output-on-failure > <LOG>/test-1.log 2>&1` | 0 | `test-1.log`: `100% tests passed out of 2` |
| 3 | `cd build/tests && ./InstrumentSetupUiTest layerRackMixesOpensAndRemovesALayer > <LOG>/test-2-layer-rack.log 2>&1` | 0 | `test-2-layer-rack.log`: `2 passed, 1 skipped` - the mix slot was SKIPPED then, this build had no `tripleoscillator`; superseded by run 15 |
| 4 | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p19-4 cmake --build build --target InstrumentLayerZonePublishTest -j4 > <LOG>/build-2.log 2>&1` (with the new pin) | 0 | `build-2.log` |
| 5 | `cd build/tests && ./InstrumentLayerZonePublishTest aLayerMixIsPublishedAsItsBusGain > <LOG>/test-4-mix-publish.log 2>&1` | 0 | `test-4-mix-publish.log`: `3 passed, 0 skipped` |
| 6 | mutation steps above | 1 (RED), 0 (GREEN) | `test-5-mutation-red.log`, `test-6-green-restored.log` |
| 7 | `cd build/tests && ctest -R '^(InstrumentLayerZonePublishTest\|InstrumentLayersCommandsTest\|InstrumentSetupUiTest)$' --output-on-failure > <LOG>/test-7-family.log 2>&1` (before the instrument plugins were built) | 8 | `test-7-family.log`: two untouched slots (`midiPartsAddRefusedAtTheLimit`, `midiPartsOverLimitSavedListLoadsWhole`) `QVERIFY(!m_device.isEmpty())` failed: this build tree had no instrument plugin `.so` yet |
| 8 | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p19-4 cmake --build build --target tripleoscillator audiofileprocessor -j4 > <LOG>/build-5-plugins.log 2>&1` | 0 | `build-5-plugins.log`; populates `build/plugins`, which the tests' `LMMS_TEST_PLUGIN_DIR` already points at |
| 9 | `cd build/tests && ctest -R '^(InstrumentLayerZonePublishTest\|InstrumentLayersCommandsTest\|InstrumentSetupUiTest)$' --output-on-failure > <LOG>/test-8-family-with-plugins.log 2>&1` | 0 | `test-8-family-with-plugins.log`: `100% tests passed out of 3` |
| 10 | `git diff --name-only -- src include > <LOG>/product-diff.log 2>&1` | 0 | empty output, 0 bytes: no product change |
| 11 | `cd build/tests && ./InstrumentSetupUiTest layerRackMixesOpensAndRemovesALayer > <LOG>/test-9-gui-mix.log 2>&1` | 0 | `test-9-gui-mix.log`: `3 passed, 0 skipped` - the rendered-audio mix test now really ran |
| 12 | `cd build/tests && ./InstrumentLayerZonePublishTest aLayerMixIsPublishedAsItsBusGain > <LOG>/test-10-pin-with-plugins.log 2>&1` | 0 | `test-10-pin-with-plugins.log`: `3 passed, 0 skipped` |
| 13 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > <LOG>/gates-2.log 2>&1` | 0 | `gates-2.log`: all eight rows PASS, `RESULT: PASS` |

Gate table (`gates-2.log`):

```text
feature-check kads-new-p19-4 (base fa4a5f2e7, 431 changed files; logs in .../build/feature-check)
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

Run 7's two failures are a build-environment gap (no instrument `.so`), not a regression from the
pin: both slots are untouched, they fail on `!m_device.isEmpty()`, and after building the plugins the
same binary passes 3/3 (run 9). I did not change or weaken them.

## Files committed

- `tests/src/core/InstrumentLayerZonePublishTest.cpp` (existing fork-new file: one include and the
  `aLayerMixIsPublishedAsItsBusGain` slot)
- `docs/qwen-fixes-2026-10/cluster-new-p19-4.md` (this report)

No product, registration, snapshot or manual file changed. `InstrumentLayerZonePublishTest.cpp` is
already in `tests/fork-sources.txt`, so no list file changes; nothing new runs on the audio thread.

## Self-check

1. **The test FAILS with the handoff removed and PASSES with it (exit codes).** The pin is RED=1 with
   the handoff mutation and GREEN=0 with the branch's code (`test-5-mutation-red.log`,
   `test-6-green-restored.log`). There is no committed product change, so step 4b's `fix.patch` is
   empty (`product-diff.log`, 0 bytes, EXIT=0); the mutation was local and restored, verified by an
   empty `git diff --stat -- src include`.
2. **The test is deterministic.** It drives `ControlRegistry::invoke` with literal arguments and reads
   `AudioBusHandle::gain()`, the same relaxed atomic the render thread loads. No clock, sleep, thread
   or audio device is involved (the fixture stops the dummy device thread first, as every test here
   does). The RED/GREEN runs executed exactly one slot each (`Totals: 3 passed` including init and
   cleanup, `0 skipped`).
3. **The fix is at the cause and complete.** No product change. For completeness I checked the two
   paths into the mix: `rack.layer_set` -> `setLayerMix` (`ControlCommandsRackLayers.cpp:433-440`) and
   the undo/redo half -> `InstrumentLayers::setMix` (`:426-430`). Both always call `publishMix`
   (`InstrumentLayers.cpp:387-395`). The sibling rack commands and `loadSettings` also publish
   (`InstrumentLayers.cpp:239`, `:257`, `:278`, `:379`). The only audio-thread reader is the atomic
   gain (`AudioBusHandle.cpp:187`).
4. **No collateral damage.** The only product-file diff is empty; saved projects, load paths, limits
   and other callers are untouched. The new slot runs with or without instrument plugins (it uses
   layer-0), undoes its final solo change, then undoes its newly added track so later slots inherit
   neither its undo state nor its track. The full three-test family passes 3/3 after the plugins are
   built (run 9).
5. **Verdict evidence (FALSE-POSITIVE).** The handler's state is control-thread only
   (`include/InstrumentLayers.h:202`); it is published as `std::atomic<float>` gains
   (`include/AudioBusHandle.h:152-153`, `:240`, `InstrumentLayers.cpp:407`); the audio thread loads
   that atomic once per period and applies it to the whole bus buffer (`AudioBusHandle.cpp:181-196`,
   called at `:249`). The existing GUI test drives the same registered command
   (`LayerRackPanel.cpp:126-130`) and observes both the published gain and real rendered audio
   (`InstrumentSetupUiTest.cpp:404-422`), rerun green with the plugins present (run 11, `0 skipped`).
6. **Sibling commands in the command files the check lists.** This cluster adds or changes no command
   file and registers no command: my commit touches only
   `tests/src/core/InstrumentLayerZonePublishTest.cpp` and this report (`git show --stat 4f90cec1a`).
   `submit_check.py` builds that sibling list from the whole `base..HEAD` diff (431 files of the
   train's merged commits since `fa4a5f2e7`), not from this cluster. None of `app.about`, `app.ask`,
   `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`,
   `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`,
   `app.whats_new`, `arrangement.fit_tracks` or `arrangement.get_state` reads or writes a layer mix;
   they are untouched by this change. The layer command family itself (`rack.layer_set`,
   `rack.layers`, `rack.layer_add`, `rack.layer_remove`) is unchanged and consistent - all of it
   publishes through `InstrumentLayers::publishMix`. No `ControlCommandsSnapshot` row moves.

## Other findings

- The finding's "no audio-safe handoff is visible here" is a readability issue, not a defect: the
  handoff is in the callee, one level below the cited line. No action taken.
- This build tree had no instrument plugin `.so` and two untouched `InstrumentLayerZonePublishTest`
  slots (`midiPartsAddRefusedAtTheLimit`, `midiPartsOverLimitSavedListLoadsWhole`) fail with
  `QVERIFY(!m_device.isEmpty())` until `tripleoscillator`/`audiofileprocessor` are built. Out of
  scope here; noted for the build recipe.
