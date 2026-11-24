ALREADY-FIXED

# Cluster ci-1660dea90b5a288f-port — CI regression: build / linux-x86_64

ci-1660dea90b5a288f-port: ALREADY-FIXED

No product code is changed. Round 4 adds the missing Qt 5 negative control to the existing
`Train51Portability` ctest (`tests/train-51-portability-check.py`). The separate Qt 5 CMake build tree the reviewer
asked for is BLOCKED by the environment (see "Qt 5 build tree").

## What failed

- Hosted run 43764792400 on `31f559ab47`, Qt 5 `linux-x86_64`, log
  `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/31f559ab479dcd1b822545aae477222f58f32c0d/run-43764792400-job-132967358846.log`:
  the build stopped at `tests/src/gui/RowTableViewTest.cpp:54` and `:57`. `itemDelegateForIndex` is Qt 6-only, and
  Qt 5.15 has `itemDelegate(const QModelIndex&)` instead.
- Hosted run 43786984969 on `fa4a5f2e78`, the newest hosted run and an ancestor of HEAD, log
  `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/fa4a5f2e785c73e32216931e33cdbe140c06647d/run-43786984969-job-133036727661.log`:
  RowTableViewTest builds (`:12365`). The job fails at `:12486` on
  `tests/src/gui/MixerSendsOnFadersTest.cpp:278:81: no matching function for call to 'QStringList::QStringList(const
  int&, QString)'`.

## Why it is already fixed

- `f3b5426267` changes both calls to `itemDelegate(index)`. HEAD has them at `tests/src/gui/RowTableViewTest.cpp:54`
  and `:57`. `git merge-base --is-ancestor f3b5426267 HEAD` → EXIT=0.
- `421f6f13ae` (cluster ci-ec26fd9bddef0dd4) replaces `QStringList(channels, …)` with a portable loop. HEAD has it at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:279-281`, and the assertion `QVERIFY(faders == expected)` is unchanged.
  `git merge-base --is-ancestor 421f6f13ae HEAD` → EXIT=0.
- The original cluster commits `a8ad91c1a0` and `d76bffa5e2` are not ancestors of HEAD (EXIT=1). f3b5426267
  supersedes them.

## Regression guard added (round 4)

Until now, `mixer_faders_probe` in `tests/train-51-portability-check.py` only showed that HEAD's block compiles. Nothing
proved the probe would catch the hosted error. It now also compiles the pre-fix line
`const QStringList expected(channels, QStringLiteral("volume"));` against the real Qt 5 headers (pkg-config
`Qt5Core`) and requires that compile to fail with a `QStringList` diagnostic (`mixer-faders-qt5-original`). This only
adds a check; no existing check is relaxed. `row_probe` already had the matching Qt 5 control (`row-qt5-original`).

## Commands and EXIT codes

All logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/ci-1660dea90b5a288f-port.r4/logs/`.

