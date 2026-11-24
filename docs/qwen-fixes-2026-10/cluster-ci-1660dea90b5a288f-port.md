ALREADY-FIXED

# Cluster ci-1660dea90b5a288f-port — CI regression: build / linux-x86_64

ci-1660dea90b5a288f-port: ALREADY-FIXED

## What failed

The original cluster (ci-1660dea90b5a288f, report on commit a8ad91c1a0, which is not on this branch) was the
hosted Qt 5 `linux-x86_64` job of run 43764792400 on `31f559ab47`. It stopped at
`tests/src/gui/RowTableViewTest.cpp:54` and `:57`. `QAbstractItemView::itemDelegateForIndex` is Qt 6-only, and
Qt 5.15 spells it `itemDelegate(const QModelIndex&)`.

A newer hosted run, 43786984969 on `fa4a5f2e78`, shows that RowTableViewTest builds there. Its `linux-x86_64` job
(`train-ci/fa4a5f2e…/run-43786984969-job-133036727661.log:12486`) failed on a different Qt 5 error instead:
`tests/src/gui/MixerSendsOnFadersTest.cpp:278:81: no matching function for call to 'QStringList::QStringList(const
int&, QString)'`. That log reports RowTableViewTest compiling at `:12358`, linking at `:12363` and "Built target
RowTableViewTest" at `:12365`. The linux-arm64, macos and windows-arm64 jobs of that run fail on the same
MixerSendsOnFadersTest line.

## Why this is already fixed on this branch

- **RowTableViewTest**: `f3b5426267`, "fix(train-sol): Fixed four test compilation defects…", replaced both
  `itemDelegateForIndex(` calls with `itemDelegate(`. HEAD has it at `tests/src/gui/RowTableViewTest.cpp:54` and
  `:57`. The only remaining `itemDelegateForIndex` in the tree is the negative control at
  `tests/train-51-portability-check.py:93`.
  `git merge-base --is-ancestor f3b5426267 HEAD` → EXIT=0.
- **MixerSendsOnFadersTest**: `421f6f13ae`, "fix(ci): make the mixer faders answer compile on Qt 5.15 (cluster
  ci-ec26fd9bddef0dd4)", replaced `QStringList(channels, …)` with a portable loop. HEAD has the loop at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:279-281`. The assertion `QVERIFY(faders == expected)` is unchanged.
  `git merge-base --is-ancestor 421f6f13ae HEAD` → EXIT=0.
- The prior cluster commits `a8ad91c1a0` and `d76bffa5e2` are not ancestors of HEAD (both EXIT=1). Their change
  (a Qt-version `#if` helper) is superseded by f3b5426267's unconditional `itemDelegate(index)`. That form compiles
  on both Qt 5 and Qt 6 here (see the guard below).
- `fa4a5f2e78` is the newest hosted run in `train-ci/` and is an ancestor of HEAD (EXIT=0). No hosted log exists for
  a later commit.

## Commands and EXIT codes (logs in `~/.config/kads/zene/state/runs/ci-1660dea90b5a288f-port.r2/logs`)

```
git merge-base --is-ancestor f3b5426267 HEAD     EXIT=0
git merge-base --is-ancestor 421f6f13ae HEAD     EXIT=0
git merge-base --is-ancestor fa4a5f2e785c73e32216931e33cdbe140c06647d HEAD   EXIT=0
git merge-base --is-ancestor a8ad91c1a0 HEAD     EXIT=1
git merge-base --is-ancestor d76bffa5e2 HEAD     EXIT=1

python3 tests/train-51-portability-check.py --source-root . > logs/guard-1.log 2>&1      EXIT=0
  PASS row-qt5-fixed: compiled            (real Qt5Test/Qt5Widgets headers via pkg-config)
  PASS row-qt6-fixed: compiled
  PASS row-qt5-original: failed as expected   (negative control: itemDelegateForIndex on Qt 5)
  PASS mixer-faders-qt5: compiled
  PASS mixer-faders-qt6: compiled
cmake --build build --target zene RowTableViewTest MixerSendsOnFadersTest -j4 > logs/build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R 'RowTableViewTest|MixerSendsOnFadersTest' --output-on-failure > logs/test-1.log 2>&1   EXIT=0
  100% tests passed out of 2
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1   EXIT=0
  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures, complexity, file-length: all PASS — RESULT: PASS
```

No product or test code is changed by this job. There is no new regression test because nothing is broken. The
existing regression guard is `tests/train-51-portability-check.py`, which compiles the real test sources against Qt 5
and Qt 6 and includes a negative control for the original error.

## Self-check

- Verdict backed by evidence from this session: yes. The ancestry checks, hosted log lines and guard output are all
  quoted above.
- Regression test: not applicable (ALREADY-FIXED). The existing guard `row-qt5-original` fails as expected on the
  original code, and `row-qt5-fixed` compiles on HEAD (guard-1.log).
- Realtime, undo, commands, saved data: not touched. The diff is this report only.
- Registration: no new or changed product files. The gates pass (gates-1.log).
- Not verified locally: a full Qt 5 CMake build of the whole tree. The local build tree is Qt 6
  (`-DWANT_QT6=ON`). Qt 5 coverage comes from the guard's real-header compiles of the two affected test bodies.

Delegation: Haiku (1) mapped the seven hosted job logs of run 43786984969 and extracted the linux-x86_64 errors;
Haiku (2) ran the portability guard, the build, ctest and feature_check and reported the EXIT codes, which I
re-read from the logs. Sonnet and DeepSeek were not used because there was no code to write.

## Other findings

Not fixed here. These are from hosted run 43786984969, so they may already be fixed on HEAD:
- msvc-x64: `tests/src/gui/TouchLayoutTest.cpp(86): error C2872: 'ClipView': ambiguous symbol`. The guard's
  `touch-qualified-fixed` probe passes on HEAD.
- mingw64: `tests/src/core/RemotePluginAudioPortsTest.cpp:75` error in `countCriticals` (the message is cut off in the
  log). Not investigated.
