FIXED

# cluster ci-6cf85fbc066bfe48: CI regression, build at 1ab89d758a07 (macos-x86_64, Qt 5)

## Classification
REAL. It is a source defect in test code, not an infrastructure problem. The hosted
macos-x86_64 job builds against Qt 5 (`/usr/local/opt/qt@5`), which is also the default
when `WANT_QT6` is off (CMakeLists.txt:371-373, `LMMS_QT_MIN_VERSION 5.15.0`). Its build
stopped at three calls:

- tests/src/core/StemModelStoreTest.cpp:89
- tests/src/core/StemModelStoreTest.cpp:540
- tests/src/core/StemModelStoreTest.cpp:645

Each one was `connect(sock, &QSslSocket::sslErrors, sock, [...])`.

Cause: Qt 5.15 declares two `QSslSocket::sslErrors` members.
`/usr/include/qt/QtNetwork/qsslsocket.h:196` has the deprecated getter
`QList<QSslError> sslErrors() const` (guarded by `QT_DEPRECATED_SINCE(5, 15)`), and
`:216` has the signal `void sslErrors(const QList<QSslError>&)`. So `&QSslSocket::sslErrors`
is an overload set, and the pointer-to-member `connect` cannot deduce `Func1`. Qt 6
removed the getter (`/usr/include/qt6/QtNetwork/qsslsocket.h:125` has only
`sslHandshakeErrors()`, and `:154` is the signal), which is why the Qt 6 build on this box
compiled the code. A search of src, include, plugins and tests finds no other
`&Q*Socket::sslErrors` or `&Q*Socket::error` pointer-to-member connects.

## Reproduction on this box (Qt 5.15.19 headers and moc in /usr/include/qt)
The scratch probe `logs/qt5probe.py` recompiles `tests/src/core/StemModelStoreTest.cpp`
with `-fsyntax-only`. It reuses the compile line from build/compile_commands.json,
retargeted at the Qt 5 headers and the Qt 5 moc, through the helpers of
tests/Qt5QJsonValueIntegerCheck.py.
- Before the fix: `python3 logs/qt5probe.py <wt> logs/scratch` gave EXIT=1, with the same
  three errors as CI at :89, :540 and :645 ("no matching function for call to ...connect(
  QSslSocket*&, <unresolved overloaded function type>, ...)") (logs/01-qt5-probe-before.log).
- After the fix: EXIT=0, `compile rc 0` (logs/03-qt5-probe-after.log).

## Fix
In tests/src/core/StemModelStoreTest.cpp:89, :540 and :645, the call now names the
overload with `QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors)`. The tree
already resolves Qt 5 overloaded signals this way (e.g. src/gui/noteeditor/StepGridPanel.cpp:142).
`QOverload::of` also compiles on Qt 6, where the signal has only one overload. Test
behaviour does not change. No product code, audio-thread code, undo or control commands
are touched.

## Regression test
tests/qt5-overloaded-signal-check.py (ctest `Qt5OverloadedSignalCheck`). This deterministic
source scanner runs on every host with python3, including Qt 6 hosts. I extended its Qt 5
overload table with `QSslSocket: {sslErrors}`, added a red fixture (a bare
`&QSslSocket::sslErrors`, which must be flagged at line 9), and added a green fixture
(the `QOverload<const QList<QSslError>&>::of` spelling, which must not be flagged).

| step | command | EXIT |
|---|---|---|
| RED (scanner extended, call sites unfixed) | `ctest -R '^Qt5OverloadedSignalCheck$' --output-on-failure > logs/02-test-red.log` | 8 (5 findings in StemModelStoreTest.cpp) |
| GREEN | `ctest -R '^Qt5OverloadedSignalCheck$' --output-on-failure > logs/04-test-green.log` | 0 |
| 4b revert | `git diff -- tests/src/core/StemModelStoreTest.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b red scan | `python3 tests/qt5-overloaded-signal-check.py <wt> > logs/07-4b-red-scan.log` | 1 |
| 4b red Qt 5 compile | `python3 logs/qt5probe.py <wt> logs/scratch > logs/08-4b-red-qt5.log` | 1 |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b green scan | `python3 tests/qt5-overloaded-signal-check.py <wt> > logs/09-4b-green-scan.log` | 0 |
| 4b green Qt 5 compile | `python3 logs/qt5probe.py <wt> logs/scratch > logs/10-4b-green-qt5.log` | 0 |
| Qt 6 build | `cmake --build build --target zene StemModelStoreTest -j4 > logs/05-build.log` | 0 |
| family | `ctest -R 'StemModelStoreTest\|Qt5' --output-on-failure > logs/06-test-family.log` | 0 (100% tests passed out of 15) |

The "fixed product file" for step 4b is tests/src/core/StemModelStoreTest.cpp, the code
that broke the hosted build. No file under src/ needed a change.

## Gates
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <wt> > logs/11-feature-check.log`
gave EXIT=0. Every gate passed: fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures, complexity and file-length.

## Other findings
- tests/src/core/StemModelStoreTest.cpp is not listed in tests/fork-sources.txt (StemJobLoaderTest.cpp
  and StemJobManagerTest.cpp are). This predates the fix, and I left it alone here.

## Self-check
- **Fails without the fix, passes with it:** with the StemModelStoreTest.cpp change reverted
  (`git apply -R logs/fix.patch`, EXIT=0), `tests/qt5-overloaded-signal-check.py` gave EXIT=1
  and the real Qt 5.15 compile probe gave EXIT=1. After `git apply logs/fix.patch` (EXIT=0)
  they gave EXIT=0 and EXIT=0. Through ctest, RED was EXIT=8 (logs/02-test-red.log) and GREEN
  was EXIT=0 (logs/04-test-green.log).
- **Deterministic:** the test is a pure source scan. It uses no compiler, no Qt headers, no
  timing, no sleeps and no threads. Its fixed in-file red/green fixtures run first: the red
  fixture must be flagged at lines 4-9 and the green fixture must not be flagged.
- **At the cause and complete:** the cause is the bare `&QSslSocket::sslErrors` member
  pointer, an overload set on Qt 5.15 (qsslsocket.h:196 and :216). All three uses in the tree
  are fixed: `rg -n "&Q\w*(Socket|Reply)::(error|sslErrors)\b" src include plugins tests`
  matched only StemModelStoreTest.cpp:89, :540 and :645. The scanner now flags any new one.
  Qt5SignalOverloadCheck, Qt5SignalOverloadArmCheck and the other Qt 5 checks still pass
  (logs/06-test-family.log: 15/15).
- **No collateral damage:** only test sources and a test scanner changed. No product code,
  project load or save path, or saved data is touched. The added table entry flags only
  `&QSslSocket::sslErrors` without an explicit overload, and the scan over the whole tree
  is clean after the fix.
- **ALREADY-FIXED / FALSE-POSITIVE:** does not apply. The verdict is FIXED, and the defect
  was reproduced against Qt 5.15 (logs/01-qt5-probe-before.log, EXIT=1).
- **Sibling commands (app.*, arrangement.*):** I changed no control-command file. The
  checker took this list from the branch's diff against its base (254 files from the train),
  not from this fix, which touches only tests/qt5-overloaded-signal-check.py,
  tests/src/core/StemModelStoreTest.cpp, tests/fork-sources.txt and this report. None of
  those commands is affected, and the ControlCommandsSnapshot does not move.
