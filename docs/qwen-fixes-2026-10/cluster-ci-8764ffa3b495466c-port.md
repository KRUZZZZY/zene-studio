ALREADY-FIXED

# cluster-ci-8764ffa3b495466c-port — Port: CI regression: build / windows-arm64

ci-8764ffa3b495466c-port: ALREADY-FIXED

## What the original cluster was

The original repair for cluster ci-8764ffa3b495466c is commit `69b1830a4b`, mirrored by the CI
repair commit `f22042415b` (`CI-Repair-Id: ci-repair-ci-8764ffa3b495466c-69b1830a4bdd`). The
hosted Qt 5.15 jobs (windows-arm64 among them) failed to compile
`tests/src/gui/MixerSendsOnFadersTest.cpp:278`:

    QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));

`QStringList(size, value)` is Qt 6's inherited `QList(qsizetype, const T&)` constructor. Qt 5.15's
`QStringList` lacks it. Neither `69b1830a4b` nor `f22042415b` is an ancestor of this branch.

## Evidence that this branch already has the fix

- `git merge-base --is-ancestor 421f6f13ae HEAD` reports an ancestor. Commit `421f6f13ae`
  ("make the mixer faders answer compile on Qt 5.15 (cluster ci-ec26fd9bddef0dd4)") made the
  same change to the same line.
- `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282` on this branch reads:
  `QStringList expected;`, then
  `for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`, then
  `QVERIFY(faders == expected);`.
- A search for `QStringList(channels` under `tests/` finds 0 matches.

No product code and no `MixerSendsOnFadersTest.cpp` code changed in this job. The branch's loop
that uses `i` is kept. The `MixerSendsOnFadersTest.cpp` hunk of `f22042415b` was not taken.

## Round 3: the ported regression check

The test parts of `f22042415b` were brought over:
- `tests/Qt5QStringListSizeConstructorCheck.py`
- its registration in `tests/CMakeLists.txt`: ctest `Qt5QStringListSizeConstructorCheck`, in a
  Linux + `PYTHON3_EXECUTABLE` block after `Qt5QStringListAssignmentCheck`
- the `tests/fork-sources.txt` entries (the script and this report)
- the `tests/upstream-modifications.txt` row for `tests/CMakeLists.txt`

Parser check: `derived_statement()` finds the single `QVERIFY(faders ==` line, then walks up at
most 5 lines to the `QStringList <name>;` declaration. It does not depend on the loop variable's
name. Run against the branch source, it returned exactly lines 280-282 (the `i` loop included).

One defect in the script as ported, now fixed. In `qt5_control_verdict()` and `self_test()`, the
Qt 6-only control `QT5_ONLY_FIXTURE` is a complete translation unit (`#include`s plus a
`probe()` definition), but it went through `compile_statement()`, which wraps its argument in
another function body. The control therefore failed with
`error: a function-definition is not allowed here before '{' token`, not because the
`QStringList(size, value)` constructor is missing. That made the "this Qt 5 tree has the trap"
control vacuous. The fix:
- compile the control as-is with `compile_fixture()`
- in `qt5_control_verdict()`, require the diagnostics to name `QStringList` and not to contain
  `function-definition`

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/ci-8764ffa3b495466c-port.r3/logs)

| step | command | log | EXIT |
|---|---|---|---|
| self-test | `python3 tests/Qt5QStringListSizeConstructorCheck.py --self-test` | selftest-1.log ("self-test: Qt 5 rejects the size constructor and accepts the append-built expected list") | 0 |
| configure | `cmake build` (refused by the lane guard: "configuring is the parent's job") | configure-1.log | 97 |
| build (regenerates) | `cmake --build build --target zene -j4` | build-1.log | 0 |
| green 1 | `cd build/tests && ctest -R '^Qt5QStringListSizeConstructorCheck$' --output-on-failure` | test-1-green.log ("1/1 Test #834: Qt5QStringListSizeConstructorCheck ... Passed") | 0 |
| red | lines 280-282 temporarily replaced with `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`, then the same ctest | test-2-red.log ("error: no matching function for call to 'QStringList::QStringList(int&, QString)'", "0% tests passed, 1 tests failed out of 1") | 8 |
| restore | `cp` of the byte backup, then `cmp` against it | — | CMP=0; `git status --porcelain` showed only the 4 intended paths |
| green 2 | same ctest | test-3-green.log ("Passed") | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | gates-1.log (RESULT: PASS, all 8 rows PASS) | 0 |
| completion | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-8764ffa3b495466c-port` | submit-1.log (RESULT: PASS - submit; every "?" item is answered below) | 0 |

## Self-check

- Fails with the fix reverted and passes with it: yes. With the Qt 6-only line in place of
  `MixerSendsOnFadersTest.cpp:280-282`, `Qt5QStringListSizeConstructorCheck` exits 8. With the
  branch source it exits 0 (run twice: green 1 and green 2).
- Deterministic: yes. The check is a pure compile probe. It runs `c++ -fsyntax-only` on a fixture
  derived from the test source, with no timing, sleeps or threads. The Qt 5 control proves the
  tree has the trap, and the check reports itself skipped (never passed) on a tree without Qt 5.
- At the cause and complete: the only use of the Qt 6-only `QStringList(count, value)` shape was
  the old line 278. It was replaced by `421f6f13ae`, and `QStringList(channels` now has 0 matches
  in `tests/`. This job changed no product code.
- No collateral damage: no product code, file format or limit changed. The additions are a
  Linux-only test, its CMake registration and list entries.
- ALREADY-FIXED evidence: `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282`, from `421f6f13ae`.
  It is guarded by `Qt5QStringListSizeConstructorCheck` (new) and `Train51Portability`
  `mixer_faders_probe` (existing).
- Tests and sibling commands that submit_check lists from the branch diff against its base
  (`control-*.py`, `app.*`, `arrangement.*`): these come from merge-train commits already on this
  branch, not from this job. This job's commits touch only the four test/registration files
  above and this report.

Delegation: Haiku (two starts) did the registration edits in `tests/CMakeLists.txt`,
`tests/fork-sources.txt` and `tests/upstream-modifications.txt`, then ran the build and the
green, red, restore and green ctest runs. I reviewed the diff and the log tails. In the first
start Haiku wrongly reported this report as missing; `ls` showed it exists. I diagnosed and fixed
the control-fixture defect in the script myself, a 4-line change. Sonnet and DeepSeek were not
needed.

## Other findings

- `tests/Qt5QStringListSizeConstructorCheck.py` as committed in `f22042415b` (on another branch)
  has the vacuous Qt 5 control described above. Any branch carrying that commit has the same weak
  control.
