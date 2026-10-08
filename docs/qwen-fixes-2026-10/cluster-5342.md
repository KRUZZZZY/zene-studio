# Cluster 5342 — the scheduled-source cap counts characters, not bytes

Verdict: **FIXED**

## Finding

`src/core/ScriptClock.cpp:230` (historical): `setSchedule` gated the scheduled
Lua source with `source.size() > SourceByteCap`. `QString::size()` counts UTF-16
code units, but `SourceByteCap` (`include/ScriptClock.h:162`,
`static constexpr int SourceByteCap = 65536`) is documented as BYTES — the
transaction records and the compile check carry the UTF-8 encoding — and the
refusal message itself says "the source is %1 bytes". A multi-byte source
inside the unit count but over the byte cap passed the check.

## Fix

`src/core/ScriptClock.cpp:255-261` — measure the UTF-8 encoding and report its
byte count:

```cpp
// The cap is in BYTES - the transaction records and the compile check carry
// the UTF-8 encoding - so count the encoded size, not the UTF-16 units.
const int sourceBytes = source.toUtf8().size();
if (sourceBytes > SourceByteCap)
{
    *error = QStringLiteral("the source is %1 bytes; a scheduled script is capped at %2")
        .arg(sourceBytes).arg(SourceByteCap);
    return false;
}
```

Behaviour-preserving otherwise: ASCII sources have identical unit and byte
counts, so the existing ASCII cap test (`registryRefusalsAreTyped`,
`tests/src/core/ScriptClockTest.cpp:273`) keeps its exact message and outcome.

## Regression

New QtTest slot `byteCapCountsBytes` in the existing family file
`tests/src/core/ScriptClockTest.cpp` (declaration line 135, body line 305):
a valid-Lua comment source of 40003 UTF-16 units / 80003 UTF-8 bytes must be
refused with the byte count in the message, and a 60003-byte multi-byte source
must still be accepted.

## Evidence table

RED (original code, test added first):

```
$ cmake --build build --target ScriptClockTest > LOGS/build-2.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && ctest -R ScriptClockTest --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?
EXIT=8
```

test-1.log: `FAIL! : ScriptClockTest::byteCapCountsBytes() '!clock.setSchedule(QStringLiteral("lc-t-bytes"), oversized, QStringLiteral("bar"), 0, &error)' returned FALSE.` — `Totals: 10 passed, 4 failed, 0 skipped, 0 blacklisted, 1233ms` (the oversized source was scheduled, which also cascaded three later registry slots).

GREEN (after the fix, zene + test rebuilt):

```
$ cmake --build build --target zene ScriptClockTest > LOGS/build-3.log 2>&1; echo EXIT=$?
EXIT=0
$ cd build/tests && ctest -R ScriptClockTest --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?
EXIT=0
```

test-2.log: `100% tests passed, 0 tests failed out of 1` (ctest #204 ScriptClockTest; its 14 QtTest slots all pass).

Gates (after the final edit):

```
$ bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?
COMPLEXITY_EXIT=0
$ bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo EXIT=$?
LENGTH_EXIT=0
```

Family count: `ctest -R ScriptClockTest` selects exactly 1 ctest (ScriptClockTest, #204).

Note: `build-1.log` (EXIT=2) was a failed compile of my own test edit — a
`QLiteralString` typo, fixed before build-2.log; no production code was
involved.

## Files committed

- `src/core/ScriptClock.cpp` (fork-NEW, already in `tests/fork-sources.txt:1557`)
- `tests/src/core/ScriptClockTest.cpp` (existing family file, already in `tests/all-sources.txt:2531`)
- `docs/qwen-fixes-2026-10/cluster-5342.md` (this report)

No new product files; no inherited files changed, so `upstream-modifications.txt`
needs no entry. No control commands added.
