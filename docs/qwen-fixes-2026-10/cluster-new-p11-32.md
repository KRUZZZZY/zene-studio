# cluster new-p11-32 — vca.color_members dirties the project when there is nothing to color

Verdict: **FIXED**

## Finding
`vca.color_members` (MX-067) paints, journals an undo step and marks the project modified even
when the group has no members or every member already wears that colour.

## Evidence
- `src/core/ControlCommandsMixerTools.cpp:401-422` (`colorMembers`): after building `before`
  (the members' current colours) and `after` (all set to the requested colour), it called
  `paint(after)`, `addUndoStep(...)` and `Engine::getSong()->setModified()` unconditionally —
  so an empty group (`before`/`after` empty) or an already-coloured group still journalled and
  dirtied. Sibling commands in the same file (`options` at :85-98, `sendEnable` at :107-129)
  already journal only when the value actually changes; `colorMembers` was the odd one out.

## Fix
`src/core/ControlCommandsMixerTools.cpp` — journal and dirty only when the colours change:

```cpp
std::vector<std::pair<int, std::optional<QColor>>> after;
for (const auto& entry : before) { after.emplace_back(entry.first, color); }
if (after != before) // an empty group, or one already in this colour, changes nothing to journal
{
    paint(after);
    addUndoStep([paint, before]() { paint(before); }, [paint, after]() { paint(after); });
    Engine::getSong()->setModified();
}
```

The reply (`colored`, `color`) is unchanged, so behaviour is preserved for real colourings;
only the no-op journalling/dirtying is removed.

## Regression test
`tests/src/core/MixerToolsTest.cpp` — new case `colouringWhatIsAlreadyColoured()`:
colours an empty group (`colored: 0` reply kept), then a real change (`mixer.channel_eq`
low=12 on channel 4) followed by the no-op colouring, and asserts one `control.undo` still
reverts the real change; then a group whose member already wears the colour is coloured again
and one undo must reach the first colouring. Before the fix the no-op pushed an undo step and
swallowed the undo (`m_eqLowModel` stayed 12); after the fix undo reaches the real change.

## Commands and exit codes (logs in ~/.local/share/zene-bunny-fix/qwen/runs/new-p11-32.r1/logs)
RED (test on original code):
- `cmake --build build --target MixerToolsTest > LOGS/build-2.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R MixerToolsTest --output-on-failure > LOGS/test-1.log 2>&1` → EXIT=8
  (1 test run; FAIL: `channel(4)->m_eqLowModel.value()` actual 12, expected 0)

GREEN (after fix):
- `cmake --build build --target MixerToolsTest > LOGS/build-4.log 2>&1` → EXIT=0
- `QT_QPA_PLATFORM=offscreen ctest -R MixerToolsTest --output-on-failure > LOGS/test-3.log 2>&1` → EXIT=0
  ("100% tests passed, 0 tests failed out of 1")

Reverse-patch proof (test catches the bug):
- `git diff -- src/core/ControlCommandsMixerTools.cpp > LOGS/fix.patch`
- `git apply -R LOGS/fix.patch` → OK; `cmake --build build --target MixerToolsTest > LOGS/build-5.log 2>&1` → EXIT=0;
  `ctest -R MixerToolsTest ... > LOGS/test-4.log 2>&1` → EXIT=8 (FAIL without the fix)
- `git apply LOGS/fix.patch` → OK; `cmake --build build --target MixerToolsTest > LOGS/build-6.log 2>&1` → EXIT=0;
  `ctest -R MixerToolsTest ... > LOGS/test-5.log 2>&1` → EXIT=0 (PASS with the fix)

Gates:
- `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` → EXIT=0
- `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` → EXIT=0

Product rebuild:
- `cmake --build build --target zene > LOGS/build-7.log 2>&1` → EXIT=0

Note: an intermediate run (build-3.log EXIT=0, test-2.log EXIT=8) was a test-side mistake —
`addMember(1)` is refused because channel 1 already belongs to the Drums group
(`VcaGroup::addMember` rejects channels in another group, `src/core/VcaGroup.cpp:105-121`);
the test now uses channel 3.

## Files committed
- `src/core/ControlCommandsMixerTools.cpp` (fix)
- `tests/src/core/MixerToolsTest.cpp` (regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p11-32.md` (this report)
