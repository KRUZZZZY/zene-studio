ALREADY-FIXED

# cluster new-p12-242 — Effect views dereference a deleted channel's chain

Verdict: **ALREADY-FIXED** on branch `bugs/qwen-new-p12-242`. The stale-pointer
device view the finding describes was replaced by a stable-id view in commit
`2b8af3d413bb4889e695dedf942d4ea8fc6d2e98` ("fix(lua): the device view resolves
by stable id (cluster 1663)"), which is an ancestor of this branch's HEAD
(`git merge-base --is-ancestor 2b8af3d413 HEAD` → EXIT=0).

No product code was changed by this run. The only edit is the regression test
it already had, strengthened to cover the two extra calls the finding names
(`fx:id()`, `fx:parameterCount()`); see "What this run changed" below.

## The report's claim, and what the code says today

The finding: `LuaEffect` kept a raw `EffectChain*` and a raw `Effect*`; its
validity check dereferenced the chain without checking that the owning channel
still exists, so a script that kept an effect view across
`mixer.remove_channel` read freed memory.

Every part of that is no longer true of the tree:

| Claim | Code today |
| --- | --- |
| the view holds a `EffectChain*` | it holds a channel id and a device id and nothing else — `include/ScriptDawBindings.h:177-181` (`Effect* m_effective() const;`, `EffectChain* m_chain() const;`, `QString m_mixerChannelId;`, `int m_effectId{-1};`) |
| the view holds an `Effect*` | `include/ScriptDawBindings.h:154-156` — the constructor takes `(const QString& mixerChannelId, int effectId)`; there is no `Effect*` member |
| `isValid()` dereferences the chain without checking the channel | `LuaEffect::isValid()` is `m_effective() != nullptr` (`include/ScriptDawBindings.h:158`), and `LuaEffect::m_chain()` (`src/core/ScriptDawEffects.cpp:124-127`) resolves through `liveChain(m_mixerChannelId)` (`src/core/ScriptDawEffects.cpp:52-57`) on **every** access — it holds no pointer between accesses |
| the effect is resolved through a dangling pointer or a position | `liveEffect()` (`src/core/ScriptDawEffects.cpp:63-72`) walks the *live* chain returned above and matches `effect->id()`; a removed channel yields `nullptr` before the walk, and a removed device is not found |
| `id()` / `index()` can read the freed chain | `LuaEffect::index()` (`src/core/ScriptDawEffects.cpp:134-147`) takes the chain from `m_chain()` and returns `-1` when it is null; `LuaEffect::id()` (`:149-153`) returns an empty string when `index() < 0` |

Why the chain is genuinely `nullptr` once the channel is gone, rather than a
stale pointer:

* `src/core/ScriptDawEffects.cpp:56` → `ScriptDawBindings::mixerChannelEffectChain()`
  (`src/core/ScriptDawBindings.cpp:262-268`) →
  `control::resolveMixerChannel(id, &error)` (`src/core/ControlMixerSupport.cpp:50-84`),
  which walks `Mixer::mixerChannel(i)` and compares `channel->id() == wanted`,
  then reports `ControlErrorKind::NotFound` and returns `nullptr` when no live
  channel carries that id. There is deliberately no positional fallback
  (comment at `src/core/ControlMixerSupport.cpp:59-70`).
* The chain really does die with the channel: `Mixer::deleteChannel()` deletes
  the `MixerChannel` at `src/core/Mixer.cpp:1489` (`delete ch;`), and the chain
  is `MixerChannel::m_fxChain`, a member.

Siblings of the same class, checked for consistency (the "complete at the
cause" item):

* `LuaEffectChain` — id-only, resolves per access (`include/ScriptDawBindings.h:186-206`,
  `src/core/ScriptDawEffects.cpp:87-118`).
* `LuaMixerChannel` — holds `QString m_id` only (`include/ScriptDawBindings.h:127-128`).
* `LuaEffect::parameter()` returns a `LuaFloatModel` that holds
  `QPointer<AutomatableModel>` (`include/ScriptBindings.h:148-152`), so a
  parameter view kept across the device's deletion nulls itself instead of
  dangling. `LuaBoolModel` likewise (`include/ScriptBindings.h:166-167`).
* The view pool is keyed by the device's project-scoped id
  (`src/core/ScriptDawBindings.cpp:284-298`, `control::effectIdOf()` =
  `fx-<Effect::id()>`), and `Effect::id()` comes from the project-scoped
  `ProjectIds` counter (`include/Effect.h:372-377`), so a later device cannot
  inherit a removed device's id and hand back its view.

## Test matrix (step 3) and the state of each row

| Path | Where it is covered | Result this session |
| --- | --- | --- |
| normal path: a live device view reads its own plugin/parameters | `ScriptDawEffectsTest::aDeviceViewOfARemovedChannelReadsNothing`, first `print` (`tests/src/core/ScriptDawEffectsTest.cpp:217`) | PASS (`before valid=true plugin=rackparamtest`) |
| failure path in the finding: keep the view, `mixer.remove_channel`, then `isValid()` / `id()` / `index()` / `parameterCount()` | same test, second `print` (`tests/src/core/ScriptDawEffectsTest.cpp:219-220`), assertion at `:228` | PASS (`after valid=false index=-1 id=[] params=0`) |
| sibling failure path: remove the *device* and keep its view | `ScriptDawEffectsTest::aWrapperKeptAcrossTheDevicesRemovalReadsNothing` (`:173`) | PASS (`after valid=false id=[] plugin=[] params=0 count=0`) |
| the whole recipe on the channel view | `ScriptDawBindingTest::aRemovedChannelsViewIsInert` (`tests/src/core/ScriptDawBindingTest.cpp:581`) | PASS |
| exhaustion / reconfiguration: pool growth, queue-full, pair reservation, removal+undo | `ScriptDawQueueFullTest`, `ScriptDawPairReservationTest`, `ScriptDawRemovalUndoTest`, `ScriptDawModelRegistrationTest`, `ScriptMemoryBudgetTest` | PASS except the pre-existing environment failure named under "Other findings" |
| audio thread | nothing in this report touches `process`/`render` code; the fix that is already in the tree is a read-side resolver (`liveChain`/`liveEffect`), and the diff of this run is a test file only | by design |
| supported platforms | no platform-specific API in the code under test (QString, std::vector only); nothing in this run's diff needs a platform build | not built elsewhere |

## Commands and EXIT codes (copied from the logs)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-242.r1/logs`.

Baseline, with the product code as it stands on this branch:

```
cmake --build build --target ScriptDawEffectsTest ScriptDawBindingTest -j4      EXIT=0   (build-1-baseline.log)
ctest -R 'ScriptDawEffectsTest|ScriptDawBindingTest' --output-on-failure          EXIT=0   (test-1-baseline.log)
   -> "100% tests passed out of 2", both binaries Passed
```

Why the RED had to be reconstructed rather than reverse-applied: the historical
fix's own hunks no longer reverse-apply, because later commits rewrote those
regions.

```
git show 2b8af3d413 -- include/ScriptDawBindings.h src/core/ScriptDawBindings.cpp \
    src/core/ScriptDawEdit.cpp src/core/ScriptDawEffects.cpp > logs/prefix-1663.patch
git apply -R --check logs/prefix-1663.patch                                      EXIT=1
   -> "error: patch failed: src/core/ScriptDawEdit.cpp:388 ... does not apply"
      (same for ScriptDawBindings.cpp and ScriptDawBindings.h; only
       ScriptDawEffects.cpp would have applied)
```

So the pre-1663 *shape* of the device view was restored locally — the wrapper
resolves its chain and device pointer once and thereafter dereferences the
pointer it cached, which is the finding's "retains raw `EffectChain*` and
`Effect*`". Forward patch: `logs/prefix-1663-shape.patch` (34 lines, the two
`mutable` members plus the two one-line caches; the tree was restored with
`git checkout --` afterwards, EXIT=0).

RED, with that shape in place (product code temporarily pre-fix, test as it is
in this commit):

```
cmake --build build --target ScriptDawEffectsTest ScriptDawBindingTest -j4       EXIT=0   (build-5-red.log)
ctest -R 'ScriptDawEffectsTest' --output-on-failure                              EXIT=8   (test-5-red.log)
   -> FAIL! : aWrapperKeptAcrossTheDevicesRemovalReadsNothing
      Actual   "after valid=true id=[] plugin=[rackparamtest] params=7 count=0"
      Expected "after valid=false id=[] plugin=[] params=0 count=0"
   -> Received signal 11 (SIGSEGV), code SEGV_MAPERR, at instruction address
      0x00007f82b6e0c924, accessing address 0x0000000000000049
      (aDeviceViewOfARemovedChannelReadsNothing)
```

The earlier RED run of the same experiment, against the *unstrengthened* test,
also failed (`ctest -R 'ScriptDawEffectsTest|ScriptDawBindingTest'` → EXIT=8,
`test-2-prefix-shape.log`), with `Actual "after valid=true index=-1"` against
`Expected "after valid=false index=-1"` — so both the old and the strengthened
assertion catch the pre-fix shape, and the strengthened one catches it as a
crash of the `isValid()` call the finding names.

GREEN, product code restored (`git checkout --` EXIT=0):

```
cmake --build build --target ScriptDawEffectsTest ScriptDawBindingTest zene -j4  EXIT=0   (build-6-green-restored.log)
ctest -R 'ScriptDawEffectsTest|ScriptDawBindingTest|ScriptDawEditTest' --output-on-failure  EXIT=0   (test-6-green.log)
   -> "100% tests passed out of 2"
cmake --build build --target ScriptDawEffectsTest -j4                            EXIT=0   (build-9-final.log)
./ScriptDawEffectsTest aDeviceViewOfARemovedChannelReadsNothing                  EXIT=0   (test-9-single-case.log)
   -> "PASS : ScriptDawEffectsTest::aDeviceViewOfARemovedChannelReadsNothing()",
      "Totals: 3 passed, 0 failed"
```

Script family (`ctest -R 'Script|ControlAutomationScript|ControllerScript'`,
after building the 14 family binaries that this build tree had never built —
`cmake --build build -j4 --target ... > build-8-script-family.log 2>&1`, EXIT=0):

```
ctest -R 'Script|ControlAutomationScript|ControllerScript' --output-on-failure    EXIT=8   (test-8-script-family.log)
   -> 13 of 16 Passed; ScriptDawEffectsTest, ScriptDawBindingTest,
      ScriptDawRemovalUndoTest, ScriptDawQueueFullTest, ScriptDawPairReservationTest,
      ScriptDawModelRegistrationTest, ScriptBindingsTest, ScriptCommandBridgeTest,
      ScriptImportTest, ScriptClockTest, ScriptStabilisationTest,
      ScriptBudgetActionTest, ControllerScriptTest all Passed
   -> 3 Failed, none of them containing this run's change: see "Other findings"
```

Gates:

```
python3 .../qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1              EXIT=0  (gates-1.log)
   -> RESULT: PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
      check-namespace, no-tautology, doc-figures, complexity "no new item in your
      files", file-length "no new item in your files")
```

## What this run changed

`tests/src/core/ScriptDawEffectsTest.cpp` — one assertion strengthened, nothing
removed or relaxed. The case already removed the owning channel and asserted
`isValid()` and `index()`; it now also asserts `fx:id()` and
`fx:parameterCount()` on the view it kept, which are two of the calls the
finding names and the two that dereference the device rather than the chain.
The expected value became `"after valid=false index=-1 id=[] params=0"`.

No product code, no registration list, no control command, no gate.

## Self-check

* **The regression test FAILS with your product change reverted and PASSES with
  it (exit codes).** The product change is not this run's: it is `2b8af3d413`.
  Its own hunks no longer reverse-apply (`git apply -R --check` → EXIT=1), so
  the pre-fix shape was restored by hand as `logs/prefix-1663-shape.patch`
  (pointer cached in `LuaEffect::m_chain()`/`m_effective()` instead of resolved
  per access — the finding's "retains raw `EffectChain*` and `Effect*`"). RED:
  `ctest -R 'ScriptDawEffectsTest'` → **EXIT=8**, SIGSEGV in
  `aDeviceViewOfARemovedChannelReadsNothing` and a wrong-value FAIL in
  `aWrapperKeptAcrossTheDevicesRemovalReadsNothing` (`test-5-red.log`). GREEN
  after `git checkout --`: **EXIT=0** (`test-6-green.log`, `test-9-single-case.log`).
* **The test is deterministic: no wall-clock timing, sleeps or races.** The
  case drives the Lua surface only: it creates a channel with
  `Engine::mixer()->createChannel()`, appends two `racktest::RackParamEffect`s
  directly, runs one script that takes `channel:chain():effect(1)`, issues
  `zene.command("mixer.remove_channel", {channel = id})` (which the engine
  applies synchronously in that call) and then reads the kept view. There is no
  sleep, no timer and no thread; the only injected value is the channel index
  the test creates. It failed the same way twice (with the unstrengthened and
  the strengthened assertion).
* **The fix is at the cause and complete.** The cause is the wrapper's state,
  and the wrapper now holds stable ids only
  (`include/ScriptDawBindings.h:177-181`); the chain comes from a live-id
  lookup per access (`src/core/ScriptDawEffects.cpp:52-57` →
  `src/core/ControlMixerSupport.cpp:50-84`), the device from a walk of that
  live chain by `Effect::id()` (`src/core/ScriptDawEffects.cpp:63-72`). Every
  reader of the view goes through those two: `isValid`, `index`, `id`,
  `pluginName`, `displayName`, `enabled`, `setEnabled`, `parameterCount`,
  `parameterName`, `parameter` (`src/core/ScriptDawEffects.cpp:124-195`,
  `src/core/ScriptDawEdit.cpp:445-456`). Siblings checked: `LuaEffectChain`,
  `LuaMixerChannel`, `LuaFloatModel`/`LuaBoolModel` (QPointer) — all listed
  above.
* **No collateral damage.** No product code changed, so no saved project, no
  command and no limit changed. The pool key
  (`src/core/ScriptDawBindings.cpp:284-298`) and the id resolution are
  untouched. The one test assertion that changed became stricter, not weaker:
  it still asserts everything it asserted before (`valid=false`, `index=-1`)
  and adds two fields.
* **Sibling commands in the command files the diff touches** (`app.*`, `arrangement.*`, ...): the check lists them because `base..HEAD` on this branch already changed `src/core/ControlCommands*`, not because of this run. This run's diff (`git diff --name-only 10eb11b2a4^ 10eb11b2a4`) is `tests/src/core/ScriptDawEffectsTest.cpp` and this report — no command file, no command id, no reversibility row. `mixer.remove_channel` itself is unchanged; the finding's fix adds no control command (it is read-side resolution in the Lua bindings), so no sibling command can be affected.
* **ALREADY-FIXED evidence.** Commit `2b8af3d413` is an ancestor of HEAD
  (`git merge-base --is-ancestor 2b8af3d413 HEAD` → EXIT=0).
  `include/ScriptDawBindings.h:154-156,177-181` show the id-only state;
  `src/core/ScriptDawEffects.cpp:52-57,63-72,124-132,134-147,149-153` show the
  per-access resolution; `src/core/ControlMixerSupport.cpp:50-84` shows the
  `NotFound` → `nullptr` contract for a removed channel;
  `src/core/Mixer.cpp:1489` shows the chain dying with the channel.

## Other findings (not fixed here — outside this finding's scope)

1. Three script-family tests fail in this build tree for environment reasons,
   none of whose binaries contain this run's change (`git diff --name-only` is
   `tests/src/core/ScriptDawEffectsTest.cpp` only):
   * `ScriptEngineTest::testInstrumentParameterReadWrite` — the new instrument
     is `"dummy"` where the test expects `"TripleOscillator"` (plugin not
     resolved in this tree).
   * `ControlAutomationScriptTest` — four cases fail with "no instrument track
     with parameters in the fresh song" / empty track list, the same missing
     plugin.
   * `ScriptMemoryBudgetTest::aSystemAllocationFailureIsNotReportedAsBudgetExhaustion`
     — allocating `SIZE_MAX` succeeded on this box, so the test's premise
     (that it fails) does not hold here.
2. `control::resolveMixerChannel()` writes `*error` without a null check on its
   first failure path (`src/core/ControlMixerSupport.cpp:55-59`), so a caller
   passing `nullptr` would crash. Not exercised by this finding; left alone.
