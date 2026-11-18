# Cluster 3395 — macro values, target windows and what a project may load

Verdict: **FIXED**

3395: FIXED
5266: FIXED
new-p12-193: FIXED

Three findings in `src/core/RackMacros.cpp`, fixed in three commits, each with
its own regression case and its own RED/GREEN proof.

| cluster | verdict | what was wrong | where |
|---|---|---|---|
| 3395 | FIXED | `std::clamp` keeps a NaN (every comparison against NaN is false), so `addMacro()` / `setValue()` stored a value that is not a number, and a project could load one (`value="nan"`). | `src/core/RackMacros.cpp` — `addMacro`, `setValue`, `loadSettings` |
| 5266 | FIXED | `isValidMacroTarget()` compared the window against its range with `<` / `>` only, so a NaN `low` / `high` / `curve` passed every check, and the loader took the persisted floats as they came. | `src/core/RackMacros.cpp` — `isValidMacroTarget`, `apply`, `loadSettings` |
| new-p12-193 | FIXED | The loader took the macro's own value and the target window as they came: a project with `value="2"` and a window `2..3` loaded out of range, and applying it drove the parameter to its maximum for every macro value. | `src/core/RackMacros.cpp` — `loadSettings` |

## 3395 — NaN macro values survive clamping

**Verdict: FIXED.**

### The bug

`RackMacros::addMacro()` and `RackMacros::setValue()` clamped the value with
`std::clamp(value, 0.0f, 1.0f)`. `std::clamp` is
`(v < lo) ? lo : (hi < v) ? hi : v`, and **every comparison against NaN is
false** — so NaN was handed straight back and stored, despite the 0..1
contract. The same hole was open on the load path: `QString::toFloat()` reads a
`"nan"` (or `"inf"`) attribute as exactly that (measured with Qt 6.12:
`QString("nan").toFloat()` → `nan`, `ok=1`), so a project file could carry a
NaN macro value.

A NaN that reaches `apply()` is written into every parameter model the macro
drives: `shapeMacroValue()` clamps NaN to NaN, `std::pow(NaN, x)` is NaN, and
`model->setValue(NaN)` stores a number that is not a number.

The agent surface could reach it too: the schema property is
`{"type":"number","minimum":0,"maximum":1}` (`src/core/ControlCommandsRackMacros.cpp:66`),
and `ControlSchema.cpp:88-94` compares with `<` / `>`, so a NaN passed the
range check.

### The fix

* `src/core/RackMacros.cpp` — a new file-local helper
  `isFiniteMacroValue()`; the value is checked before it is stored:
  * `addMacro()` returns **-1** and adds nothing for a nonfinite value;
  * `insertMacro()` returns **false** for a macro whose value is not finite;
  * `setValue()` returns **false** and the macro keeps the value it had;
  * `apply()` writes nothing for a macro whose own value is not finite (the
    backstop right before a parameter model);
  * `loadSettings()` stores **0** for a loaded value that is not a number, so
    the macro still works and the loaded state is a number.
* `include/RackMacros.h` — the three contracts are documented on the
  declarations (`addMacro`, `insertMacro`, `setValue`).
* `src/core/ControlCommandsRackMacros.cpp` — `rack.macro_add` refuses the
  value `addMacro()` would not take (`InvalidArgs`), and `rack.macro_set`
  refuses a nonfinite value before it writes anything.
* `src/core/ControlCommandsRackMacroPair.cpp` — `rack.macro_set_pair` checks
  both values before either is written, so the pair is still "set whole or
  refused whole".

Every caller of the changed functions was checked: the two command files
above, `ControlRackSupport.cpp:212` (the undo restore, whose value comes from
the recorded state and is finite), `ControlCommandsRackMacroPair.cpp:58`
(`applyMacroValue`, whose value now comes from the checked argument) and
`Rack.cpp:424` (the loader). No other caller stores a macro value.

### Regression test

`tests/src/core/RackMacrosTest.cpp` — new case
`aNonfiniteMacroValueIsRefused()`: `addMacro(NaN)` returns -1 and adds
nothing, `setValue(NaN)` / `setValue(inf)` are refused and keep the old value,
`insertMacro` refuses a macro whose value is NaN, a project with `value="nan"`
loads **0** (a number), `rack.macro_add` and `rack.macro_set` answer
`InvalidArgs` for a NaN value, and the macro list is unchanged afterwards.

