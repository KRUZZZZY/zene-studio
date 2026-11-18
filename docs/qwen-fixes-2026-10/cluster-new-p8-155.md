# cluster new-p8-155 — A valid JSON array resets the saved MIDI transform

Verdict: **FIXED**

## Evidence

`src/core/MidiInputTransform.cpp` (`MidiInputTransform::load`, was line 238):

```cpp
const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
if (document.isNull()) { return; }
MidiInputTransform candidate;
QString ignored;
if (!candidate.fromJson(document.object(), &ignored)) { return; }
```

`"[]"` parses as a valid `QJsonDocument` (an array, so `isNull()` is false), and
`document.object()` answers an **empty object** for an array document. The fresh
candidate accepts the empty object (every field undefined), and `load()` then
replaces the track's transform with the identity — silently clearing what the
user had set.

Fix: require `document.isObject()` before constructing and applying the
candidate (`!document.isObject()` also covers parse failures and JSON `null`,
preserving the previous guard's behaviour for those).

## Regression

Added `aValidJsonArrayIsNotATransform()` to the existing
`tests/src/core/MidiInputTransformTest.cpp`: a transform with
`transpose = 12` in force, loaded through `inputtransform="[]"` and
`inputtransform="\"transpose\""` (valid JSON, not an object), must keep
`transpose = 12` and stay active.

## Commands and exit codes

RED (original code, new test):

```
cmake --build build --target MidiInputTransformTest > $LOGS/build-1.log 2>&1   EXIT=0
ctest -R MidiInputTransformTest --output-on-failure > $LOGS/test-1.log 2>&1    EXIT=8
```

`test-1.log` shows `FAIL! : MidiInputTransformTest::aValidJsonArrayIsNotATransform() Compared values are not the same`
and `Totals: 12 passed, 1 failed` (1 test executed, 1 failed out of 1).

GREEN (after fix):

```
cmake --build build --target MidiInputTransformTest > $LOGS/build-2.log 2>&1   EXIT=0
ctest -R MidiInputTransformTest --output-on-failure > $LOGS/test-2.log 2>&1    EXIT=0
```

`test-2.log` shows `1/1 Test #176: MidiInputTransformTest ... Passed` and
`100% tests passed, 0 tests failed out of 1` (1 test executed).

Gates:

```
bash tests/complexity-gate.sh --check    EXIT=0
bash tests/file-length-gate.sh --check   EXIT=0
```

## Files committed

- `src/core/MidiInputTransform.cpp` (the fix)
- `tests/src/core/MidiInputTransformTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p8-155.md` (this report)

All three were already registered in `tests/fork-sources.txt`; no new files.
