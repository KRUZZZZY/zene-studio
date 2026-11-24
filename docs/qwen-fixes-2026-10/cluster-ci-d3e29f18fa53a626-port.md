ALREADY-FIXED

# cluster ci-d3e29f18fa53a626-port — hosted `build / macos-x86_64` Qt 5 `QStringList` constructor

## Verdict

ALREADY-FIXED on this branch. The finding is real at its pinned commit
`fa4a5f2e785c73e32216931e33cdbe140c06647d` (`050/m0-train-35`):
`tests/src/gui/MixerSendsOnFadersTest.cpp` built its expected fader list with
`QStringList(channels, QStringLiteral("volume"))`, a two-argument fill constructor that Qt 6 supplies through
`QStringList = QList<QString>` but Qt 5.15 does not have. The hosted `macos-x86_64` job (Qt 5) therefore stopped
with `error: no matching function for call to 'QStringList::QStringList(const int&, QString)'`.

This branch already contains the portable spelling at
`tests/src/gui/MixerSendsOnFadersTest.cpp:278-282`, landed by `421f6f13ae` and merged through
`050/m0-train-36`. No product source needs changing for this port. This round ports the missing dedicated
regression checker from `257e9290e0` so the branch carries a test that itself fails on the historical spelling
and passes on the portable one.

## Evidence

- The pinned failing line at `fa4a5f2e785c73e32216931e33cdbe140c06647d`:
  `git show fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp`
  shows `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`.
- The current tree builds the same expected list one element at a time at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`:
  `QStringList expected; for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`
  followed by the unchanged `QVERIFY(faders == expected);`. The assertion is not weakened.
- `421f6f13ae` is the ancestor that introduced the portable loop; its diff is exactly the replacement of the
  two-argument constructor with `QStringList expected;` plus the append loop.
- Qt 5.15's `QStringList` has no count/value constructor: `/usr/include/qt/QtCore/qstringlist.h:111` derives it
  from `QList<QString>` and exposes only default, single-`QString`, copy/move, initializer-list and iterator-pair
  constructors. Qt 6 makes `QStringList` an alias of `QList<QString>` (`/usr/include/qt6/QtCore/qcontainerfwd.h`)
  whose `QList` has the size/value constructor, which is why the old line compiled locally with Qt 6 and only
  failed on the Qt 5 hosted job.
- `pkg-config --exists Qt5Core` and `pkg-config --exists Qt6Core` both returned EXIT=0 on this box, so the probe
  exercises both toolchains locally.
- The hosted failure is recorded in the CI queue record for this unit: run `43786984969`, job `133036727658`,
  URL `https://github.com/KRUZZZZY/zene-studio/actions/runs/43786984969`, with the exact
  `QStringList::QStringList(const int&, QString)` diagnostic at the historical line.
- `multi_grep` over `*.{cpp,h,hpp}` for the two-argument spellings found no live construction besides the
  explanatory comment in the test; the only occurrence is the checker's deliberate failing control.

## Ported regression test

This round adds the missing dedicated checker from commit `257e9290e0`:

- `tests/ci-d3e29f18-mixer-faders-portability-check.py` — re-created verbatim from `257e9290e0`.
  `expected_list_block` (`:72-85`) extracts the actual shipped block between
  `const QStringList faders = drivesOf(` and `QVERIFY(faders ==`; `probe` (`:88-117`) compiles that block
  against Qt 5 (and Qt 6 when installed) and also compiles the historical
  `QStringList expected(channels, QStringLiteral("volume"));` as a control that must fail with a
  `QStringList` / `no matching` diagnostic.
- `tests/CMakeLists.txt:6566-6573` registers it as CTest `CiD3e29f18MixerFadersPortability` on Linux when Python
  is available.
- `tests/fork-sources.txt:4083-4085` lists the checker and this port report.

The existing portable list is left unchanged, and the existing `Train51Portability` checker is untouched.

## Test matrix

- Normal path: the extracted block compiles against Qt 5 and Qt 6 (`faders-qt5-fixed`, `faders-qt6-fixed`).
- Failure control: the historical spelling must fail to compile with the Qt 5 diagnostic
  (`faders-qt5-original`), so a probe that cannot reproduce the defect cannot pass silently.
- Reconfiguration path: the CTest is registered only on Linux with Python; reconfiguring this build tree made
  it visible and runnable, and the Qt 6 compile leg is skipped cleanly when Qt6Core is absent.
- Exhaustion/limits: the expected list is built by a loop over `channels`, and mixer channels are an unbounded
  vector, so any channel count is covered; no fixed limit is involved.
- Audio-thread constraints: the change is test/CI only and touches no process/render/audio code.
- Supported-platform build: the probe compiles with the local g++ against Qt 5 and Qt 6. MSVC and macOS were not
  available locally; the checker's Qt 5 leg reproduces the hosted macOS failure on this box.

## Commands and EXIT codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r3/logs`.

