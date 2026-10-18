# Cluster new-p12-229 — Lua object views can dereference deleted project objects

**Verdict:** FIXED
**Difficulty (finding):** hard / needs_context
**Confidence:** high

Rework round 3 adds the missing `LuaInstrument` view transition regression on top
of the round 2 QPointer fix. The production change is unchanged: every Lua wrapper
member that named a project object is a `QPointer`, so a destroyed target clears
the handle and every accessor takes its existing null-wrapper sentinel branch.

## What changed this round

- `tests/src/core/ScriptBindingsTest.cpp` gains
  `instrumentViewInvalidatedWhenTargetDestroyed()`. It adds an instrument track to
  the PatternStore, calls `InstrumentTrack::loadInstrument(QString())` so the
  `Instrument::instantiate()` fallback produces a live `DummyInstrument` without
  any plugin module, saves the resulting `LuaInstrument` view, asserts it is
  valid, then replaces the instrument with a second `loadInstrument(QString())`
  (which deletes the old `Instrument`) and asserts the saved view is now invalid
  with `name()` empty, `parameterCount() == 0`, `parameterName(0)` empty and
  `parameterModel(0)` invalid. `ScriptBindingsTest` now runs 29 Qt cases (was 28).

No production code changed this round; the `LuaInstrument` QPointer and its
out-of-line constructor were already in place from round 2.

## Evidence table

| # | Command | EXIT | Log |
|---|---------|------|-----|
| 1 | `cmake --build build --target zene ScriptBindingsTest -j12` (GREEN build) | 0 | `build-1.log` |
| 2 | from `build/tests`: `ctest -R '^ScriptBindingsTest$' --output-on-failure` | 0 — 29 passed | `test-1.log` |
| 3 | `git apply -R …/fix.patch` (production hunks reversed) | 0 | `fix.patch` |
| 4 | `cmake --build build --target zene ScriptBindingsTest -j12` (reversed) | 0 | `build-2.log` |
| 5 | from `build/tests`: `ctest -R '^ScriptBindingsTest$' --output-on-failure` (RED) | 8 — 26 passed, 3 failed | `test-2.log` |
| 6 | `git apply …/fix.patch` (restored) | 0 | — |
| 7 | `cmake --build build --target zene ScriptBindingsTest -j12` (restored) | 0 | `build-3.log` |
| 8 | from `build/tests`: `ctest -R '^ScriptBindingsTest$' --output-on-failure` (GREEN) | 0 — 29 passed | `test-3.log` |
| 9 | `bash tests/complexity-gate.sh --check` | 1 — pre-existing, list unchanged | `gate-cc.log` |
| 10 | `bash tests/file-length-gate.sh --check` | 1 — pre-existing, list unchanged | `gate-fl.log` |

RED detail (reversed production hunks, test file untouched):
`ScriptBindingsTest::instrumentViewInvalidatedWhenTargetDestroyed()` fails
`'!saved.isValid()' returned FALSE`; the two pre-existing saved-view cases also
fail as before. GREEN detail (restored): 29 passed, 0 failed.

## Files committed

- `tests/src/core/ScriptBindingsTest.cpp` — new
  `instrumentViewInvalidatedWhenTargetDestroyed()` case.
- `docs/qwen-fixes-2026-10/cluster-new-p12-229.md` — this report.

Production files (`include/ScriptBindings.h`, `src/core/ScriptBindings.cpp`,
`src/core/ScriptDawBindings.cpp`) are unchanged this round; their round-2 QPointer
fix is what the new regression now also guards.