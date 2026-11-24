ALREADY-FIXED

# cluster ci-d3e29f18fa53a626-port — hosted `build / macos-x86_64` Qt 5 `QStringList` constructor

## Verdict

ALREADY-FIXED on this branch, and now pinned by a regression test that fails on the historical spelling and
passes on the portable one. The finding is real at its pinned commit
`fa4a5f2e785c73e32216931e33cdbe140c06647d` (`050/m0-train-35`):
`tests/src/gui/MixerSendsOnFadersTest.cpp` built its expected fader list with
`QStringList(channels, QStringLiteral("volume"))`, a two-argument fill constructor that Qt 6 supplies through
`QStringList = QList<QString>` but Qt 5.15 does not have, so the Qt 5 hosted job stopped with
`error: no matching function for call to 'QStringList::QStringList(const int&, QString)'`.

The branch already carries the portable spelling at `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`, landed by
`421f6f13ae` and merged through `050/m0-train-36`. No product source needs changing for this port. This round
re-runs and records the ported dedicated checker from `257e9290e0`
(`tests/ci-d3e29f18-mixer-faders-portability-check.py`, CTest `CiD3e29f18MixerFadersPortability`) as the
regression test for the cluster, with the red/green experiment below.

## Evidence

- Historical failing line, read this session:
  `git show fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp` (EXIT=0) shows
  `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`.
- Current portable line, read this session at `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`:
  `QStringList expected; for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`
  followed by the unchanged `QVERIFY(faders == expected);`. The assertion is not weakened.
- `git show 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp` (EXIT=0) is exactly the replacement of the
  two-argument constructor with the append loop; `421f6f13ae` is an ancestor of this branch.
- Qt toolchains on this box: `pkg-config --modversion Qt5Core Qt6Core` printed `5.15.19` and `6.12.0`
  (EXIT=0), so the probe exercises both. Qt 5.15's `QStringList` constructor list is what the compiler prints in
  the red run below: default, single-`QString`, copy/move, `QList`, initializer-list and iterator pair only.
- The ported checker is byte-identical to the original: `diff -u <git show 257e9290e0:...> tests/...` gave
  DIFF_EXIT=0.

## Ported regression test

- `tests/ci-d3e29f18-mixer-faders-portability-check.py` — re-created verbatim from `257e9290e0`
  (DIFF_EXIT=0 above). `expected_list_block` (`:72-85`) extracts the shipped block between
  `const QStringList faders = drivesOf(` and `QVERIFY(faders ==`; `probe` (`:88-117`) compiles that block against
  Qt 5 and (when installed) Qt 6, and also compiles the historical
  `QStringList expected(channels, QStringLiteral("volume"));` as a control that must fail with a
  `QStringList` / `no matching` diagnostic.
- `tests/CMakeLists.txt:6566-6573` registers it as CTest `CiD3e29f18MixerFadersPortability` on Linux with
  Python. This build tree already knows the test: `ctest -N -R CiD3e29f18` lists
  `Test #834: CiD3e29f18MixerFadersPortability`.
- `tests/fork-sources.txt:4083-4085` lists the checker and this report; the file ends with a newline.

The portable list in `MixerSendsOnFadersTest.cpp` is left unchanged and `Train51Portability` is untouched.

## Test matrix

- Normal path: the extracted block compiles against Qt 5 (`faders-qt5-fixed`) and Qt 6 (`faders-qt6-fixed`).
- Failure control: the historical spelling must fail with the Qt 5 diagnostic (`faders-qt5-original`), so a probe
  that cannot reproduce the defect cannot pass silently.
- Reconfiguration path: the CTest is registered only on Linux with Python, and the Qt 6 leg is skipped cleanly
  (`OMITTED`) when Qt6Core is absent. Configuring this tree is the parent's job — the build guard
  (`bunny-guard: configuring is the parent's job`) refused `cmake .` with EXIT=97, and the already-configured tree
  lists test #834, so no reconfigure was needed.
- Exhaustion/limits: the expected list is a loop over `channels`; mixer channels are an unbounded vector, so no
  fixed limit is involved and none is added.
- Audio-thread constraints: test and CI only; nothing runs in process/render code.
- Supported-platform build: the probe compiles the shipped block with local g++ against Qt 5.15 and Qt 6.12.
  MSVC and macOS were not available here; the Qt 5 leg reproduces the hosted macOS failure.

## Commands and EXIT codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/ci-d3e29f18fa53a626-port.r4/logs`.