| Step | Command | Exit |
|---|---|---|
| Inspect the ported checker commit | `git show --stat 257e9290e0` | 0 |
| Inspect the ancestor fix hunk | `git show 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp` | 0 |
| Confirm both Qt toolchains | `pkg-config --exists Qt5Core` / `pkg-config --exists Qt6Core` | 0 / 0 |
| GREEN before the experiment | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root "$PWD"` (`test-1-green-current.log`) | 0 |
| Reconfigure | `cmake -S . -B build` (`build-1-reconfigure.log`) | 0 |
| Build the reverse patch | `git diff 421f6f13ae^ 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp > .../revert-421f6f13ae.patch` | 0 |
| Check the reverse patch applies | `git apply -R --check .../revert-421f6f13ae.patch` | 0 |
| Revert the fix only | `git apply -R .../revert-421f6f13ae.patch` | 0 |
| RED on the historical spelling | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root "$PWD"` (`test-2-red-reverted.log`) | 1 |
| Restore the fix | `git apply .../revert-421f6f13ae.patch` | 0 |
| GREEN after restoring | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root "$PWD"` (`test-3-green-restored.log`) | 0 |
| Build the affected Qt test | `nice -n 10 env -u MAKEFLAGS -u CMAKE_BUILD_PARALLEL_LEVEL /usr/bin/cmake --build build --target MixerSendsOnFadersTest -j2` (`build-2-mixer-faders.log`) | 0 |
| CTest portability regression | `cd build/tests && ctest -R '^CiD3e29f18MixerFadersPortability$' --output-on-failure` (`test-4-ctest-portability.log`) | 0 (1/1) |
| Run the affected Qt test | `cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` (`test-5-ctest-mixer-faders.log`) | 0 (1/1) |
| Run the regression family | `cd build/tests && ctest -R '^(CiD3e29f18MixerFadersPortability|MixerSendsOnFadersTest|Train51Portability)$' --output-on-failure` (`test-6-family.log`) | 0 (3/3) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`) | 0 (RESULT: PASS) |

The RED run's log contains the exact failure:

```
mixer-faders portability check: faders-qt5-fixed: expected compile success; exit 1:
error: no matching function for call to 'QStringList::QStringList(int&, QString)'
    (void)(faders == QStringList(channels, QStringLiteral("volume")));
```

The GREEN runs print `PASS faders-qt5-fixed: compiled`, `PASS faders-qt6-fixed: compiled` and
`PASS faders-qt5-original: failed as expected`.

No `src/`, `include/` or plugin file changed this round, so the procedure's
`git diff -- <product files>` step yields an empty patch; as the diagnostic checkpoint directs, the
reverse/restore experiment reverses the ancestor `421f6f13ae` hunk in the test source instead.

## Self-check

- **Regression test FAILS with the fix reverted and PASSES with it.** Reverting `421f6f13ae`'s hunk
  (`git apply -R`, EXIT=0) makes the checker EXIT=1 with the exact Qt 5
  `no matching function for call to 'QStringList::QStringList(int&, QString)'` diagnostic
  (`test-2-red-reverted.log`). Restoring it (`git apply`, EXIT=0) makes the same checker EXIT=0
  (`test-3-green-restored.log`). CTest `CiD3e29f18MixerFadersPortability` is 1/1 and
  `MixerSendsOnFadersTest` is 1/1; the family run is 3/3.
- **Deterministic.** The checker's inputs are the shipped source text and the compiler's Qt responses; there is no
  wall-clock time, sleep, external service or concurrent state. The only injected value is the `channels`
  parameter of the probe, and the compile result is deterministic.
- **Cause and completeness.** The cause is a test-source Qt 6-only constructor. The live tree has no remaining
  two-argument `QStringList` fill construction; the assertion still requires exactly one `"volume"` per channel.
  The sibling runtime path (`sends_on_faders` set, read back through `window.mixer_view`) is exercised by
  `MixerSendsOnFadersTest`, which passes.
- **No collateral damage.** This port changes no `src/`, `include/`, plugin, save/load, undo, command or audio
  code; project and wire behaviour are untouched. No control command was added, so the
  `ControlCommandsSnapshot` canary does not move.
- **ALREADY-FIXED code reference.** `tests/src/gui/MixerSendsOnFadersTest.cpp:278-282` is the portable list and
  `421f6f13ae` is the ancestor commit, both cited above with EXIT=0 checks. The new checker at
  `tests/ci-d3e29f18-mixer-faders-portability-check.py:72-117` pins it.
- **Qt 5.15 / Qt 6 portability.** The probe compiles the same shipped block with both Qt 5 and Qt 6 on this box.
  MSVC and macOS were not run locally; the change only removes a Qt-6-only expression, and the hosted macOS
  diagnostic is the evidence that the old expression was the failure.
- **Sibling commands.** `git show --stat` for this round's commits shows only tests, the report and the
  registration lists; no command file or command registration changed. The sibling ids the submit check lists
  come from the existing train range between `fa4a5f2e` and this branch, not from this port.
- **Registration.** `fork-sources` gate PASS, `all-sources-reproduce` PASS, `no-upstream-regression` PASS,
  `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS; complexity and file-length report no new item
  in the changed files. `tests/fork-sources.txt` ends with a newline.

## Other findings

- `MixerModesTest` has no executable in this build tree, so the family run used the built applicable targets
  `MixerSendsOnFadersTest`, `Train51Portability` and the new
  `CiD3e29f18MixerFadersPortability`; no test was skipped or weakened.
