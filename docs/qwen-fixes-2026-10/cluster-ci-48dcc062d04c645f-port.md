ALREADY-FIXED

# cluster ci-48dcc062d04c645f-port: "Port: CI regression: build / linux-x86_64"

Verdict: ALREADY-FIXED on branch `bugs/qwen-ci-48dcc062d04c645f-port-port`. No product or test change is needed.

## What the original failure was

The original cluster `ci-48dcc062d04c645f` (report: `docs/qwen-fixes-2026-10/cluster-ci-48dcc062d04c645f.md`)
was a Qt 5-only compile error in the hosted `build / linux-x86_64` job. Three test files assigned brace lists to
`QStringList` members (`m_client.readable = {QStringLiteral(...)}`). In Qt 5 that call is ambiguous because
`QStringList` has extra `operator=(const QList<QString>&)` / `operator=(QList<QString>&&)` overloads. The fix
made each assignment explicit as `= QStringList{...}`.

## Evidence that the fix is already on this branch

- The fix commit `6474195bd62e4eb990ca35647be6fc260825f357` ("fix(ci): CI regression: build / linux-x86_64",
  cherry-picked from `45a1f7fe78`) is an ancestor of HEAD `e4194e0bf51c264ca8117ba40def20bc54786244`:
  `git merge-base --is-ancestor 6474195bd6 HEAD` -> EXIT 0.
- `git diff 6474195bd6 HEAD --stat -- <the three files>` prints nothing, so the fixed lines have not changed since then.
- The fixed lines in HEAD:
  - `tests/src/core/ControllerScriptTest.cpp:139,140,299,318`: `m_client.readable/writable = QStringList{QStringLiteral(...)}`
  - `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301`: `m_client.writable = QStringList{...}`
  - `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`: `m_client.writable = QStringList{...}`
- An fff search of `tests/` for `readable = {`, `writable = {` and `List = {QString` found 0 matches. A search for
  ` = {QStringLiteral(` found only copy-initialised declarations (`const QStringList x = {...}`, which is
  unambiguous in Qt 5) and the fixtures inside the guard scripts themselves.
- The repository already has guard tests for this pattern (`tests/CMakeLists.txt:6570,6590,6599`). All of them pass on this branch:

| Command (run from `build/tests`) | EXIT | Log |
|---|---|---|
| `ctest -R Qt5QStringList --output-on-failure` (3 tests: Qt5QStringListAssignmentCheck, Qt5QStringListBraceCheck, Qt5QStringListAssignCheck; "100% tests passed out of 3") | 0 | `logs/test-1.log` |
| `ctest -R Qt5QStringListAssignCheck -V` (prints "Qt5QStringListAssignCheck: PASS (no bare QStringList brace assignments)") | 0 | `logs/test-2.log` |

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-48dcc062d04c645f-port.r2/logs/`.

Unverified: I did not run a full Qt 5 configure and build of the test targets in this session. The evidence above
is the ancestry of the fix commit, the unchanged fixed lines, and the guard scripts.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT 0, `RESULT: PASS`
(fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
complexity, file-length all PASS). Log: `logs/gates-1.log`.

## Self-check

- Regression test: none was added because nothing was fixed in this job. The fix shipped earlier in `6474195bd6`, and
  the existing guards `Qt5QStringListBraceCheck`, `Qt5QStringListAssignCheck` and `Qt5QStringListAssignmentCheck`
  already cover the pattern (EXIT 0 above).
- Product code changed: none. Only this report is committed. Realtime, undo, control commands and registration are unaffected.
- Git: no push, merge, rebase or stash was used. Only this report was staged, by explicit path.

Delegation: Haiku ran the `ctest -R Qt5QStringList` guard suite and `feature_check.py`, and reported their exit codes. I re-read both log tails myself before relying on them. I ran the verbose single-test rerun and the ancestry and line checks myself because they were one-liners. Sonnet and DeepSeek were not used because there was no code to write.

## Other findings

None.
