# cluster new-p8-111 — Empty numeric attributes are treated as absent

Verdict: **FIXED**

## Finding
`src/core/DawProjectReadShared.h` (historical line 155): `doubleAttribute` and
`intAttribute` chose the absent-attribute fallback with `value.isEmpty()`, so a
present-but-empty numeric attribute (`<RealPoint time="">`) read as the default
(time zero) instead of being refused as malformed.

## Evidence
- `src/core/DawProjectReadShared.h:155-156` (before): `const QString value = attribute(...); if (value.isEmpty()) { return fallback; }` —
  `attribute()` returns an empty string for BOTH a missing and a present-but-empty attribute, so the two were indistinguishable.
- Same shape in `intAttribute` (before, line 171-172).
- `src/core/DawProjectRead.cpp:310`: `parseDocument` ends with `return state.error.isEmpty();` —
  an error recorded by `malformedNumericAttribute` therefore refuses the document.
- Fix now at `src/core/DawProjectReadShared.h:157` and `:177`:
  `if (!attributes.hasAttribute(name)) { return fallback; }` guards the absent-attribute
  fallback; a present-but-empty value falls through to `malformedNumericAttribute`.
- Regression test added at `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:966`
  (`emptyNumericAttributesAreNotAbsent()`): empty `value` on `<Tempo>`, empty `time` on
  `<RealPoint>`, empty `numerator` on `<TimeSignature>` must all be refused.

## Commands and exit codes (logs in ~/.local/share/zene-bunny-fix/qwen/runs/new-p8-111.r1/logs)
RED — regression on the ORIGINAL code:

```
cmake --build build --target DawProjectInterchangeRoundTripTest > LOGS/build-1.log 2>&1
```
build-1.log ends `[100%] Built target DawProjectInterchangeRoundTripTest` (no errors).

```
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-1.log 2>&1
EXIT=8
```
test-1.log: `FAIL! : DawProjectInterchangeRoundTripTest::emptyNumericAttributesAreNotAbsent()
'!dawProjectModelFromXml(emptyTempo, &model, &report, &error)' returned FALSE. (an empty tempo
value was read as the 120 default)` — 1 test run, 1 failed.

GREEN — after the fix (header rebuilt into zene and the test target):

```
cmake --build build --target zene DawProjectInterchangeRoundTripTest > LOGS/build-2.log 2>&1
```
build-2.log: 24 `Built target` lines, no errors.

```
cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > LOGS/test-2.log 2>&1
EXIT=0
```
test-2.log: `1/1 Test #210: DawProjectInterchangeRoundTripTest ... Passed` —
`100% tests passed, 0 tests failed out of 1`.

Gates (unpiped exit codes):

```
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; echo EXIT=$?   → 0
bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; echo EXIT=$?  → 0
```

## Files committed
- `src/core/DawProjectReadShared.h` (product-new file, already in tests/fork-sources.txt)
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp` (existing test file, already registered)
- `docs/qwen-fixes-2026-10/cluster-new-p8-111.md` (this report)