### Commands run (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3395.r1/logs)

From the worktree root unless noted. `LOGS` is that folder.

```
cmake --build build --target RackMacrosTest > $LOGS/build-a1.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-a1.log 2>&1; echo EXIT=$?
EXIT=8   # RED (first pass): only the new case failed —
         #   "FAIL! : RackMacrosTest::aNonfiniteMacroValueIsRefused()
         #    'std::isfinite(reloaded.macro(0)->value)' returned FALSE"
         #   "Totals: 18 passed, 1 failed"

cmake --build build --target RackMacrosTest > $LOGS/build-a2.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-a2.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN: "100% tests passed out of 1"

# 4b: the product change reverted, the test must fail again
git diff -- src/core/RackMacros.cpp include/RackMacros.h \
    src/core/ControlCommandsRackMacros.cpp src/core/ControlCommandsRackMacroPair.cpp \
    > $LOGS/fix-3395.patch
git apply -R $LOGS/fix-3395.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-a3-red.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-a3-red.log 2>&1; echo EXIT=$?
EXIT=8   # RED with the fix reverted:
         #   "FAIL! : RackMacrosTest::aNonfiniteMacroValueIsRefused() Compared values are not the same"
         #   "Totals: 18 passed, 1 failed, 0 skipped"

git apply $LOGS/fix-3395.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-a4-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-a4-green.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN with the fix restored: "100% tests passed out of 1"

cmake --build build --target zene > $LOGS/build-a5-zene.log 2>&1; echo EXIT=$?
EXIT=0

cmake --build build --target RackTest RackZonesTest RackZoneFeedRunTest \
    RackZoneRoutingTest EffectRackAddTest RackDeferredLifetimeTest RackMenuTest \
    > $LOGS/build-a7-rackfamily.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R 'Rack' --output-on-failure > $LOGS/test-a8-rack-family.log 2>&1; echo EXIT=$?
EXIT=0   # the whole rack family: "100% tests passed out of 8" (8 tests)
```

## 5266 — NaN macro target values can reach parameter models

**Verdict: FIXED.**

### The bug

`isValidMacroTarget()` checked the window with range comparisons only:

```cpp
if (target.low < 0.0f || target.low > 1.0f) { return false; }
if (target.curve < -1.0f || target.curve > 1.0f) { return false; }
return target.high >= 0.0f && target.high <= 1.0f;
```

**Every comparison against NaN is false**, so a NaN `low`, `high` or `curve`
passed all of them: the target was accepted as a valid fraction pair, bound to
a parameter, and applied. `shapeMacroValue(value, NaN)` is
`std::pow(x, std::pow(4.0f, -NaN))` = NaN, so the applied fraction was a NaN
and `model->setValue(NaN)` wrote a number that is not a number into the
parameter model.

The loader had the same hole: `loadSettings()` took the persisted `low`,
`high` and `curve` as they came, and `QString::toFloat()` reads a `"nan"`
attribute as exactly a NaN.

### The fix

* `src/core/RackMacros.cpp` — the window is checked as what it is:
  * two new file-local helpers, `isMacroFraction()` (a finite number inside
    0..1) and `isMacroCurve()` (a finite number inside -1..1). The finiteness
    is its own check, because the range comparisons cannot say it;
  * `isValidMacroTarget()` uses them, so a NaN bound or curve is refused —
    the agent surface (`rack.macro_target_add`, which already calls
    `isValidMacroTarget()` at bind time) inherits the refusal;
  * `apply()` skips a target whose window carries a nonfinite number: the
    backstop right before a parameter model;
  * `loadSettings()` does not load a persisted target whose window carries a
    nonfinite number.
* `include/RackMacros.h` — `isValidMacroTarget()`'s contract now says that a
  nonfinite bound or curve is refused.

### Regression test

