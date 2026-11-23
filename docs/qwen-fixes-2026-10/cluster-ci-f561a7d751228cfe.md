FIXED

# cluster ci-f561a7d751228cfe: CI regression, build at 21c2f3ee2e0d

## Classification
Test-code build defect (REAL). It is not a product bug and not an infrastructure problem. Hosted `build / linux-arm64` (`-DUSE_WERROR=ON`, Qt 5) stopped at
`tests/src/gui/ShortcutsPageTest.cpp:58:45: error: ignoring return value of 'bool QTest::qWaitForWindowExposed(QWidget*, int)', declared with attribute 'nodiscard' [-Werror=unused-result]`.
On 21c2f3ee2e0d (this branch's HEAD at the start of the session), `tests/src/gui/ShortcutsPageTest.cpp:58` and its sibling `tests/src/gui/ShortcutsCommandsTest.cpp:130` both called `QTest::qWaitForWindowExposed(...)` and discarded the bool result. Qt declares the function Q_REQUIRED_RESULT (Qt 5 `qtestsupport_widgets.h:51`) and `[[nodiscard]]` (Qt 6). The local build tree carries `-Wno-error=unused-result`, which is why the defect compiled here.

I reproduced it locally on this commit: the regression check recompiles the real translation units with `-Werror=unused-result` and gets the same error (`test-4-red.log`: `ShortcutsPageTest.cpp:58:45: error: ignoring return value of 'bool QTest::qWaitForWindowExposed(QWidget*)', declared with attribute 'nodiscard'`).

## Fix
- `tests/src/gui/ShortcutsPageTest.cpp:58` and `tests/src/gui/ShortcutsCommandsTest.cpp:130` now read `QVERIFY(QTest::qWaitForWindowExposed(...))`, the same idiom as the sibling tests (for example `tests/src/gui/TouchLayoutTest.cpp:169`). A window that never appears now fails `initTestCase` instead of being ignored.
- I searched `tests/src` for every statement that starts with `QTest::qWaitFor*(`. These two lines were the only ones.

## Regression test
I extended the existing ctest `QWaitForResultCheck` (`tests/qwaitfor-result-check.py`, registered in `tests/CMakeLists.txt` for cluster ci-d1d791015ce10efd):
- `SOURCES` now includes the two Shortcuts tests. They are recompiled from `compile_commands.json` with `-Werror=unused-result`.
- A new fixture pair for `qWaitForWindowExposed` shows that these flags reject a discarded result and accept a used one.
- A textual sweep over `tests/src/**/*.{cpp,h}` fails on any statement that starts with `QTest::qWaitFor*(`, so future siblings are caught without compiling every unit.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/ci-f561a7d751228cfe.r1/logs)
- `cmake --build build --target UpdateCheckTest ShortcutsPageTest ShortcutsCommandsTest -j4 > build-2.log` : EXIT=0
- `ctest -R '^QWaitForResultCheck$' --output-on-failure > test-2-green.log` : EXIT=0
- `ctest -R '^(ShortcutsPageTest|ShortcutsCommandsTest)$' --output-on-failure > test-3-family.log` : EXIT=0 (100% tests passed out of 2)
- `git diff -- tests/src/gui/ShortcutsPageTest.cpp tests/src/gui/ShortcutsCommandsTest.cpp > fix.patch && git apply -R fix.patch` : EXIT=0
- `cmake --build build --target ShortcutsPageTest ShortcutsCommandsTest -j4 > build-3-reverted.log` : EXIT=0
- `ctest -R '^QWaitForResultCheck$' --output-on-failure > test-4-red.log` : EXIT=8. Both Shortcuts files fail to compile, and the sweep reports `ShortcutsPageTest.cpp:58` and `ShortcutsCommandsTest.cpp:130`. The fixtures and UpdateCheckTest pass.
- `git apply fix.patch` : EXIT=0
- `cmake --build build --target ShortcutsPageTest ShortcutsCommandsTest -j4 > build-4-reapplied.log` : EXIT=0
- `ctest -R '^QWaitForResultCheck$' --output-on-failure > test-5-green.log` : EXIT=0 (100% tests passed out of 1)
- `python3 .../feature_check.py <worktree> > gates-1.log` : EXIT=0, RESULT: PASS (all 8 gates PASS)

## Constraints
- No product code changed. Nothing touches the audio thread, undo or control commands.
- No assertion was weakened: the result is now checked where before it was ignored.
- I did not reproduce the failure on arm64/Qt 5 hardware. The local reproduction uses Qt 6, whose declaration is also `[[nodiscard]]`, with the hosted `-Werror=unused-result`.

Delegation: Sonnet extended `tests/qwaitfor-result-check.py` (fixture pair, sweep, sources) and ran the first RED. It failed for a different reason: the `.moc` files had not been generated yet. Haiku built the targets, then ran green, revert/red, re-apply/green, and logged every exit code. I diagnosed the failure, applied the two one-line fixes, reviewed the diffs, re-checked the logs, ran the gates and wrote this report.

## Other findings
- Both Shortcuts tests still call `QTest::qWait(100)` after the window is exposed. That is a fixed sleep in existing test code, and I left it unchanged here.
