ALREADY-FIXED

# cluster ci-48dcc062d04c645f-port: "Port: CI regression: build / linux-x86_64"

Verdict: ALREADY-FIXED on branch `bugs/qwen-ci-48dcc062d04c645f-port-port`. No product or test code is changed.
Only this report is committed (round 4 rewrites it to cite this round's logs only).

## What the original failure was

The original cluster `ci-48dcc062d04c645f` (report: `docs/qwen-fixes-2026-10/cluster-ci-48dcc062d04c645f.md`)
was a Qt 5-only compile error in the hosted `build / linux-x86_64` job. Three test files assigned brace lists to
`QStringList` members, for example `m_client.readable = {QStringLiteral("Pad:in")}`. Qt 5's `QStringList` has extra
`operator=(const QList<QString>&)` / `operator=(QList<QString>&&)` overloads, so the brace assignment is ambiguous
there. Qt 6 compiles it. The fix spelled the type out: `= QStringList{...}`.

## Evidence that the fix is on this branch

- The fix commit `6474195bd62e4eb990ca35647be6fc260825f357` ("fix(ci): CI regression: build / linux-x86_64",
  cherry-picked from `45a1f7fe78`) changes exactly `tests/src/core/ControllerScriptTest.cpp`,
  `tests/src/core/ExternalMidiDeviceTest.cpp` and `tests/src/gui/ExternalDeviceCardTest.cpp`. It is an ancestor of the
  train base `e4194e0bf5` that this branch starts from (`git merge-base --is-ancestor 6474195bd6 e4194e0bf5` -> EXIT 0).
- The fixed lines in HEAD (the nine sites the guard flags on the pre-fix files, see below):
  - `tests/src/core/ControllerScriptTest.cpp:139,140,299,318`
  - `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301`
  - `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`

All logs below are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-48dcc062d04c645f-port.r4/logs/`.

### The three test targets build and pass

| Command | EXIT | Log |
|---|---|---|
| `cmake --build build --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j4` | 0 | `build-1.log` |
| `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest)$' --output-on-failure` | 0 | `test-1.log`: tests #70, #345, #560 Passed, "100% tests passed out of 3" |

### Red/green: the pre-fix files are caught, HEAD is clean

Red setup: `git show 6474195bd6^:<path> > <path>` for the three files (`red-swap-status.log`: three ` M` lines).
Green setup: `git checkout HEAD -- <the three paths>` (`green-restore-status.log`: empty, so the tree was clean again).
`/usr/bin/c++` is `CMAKE_CXX_COMPILER` from `build/CMakeCache.txt`.

| Run | Command | EXIT | Log |
|---|---|---|---|
| red (pre-fix files) | `python3 tests/Qt5QStringListBraceCheck.py --root . --cxx /usr/bin/c++` | 1 | `red-brace-1.log`: "FAIL: 9 ambiguous QStringList brace assignment(s)". It lists exactly the nine lines above. |
| green (HEAD) | same | 0 | `green-brace-1.log`: "PASS: no ambiguous QStringList brace assignment in the first-party tree" |
| red (pre-fix files) | `python3 tests/Qt5QStringListAssignCheck.py --root . --build-dir build --cxx /usr/bin/c++` | 0 | `red-assign-1.log`: "PASS (no bare QStringList brace assignments)" |
| green (HEAD) | same | 0 | `green-assign-1.log` |

Both BraceCheck runs print `probe: Qt 5 rejects the brace assignment and accepts QStringList{...}`. So the guard's
compile probe ran against this box's Qt 5, and the red result is backed by a real Qt 5 compiler rejection, not only by
the source scan. `Qt5QStringListBraceCheck` is a registered ctest in `tests/CMakeLists.txt`.

The diagnostic checkpoint asked for the red run with `tests/Qt5QStringListAssignCheck.py`. That guard exits 0 on the
pre-fix files (`red-assign-1.log`), so it cannot produce a red run for this pattern. The red/green proof uses its
sibling `Qt5QStringListBraceCheck.py` instead (see Other findings).

### Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT 0, `RESULT: PASS`, all
eight rows PASS (`gates-1.log`).

## Note on the proof gate ("no artifact shows `test`")

Rounds 2 and 3 were bounced because no artifact named "a test file this branch added or changed". This branch's own
commits (`e4194e0bf5..HEAD`) change only this report. The three test files were changed by `6474195bd6`, which is
already in the train base (`e4194e0bf5`) and in the feature-check base (`fa4a5f2e7`; `git diff --name-only
fa4a5f2e7...HEAD -- <the three files> tests/Qt5QStringListBraceCheck.py` prints nothing). They differ only against
`master` (`git diff --stat master...HEAD` lists them). `proof.json` names them as the checkpoint asked. If the gate
measures "this branch" against the train base, an ALREADY-FIXED unit has no changed test file to name. I did not create
a test change only to satisfy the gate: BraceCheck's self-test already covers member receivers (`plan.documents = {`,
`tests/Qt5QStringListBraceCheck.py:102`), so a new fixture would add no coverage.

## Self-check

- Fails without the fix and passes with it: `Qt5QStringListBraceCheck.py` red EXIT 1 on the pre-fix files
  (`red-brace-1.log`), green EXIT 0 on HEAD (`green-brace-1.log`). No product change exists to revert because the fix
  is already in the base. The red run reverts the fix's three files instead.
- Deterministic: `Qt5QStringListBraceCheck.py` is a static source scan plus a fixed Qt 5 compile probe. It has no
  timing, sleeps or threads. The three ctests ran in 0.52 s, 0.84 s and 0.72 s with no timing dependence on this change.
- At the cause and complete: the red run flags exactly the nine sites `6474195bd6` fixed. The green run flags none
  anywhere in the first-party tree.
- No collateral damage: this round changes no product or test code, so project loading and saved data are untouched.
  The three test targets still pass (`test-1.log`, EXIT 0).
- ALREADY-FIXED evidence: `tests/src/core/ControllerScriptTest.cpp:139`, `tests/src/core/ExternalMidiDeviceTest.cpp:155`,
  `tests/src/gui/ExternalDeviceCardTest.cpp:123` (and the other lines above) use `= QStringList{...}`. Fix commit
  `6474195bd6` is an ancestor of HEAD.
- Sibling commands: this job changes no command file. The listed commands (`app.*`, `arrangement.*`) come from the
  branch's inherited diff against the check's base. None is affected by a test-only Qt 5 spelling fix.
- Completion check: `submit_check.py` -> EXIT 0, `RESULT: PASS - submit` (`submit-check-1.log`, before this report was
  rewritten; it was rerun after the commit, see `submit-check-2.log`).

Delegation: Haiku built the three test targets and ran their ctest (`build-1.log`, `test-1.log`). I checked the
`test-1.log` tail myself. I ran the guard red/green runs myself because they swap files in the worktree and each step
had to be checked (status logs) before the next. I ran the gate and completion scripts myself; they are one command
each. Sonnet and DeepSeek were not used because there was no code to write.

## Other findings

- `tests/Qt5QStringListAssignCheck.py` misses `m_client.readable = {...}` / `m_client.writable = {...}` in these three
  files. On the pre-fix sources it prints "PASS (no bare QStringList brace assignments)" and exits 0
  (`red-assign-1.log`). `Qt5QStringListBraceCheck.py` catches them, so CI still has coverage. I did not change the
  guard here.
- The proof gate's "test file this branch added or changed" requirement seems to be applied to this ALREADY-FIXED port
  unit (see the note above). The gate's own rule for ALREADY-FIXED is a code-reference or a regression test. Someone
  should check how the unit is classified.
