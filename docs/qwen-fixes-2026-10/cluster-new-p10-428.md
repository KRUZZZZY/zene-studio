# Cluster new-p10-428 — Invalid time signatures can cause division by zero

Verdict: **FALSE-POSITIVE**

## Finding
`include/TimePos.h:128` — `TimePos::ticksPerBar(const TimeSig&)` divides by
`sig.denominator()`, and `TimePos::ticksPerBeat` (line 99) divides by
`sig.numerator()`. The claim: `TimeSig` accepts any numerator/denominator, so
`TimeSig(4,0)` or `TimeSig(0,4)` would cause division by zero.

## Why this is a false positive
The division-by-zero path is unreachable in this fork: every `TimeSig` that
reaches these helpers is built from `MeterModel`, whose numerator and
denominator are `IntModel`s clamped to `[1, 32]` on every mutation path
(construction, GUI set, XML load, automation clips, controller connections).
The unvalidated `TimeSig(int,int)` constructor is never called anywhere in
product code.

### Evidence (file:line)
1. `include/TimePos.h:128` — `ticksPerBar(sig) = DefaultTicksPerBar * sig.numerator() / sig.denominator()`;
   `include/TimePos.h:99` — `ticksPerBeat(sig) = ticksPerBar(sig) / sig.numerator()`.
2. `src/core/TimePos.cpp:33` — `TimeSig(int,int)` stores values unvalidated;
   `src/core/TimePos.cpp:39` — `TimeSig(const MeterModel&)` copies model values.
3. Only call sites of the sig-taking helpers build `TimeSig` from the song's
   `MeterModel` (`m_timeSigModel`):
   - `include/Song.h:195` — `TimePos::ticksPerBar(m_timeSigModel)`
   - `include/Song.h:201` — `getPlayPos().getBeatWithinBar(m_timeSigModel)`
   - `include/Song.h:206` — `getPlayPos().getTickWithinBeat(m_timeSigModel)`
   - `src/core/ScriptClock.cpp:353` — `const TimeSig timeSig(song->getTimeSigModel());`
4. `src/core/MeterModel.cpp:35-36` — `m_numeratorModel( 4, 1, 32, ... )`,
   `m_denominatorModel( 4, 1, 32, ... )`; ctor arg order confirmed at
   `include/AutomatableModel.h:667` — `IntModel( int val, int min, int max, ... )`.
5. Clamping chain: `src/core/AutomatableModel.cpp:340` — `setValueInternal` does
   `m_value = fittedValue(value)`; `src/core/AutomatableModel.cpp:427,437` —
   `fittedValue` does `std::clamp(value, m_minValue, m_maxValue)` twice;
   ctor clamps at `src/core/AutomatableModel.cpp:68`.
6. XML load path clamps too: `src/core/AutomatableModel.cpp:275` —
   `setValue(...)` from the saved element, and `:296` — `setInitValue(...)`
   (which calls `setValue` at `:711`). `MeterModel::loadSettings`
   (`src/core/MeterModel.cpp:70-71`) delegates to these.
7. `TimeSig(int,int)` has no callers: `rg -n "TimeSig\(" src include tests`
   returns only the declarations (`include/TimePos.h:54-55`) and the
   definitions (`src/core/TimePos.cpp:33,39`).

Since numerator and denominator are always in `[1, 32]`, neither
`/ sig.denominator()` nor `/ sig.numerator()` can divide by zero, and
`ticksPerBar(sig) >= 192*1/32 = 6 > 0`, so the `%` in `getTickWithinBar`
(line 102) is safe as well.

## Commands run (exact, with observed exit codes)
```
$ bash tests/complexity-gate.sh --check > $LOGS/gates-1.log 2>&1; echo EXIT=$?
COMPLEXITY_EXIT=0
$ bash tests/file-length-gate.sh --check > $LOGS/gates-2.log 2>&1; echo EXIT=$?
LENGTH_EXIT=0
$ rg -n "TimeSig\(" src include tests --type cpp -g '!plugins/**'                      # -> search-1.log
EXIT=0
$ rg -n "TimeSig timeSig|ticksPerBar\(m_timeSigModel\)|getBeatWithinBar\(m_timeSigModel\)|getTickWithinBeat\(m_timeSigModel\)" src include --type cpp -g '!plugins/**'
EXIT=0
$ rg -n "m_numeratorModel|m_denominatorModel" src/core/MeterModel.cpp
EXIT=0
$ rg -n "fittedValue|std::clamp" src/core/AutomatableModel.cpp
EXIT=0
```
No build or ctest runs were performed: no product code or test was changed, so
there is no regression to demonstrate (the claimed crash path does not exist).

## Changes
None. No product files touched, no tests touched, no new files to register.
Only this report was added and committed.
