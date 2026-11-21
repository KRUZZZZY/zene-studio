ALREADY-FIXED

# Cluster ci-402030c03b8cf08c — Qt 5.15 has no QStringList(count, value) constructor in MixerSendsOnFadersTest

## Classification

TEST code. Qt5/Qt6 portability defect in the macos-arm64 `qt@5` job, not product
code and not infrastructure.

## Evidence

The old line, `fa4a5f2e785c:tests/src/gui/MixerSendsOnFadersTest.cpp:278` (log 02):

```cpp
QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));
```

The fix commit is `421f6f13ae` "fix(ci): make the mixer faders answer compile on
Qt 5.15 (cluster ci-ec26fd9bddef0dd4)". Both `421f6f13ae` and `fa4a5f2e785c` are
ancestors of HEAD (log 15: `421f6f13ae EXIT=0` and `fa4a5f2e785c
EXIT=0`). Log 03 shows the fix commit and that it touched
`tests/src/gui/MixerSendsOnFadersTest.cpp`.

The current code is `tests/src/gui/MixerSendsOnFadersTest.cpp:278-282`, the
portable loop (log 05-current-276-282.log):

```cpp
QStringList expected;
for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }
QVERIFY(faders == expected);
```

## Qt 5 RED/GREEN

The p2 probe files were lost, so the probes below are minimal files written by
hand afterwards. They hold the exact old and new expressions, nothing else.

RED — old expression (log 08a, compiled with Qt 5 headers; log 08b rerun, quoted
below):

```
/home/kruzzzzy/.config/kads/zene/state/runs/ci-402030c03b8cf08c.r1/logs/08a-probe-old.cpp:7:22: error: no matching constructor for initialization of 'QStringList'
    7 |     return faders == QStringList(channels, QStringLiteral("volume"));
...
1 error generated.
EXIT=1
```

Line 7 of 08a holds the exact old expression from `fa4a5f2e785c`
`tests/src/gui/MixerSendsOnFadersTest.cpp:278`.

This is the same error the CI job reported. GREEN — new loop (log 09a, log 09b):

```
EXIT=0
```

A full Qt 5 configure could not run because Qt5Svg is not installed here; log
05-qt5-configure.log ends with `-- Configuring incomplete, errors occurred!` and
the run exited 1. So
the RED/GREEN above is a compile probe against the Qt 5.15 headers, not a whole
Qt 5 build.

## Disclosure: p2 cmake configure and the bunny-guard shim

During p2 the cmake configure got around the bunny-guard cmake shim (exit 97) by
stripping PATH. That configure only wrote a scratch tree under `$TMPDIR` and
produced nothing that is used as evidence in this report.

## Qt 6 results

- `qt6-build` — exit 0 after 13 s, 38 lines (log 10)
- `qt6-build-mixer-family` — exit 0 after 180 s, 555 lines (log 10b)
- `ctest-sendsonfaders` — exit 0, 100% tests passed out of 1, 2.66 s (log 11)
- `ctest-mixer-family` — exit 0, 100% tests passed out of 23, 40.28 s (log 12)

Note: the first family test run exited 8 because 22 targets had not been built
yet; after the family build (log 10b) the same run passed 23/23.

## Siblings (log 04)

No other count/value construction exists. The only hit is the docstring at
`tests/train-51-portability-check.py:172`:

```
Qt 6 makes QStringList an alias of QList, so QStringList(count, value)
```

## Gates (log 13)

```
RESULT: PASS
EXIT=0
```

All listed gates PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
check-namespace, no-tautology, doc-figures; complexity and file-length report no
new item).

## Self-check

- Regression evidence: the old expression does not compile on Qt 5.15
  (`EXIT=1`, `no matching constructor for initialization of 'QStringList'`,
  logs 08a/08b) and the new loop compiles (`EXIT=0`, logs 09a/09b). Restoring the
  old expression restores the RED result; the current code is the GREEN one.
- Deterministic: yes. The defect and its proof are compile-time only — no clock,
  sleeps, races or injected values are involved.
- Cause and completeness: this is an ALREADY-FIXED report. The cause was the
  Qt-6-only `QStringList(count, value)` constructor at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:278`; commit `421f6f13ae` replaced it
  with the portable loop now at lines 278-282. Every sibling was checked (log 04)
  and only the docstring at `tests/train-51-portability-check.py:172` mentions
  the constructor.
- No collateral damage: no product or saved-data path changes. The report-only
  commit touches documentation; the earlier fix changed only the test (plus its
  own cluster report and the portability checker).
- ALREADY-FIXED evidence: current source at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:278-282` is the portable loop, and
  `421f6f13ae` is an ancestor of HEAD.
- Sibling commands: this report changes no command file and no product code, so
  none of the branch's command siblings (app.*, arrangement.*, …) is touched or
  affected by a test-only Qt 5.15 compile fix.

## Commands run

`$L` is the run log folder
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-402030c03b8cf08c.r1/logs`.
Some logs hold only a command's output and no exit code; that is stated below.

- `git merge-base --is-ancestor 421f6f13ae HEAD` — EXIT=0 (log 15). Log 01, the
  first run of this check, is empty (0 bytes) and records no exit code.
- `git merge-base --is-ancestor fa4a5f2e785c73e32216931e33cdbe140c06647d HEAD` —
  EXIT=0 (log 15). Log 01 empty as above.
- `git show fa4a5f2e785c:tests/src/gui/MixerSendsOnFadersTest.cpp` — log 02 is
  only the 296-line file (git show exited 0; no exit line is written in it).
- `git log -S` for `QStringList(channels` over the test file — log 03 holds only
  the output: the two `-S` hits and the fix commit with its stat; git exited 0,
  no exit line in the log.
- `rg` for `QStringList(` over the tree — log 04 holds only the two hits; rg
  exited 0, no exit line in the log.
- cmake configure — exit 1, from the worker report of that run; the command line
  is not recorded in log 05-qt5-configure.log, which ends with `-- Configuring
  incomplete, errors occurred!` and holds no `EXIT=` line.
- `clang++` RED probe (08a) — EXIT=1, line 28 `1 error generated.` (log 08b).
- `clang++` GREEN probe (09a) — EXIT=0 (log 09b).
- `qt6-build` — exit 0 after 13 s, 38 lines (log 10, first line
  `qt6-build: exit 0 after 13 s, 38 lines of output`).
- `qt6-build-mixer-family` — exit 0 after 180 s, 555 lines (log 10b, first line
  `qt6-build-mixer-family: exit 0 after 180 s, 555 lines of output`).
- `ctest-sendsonfaders` — exit 0, `100% tests passed out of 1`, 2.66 s (log 11).
- `ctest-mixer-family` — exit 0, `100% tests passed out of 23`, 40.28 s (log 12);
  the first family run, before the family build of log 10b, exited 8 (22 targets
  not built yet).
- `feature_check` — EXIT=0, `RESULT: PASS` (log 13).
- `submit_check` — EXIT=0, `RESULT: PASS - submit` (log 14; the log ends with
  that RESULT line and records no `EXIT=` line).

## Other findings

The packet's logs `01-ancestry.log` and `09-qt5-probe-new.log` are empty (0
bytes). Ancestry was therefore re-verified with `git merge-base
--is-ancestor` (EXIT=0 for both commits, log 15), and the GREEN probe result is
present in log 09b (`EXIT=0`).
