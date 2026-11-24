ALREADY-FIXED

# Cluster ci-c22070dd8cbbc7c0-port: port of "CI regression: build / linux-arm64"

ci-c22070dd8cbbc7c0-port: ALREADY-FIXED

## What the original cluster was

`docs/qwen-fixes-2026-10/cluster-ci-c22070dd8cbbc7c0.md` (the accepted fix) explains the failure.
The hosted `build / linux-arm64` job builds against Qt 5.15. In Qt 5.15, `QSpinBox` declares both
`valueChanged(int)` and the deprecated `valueChanged(const QString&)`. As a result, the bare member
pointer `&QSpinBox::valueChanged` in `src/gui/FollowActionEditor.cpp` (historically `:207`) is an
`<unresolved overloaded function type>`, and `QObject::connect` cannot deduce its signal argument.
The fix is `QOverload<int>::of(&QSpinBox::valueChanged)`.

## Decision: ALREADY-FIXED on this branch (HEAD e4194e0bf5)

- The product fix is already present at `src/gui/FollowActionEditor.cpp:210`:
  `QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), jump, [jump] { jump->setProperty("edited", true); });`
  The comment at `:207-209` explains why. It arrived with `ab20525e38 fix(ci): CI regression: build / windows-arm64`
  (cherry-picked from 5d64ba4d1e). `git merge-base --is-ancestor ab20525e38 HEAD` returned EXIT 0.
