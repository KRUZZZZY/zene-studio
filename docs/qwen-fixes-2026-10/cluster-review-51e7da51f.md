# Cluster review-51e7da51f - three findings in src/core/ScriptDawBindings.cpp

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