`tests/src/core/RackMacrosTest.cpp` — new case
`aNonfiniteMacroTargetNeverReachesAParameter()`: a macro that drives the
0..100 parameter through a NaN-curve target writes **nothing** and leaves the
model at the value it had (`writes.size() == 0`, the model is still finite);
`isValidMacroTarget()` refuses a NaN `curve`, `low` and `high`; and a project
whose target says `curve="nan"` loads **no** target.

### Commands run (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3395.r1/logs)

```
cmake --build build --target RackMacrosTest > $LOGS/build-b1.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-b1.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN with the fix in place: "100% tests passed out of 1"

# 4b: the product change reverted, the test must fail again
git diff -- src/core/RackMacros.cpp include/RackMacros.h > $LOGS/fix-5266.patch
git apply -R $LOGS/fix-5266.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-b3-red.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-b3-red.log 2>&1; echo EXIT=$?
EXIT=8   # RED with the fix reverted:
         #   "FAIL! : RackMacrosTest::aNonfiniteMacroTargetNeverReachesAParameter()
         #    Compared values are not the same
         #    Actual   (static_cast<int>(writes.size())): 1
         #    Expected (0)                              : 0"
         #   "Totals: 19 passed, 1 failed" - the NaN curve wrote the parameter.

git apply $LOGS/fix-5266.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-b4-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-b4-green.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN with the fix restored: "100% tests passed out of 1"
```

## new-p12-193 — Project-loaded macros bypass range validation

**Verdict: FIXED.**

### The bug

`loadSettings()` read the persisted numbers and stored them as they came:

```cpp
entry.value = macroElement.attribute(QStringLiteral("value"), QStringLiteral("0")).toFloat();
...
target.low = targetElement.attribute(QStringLiteral("low"), QStringLiteral("0")).toFloat();
target.high = targetElement.attribute(QStringLiteral("high"), QStringLiteral("1")).toFloat();
```

A macro's value is a 0..1 scalar and a target's window is a pair of
fractions inside 0..1 - the contract the API enforces (`addMacro()` clamps,
`isValidMacroTarget()` checks) - but the loader enforced neither. A project
with `value="2"` and a window `2..3` therefore loaded:

* a macro whose state reported a value outside 0..1, and
* a window that is not a fraction pair at all. Applied, the fraction is
  `std::clamp(low + shape(value, curve) * (high - low), 0, 1)` =
  `std::clamp(2 + 1, 0, 1)` = **1** for every macro value - the parameter was
  pinned at its maximum and the macro did nothing else.

### The fix

`src/core/RackMacros.cpp`, in `loadSettings()` only:

* the loaded value is **clamped** into 0..1 - the same clamp the API applies
  (`addMacro()`, `setValue()`), so a project that says `value="2"` loads a
  macro at 1.0 and the loaded state is inside the contract. A value that is not
  a number at all stays 0 (cluster 3395);
* a loaded **target** is kept only when `isValidMacroTarget()` accepts it: a
  fraction pair inside 0..1, a curve inside -1..1, a chain and an effect
  index, and a parameter name. A target that is not one is **dropped**, not
  clamped - clamping a window outside 0..1 would keep the parameter pinned at
  its end, which is the bug itself.

Nothing else changed: a project inside the contract loads exactly as it did,
and `saveSettings()` still writes what the list holds.

### Regression test

`tests/src/core/RackMacrosTest.cpp` — new case
`aProjectLoadedMacroIsValidated()`: a project with `value="2"` and a window
`2..3` loads a macro whose value is inside 0..1 (1.0) and **no** target;
applying it writes nothing and leaves the 0..100 parameter where it was
(pre-fix it was driven to its maximum, 100). The same case then loads a
project inside the contract (value 0.5, window 0.25..0.75) and requires it to
load unchanged and to write the same 50 as before.

### Commands run (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3395.r1/logs)

