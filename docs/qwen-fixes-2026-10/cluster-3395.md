# Cluster 3395 — macro values, target windows and what a project may load

Verdict: **FIXED**

3395: FIXED
5266: FIXED

Two findings in `src/core/RackMacros.cpp`, fixed in two commits, each with its
own regression case and its own RED/GREEN proof; the third of this batch
(new-p12-193) follows in the commit after this one.

| cluster | verdict | what was wrong | where |
|---|---|---|---|
| 3395 | FIXED | `std::clamp` keeps a NaN (every comparison against NaN is false), so `addMacro()` / `setValue()` stored a value that is not a number, and a project could load one (`value="nan"`). | `src/core/RackMacros.cpp` — `addMacro`, `setValue`, `loadSettings` |
| 5266 | FIXED | `isValidMacroTarget()` compared the window against its range with `<` / `>` only, so a NaN `low` / `high` / `curve` passed every check, and the loader took the persisted floats as they came. | `src/core/RackMacros.cpp` — `isValidMacroTarget`, `apply`, `loadSettings` |

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
