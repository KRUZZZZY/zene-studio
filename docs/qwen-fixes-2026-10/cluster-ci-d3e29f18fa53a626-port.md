ALREADY-FIXED

# cluster ci-d3e29f18fa53a626-port — hosted `build / macos-x86_64` Qt 5 `QStringList` constructor

## Verdict

ALREADY-FIXED on this branch. The finding is real at its pinned commit `fa4a5f2e785c73e32216931e33cdbe140c06647d`
(`050/m0-train-35`): `tests/src/gui/MixerSendsOnFadersTest.cpp` built its expected fader list with
`QStringList(channels, QStringLiteral("volume"))`, a two-argument fill constructor that Qt 6 supplies through
`QStringList = QList<QString>` but Qt 5.15 does not have. The hosted `macos-x86_64` job (Qt 5) therefore stopped
with `error: no matching function for call to 'QStringList::QStringList(const int&, QString)'`.

This branch (`050/m0-train-51`) already contains the portable spelling at
`tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`, landed by `421f6f13ae` and merged through `050/m0-train-36`.
No product source needs changing for this port.

## Evidence

- The pinned failing line at `fa4a5f2e785c73e32216931e33cdbe140c06647d`:
  `git show fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp`
  shows `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`.
- The current tree builds the same expected list one element at a time at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`:
  `QStringList expected; for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`
  followed by the unchanged `QVERIFY(faders == expected);`. The assertion is not weakened.
- `git merge-base --is-ancestor 421f6f13ae HEAD` returned EXIT=0, and
  `git log --oneline -1 421f6f13ae` is
  `421f6f13ae fix(ci): make the mixer faders answer compile on Qt 5.15 (cluster ci-ec26fd9bddef0dd4)`.
- Qt 5.15's `QStringList` has no count/value constructor: `/usr/include/qt/QtCore/qstringlist.h:115-121` lists
  only default, single-`QString`, `QList`, initializer-list and iterator-pair constructors. Qt 6 makes
  `QStringList` an alias of `QList<QString>` (`/usr/include/qt6/QtCore/qcontainerfwd.h:41`) and `QList` has the
  size/value constructor (`/usr/include/qt6/QtCore/qlist.h:311`), which is why the old line compiled locally
  with Qt 6 and only failed on the Qt 5 hosted job.
- The hosted failure is recorded in the CI queue record for this unit: run `43786984969`, job `133036727658`,
  URL `https://github.com/KRUZZZZY/zene-studio/actions/runs/43786984969`, with the exact
  `QStringList::QStringList(const int&, QString)` diagnostic at the historical line.
