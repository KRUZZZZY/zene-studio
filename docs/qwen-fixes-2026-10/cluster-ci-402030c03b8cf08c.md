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
ancestors of HEAD (log 01; live check: `git merge-base --is-ancestor` → 0 for
each). Log 03 shows the fix commit and that it touched
`tests/src/gui/MixerSendsOnFadersTest.cpp`.

The current code is `tests/src/gui/MixerSendsOnFadersTest.cpp:278-282`, the
portable loop (log 05-current-276-282):

```cpp
QStringList expected;
for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }
QVERIFY(faders == expected);
```

## Qt 5 RED/GREEN

The p2 probe files were lost, so the probes below are minimal files written by
hand afterwards. They hold the exact old and new expressions, nothing else.

RED — old expression (log 08a, compiled with Qt 5 headers; log 08b rerun):

```
tests/src/gui/MixerSendsOnFadersTest.cpp:278 (old): return faders == QStringList(channels, QStringLiteral("volume"));
error: no matching constructor for initialization of 'QStringList'
1 error generated.
EXIT=1
```

This is the same error the CI job reported. GREEN — new loop (log 09a, log 09b):

```
EXIT=0
```

A full Qt 5 configure could not run because Qt5Svg is not installed here; log 05
ends with `-- Configuring incomplete, errors occurred!` and the run exited 1. So
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

## Other findings

The packet's logs `01-ancestry.log` and `09-qt5-probe-new.log` are empty (0
bytes). Ancestry was therefore re-verified live with `git merge-base
--is-ancestor` (exit 0 for both commits), and the GREEN probe result is present in
log 09b (`EXIT=0`).