```
python3 tests/train-51-portability-check.py --source-root . > guard-1.log 2>&1          EXIT=0
  PASS row-qt5-fixed: compiled
  PASS row-qt6-fixed: compiled
  PASS row-qt5-original: failed as expected
  PASS mixer-faders-qt5: compiled
  PASS mixer-faders-qt5-original: failed as expected     <- new
  PASS mixer-faders-qt6: compiled
cd build/tests && ctest -R Train51Portability --output-on-failure > test-green-1.log 2>&1   EXIT=0 (100% passed out of 1)
# red A: RowTableViewTest.cpp:54,57 set back to view.itemDelegateForIndex(
cd build/tests && ctest -R Train51Portability --output-on-failure > test-red-row.log 2>&1    EXIT=8
  train51 portability check: RowTableViewTest no longer has both effective index-delegate assertions
git checkout -- tests/src/gui/RowTableViewTest.cpp                                           EXIT=0
# red B: MixerSendsOnFadersTest.cpp loop set back to const QStringList expected(channels, QStringLiteral("volume"));
cd build/tests && ctest -R Train51Portability --output-on-failure > test-red-mixer.log 2>&1  EXIT=8
  train51 portability check: mixer-faders-qt5: expected compile success; exit 1: ...
  no matching function for call to 'QStringList::QStringList(const int&, QString)'   (real Qt 5 headers)
git checkout -- tests/src/gui/MixerSendsOnFadersTest.cpp                                     EXIT=0
cd build/tests && ctest -R Train51Portability --output-on-failure > test-green-2.log 2>&1   EXIT=0 (100% passed out of 1)
cmake --build build --target zene RowTableViewTest MixerSendsOnFadersTest -j4 > build-1.log 2>&1   EXIT=0
cd build/tests && ctest -R 'RowTableViewTest|MixerSendsOnFadersTest' --output-on-failure > test-gui-2.log 2>&1   EXIT=0 (100% passed out of 2)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gates-1.log 2>&1   EXIT=0, RESULT: PASS
```

`test-gui-1.log` (EXIT=8) is an earlier run from before the rebuild. Both tests were "Not Run" because this round's
build tree had no test binaries yet. `build-1.log` built them, and `test-gui-2.log` is the valid run.

## Qt 5 build tree: BLOCKED

`cmake -S <worktree> -B <worktree>/build-qt5 -DWANT_QT6=OFF > configure-qt5-1.log 2>&1` → EXIT=97. The whole log is
`bunny-guard: configuring is the parent's job; the lane build tree is already configured. Use 'cmake --build build --target <t>'.`
The lane tree is Qt 6. As the run rules require, I stopped this step without working around the guard. A full Qt 5
build and ctest of RowTableViewTest and MixerSendsOnFadersTest were **not run**. The Qt 5 evidence above comes from
compiling the test bodies against the real installed Qt 5 headers.

## Self-check

- Fails without the fix, passes with it: with the pre-fix test lines restored, `Train51Portability` gives EXIT=8 for
  each (test-red-row.log, test-red-mixer.log); on HEAD it gives EXIT=0 (test-green-2.log). The mixer red is a real Qt 5
  compiler rejection. The new negative control passes on HEAD (guard-1.log).
- Deterministic: the guard is compile-only and uses no timing. Both GUI tests run offscreen, drive `syncWithMixer()`
  directly and use no sleeps.
- At the cause and complete: these are the only two Qt 5 errors in the linux-x86_64 logs of both hosted runs. fff
  finds no other `itemDelegateForIndex` outside the guard's negative control.
- No collateral damage: no product code, saved data or limits are touched. The guard gains one negative control and
  loses none.
- ALREADY-FIXED evidence: `tests/src/gui/RowTableViewTest.cpp:54,57` and
  `tests/src/gui/MixerSendsOnFadersTest.cpp:279-281`, plus the ancestry checks above.
- Sibling commands and control tests listed by submit_check: they come from other work merged on this branch. This
  job changes no command file.

Delegation: in round 4, Haiku (1) ran the Qt 5 configure attempt and the red/green `Train51Portability` runs with
the temporary test-source edits, and reverted them. Haiku (2) rebuilt and ran the two GUI tests. I wrote the
guard's new negative control myself, since it is a 15-line edit to an open file, and I re-read every result from the
logs. In round 3, Sonnet tried the Qt 5 build and was refused by the guard.

## Other findings

These are from hosted run 43786984969 and are not fixed here:
- msvc-x64: `tests/src/gui/TouchLayoutTest.cpp(86): error C2872: 'ClipView': ambiguous symbol`. The guard's
  `touch-qualified-fixed` probe passes on HEAD.
- mingw64: an error at `tests/src/core/RemotePluginAudioPortsTest.cpp:75` in `countCriticals`. Not investigated.
- Lanes have no Qt 5 CMake build tree, and the guard refuses to create one.