- The only live occurrence of the two-argument spelling is the explanatory comment at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:278-279`; `multi_grep` over `*.{cpp,h,hpp}` for
  `QStringList(channels`, `QStringList(count`, `QStringList(size`, `QStringList(n` and
  `QStringList(num` found no other construction.
- The product contract is unchanged: `src/gui/ControlCommandsMixerView.cpp` still returns one `"volume"` per
  channel when no send is shown. Only the test's way of building the expected list changed.

## Regression

`tests/train-51-portability-check.py:169` (`mixer_faders_probe`) extracts the expected-list block actually
shipped by `MixerSendsOnFadersTest.cpp`, injects `channels = 3` and a stub `QVERIFY`, and compiles it against
`pkg-config Qt5Core` and, when present, `Qt6Core`. It is registered as the existing CTest `Train51Portability`
in `tests/CMakeLists.txt:6536-6539`. The probe is deterministic: its inputs are the source text and the
injected channel count; there is no clock, sleep or race.

The proof below is the procedure's required reverse/restore test. Reverting only the `421f6f13ae` hunk makes
the Qt 5 compile fail with the hosted job's diagnostic; restoring it makes the probe pass and the runtime test
pass.

## Commands and EXIT codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r2/logs`.

| Step | Command | Exit |
|---|---|---|
| Inspect the pinned failing source | `git show fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp \| sed -n '270,285p'` | 0 |
| Prove the fix commit is on this branch | `git merge-base --is-ancestor 421f6f13ae HEAD` | 0 |
| GREEN before the experiment | `python3 tests/train-51-portability-check.py --source-root "$PWD"` (`test-1-train51-portability.log`) | 0 |
| Build the reverse patch | `git diff 421f6f13ae^ 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r2/logs/revert-fix.patch` | 0 |
| Check the reverse patch applies | `git apply -R --check /home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r2/logs/revert-fix.patch` | 0 |
| Revert the fix only | `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r2/logs/revert-fix.patch` | 0 |
| RED on the historical spelling | `python3 tests/train-51-portability-check.py --source-root "$PWD"` (`test-2-red-reverted.log`) | 1 |
| Restore the fix | `git apply /home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r2/logs/revert-fix.patch` | 0 |
| GREEN after restoring | `python3 tests/train-51-portability-check.py --source-root "$PWD"` (`test-3-green-restored.log`) | 0 |
| CTest portability regression | `cd build/tests && ctest -R '^Train51Portability$' --output-on-failure` (`test-4-ctest-train51.log`) | 0 (1/1) |
| Build the affected Qt test | `nice -n 10 env -u MAKEFLAGS -u CMAKE_BUILD_PARALLEL_LEVEL /usr/bin/cmake --build build --target MixerSendsOnFadersTest -j2` (`build-2-mixer-faders-direct.log`) | 0 |
| Run the affected Qt test | `cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` (`test-5-ctest-mixer-faders.log`) | 0 (1/1) |
| Run the regression family | `cd build/tests && ctest -R '^(MixerSendsOnFadersTest\|Train51Portability)$' --output-on-failure` (`test-6-family.log`) | 0 (2/2) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-d3e29f18fa53a626-port` (`gates-1.log`) | 0 |

The RED run's log contains the exact failure:

```
train51 portability check: mixer-faders-qt5: expected compile success; exit 1:
error: no matching function for call to 'QStringList::QStringList(const int&, QString)'
    QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));
```

The GREEN run prints `PASS mixer-faders-qt5: compiled` and `PASS mixer-faders-qt6: compiled`.

The first build attempt (`build-1-mixer-faders.log`) entered the shared build-admission wrapper but stayed
waiting for a root-build slot for over ten minutes with no child process; it was stopped with Ctrl-C (EXIT=130)
and the single target was built directly with `-j2` instead. The hosted macOS runner itself was not available
here, so the hosted build was not re-run; the Qt 5 compile probe reproduces its diagnostic on this box.

## Self-check

- **Regression fails reverted, passes restored.** Reverted (`git apply -R`, EXIT=0):
  `train-51-portability-check.py` EXIT=1 with the exact Qt 5 diagnostic above. Restored (`git apply`, EXIT=0):
  the same probe EXIT=0, CTest `Train51Portability` 1/1, `MixerSendsOnFadersTest` 1/1, family 2/2.
- **Deterministic.** The probe reads the shipped source block and injects `channels = 3` and a stub `QVERIFY`;
  it uses no wall-clock time, sleep, external service or concurrent state.
- **Cause and completeness.** The cause is a test-source Qt 6-only constructor. The live tree has no remaining
  two-argument `QStringList` fill construction (multi-term search above); the assertion still requires exactly
  one `"volume"` per channel. The sibling runtime path (`sends_on_faders` set, then empty) is exercised by
  `MixerSendsOnFadersTest`.
- **No collateral damage.** This port changes no `src/`, `include/`, plugin, save/load, undo, command or audio
  code; the existing project and wire behaviour is untouched. No control command was added.
- **ALREADY-FIXED code reference.** `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282` is the portable list and
  `421f6f13ae` is the ancestor commit (both cited above with EXIT=0 checks).
- **Qt 5.15 / Qt 6 portability.** The probe compiles the same shipped block with both Qt 5 and Qt 6 on this box.
  MSVC and macOS were not run locally; the change only removes a Qt-6-only expression, and the hosted macOS
  diagnostic is the evidence that the old expression was the failure.

## Other findings

- The shared build-admission wrapper was saturated (both `root-build-*.lock` slots held and the make token pool
  at zero with eight make processes) for over ten minutes, so the first build never started. This is build
  infrastructure, not product behaviour; the single target was built directly with `-j2`.
- `MixerModesTest` has no executable in this build tree, so the family run used the built applicable targets
  `MixerSendsOnFadersTest` and `Train51Portability`; no test was skipped or weakened.
