FIXED

Cluster new-p15-213: `SimpleTextFloat` can return a dangling source object.

## Root cause

`include/SimpleTextFloat.h:63-74` stored the controller as a raw `const QObject* m_source`. `src/gui/widgets/SimpleTextFloat.cpp:87-92` cleared it only in `hide()`. `FloatModelEditorBase` sets itself at `src/gui/widgets/FloatModelEditorBase.cpp:115` and reads the source at `:113`, `:548`, `:563`; `Fader` sets itself at `src/gui/widgets/Fader.cpp:534`; `VstPlugin` reads it at `plugins/VstBase/VstPlugin.cpp:879`. Destroying a controller while the shared text float remains alive left `source()` returning a dangling pointer.

## Fix

- `include/SimpleTextFloat.h`: changed `m_source` to `QPointer<const QObject>` and `setSource` to `m_source = const_cast<QObject*>(source)`. `source()` returns the guarded pointer (null after destruction). Added `#include <QPointer>`.
- `src/gui/widgets/SimpleTextFloat.cpp`: `hide()` now calls `m_source.clear()` (was `= nullptr`).

Qt 5.15 / Qt 6 compatible. No behavior change for live sources; documented callers unchanged.

## Regression test

Added 3 slots to existing target `tests/src/gui/KnobKeyboardTest.cpp` (already generated in `build/tests`, no new CMake target):

- `simpleTextFloatSourceIdentityResetCases`: set/replace/null/hide.
- `simpleTextFloatSourceClearsWhenSourceDestroyed`: destroy active source while float alive; `source()` must be null.
- `simpleTextFloatDestroyedSourceDoesNotClearReplacement`: destroy parent-owned source, set replacement; old destruction must not clear it.

## Evidence

| Check | Command | EXIT |
|---|---|---|
| RED (no fix) | `cmake --build build --target KnobKeyboardTest` + `QT_QPA_PLATFORM=offscreen ./build/tests/KnobKeyboardTest simpleTextFloatSourceClearsWhenSourceDestroyed` | build EXIT=0, test EXIT=1 (`'textFloat.source() == nullptr' returned FALSE`) |
| RED (identity) | same binary, `simpleTextFloatSourceIdentityResetCases` | EXIT=0 (ordinary cases pass pre-fix, as expected) |
| GREEN (fix) | `cmake --build build --target KnobKeyboardTest` + `QT_QPA_PLATFORM=offscreen ./build/tests/KnobKeyboardTest simpleTextFloatSourceClearsWhenSourceDestroyed simpleTextFloatSourceIdentityResetCases simpleTextFloatDestroyedSourceDoesNotClearReplacement` | EXIT=0, all 3 PASS |
| Family | `ctest -R "Knob"` from `build/tests` | EXIT=0, 3/3 passed (KnobKeyboardTest, KnobAccessibleTargetSizingTest, KnobAccessibleNullItemTest) |
| Patch cycle | `git diff -- include/SimpleTextFloat.h src/gui/widgets/SimpleTextFloat.cpp > logs/fix.patch` then `git apply -R fix.patch` + rebuild + test | 2 FAIL, EXIT=1 |
| Patch cycle | `git apply fix.patch` + rebuild + test | 3 PASS, EXIT=0 |
| zene rebuild | `cmake --build build --target zene` | EXIT=0 |
| feature_check | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | EXIT=0, RESULT PASS (8 gates PASS) |

Files changed: `include/SimpleTextFloat.h`, `src/gui/widgets/SimpleTextFloat.cpp`, `tests/src/gui/KnobKeyboardTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p15-213.md`.

## Self-check

- No new CMake target; test added to existing generated `KnobKeyboardTest` target (round-2 blocker avoided).
- Null-getGUI constructor guard (`SimpleTextFloat.cpp:42`) unchanged.
- No audio-thread path: `source()`/`setSource()` used only by GUI controls (`FloatModelEditorBase`, `Fader`) and Qt widget timer (`VstPluginKnob::timerEvent`); no realtime allocation/lock.
- No new control commands, no save-data changes, no gate limits altered.
