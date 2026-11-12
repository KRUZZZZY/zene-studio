FIXED - cluster ci-7d1b70f4f0a0b12b: Qt 5 CI build stops on ambiguous `QStringList` brace assignments in three test files

Verdict: REAL, test-source portability defect (not infrastructure, not a product
runtime bug). The hosted `build / linux-arm64` job at
`0c3b9e3c23434f269526f189c02e88698ce63ec5` builds the Qt 5 lane
(`.github/workflows/build.yml:158` sources `/opt/qt5*/bin/qt5*-env.sh`; Qt 6
jobs pass `-DWANT_QT6=ON` at `.github/workflows/build.yml:1190`). On Qt 5 the
same compiler error reproduces locally on x86_64; the defect is in the test
sources, not in the architecture.

Round 2's saved `fix.patch` was still available and was applied unchanged.
At the start of this round `HEAD` was the train commit `0c3b9e3c23` with a clean
tree; no cluster-specific commit was present on the branch, so this report and
the fix are committed together below. The identical fix exists on the sibling
branch as `b879feb420` for cluster `ci-48dcc062d04c645f`, which corroborates
the cause but was not used as a substitute for local evidence.

## The reported failure

`tests/src/core/ControllerScriptTest.cpp`, four assignments of the form

```
m_client.readable = {QStringLiteral("Pad:in")};   // lines 139, 299
m_client.writable = {QStringLiteral("Pad:out")};  // lines 140, 318
```

Qt 5's `QStringList` declares `operator=(const QList<QString>&)` and
`operator=(QList<QString>&&)` (`/usr/include/x86_64-linux-gnu/qt5/QtCore/qstringlist.h:124,126`)
in addition to its copy/move assignment. A brace-enclosed list can initialize
either `QStringList` or its `QList<QString>` base, so all four candidates are
viable and overload resolution is ambiguous. Qt 6 defines `QStringList` as
`QList<QString>` without those extra overloads, so the Qt 6 lane compiled the
same source; that is why only the Qt 5 job failed.

The failing target stopped the build before the sibling targets were compiled,
so the same pattern was still hidden in:

- `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301`
- `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`

All nine sites are test code. No product source is involved; the product
targets (`zene`, `lmmsobjs`) build and run as before.

## Test matrix

| Path | Case | Expected | Result |
|---|---|---|---|
| Compile regression, Qt 5 | `ControllerScriptTest` before the fix | fails at lines 139, 140, 299, 318 | EXIT 2 (`build-1-qt5-red-controller.log`) |
| Compile regression, Qt 5 | `ExternalMidiDeviceTest` before the fix | fails at lines 155, 292, 301 | EXIT 2 (`build-2-qt5-red-externalmidi.log`) |
| Compile regression, Qt 5 | `ExternalDeviceCardTest` before the fix | fails at lines 123, 232 | EXIT 2 (`build-3-qt5-red-externalcard.log`) |
| Ordinary path | `ControllerScriptTest::theExampleRunsACommandAndSendsFeedbackToItsPort` | command and feedback reach the bound port | pass, EXIT 0 (`test-6-qt5-controller-functions.log`) |
| Ordinary path | `ExternalMidiDeviceTest::theCommandConfiguresTheDeviceAndThePatchReachesTheOutput` | port/channel/bank/program applied and patch sent | pass, EXIT 0 (`test-7-qt5-externalmidi-functions.log`) |
| Ordinary path | `ExternalDeviceCardTest::theControlsConfigureTheDevice` | controls leave the device the commands leave | pass, EXIT 0 (`test-8-qt5-externalcard-functions.log`) |
| Failure/exhaustion | `ControllerScriptTest::scriptErrorsAreBoundedAndReported` | bounded script errors | pass, EXIT 0 (`test-6`) |
| Failure/exhaustion | `ExternalMidiDeviceTest::aRefusedCallChangesNothing`, `::aPresetWhosePortIsGoneAppliesTheRestAndSaysSo`, `::aDamagedPresetListIsBounded` | refusal, missing port and damaged preset list stay bounded | pass, EXIT 0 (`test-7`) |
| Realtime | `ControllerScriptTest::deliveryNeverBlocksTheAudioThreadAndPortLossIsReported` | flooded inbox drains boundedly, zero allocations/frees on producer thread | pass, EXIT 0 (`test-6`); no product code changed |
| Portability | same three targets, Qt 6 | builds and tests pass | build EXIT 0 (`build-4`, `build-12`), ctest 3/3 EXIT 0 (`test-2`, `test-5`) |
| Portability | same three targets, Qt 5 | builds and tests pass after the fix | build EXIT 0 (`build-5`, `build-10`), ctest 3/3 EXIT 0 (`test-1`, `test-3`) |

