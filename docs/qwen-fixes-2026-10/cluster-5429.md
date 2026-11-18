# Cluster 5429 — Invalid key text silently becomes key 0

Verdict: **FIXED**

## The bug

`src/core/TrackKeyNames.cpp:142` (as loaded):

```cpp
setName(id, key.attribute(QStringLiteral("key")).toInt(), key.attribute(QStringLiteral("name")));
```

`QString::toInt()` returns 0 when the `key` attribute is missing or not a number, so
malformed project data — e.g. `<key name="Kick"/>` inside a `<track>` — assigned that
label to key 0 and overwrote key 0's real name. The sibling `id` attribute already
checks its conversion (`TrackKeyNames.cpp:138-139`, "no id is no track"); the `key`
attribute did not.

## The fix

`src/core/TrackKeyNames.cpp` — check the conversion's `ok` result before calling
`setName`, mirroring the existing `id` guard:

```cpp
bool keyOk = false;
const int keyNumber = key.attribute(QStringLiteral("key")).toInt(&keyOk);
if (!keyOk) { continue; } // no key attribute or not a number: never key 0 by default (review 5429 #1)
setName(id, keyNumber, key.attribute(QStringLiteral("name")));
```

## Regression

New test `tests/src/core/TrackKeyNamesLoadTest.cpp` (registered in `tests/CMakeLists.txt`
LMMS_TESTS and in `tests/fork-sources.txt`; the CMakeLists change is recorded in
`tests/upstream-modifications.txt`). It parses a `<key-names>` section with a valid
`key="0"`, a valid `key="5"`, and a ghost `<key name="Ghost"/>` with no key attribute,
then asserts the valid keys load and the ghost did not become key 0.

## Evidence

| Step | Command (from the worktree unless noted) | EXIT |
|---|---|---|
| RED build (original code) | `cmake --build build --target TrackKeyNamesLoadTest > LOGS/build-9.log 2>&1` | 0 |
| RED run (original code) | `cd build/tests && ctest -R TrackKeyNamesLoadTest --output-on-failure > LOGS/test-5.log 2>&1` | 8 |
| GREEN build (fixed code) | `cmake --build build --target TrackKeyNamesLoadTest > LOGS/build-10.log 2>&1` | 0 |
| GREEN run (fixed code) | `cd build/tests && ctest -R TrackKeyNamesLoadTest --output-on-failure > LOGS/test-6.log 2>&1` | 0 |
| Family (fixed code) | `cd build/tests && ctest -R "NoteEdit\|TrackKeyNames" --output-on-failure > LOGS/test-7.log 2>&1` | 0 (6/6 tests passed) |
| zene rebuild | `cmake --build build --target zene > LOGS/build-11.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-3.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-4.log 2>&1` | 0 |

RED failing assertion (test-5.log):

```
FAIL!  : TrackKeyNamesLoadTest::malformedKeyAttributeIsNotKeyZero() Compared values are not the same
   Actual   (TrackKeyNames::instance().name(7, 0)): "Ghost"
   Expected (QStringLiteral("Real"))              : "Real"
```

LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5429.r1/logs`

## Files committed

- `src/core/TrackKeyNames.cpp` — the fix (already registered in `tests/fork-sources.txt`)
- `tests/src/core/TrackKeyNamesLoadTest.cpp` — new regression test (registered in `tests/fork-sources.txt`)
- `tests/CMakeLists.txt` — registers the new test in LMMS_TESTS (reason appended to its `tests/upstream-modifications.txt` entry)
- `tests/fork-sources.txt` — new-file registration
- `tests/upstream-modifications.txt` — reason for the CMakeLists registration
- `docs/qwen-fixes-2026-10/cluster-5429.md` — this report
