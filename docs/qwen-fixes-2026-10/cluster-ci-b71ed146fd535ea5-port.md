ALREADY-FIXED

# cluster ci-b71ed146fd535ea5-port — hosted `build / linux-x86_64` Qt 5 `QStringList` constructor

ci-b71ed146fd535ea5-port: ALREADY-FIXED

## Verdict

ALREADY-FIXED on this branch. The finding is real at its pinned base
`fa4a5f2e785c73e32216931e33cdbe140c06647d` (`050/m0-train-35`): the hosted
`build / linux-x86_64` job builds with Qt 5.15, where `QStringList` is its own
class and has no `QStringList(count, value)` constructor. That line in
`tests/src/gui/MixerSendsOnFadersTest.cpp` stopped the build with
`no matching function for call to 'QStringList::QStringList(const int&, QString)'`.

This branch already contains the portable spelling from commit `421f6f13ae`
(`fix(ci): make the mixer faders answer compile on Qt 5.15 (cluster
ci-ec26fd9bddef0dd4)`), which is an ancestor of `HEAD`. It also already carries
the equivalent regression check from cluster `ci-8764ffa3b495466c-port`
(`tests/Qt5QStringListSizeConstructorCheck.py`, registered as ctest
`Qt5QStringListSizeConstructorCheck`). No product or test source needs changing
for this port.

## Evidence

- The pinned failing source still shows the defect:
  `git show fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp`
  has `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`.
- The current tree builds the same expected list one element at a time at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`:
  `QStringList expected;`,
  `for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`,
  `QVERIFY(faders == expected);`. The assertion is unchanged and not weakened.
- `git merge-base --is-ancestor 421f6f13ae HEAD` exits 0, and
  `git log --oneline -1 421f6f13ae` prints
  `421f6f13ae fix(ci): make the mixer faders answer compile on Qt 5.15 (cluster ci-ec26fd9bddef0dd4)`.
- Qt 5.15's `QStringList` has no count/value constructor:
  `/usr/include/qt/QtCore/qstringlist.h:115-122` lists default, single-`QString`,
  `QList`, initializer-list and iterator-pair constructors only. Qt 6 aliases
  `QStringList` to `QList<QString>`, whose size/value constructor is what made
  the old line compile on the Qt 6 hosts.
- The existing regression check derives the shipped `QVERIFY(faders == ...)`
  statement from the source and compiles it against Qt 5 and Qt 6. It is
  registered at `tests/CMakeLists.txt:6606-6614` and its source is
  `tests/Qt5QStringListSizeConstructorCheck.py`.
- `multi_grep` for `QStringList(channels` finds the live construction only in
  the checker's failing fixture and in reports; the product source has no
  occurrence.

## RED/GREEN proof with the existing regression check

The reverse patch is the product hunk of `421f6f13ae` (the fix already on the
branch). Reversing it restores the historical Qt 6-only line; the existing
checker then fails against the local Qt 5.15.19 tree with the hosted
diagnostic. Restoring the hunk makes it pass again.

## Commands and EXIT codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-b71ed146fd535ea5-port.r2/logs`.

| Step | Command | Exit |
|---|---|---|
| Code evidence (pinned/current source, ancestor, Qt 5 header) | see `16-code-evidence.log` | 0 |
| GREEN baseline, direct checker | `python3 tests/Qt5QStringListSizeConstructorCheck.py --root .` (`01-check-green.log`) | 0 |
| GREEN baseline, ctest | `cd build/tests && ctest -R '^Qt5QStringListSizeConstructorCheck$' --output-on-failure` (`02-ctest-green.log`) | 0 |
| Build the affected Qt test | `cmake --build build --target MixerSendsOnFadersTest -j4` (`03-build-mixer-faders.log`) | 0 |
| Run the affected Qt test | `cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` (`04-ctest-mixer-faders.log`) | 0 |
| Generate the reverse patch | `git diff 421f6f13ae^ 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp > <LOGS>/05-revert-fix.patch` | 0 |
| Check the reverse patch applies | `git apply -R --check <LOGS>/05-revert-fix.patch` (`06-revert-check.log`) | 0 |
| Revert the fix only | `git apply -R <LOGS>/05-revert-fix.patch` (`07-revert-apply.log`) | 0 |
| RED, direct checker | `python3 tests/Qt5QStringListSizeConstructorCheck.py --root .` (`08-check-red.log`): `error: no matching function for call to 'QStringList::QStringList(int&, QString)'`, `FAIL: build the expected list with append()` | 1 |
| RED, ctest | `cd build/tests && ctest -R '^Qt5QStringListSizeConstructorCheck$' --output-on-failure` (`09-ctest-red.log`): 0% passed, 1 failed | 8 |
| Restore the fix | `git apply <LOGS>/05-revert-fix.patch` (`10-restore-apply.log`) | 0 |
| GREEN, ctest restored | `cd build/tests && ctest -R '^Qt5QStringListSizeConstructorCheck$' --output-on-failure` (`11-ctest-green-restored.log`) | 0 |
| GREEN, direct restored | `python3 tests/Qt5QStringListSizeConstructorCheck.py --root .` (`12-check-green-restored.log`) | 0 |
| Rebuild restored source | `cmake --build build --target MixerSendsOnFadersTest -j4` (`13-build-restored.log`) | 0 |
| Regression family | `cd build/tests && ctest -R '^(MixerSendsOnFadersTest|Train51Portability|Qt5QStringListAssignmentCheck|Qt5QStringListSizeConstructorCheck|Qt5QStringListBraceCheck|Qt5QStringListAssignCheck|Qt5QListApiCheck)$' --output-on-failure` (`14-ctest-family.log`) | 0 (7/7) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`15-feature-check.log`): `RESULT: PASS` | 0 |

An earlier attempt at the red ctest was discarded because the restore command
ran in parallel with it and the test saw the restored source; the table above is
the strictly sequential rerun and its log.

## Self-check

- Fails with the product change reverted and passes with it: yes. Reversing the
  `421f6f13ae` hunk makes `Qt5QStringListSizeConstructorCheck` exit 1 directly
  and 8 through ctest; restoring it makes both exit 0 (`08`, `09`, `11`, `12`).
- Deterministic: yes. The checker is a source-derived `-fsyntax-only` compile
  probe against `pkg-config` Qt 5.15.19 (and Qt 6 when installed). No clock,
  sleeps, threads or races. A host without Qt 5 reports `probe: skipped`, never
  passed.
- At the cause and complete: the only product occurrence of
  `QStringList(count, value)` was this expected-list construction, and the live
  source now builds it with `append()` under the unchanged exact-list
  assertion. The same spelling is guarded by the registered checker, so a
  reintroduction fails on Qt 6 hosts too.
- No collateral damage: this job changes no product code, file format, saved
  data, limit or command. The pre-existing fix changes only a test's
  expected-list construction; the Qt 6 `MixerSendsOnFadersTest` still passes.
- ALREADY-FIXED evidence: `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`
  from `421f6f13ae`, an ancestor of `HEAD`; `/usr/include/qt/QtCore/qstringlist.h:115-122`
  shows why the old line could not compile on Qt 5.
- Realtime: nothing in this change runs on the audio thread.
- Undo/commands: no command, undo step, reversibility class or snapshot canary
  is involved.

## Other findings

- Three clusters (`ci-ec26fd9bddef0dd4`, `ci-8764ffa3b495466c`,
  `ci-b71ed146fd535ea5`) describe the same `MixerSendsOnFadersTest` Qt 5
  constructor line. The first fixed it; the second ported an equivalent
  regression checker. This port is therefore a no-change closure. No new defect
  found.