The three named test slots above run inside their target executables; the
explicit per-function runs include `initTestCase`/`cleanupTestCase`, hence the
reported totals (5, 6 and 3 passed, 0 failed).

## The fix

Replace each ambiguous brace-list assignment with an explicit `QStringList`
initializer, preserving the exact value:

```
m_client.readable = QStringList{QStringLiteral("Pad:in")};
m_client.writable = QStringList{QStringLiteral("Pad:out")};
```

The same spelling is used for all nine sites in the three files. A search for
the remaining `= {QStringLiteral` form in `tests/src` finds only declarations
that already name the type (`const QStringList name = {...}`), which are not
ambiguous. No production code, command, undo path or audio-thread path changes.

## Reproduction and reversal proof

Qt 5 build tree: the surviving tree from the earlier lane run at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-7d1b70f4f0a0b12b.r1/logs/qt5-build`
(configured with `-DWANT_QT6=OFF`, `CMAKE_HOME_DIRECTORY` pointing at this
worktree). The lane shim refuses ctest outside `<lane>/build/tests`, so the
secondary Qt 5 tree uses `/usr/bin/ctest`, exactly as in the earlier lane run.

| Step | Command (run in the worktree unless noted) | Exit |
|---|---|---|
| RED, controller | `cmake --build "$QT5" --target ControllerScriptTest -j"$(nproc)"` | 2 |
| RED, MIDI device | `cmake --build "$QT5" --target ExternalMidiDeviceTest -j"$(nproc)"` | 2 |
| RED, device card | `cmake --build "$QT5" --target ExternalDeviceCardTest -j"$(nproc)"` | 2 |
| Qt 6 pre-fix | `cmake --build build --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j"$(nproc)"` | 0 |
| GREEN, Qt 5 | `cmake --build "$QT5" --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j"$(nproc)"` | 0 |
| GREEN tests, Qt 5 | `/usr/bin/ctest -R 'ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest' --output-on-failure` (from `"$QT5"/tests`) | 0, 3/3 |
| GREEN tests, Qt 6 | `ctest -R 'ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest' --output-on-failure` (from `build/tests`) | 0, 3/3 |
| Save patch | `git diff -- tests/src/core/ControllerScriptTest.cpp tests/src/core/ExternalMidiDeviceTest.cpp tests/src/gui/ExternalDeviceCardTest.cpp > "$LOGS/fix.patch"` | 0 |
| Reverse patch | `git apply -R "$LOGS/fix.patch"` | 0 |
| RED again, controller | `cmake --build "$QT5" --target ControllerScriptTest -j"$(nproc)"` | 2 |
| RED again, MIDI device | `cmake --build "$QT5" --target ExternalMidiDeviceTest -j"$(nproc)"` | 2 |
| RED again, device card | `cmake --build "$QT5" --target ExternalDeviceCardTest -j"$(nproc)"` | 2 |
| Restore patch | `git apply "$LOGS/fix.patch"` | 0 |
| GREEN again, Qt 5 | `cmake --build "$QT5" --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j"$(nproc)"` | 0 |
| GREEN again, Qt 5 tests | `/usr/bin/ctest -R 'ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest' --output-on-failure` | 0, 3/3 |
| GREEN final, Qt 6 | `cmake --build build --target ControllerScriptTest ExternalMidiDeviceTest ExternalDeviceCardTest -j"$(nproc)"` | 0 |
| GREEN final, Qt 6 tests | `ctest -R 'ControllerScriptTest\|ExternalMidiDeviceTest\|ExternalDeviceCardTest' --output-on-failure` | 0, 3/3 |
| Product target | `cmake --build build --target zene -j"$(nproc)"` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0, PASS |

One discarded invocation used the wrong relative build path from
`build/tests` and exited 1 with `build/tests/build is not a directory`
(`build-11-qt6-final-three.log`); it was rerun correctly as `build-12` and
`test-5`, which are the results recorded above.

The RED logs contain the exact CI errors, for example:

```
tests/src/core/ControllerScriptTest.cpp:139:62: error: ambiguous overload for 'operator='
  (operand types are 'QStringList' and '<brace-enclosed initializer list>')