| Step | Command | Log | Exit |
|---|---|---|---|
| Reconfigure (refused by guard) | `cd build && cmake .` | `configure-1.log` | 97 |
| Build `zene` + `MixerSendsOnFadersTest` | `cmake --build build --target zene MixerSendsOnFadersTest -j4` | `build-1.log` | 0 |
| GREEN, checker as committed | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root .` | `test-1-portability.log` (CTest) | 0 |
| GREEN, CTest #834 | `cd build/tests && ctest -R CiD3e29f18MixerFadersPortability --output-on-failure` | `test-1-portability.log` | 0 (1/1) |
| Affected Qt test | `cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` | `test-2-mixersends.log` | 0 (1/1) |
| Build the reverse patch | `git diff 421f6f13ae^ 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp > .../red-reverse.patch` | — | 0 |
| Revert the fix hunk | `git apply -R .../red-reverse.patch` | — | 0 |
| RED, checker | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root .` | `test-3-red-reverted.log` | 1 |
| RED, CTest #834 | `cd build/tests && ctest -R CiD3e29f18MixerFadersPortability --output-on-failure` | `test-4-red-ctest.log` | 8 |
| Restore the fix hunk | `git apply .../red-reverse.patch` | — | 0 |
| GREEN, checker restored | `python3 tests/ci-d3e29f18-mixer-faders-portability-check.py --source-root .` | `test-5-green-restored.log` | 0 |
| Rebuild after restore | `cmake --build build --target zene MixerSendsOnFadersTest -j4` | `build-2-restored.log` | 0 |
| Family, after restore | `cd build/tests && ctest -R 'CiD3e29f18MixerFadersPortability\|^MixerSendsOnFadersTest$\|^Train51Portability$' --output-on-failure` | `test-6-green-family.log`, `test-7-green-family-final.log` | 0 (3/3) |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 (`RESULT: PASS`) |

The RED run's log contains the exact failure:

```
mixer-faders portability check: faders-qt5-fixed: expected compile success; exit 1:
error: no matching function for call to 'QStringList::QStringList(int&, QString)'
    6 |                 (void)(faders == QStringList(channels, QStringLiteral("volume")));
```

and CTest #834 in the same state: `1/1 Test #834 ... ***Failed`, `The following tests FAILED: 834`.

The GREEN runs print `PASS faders-qt5-fixed: compiled`, `PASS faders-qt6-fixed: compiled`,
`PASS faders-qt5-original: failed as expected`, and the family run is 3/3
(`MixerSendsOnFadersTest`, `Train51Portability`, `CiD3e29f18MixerFadersPortability`).

No `src/`, `include/` or plugin file changed on this branch since the base, so the procedure's
`git diff -- <product files>` step yields an empty patch; as the diagnostic checkpoint directs, the
reverse/restore experiment reverses the ancestor `421f6f13ae` hunk in the test source instead.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** Reverted (`git apply -R`, EXIT=0):
  checker EXIT=1 with `no matching function for call to 'QStringList::QStringList(int&, QString)'`
  (`test-3-red-reverted.log`) and CTest #834 EXIT=8 with the same diagnostic (`test-4-red-ctest.log`).
  Restored (`git apply`, EXIT=0): checker EXIT=0 (`test-5-green-restored.log`), CTest family EXIT=0, 3/3
  (`test-7-green-family-final.log`). `git status --porcelain` was empty after the restore.
- **Deterministic.** The checker reads the shipped source text and asks the compiler about it; no wall-clock time,
  sleep, network or shared mutable state. The only injected value is the probe's `channels` parameter, and the
  compile result is a pure function of source plus toolchain.
- **Cause and completeness.** The cause is the Qt 6-only fill constructor in the test source. A content search
  (`grep 'QStringList(channels'` over `tests/ src/ include/`) finds no live two-argument construction — only this
  report, other cluster reports, and the checker's deliberate failing control. The sibling checks of the same
  area stay consistent: `Train51Portability` and `MixerSendsOnFadersTest` both pass in the family run, and the
  assertion still requires exactly one `"volume"` per channel.
- **No collateral damage.** No product, save/load, undo, command or audio code changed; old projects load as
  before because nothing they read changed. No control command was added, so `ControlCommandsSnapshot` is
  untouched. The checker adds no limit that other callers could exceed.
- **ALREADY-FIXED reference.** `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282` (portable loop) against
  `fa4a5f2e785c73e32216931e33cdbe140c06647d:tests/src/gui/MixerSendsOnFadersTest.cpp:277` (historical line), with
  the ancestor commit `421f6f13ae`; all three read above with EXIT=0. The regression test that pins it is
  `tests/ci-d3e29f18-mixer-faders-portability-check.py`, CTest `CiD3e29f18MixerFadersPortability`.
- **Qt 5.15 / MSVC / macOS portability.** The probe compiles the same shipped block with Qt 5.15.19 and Qt 6.12.0
  here; the change removes a Qt-6-only expression, and the Qt 5 leg reproduces the hosted macOS diagnostic.
  MSVC was not exercised locally (no MSVC on this box) — the check is a Qt-version check, not a compiler feature.
- **Sibling commands / registration.** The submit check lists no sibling command group for this diff: no command
  file changed. `fork-sources` PASS, `all-sources-reproduce` PASS, `no-upstream-regression` PASS,
  `check-namespace` PASS, `no-tautology` PASS, `doc-figures` PASS, complexity and file-length report no new item
  in the changed files (`gates-1.log`, EXIT=0).

## Other findings

- `cmake .` in the build tree is refused by the build guard (EXIT=97, "configuring is the parent's job"). The
  configured tree already lists test #834, so the registration reaches CTest; a lane cannot force a reconfigure.
- `tests/Qt5QStringListSizeConstructorCheck.py` from another cluster's port also mentions the historical
  expression. It is untouched here and not part of this cluster; no test was weakened or duplicated.
