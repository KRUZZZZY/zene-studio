ALREADY-FIXED

# cluster ci-48dcc062d04c645f-port: "Port: CI regression: build / linux-x86_64"

Verdict: ALREADY-FIXED for the product build. The Qt 5 compile fix `6474195bd6` is on this branch. Round 5 also repairs
the guard that should have caught the defect, `tests/Qt5QStringListAssignCheck.py`, as the read-first note asked. No
product code is changed.

## What the original failure was

The original cluster `ci-48dcc062d04c645f` (report: `docs/qwen-fixes-2026-10/cluster-ci-48dcc062d04c645f.md`)
was a Qt 5-only compile error in the hosted `build / linux-x86_64` job. Three test files assigned brace lists to
`QStringList` members, for example `m_client.readable = {QStringLiteral("Pad:in")}`. Qt 5's `QStringList` has extra
`operator=(const QList<QString>&)` / `operator=(QList<QString>&&)` overloads, so the brace assignment is ambiguous
there. Qt 6 compiles it. The fix spelled the type out: `= QStringList{...}`.

## Evidence that the fix is on this branch

- Fix commit `6474195bd62e4eb990ca35647be6fc260825f357` changes exactly the three test files and is an ancestor of
  HEAD. Its red/green below uses `git show 6474195bd6^:<path>`.
- The fixed lines in HEAD: `tests/src/core/ControllerScriptTest.cpp:139,140,299,318`,
  `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301`, `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`.

## Why the guard missed it, and the guard fix

`tests/Qt5QStringListAssignCheck.py` resolves `receiver.name = {` through an index of the `QStringList` members each
class declares. Its `MEMBER` regex (`tests/Qt5QStringListAssignCheck.py:78`) accepted only `;`, `=`, `,` or `[` after
the member name. The fake `RecordingMidiClient` declares its members brace-initialised
(`QStringList readable{QStringLiteral("Pad:in")};`, `tests/src/core/ControllerScriptTest.cpp:67-68`;
`tests/src/core/ExternalMidiDeviceTest.cpp:54`; `tests/src/gui/ExternalDeviceCardTest.cpp:60`). Those members were
never indexed, so the nine bad assignments passed the scan.

The change (only in `tests/Qt5QStringListAssignCheck.py`):
- `MEMBER` accepts `{` too: `(?:[;=,\[{])`.
- `BAD_FIXTURE` gains `struct Client { QStringList readable{...}; };` + `c.readable = {...}`. Its expected count in
  `self_test_fixtures` goes from 2 to 3.
- `GOOD_FIXTURE` gains the same struct + `c.readable = QStringList{...}` (expected stays 0).
- The docstring names the 6474195bd6 / RecordingMidiClient case.

The guard is a fork file (`tests/fork-sources.txt:945`), so no `upstream-modifications.txt` line is needed
(`fork-sources` gate PASS).

## Commands and results

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-48dcc062d04c645f-port.r5/logs/`.
`red-a-script.py` there is the patched guard with only the `MEMBER` change reverted (the `diff` shows the one line).
The red B setup is `git show 6474195bd6^:<path> > <path>` for the three files (`red-b-status.log`). The green B setup
is `git checkout HEAD -- <the three paths>` (`green-b-status.log`: only the guard is modified).

| Run | Command | EXIT | Log |
|---|---|---|---|
| Red A (guard fix reverted, new fixtures) | `python3 red-a-script.py --self-test` | 1 | `red-a-1.log`: "FAIL: scan fixture bad.cpp: expected 3, found 2" |
| Green A (patched guard) | `python3 tests/Qt5QStringListAssignCheck.py --self-test` | 0 | `green-a-1.log`: "self-test: PASS" |
| Red B (pre-fix test files) | `python3 tests/Qt5QStringListAssignCheck.py --root . --build-dir build --cxx /usr/bin/c++` | 1 | `red-b-1.log`: the nine lines above, "9 finding(s), 9 confirmed by a Qt 5 compile" |
| Green B (HEAD test files) | same | 0 | `green-b-1.log`: "PASS (no bare QStringList brace assignments)" |
| Unpatched guard on pre-fix files | `python3 red-a-script.py --root . --build-dir build --cxx /usr/bin/c++` | 1 | `old-guard-prefix-1.log`: "0 finding(s)". The exit 1 comes only from the new fixture failing its self-test (Red A); the scan itself found nothing |
| Build | `cmake --build build --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j4` | 0 | `build-1.log` |
| ctest | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest)$' --output-on-failure` | 0 | `test-1.log`: #71, #346, #562 Passed, 3/3 |
| ctest | `cd build/tests && ctest -R '^Qt5QStringListAssignCheck$' --output-on-failure` | 0 | `test-2.log`: #841 Passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `gates-1.log`: RESULT: PASS, all eight rows PASS |

`/usr/bin/c++` is `CMAKE_CXX_COMPILER` from `build/CMakeCache.txt`. The green B run covers the whole first-party tree,
so indexing brace-initialised members adds no new finding anywhere on this branch.

## Self-check

- Fails without the fix and passes with it: Red A EXIT 1 / Green A EXIT 0 (guard change reverted / applied). Red B
  EXIT 1 with the nine expected sites / Green B EXIT 0 (product fix 6474195bd6 reverted / present).
- Deterministic: a static regex scan over fixed fixtures and the source tree, plus a fixed Qt 5 compile probe. It has
  no timing, sleeps or threads.
- At the cause and complete: the miss was the member index (`MEMBER`). The other ways a member is declared were
  already covered (`;`, `=`, `,`, `[`), and `{` was the missing one. Local variables go through `DECLARATION`/
  `DECL_BEFORE`, which already handle `QStringList names = {...}` and are not changed. The sibling guard
  `tests/Qt5QStringListBraceCheck.py` already caught this pattern and is not changed.
- No collateral damage: test-only change. Project loading and saved data are untouched. Green B shows no new finding
  in the tree, and every scan finding is still confirmed by a Qt 5 compile before it is reported. The three test
  targets and the `Qt5QStringListAssignCheck` ctest pass.
- ALREADY-FIXED evidence: `tests/src/core/ControllerScriptTest.cpp:139`, `tests/src/core/ExternalMidiDeviceTest.cpp:155`,
  `tests/src/gui/ExternalDeviceCardTest.cpp:123` use `= QStringList{...}`. `6474195bd6` is an ancestor of HEAD.
- Sibling commands: this job changes no command file. The listed commands (`app.*`, `arrangement.*`) come from the
  branch's inherited diff against the check's base. A test-only guard change does not affect them.

Delegation: Haiku built the three test targets and ran the four ctests (`build-1.log`, `test-1.log`, `test-2.log`); I
checked the log tails. I made the guard edit myself: one regex character plus four fixture lines, smaller than a
request. I ran the red/green runs myself because they swap worktree files and each state had to be checked before the
next. Sonnet and DeepSeek were not needed.

## Other findings

- None new. Rounds 2-4 were bounced by the proof gate because no test file was changed on this branch. This round
  changes `tests/Qt5QStringListAssignCheck.py`, which also closes the guard gap noted in round 4.