```

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-7d1b70f4f0a0b12b.r3/logs`.

## Gate table

```
PASS  fork-sources            exit 0
PASS  all-sources-reproduce   exit 0
PASS  no-upstream-regression  exit 0
PASS  check-namespace         exit 0
PASS  no-tautology            exit 0
PASS  doc-figures             exit 0
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `tests/src/core/ControllerScriptTest.cpp` (4 assignments)
- `tests/src/core/ExternalMidiDeviceTest.cpp` (3 assignments)
- `tests/src/gui/ExternalDeviceCardTest.cpp` (2 assignments)
- `docs/qwen-fixes-2026-10/cluster-ci-7d1b70f4f0a0b12b.md` (this report)

All three test files are already registered in `tests/fork-sources.txt`
(lines 2419, 2427, 3400); no registration edit is required.

## Self-check

- **Regression fails with the fix reverted and passes restored:** reversing the
  test-source hunks makes all three Qt 5 target builds fail (exit 2 each,
  `build-7`/`build-8`/`build-9`); restoring the hunks makes them build and pass
  (exit 0, `build-10`/`test-3`). This is a compile regression, so the target
  build is the regression test; the same three executables then pass 3/3 on
  both Qt 5 and Qt 6.
- **Deterministic:** the regression is a compile-time overload-resolution
  failure. It uses no wall-clock timing, sleeps, races or injected timestamps.
  The same source and toolchain always produce the same error at the same
  nine lines.
- **Cause and completeness:** the cause is the ambiguous `QStringList`
  brace-assignment overload set in Qt 5. Every occurrence in the three affected
  files was changed (nine sites); a tree-wide search found no other
  assignment-form occurrences, and the remaining declarations already name
  `QStringList`. The three sibling targets from the same CI area were built and
  tested on both Qt versions.
- **No collateral damage:** the change only spells the same `QStringList`
  initializer explicitly; the value and runtime behavior are identical. Old
  projects, save/load data, commands and undo are untouched. No limit or check
  was added. The full `zene` target still builds (exit 0), and the affected
  CTest family passes 3/3 on both Qt 5 and Qt 6.
- **Sibling commands:** this change touches no command file, command id,
  schema, handler or reversibility class. The sibling list printed by
  `submit_check.py` comes from the train-wide diff since the fix base
  (`app.*`, `arrangement.*` and others are already on the train), not from this
  commit. No command is added or changed, so none of those siblings is
  affected; the three test executables only drive existing commands.
- **Verdict evidence:** this is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The
  pre-fix Qt 5 build at `HEAD` reproduced the reported errors at
  `tests/src/core/ControllerScriptTest.cpp:139,140,299,318`,
  `tests/src/core/ExternalMidiDeviceTest.cpp:155,292,301` and
  `tests/src/gui/ExternalDeviceCardTest.cpp:123,232`; the post-fix build and
  tests pass.