```
cmake --build build --target RackMacrosTest > $LOGS/build-c1.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-c1.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN with the fix in place: "100% tests passed out of 1"

# 4b: the product change reverted, the test must fail again
git diff -- src/core/RackMacros.cpp > $LOGS/fix-p12-193.patch
git apply -R $LOGS/fix-p12-193.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-c2-red.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-c2-red.log 2>&1; echo EXIT=$?
EXIT=8   # RED with the fix reverted:
         #   "FAIL! : RackMacrosTest::aProjectLoadedMacroIsValidated()
         #    'reloaded.macro(0)->value <= 1.0f' returned FALSE."
         #   "Totals: 20 passed, 1 failed" - the project loaded its value of 2.

git apply $LOGS/fix-p12-193.patch
EXIT=0

cmake --build build --target RackMacrosTest > $LOGS/build-c3-green.log 2>&1; echo EXIT=$?
EXIT=0

cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure > $LOGS/test-c3-green.log 2>&1; echo EXIT=$?
EXIT=0   # GREEN with the fix restored: "100% tests passed out of 1"
```

## Files committed

* `src/core/RackMacros.cpp` — the macro value, the target window and the loader.
* `include/RackMacros.h` — the contracts of `addMacro()`, `insertMacro()`,
  `setValue()` and `isValidMacroTarget()`.
* `src/core/ControlCommandsRackMacros.cpp` — `rack.macro_add` and
  `rack.macro_set` refuse a nonfinite value.
* `src/core/ControlCommandsRackMacroPair.cpp` — `rack.macro_set_pair` refuses
  a nonfinite value, whole.
* `tests/src/core/RackMacrosTest.cpp` — three regression cases.
* `docs/qwen-fixes-2026-10/cluster-3395.md` — this report.

No new file, no new command, no change to a schema or a description (so
`tools/mcp-zene-control/zene_control/commands_snapshot.json` stays valid),
no header added to `tests/fork-sources.txt` (every touched file is already
listed there).

## Self-check

**1. The regression test FAILS with the product change reverted and PASSES
with it (exit codes).** Three separate proofs, one per bug - each reverts only
that bug's product diff (the two earlier fixes stay in place):

| cluster | reverted | RED | restored | GREEN |
|---|---|---|---|---|
| 3395 | `git apply -R $LOGS/fix-3395.patch` | build EXIT=0, ctest **EXIT=8** - `FAIL! ... aNonfiniteMacroValueIsRefused ... 'std::isfinite(reloaded.macro(0)->value)' returned FALSE`, "Totals: 18 passed, 1 failed" | `git apply $LOGS/fix-3395.patch` | build EXIT=0, ctest **EXIT=0** - "100% tests passed out of 1" |
| 5266 | `git apply -R $LOGS/fix-5266.patch` | build EXIT=0, ctest **EXIT=8** - `FAIL! ... aNonfiniteMacroTargetNeverReachesAParameter ... Actual writes.size(): 1, Expected: 0`, "Totals: 19 passed, 1 failed" | `git apply $LOGS/fix-5266.patch` | build EXIT=0, ctest **EXIT=0** |
| new-p12-193 | `git apply -R $LOGS/fix-p12-193.patch` | build EXIT=0, ctest **EXIT=8** - `FAIL! ... aProjectLoadedMacroIsValidated ... 'reloaded.macro(0)->value <= 1.0f' returned FALSE`, "Totals: 20 passed, 1 failed" | `git apply $LOGS/fix-p12-193.patch` | build EXIT=0, ctest **EXIT=0** |

In every RED run only the new case failed; the other 18/19/20 cases of
`RackMacrosTest` passed. The whole rack family
(`ctest -R 'Rack'`) passed after each fix: "100% tests passed out of 8"
(logs `test-a8-rack-family.log`, `test-b7-rack-family.log`,
`test-c6-rack-family.log`), and `cmake --build build --target zene` exited 0
after each fix (`build-a5-zene.log`, `build-b5-zene.log`, `build-c4-zene.log`).

**2. The test is deterministic.** No wall-clock, no sleep, no thread, no race.
The seam is the project file the test writes itself (a `QDomDocument` with the
attribute under test) and the fixed inputs it hands the API
(`std::numeric_limits<float>::quiet_NaN()`, `2.0f`, `0.25f`, `"nan"`); the
parameter model is read back through the fixture's `gainModel()`. The shared
fixture is restored through a `QScopeGuard`, so a failing run cannot leave a
NaN or a moved parameter behind for the next case.

**3. The fix is at the cause and complete.** Every caller and sibling of what
changed was checked:

