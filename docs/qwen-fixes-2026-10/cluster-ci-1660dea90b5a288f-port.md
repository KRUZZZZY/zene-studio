ALREADY-FIXED

# Cluster ci-1660dea90b5a288f-port — CI regression: build / linux-x86_64

ci-1660dea90b5a288f-port: ALREADY-FIXED. In round 3 the separate Qt 5 CMake build that the reviewer asked for was
BLOCKED by the environment (see "Round 3").

## What failed

- Hosted run 43764792400 on `31f559ab47`, Qt 5 `linux-x86_64`, log
  `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/31f559ab479dcd1b822545aae477222f58f32c0d/run-43764792400-job-132967358846.log`:
  the build stopped at `tests/src/gui/RowTableViewTest.cpp:54` and `:57`. `itemDelegateForIndex` is Qt 6-only;
  Qt 5.15 spells it `itemDelegate(const QModelIndex&)`.
- Hosted run 43786984969 on `fa4a5f2e78`, the newest hosted run, which is an ancestor of HEAD. Log
  `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/fa4a5f2e785c73e32216931e33cdbe140c06647d/run-43786984969-job-133036727661.log`:
  RowTableViewTest builds (`:12365` "Built target RowTableViewTest"). The job fails at `:12486` instead, on
  `tests/src/gui/MixerSendsOnFadersTest.cpp:278:81: no matching function for call to 'QStringList::QStringList(const
  int&, QString)'`.

## Why this is already fixed on this branch

- `f3b5426267` changes both calls to `itemDelegate(index)`, which compiles on Qt 5 and Qt 6. HEAD has them at
  `tests/src/gui/RowTableViewTest.cpp:54` and `:57`. `git merge-base --is-ancestor f3b5426267 HEAD` → EXIT=0.
- `421f6f13ae` (cluster ci-ec26fd9bddef0dd4) replaces `QStringList(channels, …)` with a portable loop. HEAD has it at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:279-281`, and the assertion `QVERIFY(faders == expected)` is unchanged.
  `git merge-base --is-ancestor 421f6f13ae HEAD` → EXIT=0.
- The original cluster commits `a8ad91c1a0` and `d76bffa5e2` are not ancestors (EXIT=1). f3b5426267 supersedes
  them.

## Round 3 (reviewer checkpoint: real Qt 5 build evidence)

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-1660dea90b5a288f-port.r3/logs/`. The round-2 log
folder `…r2/logs` no longer exists, so this report cites none of its files.

1. **Separate Qt 5 configure: BLOCKED.**
   `cmake -S <worktree> -B <worktree>/build-qt5 -DWANT_QT6=OFF … > configure-qt5-1.log 2>&1` → EXIT=97. The whole
   log is one line:
   `bunny-guard: configuring is the parent's job; the lane build tree is already configured. Use 'cmake --build build --target <t>'.`
   The lane build tree is Qt 6 (`build/CMakeCache.txt`: `WANT_QT6:BOOL=ON`, `Qt6Core_DIR:PATH=/usr/lib/cmake/Qt6Core`).
   Per the run rules, I stopped this step rather than work around the guard. As a result, the full-target Qt 5
   build, its ctest run, and the full-target Qt 5 red/green with the pre-fix lines were **not run**. `build-qt5/`
   was never created.
2. **Qt 5 red/green that did run**: `python3 tests/train-51-portability-check.py --source-root . > guard-1.log 2>&1` →
   EXIT=0. It compiles the real test bodies against the installed Qt 5 headers (pkg-config `Qt5Test`/`Qt5Widgets`)
   and the Qt 6 headers:
   ```
   PASS row-qt5-fixed: compiled
   PASS row-qt6-fixed: compiled
   PASS row-qt5-original: failed as expected      <- pre-fix itemDelegateForIndex( on Qt 5 (red)
   PASS mixer-faders-qt5: compiled
   PASS mixer-faders-qt6: compiled
   ```
   There is no compile-level red control for the pre-fix MixerSendsOnFadersTest line on Qt 5. That item is
   unverified this session.
3. **Green tests (lane Qt 6 tree)**:
   `cmake --build build --target zene RowTableViewTest MixerSendsOnFadersTest -j4 > build-1.log 2>&1` → EXIT=0.
   `cd build/tests && ctest -R 'RowTableViewTest|MixerSendsOnFadersTest' --output-on-failure > test-1.log 2>&1` →
   EXIT=0, `100% tests passed out of 2` (#462 RowTableViewTest Passed, #495 MixerSendsOnFadersTest Passed).
4. **Gates**: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log 2>&1` → EXIT=0,
   `RESULT: PASS`.

No product or test code is changed by this job. The report is the only diff.

## Self-check

- Fails without the fix, passes with it: no product change was made. The Qt 5 compile red/green for RowTableViewTest
  is the guard's `row-qt5-original` (fails as expected) against `row-qt5-fixed` (compiles), in guard-1.log. The
  full-target Qt 5 build red/green is BLOCKED (configure-qt5-1.log, EXIT=97).
- Deterministic: the guard is a compile-only probe. Both tests are offscreen widget tests that call
  `syncWithMixer()` directly and use no sleeps or wall-clock timing.
- At the cause and complete: these two are the only Qt 5 compile errors in the linux-x86_64 log of the newest hosted
  run. fff finds no remaining `itemDelegateForIndex` except the guard's negative control at
  `tests/train-51-portability-check.py:93`.
- No collateral damage: no product code, saved data or limits are touched.
- ALREADY-FIXED evidence: `tests/src/gui/RowTableViewTest.cpp:54,57` and
  `tests/src/gui/MixerSendsOnFadersTest.cpp:279-281`, plus the ancestry checks above.
- Sibling commands and control tests listed by submit_check: they come from other work merged on this branch, not
  from this job. This job changes no command file.

Delegation: round 3 used two agents. Sonnet tried the Qt 5 configure, build and red/green; the guard refused the
configure, and Sonnet stopped there and changed nothing. Haiku ran the lane build, ctest, the portability guard and
feature_check; I re-read every result from the logs. Round 2 used Haiku twice: once to map the hosted job logs, once
to run the builds, tests and checks.

## Other findings

These are from hosted run 43786984969 and are not fixed here:
- msvc-x64: `tests/src/gui/TouchLayoutTest.cpp(86): error C2872: 'ClipView': ambiguous symbol`. The guard's
  `touch-qualified-fixed` probe passes on HEAD.
- mingw64: an error at `tests/src/core/RemotePluginAudioPortsTest.cpp:75` in `countCriticals`. Not investigated.
- A Qt 5 CMake build tree for lanes is not available, and the guard refuses to create one. Proving Qt 5 portability
  needs that capability, or a hosted run.
