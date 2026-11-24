ALREADY-FIXED

# cluster ci-48dcc062d04c645f-port: "Port: CI regression: build / linux-x86_64"

Verdict: ALREADY-FIXED on branch `bugs/qwen-ci-48dcc062d04c645f-port-port`. No product or test code was changed.
Only this report is committed.

## What the original failure was

The original cluster `ci-48dcc062d04c645f` (report: `docs/qwen-fixes-2026-10/cluster-ci-48dcc062d04c645f.md`)
was a Qt 5-only compile error in the hosted `build / linux-x86_64` job. Three test files assigned brace lists to
`QStringList` members, for example `m_client.readable = {QStringLiteral("Pad:in")}`. Qt 5's `QStringList` has extra
`operator=(const QList<QString>&)` / `operator=(QList<QString>&&)` overloads, so the brace assignment is ambiguous
there. Qt 6 compiles it. The fix spelled the type out: `= QStringList{...}`.

## Evidence that the fix is on this branch

- The fix commit `6474195bd62e4eb990ca35647be6fc260825f357` (cherry-picked from `45a1f7fe78`) is an ancestor of this
  branch: `git merge-base --is-ancestor 6474195bd6 HEAD` -> EXIT 0. `git diff 6474195bd6 HEAD -- <the three files>` is empty.
- The fixed lines in HEAD:
  - `tests/src/core/ControllerScriptTest.cpp:139,140,299,318`
  - `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301`
  - `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`

All logs below are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-48dcc062d04c645f-port.r3/logs/`.

### The three test targets build and pass

| Command | EXIT | Log |
|---|---|---|
| `cmake --build build --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j4` | 0 | `build-1.log` |
| `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest)$' --output-on-failure` ("100% tests passed out of 3") | 0 | `test-1.log` |

### Red/green: the pre-fix files are caught, HEAD is clean

To get the red run, I checked out the three files as they were before the fix (`git show 6474195bd6^:<path> > <path>`)
and ran the guard. Then I ran `git checkout HEAD -- <the three paths>` (`git status --short` was empty afterwards) and ran the guard again.

| Run | Command | EXIT | Log |
|---|---|---|---|
| red (pre-fix files) | `python3 tests/Qt5QStringListBraceCheck.py --root . --cxx /usr/bin/c++` | 1 | `red-3.log`: "FAIL: 9 ambiguous QStringList brace assignment(s)". It lists exactly the nine lines above. |
| green (HEAD) | same | 0 | `green-3.log`: "PASS: no ambiguous QStringList brace assignment in the first-party tree" |

Both runs print `probe: Qt 5 rejects the brace assignment and accepts QStringList{...}`. That means the guard's compile probe
ran against this box's Qt 5 tree, so the red result is backed by a real Qt 5 compiler rejection, not only by the source scan.
`Qt5QStringListBraceCheck` is registered as a ctest (`tests/CMakeLists.txt:6590`). `/usr/bin/c++` is `CMAKE_CXX_COMPILER`
from `build/CMakeCache.txt`.

The brief named `tests/Qt5QStringListAssignCheck.py` for this step. I ran it the same way, and it gives EXIT 0 on the pre-fix
files too (`red-2.log`, `green-2.log`). So that guard does not detect this pattern, and I used the sibling guard above for the
red/green proof (see Other findings). `red-1.log` / `green-1.log` are a void first attempt: zsh did not split the path list,
so no files were swapped (`git status` was empty).

### Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT 0, `RESULT: PASS` (`gates-1.log`).

## Self-check

- Fails without the fix and passes with it: red EXIT 1 (`red-3.log`, pre-fix files) and green EXIT 0 (`green-3.log`, HEAD), as shown above.
- Deterministic: `Qt5QStringListBraceCheck.py` is a static source scan plus a fixed compile probe. It uses no timing, sleeps or threads.
- At the cause and complete: the red run flags exactly nine sites, and the green run on HEAD flags none anywhere in the first-party tree.
- No collateral damage: this round changes no product or test code. The three test targets still pass (`test-1.log`, EXIT 0).
- ALREADY-FIXED evidence: `tests/src/core/ControllerScriptTest.cpp:139`, `tests/src/core/ExternalMidiDeviceTest.cpp:155`,
  `tests/src/gui/ExternalDeviceCardTest.cpp:123` (and the other lines above). Fix commit `6474195bd6` is an ancestor of HEAD.
- Sibling commands: this job changes no command file. If the check lists commands, they come from the branch's inherited
  diff against its base.
- Gates: `feature_check.py` -> EXIT 0, `RESULT: PASS`, all eight rows PASS (`gates-1.log`).

Delegation: Haiku built the three test targets and ran their ctest (`build-1.log`, `test-1.log`); I checked both log tails.
I ran the guard red/green runs myself because they swap files in the worktree and had to be sequenced and checked one step at a time.
Sonnet and DeepSeek were not used because there was no code to write.

## Other findings

- `tests/Qt5QStringListAssignCheck.py` misses `m_client.readable = {...}` / `m_client.writable = {...}` in these three files.
  On the pre-fix sources it prints "PASS (no bare QStringList brace assignments)" and exits 0 (`red-2.log`). Its scan
  (`scan_text`/`assigned_type`, lines 141-165) resolves a member receiver through a declaration it can find locally, and it
  does not resolve these fake-client members. `Qt5QStringListBraceCheck.py` catches them, so CI still has coverage. I did not
  change the guard here.