* `addMacro()` / `insertMacro()` / `setValue()` — the three storage doors of a
  macro value. `RackMacros.h` documents all three.
* `apply()` — the one place a macro value or a target window becomes a
  parameter value; it refuses a nonfinite macro value and skips a target
  whose window carries a nonfinite number.
* `isValidMacroTarget()` — the shared validity check; the command layer
  (`rack.macro_target_add`) already called it and inherits the refusal.
* `loadSettings()` — the loader, for the value and for the targets.
* Callers of the changed API: `ControlCommandsRackMacros.cpp`
  (`rack.macro_add`, `rack.macro_set`), `ControlCommandsRackMacroPair.cpp`
  (`rack.macro_set_pair`, through `applyMacroValue`),
  `ControlRackSupport.cpp:212` (the undo restore - it writes back a recorded,
  finite value), `Rack.cpp:424` (the loader). The GUI does not store a macro
  value (`src/gui/MacroKnobs.cpp` drives a knob model, not a macro).

**4. No collateral damage.** A project inside the contract loads exactly as
before: the new-p12-193 case loads `value="0.5"` with the window
`0.25..0.75` and requires the same value, the same window and the same write
(50 on the 0..100 parameter) as before the change. The out-of-range checks
only bite data that is already outside the documented contract: a macro value
outside 0..1 (the API clamps it too), a window outside 0..1 (the API refuses
such a window at bind time), and a nonfinite number (which no API path could
store before this change). A target with an inverted window (`high < low`) is
still legal and still loads - `isValidMacroTarget()` only asks for both bounds
to be inside 0..1. `saveSettings()` is untouched: it writes what the list
holds.

**5. Realtime, undo and commands.** The macro list is control-thread only
(`include/RackMacros.h` says so: "the audio thread never reads a macro"), and
the audio-path allocation test in the same file still reports
`allocations=0`. Nothing here allocates on the audio thread, and no lock, wait
or log was added. No new control command was added, and no command's schema
or description changed. The undo steps are unchanged: `rack.macro_add` and
`rack.macro_set` still record one step, and a refused value records none.

**6. Sibling commands in the command files I changed.** The check lists the
whole rack group; here is each one against this fix:

* the value-taking ones - `rack.macro_add`, `rack.macro_set`,
  `rack.macro_set_pair` - now refuse a nonfinite value with `InvalidArgs`
  instead of storing it, and `macro_set_pair` still refuses the pair whole
  (both values are checked before either is written);
* the target-taking one - `rack.macro_target_add` - refuses a nonfinite
  window through the shared `isValidMacroTarget()`, and a window outside
  0..1 as it always did;
* `rack.macro_remove` and `rack.macro_target_remove` - they take no value and
  no window, and are untouched;
* `rack.add_chain`, `rack.remove_chain`, `rack.get_state`, `rack.set_selected`,
  `rack.split`, `rack.wrap`, `rack.layers`, `rack.layer_add`,
  `rack.layer_remove`, `rack.layer_zone_add`, `rack.layer_zone_remove`,
  `rack.layer_set`, `rack.zone_add`, `rack.zone_remove`, `rack.zone_resolve`
  - these live in `ControlCommandsRack.cpp`, `ControlCommandsRackLayers.cpp`,
  `ControlCommandsRackWrap.cpp`, `ControlCommandsRackZones.cpp` and
  `ControlCommandsDeviceTools.cpp` (`rack.split`), none of which this change
  touches, and none of them reads or writes a macro value or a target window.
  The one that meets the macro list is `rack.remove_chain`, which drops the
  macro targets of the chain it removes through `RackMacros::reindexChains()`
  (`src/core/Rack.cpp:204`) - unchanged by this fix, and covered by the
  existing case `removingAChainReindexesTheMacroTargets()`.

**7. Gates.** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py
<worktree>` exited **0** after each of the three commits
(`gate-a1.log`, `gate-b1.log`, `gate-c1.log`): every row PASS, including
`complexity` and `file-length` ("no new item in your files").

**8. Verdict ALREADY-FIXED or FALSE-POSITIVE.** Not applicable - all three
findings are FIXED, each with its own failing-then-passing regression.