- Three regression checks guard it, and all are registered in `tests/CMakeLists.txt`:
  - `Qt5OverloadedSignalCheck` (`tests/CMakeLists.txt:7004-7009`, `tests/qt5-overloaded-signal-check.py`)
  - `Qt5SignalOverloadCheck` (`tests/CMakeLists.txt:9581-9587`, `tests/qt5-signal-overload-check.py`)
  - `Qt5SignalOverloadArmCheck` (`tests/CMakeLists.txt:9599-9605`, `tests/qt5-signal-overload-arm-check.py`, the
    linux-arm64 cluster's own check)
- All three scripts are registered in `tests/fork-sources.txt` (`:744`, `:3691`, and the arm check).

No product change was needed, and none was made.

## Evidence the guards catch the bug on this tree

I temporarily reverted line 210 to the bare `&QSpinBox::valueChanged` (working tree only). Then I
ran each check and restored the line with `git checkout -- src/gui/FollowActionEditor.cpp`
(`git status` was clean afterwards).

| Command | Reverted EXIT | Restored EXIT | Logs |
| --- | --- | --- | --- |
| `python3 tests/qt5-signal-overload-check.py --root .` | **1** | 0 | `check-4-reverted-*.log`, `check-5-restored-*.log` |
| `python3 tests/qt5-signal-overload-arm-check.py --root .` | **1** | 0 | same |
| `python3 tests/qt5-overloaded-signal-check.py .` | **1** | 0 | same |

The red message (`check-4-reverted-qt5-signal-overload-check.log`) was:
`src/gui/FollowActionEditor.cpp:210: bare &QSpinBox::valueChanged - that signal is overloaded in Qt 5 ...`
followed by `FAIL: 1 unguarded overloaded-signal take(s)`.

## Commands run on the unmodified branch (logs in the run's `logs/` folder)

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 tests/qt5-signal-overload-check.py --root .` | 0 (`PASS: no unguarded take of a signal that Qt 5 overloads`) | `check-2-tree.log` |
| `python3 tests/qt5-signal-overload-check.py --self-test` | 0 | `check-3-selftest.log` |
| `cmake --build build --target zene FollowActionEditorTest -j4` | 0 | `build-1-followaction.log` |
| `cd build/tests && ctest -R '^(Qt5SignalOverloadCheck\|Qt5OverloadedSignalCheck\|Qt5SignalOverloadArmCheck\|FollowActionEditorTest)$' --output-on-failure` | 0 (`100% tests passed out of 4`; FollowActionEditorTest `Totals: 10 passed, 0 failed`) | `test-2-family.log` |
| `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `feature-check-1.log` |

An earlier ctest run (`test-1-family.log`) returned EXIT 8. The only failure was `FollowActionEditorTest (Not Run)`,
because that binary had not been built in this tree yet. After the build above, the same tests
passed 4/4.

## Round 3: closing the gap in the arm check (tests/qt5-signal-overload-arm-check.py)

The reviewer sent this back because the proof had no test artifact. The product line stays as it
is, and the ALREADY-FIXED verdict for `src/gui/FollowActionEditor.cpp:210` is unchanged. The
linux-arm64 cluster's own check had a real gap. Its `QT5_OVERLOADED` list lacked four Qt 5.15
overloads that its sibling checks know about and that have already broken a hosted Qt 5 build
(`f06e257207`, `QSslSocket::sslErrors` in StemModelStoreTest). The list now includes them. The
citations come from the qtbase 5.15 headers at `https://code.qt.io/cgit/qt/qtbase.git/plain/<path>?h=5.15`:

| Signal | Qt 5.15 overloads | Header lines |
| --- | --- | --- |
| `QSslSocket::sslErrors` | signal beside the deprecated `sslErrors() const` getter | `qsslsocket.h:216,196` |
| `QSignalMapper::mapped` | int, QString, QWidget*, QObject* | `qsignalmapper.h:71,73,75,77` |
| `QNetworkReply::error` | signal beside the `error() const` getter | `qnetworkreply.h:161,127` |
| `QAbstractSocket::error` | signal beside the `error() const` getter | `qabstractsocket.h:211,183` |

- The red fixture gains one bare connect line per new signal. `FIXTURE_EXPECTED` now holds six
  findings (fixture lines 12-17); the two existing ones moved from 7 and 8 to 12 and 13 after the
  new includes.
- The green fixture gains the disambiguated forms: `QOverload<const QList<QSslError>&>::of`,
  `qOverload<int>`, `static_cast<void (QNetworkReply::*)(QNetworkReply::NetworkError)>` and
  `QOverload<QAbstractSocket::SocketError>::of`.
- `DISAMBIGUATED` now accepts one level of nested template arguments (`TEMPLATE_ARGS`). Without
  that, the correct spelling `QOverload<const QList<QSslError>&>::of(...)` was itself flagged. The
  change only accepts more disambiguated spellings, so no previously caught bare use is let through.
- The file is 269 lines, under the 500-line cap. No function changed.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| **RED** (new fixtures, list not yet extended) | `python3 tests/qt5-signal-overload-arm-check.py --root <worktree>` | **1** (`SELF-TEST FAIL: red fixture found [(12,...),(13,...)], expected [... (14,'QSslSocket','sslErrors'), (15,'QSignalMapper','mapped'), (16,'QNetworkReply','error'), (17,'QAbstractSocket','error')]`) | `test-1-red-arm.log` |
| **GREEN** (list extended) | same | 0 (`PASS: every Qt 5 overloaded signal member pointer is disambiguated`, whole tree clean) | `test-2-green-arm.log` |
| ctest | `cd build/tests && ctest -R '^Qt5SignalOverloadArmCheck$' --output-on-failure` | 0 (`100% tests passed out of 1`) | `test-3-ctest-arm.log` |
| Product line reverted to bare `&QSpinBox::valueChanged` (working tree only) | `python3 tests/qt5-signal-overload-arm-check.py --root <worktree>` | **1** (`src/gui/FollowActionEditor.cpp:210: &QSpinBox::valueChanged is overloaded in Qt 5`) | `test-4-product-line-reverted-arm.log` |
| Product line restored (`git checkout -- src/gui/FollowActionEditor.cpp`) | same | 0 | `test-5-product-line-restored-arm.log` |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `feature-check-1.log` |

All round-3 logs are in `~/.config/kads/zene/state/runs/ci-c22070dd8cbbc7c0-port.r3/logs/`.

Proof artifact (regression-test): `tests/qt5-signal-overload-arm-check.py`, test
`Qt5SignalOverloadArmCheck`, red_exit 1, green_exit 0.

Delegation (round 3): a Sonnet engineer fetched the Qt 5.15 headers and made the list, fixture and
regex change, then ran the red, green and ctest commands. I reviewed the diff, ran the
product-line revert/restore cycle and the gates, and wrote this report and the manifest.

## Self-check

- **Verdict evidence:** the fix is at `src/gui/FollowActionEditor.cpp:210`, it is an ancestor commit
  of HEAD (`ab20525e38`), and three registered ctests pass on HEAD.
- **Regression test fails without the fix and passes with it:** with line 210 reverted, all three checks
  exited 1. With it restored, all exited 0 (table above). No new test was added, because the
  existing ones already cover the bug.
- **Deterministic:** the checks scan source files only. They use no clock, sleeps or threads.
- **No collateral damage:** this commit has no product diff; only this report is added.
- **Cause and completeness (callers/siblings):** I changed no code. The existing fix is at the cause, the
  one `connect` call. All three checks scan every first-party source for every Qt 5-overloaded signal
  take (QSpinBox/QDoubleSpinBox, QComboBox, QProcess, QButtonGroup and others), and on HEAD they report
  zero unguarded takes (`check-2-tree.log`, `test-2-family.log`). That covers every sibling connect in the tree.
- **Sibling commands (app.*, arrangement.*):** not affected. Those command files appear in the
  check's list only because the branch carries merged train work. This lane's commit adds only this
  report: `git show --stat HEAD` lists one file under `docs/`. No command file or handler was changed.
- **Round 3 regression test:** `tests/qt5-signal-overload-arm-check.py` fails with the list extension
  absent (EXIT 1, `test-1-red-arm.log`) and passes with it (EXIT 0, `test-2-green-arm.log`). It also
  fails when the product line is reverted (EXIT 1, `test-4-...`). It is a pure source scan with
  fixed fixtures: no clock, sleeps or threads.
- **Round 3 collateral:** only a test script changed. The whole-tree scan stays clean, so no existing
  first-party source is newly rejected (`test-2-green-arm.log`).
- **Realtime / undo / commands:** untouched.
- **Qt 5 compile not reproduced here:** this box has no Qt 5 tree at `~/.local/opt/qt5` (`ls` failed),
  so a real Qt 5 compile of the translation unit was not re-run in this session. The original cluster
  report records that Qt 5.15.13 compile (EXIT 1 before the fix, EXIT 0 after). The hosted arm64 job is not re-run from this box.

Delegation: a Haiku agent ran the first ctest family run and `feature_check.py`. A DeepSeek worker
built `zene` and `FollowActionEditorTest` and reran the ctest family. I ran the scanner revert/restore
cycle myself (one `sed` plus six script runs), read the code and wrote this report. Sonnet was not
used, because no fix or new test was needed.

## Other findings

- Three overlapping Qt 5 overload checks guard this same line (`Qt5OverloadedSignalCheck`,
  `Qt5SignalOverloadCheck`, `Qt5SignalOverloadArmCheck`). They came from separate cluster fixes for
  the windows-arm64, linux-x86_64 and linux-arm64 jobs. Consolidating them is a separate cleanup and
  was not done here.
