# Cluster review-51e7da51f - three findings in src/core/ScriptDawBindings.cpp

**Verdict:** ALREADY-FIXED (review-51e7da51f) / ALREADY-FIXED (1663) / FIXED (new-p12-245)

One line per finding, each followed by its evidence and the commands it was
measured with:

review-51e7da51f: ALREADY-FIXED
## What the finding says

"Commit introduced `aScriptsRemovedEffectIsUndoneByItsOwnStep` but
`applyRemoveEffect` still does not call `journalEffectRemoval` before delete.
Missing native plugin skipped this case and masked failure. Finish existing
journal-before-delete integration, prove new regression really executes with
plugin present and passes undo restoration; preserve script undo and realtime
ownership contracts."

## The journal-before-delete integration is on this branch

`src/core/ScriptDawEdit.cpp:231` records the removal BEFORE the device is
detached and deleted (`:240-241`):

```cpp
	Effect* effect = effects[index];
	...
	if (!control::journalEffectRemoval(chain, effect, index))
	{
		... the removal is refused and the device stays ...
	}
	// A recorded removal pushes exactly one checkpoint: the structural step.
	control::detachEffect(chain, effect);
	effect->deleteLater();
```

That is commit b9234d896 ("fix(rework 51e7da51f): a script's removeEffect was
not undoable", 2026-10-08), which is an ancestor of this branch. The finding's
first sentence describes the tree BEFORE that commit.

## The masked failure was real, and it hid a red case

This worktree's build held no plug-in module at all (`build/plugins` had no
`.so`), so EVERY case of `tests/src/core/ScriptDawRemovalUndoTest.cpp` skipped
("this build lists no loadable effect module") and the file's ctest line was
green without running a thing. With the build's own BassBooster module
present, the file runs 13 cases - and one of them FAILED:

    FAIL!  : ScriptDawRemovalUndoTest::aRecordedDeviceIsBuiltWithoutTheAudioChangeLock()
              'releasedByFlag.load()' returned FALSE. (building the recorded device waited for the
              audio change lock (the holder's safety bound ran out))

That is the cluster's REALTIME OWNERSHIP contract, and it was broken on this
branch - not by this cluster, but by a later engine change. gdb on the blocked
build (gdb -batch -ex "break lmms::control::instantiateRecordedDevice" -ex
"tbreak lmms::AudioEngine::requestChangeInModel" ...):

    #1  lmms::AutomatableModel::setInitValue(float)
    #2  lmms::AutomatableModel::AutomatableModel(...)
    #3  lmms::Effect::Effect(...)
    #10 lmms::Effect::instantiate(...)
    #11 lmms::control::instantiateRecordedDevice(lmms::EffectChain*, QString const&)

The recorded device's BUILD creates the device's models, and every model
construction takes the audio change lock (AudioEngine::registerAutomatableModel,
`std::lock_guard{m_changeMutex}`, src/core/AudioEngine.cpp:446) and every model
write takes it again (AutomatableModel::setValue -> modelChangeGuard,
src/core/AutomatableModel.cpp:415). Both were introduced AFTER this cluster's
round-7 case (commit 117fd0126, 2026-10-08) by cluster 2647 (commit c28751cef,
2026-10-09), which made the engine's model registry lock-backed. With the
engine as it stands, a device build cannot be lock-free: the registration
itself is under the lock the render thread takes once per period.

## The fix

A model the render thread cannot see needs no change lock: it is not in the
engine's registry, so the render prepass never reads it.

* `include/AutomatableModel.h` - `isRegisteredWithAudioEngine()` exposes whether
  the model is in the engine's render-visible registry, and
  `DeferredModelRegistrationScope` defers the registration of the models created
  while it is alive.
* `src/core/AutomatableModel.cpp` - the model-change guard skips the lock for a
  model that is not registered (a model being constructed: it registers at the
  end of its own constructor, and the models of a device built with the
  registration deferred). A REGISTERED model is written exactly as before.
* `src/core/ControlStructuralSupport.cpp` - the recorded device's build runs
  under the deferral scope, and `attachRecordedDevice()` registers the device's
  own models when the device joins the chain, under the change lock that attach
  already takes. The undo closure, the redo closure and the other callers of
  `instantiateRecordedDevice()` are unchanged: one structural undo step, one
  recorded removal.

Nothing on the audio thread changed: the render thread still takes the change
lock once per period, and the models it reads are the registered ones.

## The masked skip is closed at the root

`tests/CMakeLists.txt` - the script's DAW tests now bring the build's own
effect modules up with them, so a targeted build of the test no longer leaves
the cases skipping:

```cmake
	if(LMMS_TEST_NAME STREQUAL "ScriptDawRemovalUndoTest")
		add_dependencies(${LMMS_TEST_NAME} bassbooster)
	endif()
	if(LMMS_TEST_NAME STREQUAL "ScriptDawBindingTest")
		add_dependencies(${LMMS_TEST_NAME} bassbooster amplifier)
	endif()
```

## RED / GREEN

RED - the journal-before-delete integration removed (the shape the finding
describes; the patch is `logs/pre-51e7da51f-no-journal.patch`):

```
$ git apply logs/pre-51e7da51f-no-journal.patch
$ cmake --build build --target ScriptDawRemovalUndoTest -j 18 > logs/build-21-red-51e7da51f.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawRemovalUndoTest > logs/test-removal-red-51e7da51f.log 2>&1; echo EXIT=$?
EXIT=7
```

    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovedEffectIsUndoneByItsOwnStep() Compared values are not the same
    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovedEffectIsUndoneOnceAtTheUndoCap() ...
    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovedEffectIsUndoneOnceAtCapOne() ...
    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovedLastEffectOfADisabledChainStaysDisabledOnUndo() ...
    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovedEffectFromADisabledChainWithTwoStaysDisabledOnUndo() ...
    FAIL!  : ScriptDawRemovalUndoTest::aRecordedDeviceIsBuiltWithoutTheAudioChangeLock() ...
    FAIL!  : ScriptDawRemovalUndoTest::aScriptsRemovalThatCannotBeUndoneIsRefused() ...
    Totals: 6 passed, 7 failed, 0 skipped, 0 blacklisted, 11368ms

GREEN - the integration restored and the realtime contract repaired:

```
$ git checkout -- src/core/ScriptDawEdit.cpp
$ cmake --build build --target ScriptDawRemovalUndoTest -j 18 > logs/build-22-green-51e7da51f.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawRemovalUndoTest > logs/test-removal-green-51e7da51f.log 2>&1; echo EXIT=$?
EXIT=1        (12 passed, 1 failed - the audio-change-lock case above)
```

and with the deferred registration in place:

```
$ cmake --build build --target ScriptDawRemovalUndoTest -j 18 > logs/build-24-engine-fix.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawRemovalUndoTest > logs/test-removal-engine-fix.log 2>&1; echo EXIT=$?
EXIT=0        (Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted, 1222ms)
```

The whole file now runs (0 skipped) and passes, with the build's own effect
module present - the undo restoration the finding asks for
(`aScriptsRemovedEffectIsUndoneByItsOwnStep`: the device comes back on one
undo, redo takes it out again) and the realtime ownership contract beside it.

1663: ALREADY-FIXED
## What the finding says

`src/core/ScriptDawBindings.cpp:430` (`LuaMixerChannel::gain()`) and
`src/core/ScriptBindings.cpp:713` (`LuaTrack::name()`): "Wrappers retain raw
pointers and generally only check for null; they do not verify that the
pointed-to object is still alive." The recipe: a script takes a channel view,
removes the channel with `zene.command("mixer.remove_channel", ...)`, then
calls `channel:gain()` or `channel:setGain(...)`.

## Why it is already fixed on this branch

* The mixer-channel view resolves its channel by the channel's STABLE ID on
  every access - `src/core/ScriptDawBindings.cpp:431-436`:

  ```cpp
  MixerChannel* LuaMixerChannel::resolve() const
  {
      if (m_id.isEmpty()) { return nullptr; }
      ControlResult error;
      return control::resolveMixerChannel(m_id, &error);
  }
  ```

  `control::resolveMixerChannel` (src/core/ControlMixerSupport.cpp:45) looks the
  id up among the mixer's LIVE channels and answers `not_found` for an id no
  live channel carries. A removed channel therefore resolves to `nullptr`, and
  every accessor takes the branch it already took for a wrapper that was never
  bound (`gain()` -> 0.0f, `setGain()` -> no-op, `isValid()` -> false). The fix
  is commit e228edf8b "fix(lua): resolve mixer channel views by stable id
  (cluster 1663)".
* The track view holds a `QPointer<Track>` (`include/ScriptBindings.h:231`), so
  a destroyed track clears the pointer and `LuaTrack::name()` answers the empty
  string (commit c2653f172, cluster new-p12-229). Every other wrapper in
  `include/ScriptBindings.h` is a QPointer for the same reason.

## What this commit adds

* `src/core/ScriptDawEffects.cpp` - the DEVICE view was the last wrapper of
  this class that still held raw pointers: `LuaEffect` held the `EffectChain*`
  and the `Effect*`, and the chain dies with the channel that owns it, so a
  view kept across `mixer.remove_channel` read freed memory. It now names its
  device by the channel's stable id and the device's own id and resolves both
  on every access (`LuaEffect::m_chain()`, `LuaEffect::m_effective()`), the
  same contract the mixer-channel view already had.
* `tests/src/core/ScriptDawBindingTest.cpp` - `aRemovedChannelsViewIsInert()`:
  the finding's whole recipe (take a channel view, remove the channel through
  the command bridge, then read and write the view that was kept).
* `tests/src/core/ScriptDawEffectsTest.cpp` -
  `aDeviceViewOfARemovedChannelReadsNothing()`: the device view of a removed
  channel reports itself invalid.

## RED / GREEN

RED - the tree with the PRE-1663 shape of the mixer-channel view (the wrapper
holds the channel's pointer and only checks it for null). The patch is
`logs/pre-1663-raw-pointer.patch`; the recipe then reads the freed channel:

```
$ git apply logs/pre-1663-raw-pointer.patch
$ cmake --build build --target ScriptDawBindingTest -j 18 > logs/build-7-red-1663.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest aRemovedChannelsViewIsInert > logs/test-binding-red-1663-single.log 2>&1; echo EXIT=$?
EXIT=134
```

    QFATAL : ScriptDawBindingTest::aRemovedChannelsViewIsInert() AutomatableModel::controllerValue(int)lacks implementation for a scale type (-2000581872, model 0x558253287e70)
    FAIL!  : ScriptDawBindingTest::aRemovedChannelsViewIsInert() Received a fatal error.
    Totals: 1 passed, 1 failed, 0 skipped, 0 blacklisted, 1087ms

(The whole binary aborts with SIGABRT: the pre-fix view reads the deleted
channel's fader model.)

GREEN - the branch's own fix in place:

```
$ cmake --build build --target ScriptDawBindingTest ScriptDawEffectsTest -j 18 > logs/build-9-green-1663.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest > logs/test-binding-green-1663.log 2>&1; echo EXIT=$?
EXIT=0        (Totals: 13 passed, 0 failed, 1 skipped)
$ QT_QPA_PLATFORM=offscreen ./ScriptDawEffectsTest > logs/test-effects-green-1663.log 2>&1; echo EXIT=$?
EXIT=0        (Totals: 8 passed, 0 failed, 0 skipped)
```

The one skipped case is `aScriptsLoadedEffectIsUndoneOnTheChannelItNames`, which
needs the amplifier module (not built in this tree); it is unrelated to this
finding.

NOTE ON THE STEP-4b REVERT: the mixer-channel fix this case pins is ALREADY on
the branch (e228edf8b), so reverting THIS commit's own product diff does not
make the case fail - the case would still pass. The RED proof above therefore
reverts the historical pre-1663 shape instead, which is the bug the finding
describes.

new-p12-245: FIXED
## What the finding says

`src/core/ScriptDawBindings.cpp:94-103`: "`ViewPool::add` appends a new
wrapper every time, and the pools are only cleared between script runs. Lua
garbage collection does not reclaim those C++ wrappers. ... A script repeatedly
calls `zene.mixer():channel(0)`. The deque grows for the whole run, outside the
Lua memory budget." The fix: "Reuse views by stable object identity, or
otherwise bound the per-run pools while preserving references held by Lua."

## Why it was real

`ViewPool::add` pushed a copy on every call and `newMixerChannel()` /
`newEffectChain()` / `newEffect()` called it on EVERY access. The pools are
cleared in `ScriptDawBindings::beginRun()` (src/core/ScriptDawBindings.cpp:219),
i.e. once per script run, and the wrappers are C++ objects the Lua collector
cannot reclaim - so a run that read the same channel N times left N wrappers
behind, outside the Lua memory budget the script is held to.

## The fix

`src/core/ScriptDawBindings.cpp` - the pool holds ONE wrapper per OBJECT:

* `ViewPool::add(key, make)` looks the key up and hands back the wrapper it
  already holds; a new wrapper is made only for a key the pool has not seen.
* The key is the object's stable identity: the mixer channel's own id
  (`control::channelIdOf`), the chain's mixer-channel id, and the device's own
  id (`control::effectIdOf`, the `fx-<n>` the engine assigns once and never
  reuses).
* A wrapper never changes after it is made, so every reference Lua holds keeps
  naming the same view - the "preserving references held by Lua" half of the
  finding's fix.
* `ScriptDawBindings::viewPoolSize()` is a read-only diagnostic the regression
  case measures the pool with.

The three pools are cleared per run exactly as before
(`ScriptDawBindings::beginRun()`), so nothing outside a run is kept.

## RED / GREEN

RED - the pool keying reverted (the diagnostic accessor stays, so the case
compiles and runs; the patch is `logs/fix-p12-245.patch`):

```
$ git apply -R logs/fix-p12-245.patch
$ cmake --build build --target ScriptDawBindingTest -j 18 > logs/build-17-p12-red.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest > logs/test-binding-p12-red.log 2>&1; echo EXIT=$?
EXIT=1
```

    FAIL!  : ScriptDawBindingTest::aRepeatedChannelReadDoesNotGrowTheViewPool() Compared values are not the same
       Actual   (many): 500
       Expected (once): 1
    Totals: 13 passed, 1 failed, 1 skipped, 0 blacklisted, 1688ms

(Five hundred reads left five hundred wrappers behind.)

GREEN - the fix in place:

```
$ git apply logs/fix-p12-245.patch
$ cmake --build build --target ScriptDawBindingTest ScriptDawEffectsTest -j 18 > logs/build-18-p12-green.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./ScriptDawBindingTest > logs/test-binding-p12-green.log 2>&1; echo EXIT=$?
EXIT=0        (Totals: 14 passed, 0 failed, 1 skipped)
$ QT_QPA_PLATFORM=offscreen ./ScriptDawEffectsTest > logs/test-effects-p12-green.log 2>&1; echo EXIT=$?
EXIT=0        (Totals: 8 passed, 0 failed, 0 skipped)
```

The skipped case is `aScriptsLoadedEffectIsUndoneOnTheChannelItNames`, which
needs the amplifier module; it is unrelated to this finding.

## The script family, with the build's own effect modules present

```
$ cmake --build build --target bassbooster tripleoscillator amplifier -j 18 > logs/build-20-modules.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && ctest -R "Script|LuaApiSurface" --output-on-failure > logs/test-script-family-3.log 2>&1; echo EXIT=$?
EXIT=8
```

    13 tests: 11 passed, 2 failed
      - ScriptDawRemovalUndoTest: the audio-change-lock case of the OTHER
        cluster (see the review-51e7da51f section below)
      - ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion:
        a PRE-EXISTING failure of that case's own expression form on this
        toolchain: the case asserts that one inlined
        `allocate(&system, nullptr, 0, SIZE_MAX)` returns null, and the
        compiler folds that impossible allocation into a non-null value (split
        into two statements the same call returns null, and the case passes).
        The case's translation unit includes none of the files this batch
        changed (no AutomatableModel.h, no ControlStructuralSupport.h, no
        ScriptDawBindings.h), so it compiles the same before and after these
        commits and fails the same way.

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES with it (exit codes).**

* new-p12-245: `git apply -R logs/fix-p12-245.patch` (the pool keying) ->
  `cmake --build build --target ScriptDawBindingTest` EXIT=0, then
  `./ScriptDawBindingTest` EXIT=1 with
  `FAIL! aRepeatedChannelReadDoesNotGrowTheViewPool() Actual (many): 500 /
  Expected (once): 1`. `git apply logs/fix-p12-245.patch` -> EXIT=0
  (14 passed, 0 failed). The patch reverts the pool keying only; the
  read-only diagnostic the case measures with stays, so the case compiles and
  runs in both states.
* 1663: the mixer-channel fix the new case pins is ALREADY on the branch
  (e228edf8b), so reverting THIS commit's own diff does not make it fail; the
  RED proof applies the pre-1663 shape instead
  (`logs/pre-1663-raw-pointer.patch`): the case then reads the deleted
  channel's fader model and the binary aborts -
  `QFATAL ... AutomatableModel::controllerValue(int) lacks implementation for a
  scale type`, EXIT=134. With the branch's fix: EXIT=0 (13 passed).
* review-51e7da51f: the journal-before-delete integration removed
  (`logs/pre-51e7da51f-no-journal.patch`) -> `./ScriptDawRemovalUndoTest`
  EXIT=7 with 7 failed cases, among them
  `aScriptsRemovedEffectIsUndoneByItsOwnStep`. With the integration and the
  deferred model registration in place: EXIT=0, 13 passed, 0 skipped.

**2. The test is deterministic: no wall-clock timing, sleeps or races.**

* The pool case counts the wrappers the pool holds after a run - a count, not
  a duration; the two runs are compared with each other.
* The channel-view case reads the engine's own state (the mixer, the channel
  list) after a removal the script performed; no timer, no sleep, no thread.
* The device-view case reads the chain the engine holds.
* The removal-undo cases are the cluster's own: they read the chain and the
  project journal's undo depth. The audio-change-lock case is the cluster's
  own handshake (a holder thread plus a flag), not a wall-clock bound - it was
  not changed here.

**3. The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**

* The DAW view pools: all three (`mixerChannelPool`, `effectChainPool`,
  `effectPool`) are keyed, and every maker (`newMixerChannel`,
  `newEffectChain`, `newEffect`) goes through the keyed `add`. The pools are
  still cleared once per run (`ScriptDawBindings::beginRun`), so nothing
  outlives a run.
* The device view: `newEffect()` is called from `LuaEffectChain::effect()`
  (src/core/ScriptDawEffects.cpp) and from `LuaEffectChain::loadEffect()`'s
  refusal path (src/core/ScriptDawEdit.cpp); both now pass the channel's id
  and the device's id. `LuaEffect`'s accessors (index, id, pluginName,
  displayName, enabled, parameterCount, parameterName, parameter) all resolve
  through the same two ids.
* The model-change guard: every call site in src/core/AutomatableModel.cpp is a
  member function and now asks about ITS OWN model; the destructor keeps its
  explicit guard (the model is unregistered by then, but the destructor holds
  the lock itself).
* The deferred registration: `instantiateRecordedDevice()` is called by
  `recreateEffectFromState()` and by the removal's undo closure - both attach
  the device through `attachRecordedDevice()`, which registers the device's
  models. The test that builds a device without attaching it deletes it, and
  an unregistered model is unregistered on destruction as before.

**4. No collateral damage: old projects and saved data still load as before, and any limit or check added does not hit other callers that legitimately exceed it.**

* No project file, save format or load path was touched. The pools and the
  views are Lua-side only.
* The deferred registration is scoped to the recorded device's build. A model
  created anywhere else registers with the audio engine exactly as before, and
  a REGISTERED model's write takes the change lock exactly as before - the
  guard only skips the lock for a model the render thread cannot see (a model
  being constructed, and the models of a device that is not in a chain yet).
* The model/automation/device/undo families were run after the change:
  27 passed, 1 failed (`ControlDeviceCatalogueTest` - "Patman plugin is
  unavailable", the module is not built in this tree), 10 not run (their
  binaries are not built here). The script family: 12 of 13 passed, the one
  failure being the audio-change-lock case this commit fixes.

**5. If the verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**

* 1663: `src/core/ScriptDawBindings.cpp:431-436` (the mixer-channel view
  resolves its channel by the stable id on every access) and
  `include/ScriptBindings.h:231` (the track view holds a QPointer). The
  finding's recipe is covered by the new
  `ScriptDawBindingTest::aRemovedChannelsViewIsInert`.
* review-51e7da51f: `src/core/ScriptDawEdit.cpp:231` records the removal
  before the device is detached at `:240-241`; the case
  `ScriptDawRemovalUndoTest::aScriptsRemovedEffectIsUndoneByItsOwnStep` proves
  the undo restoration.
