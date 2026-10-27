FALSE-POSITIVE

new-p23-10: faderRoles() indexing the role-name list is not reachable with an out-of-range index. No product change; one guard case added to the existing sends-on-faders test.

Finding as reported: `src/core/ControlCommandsMixerView.cpp:68`, `names.at(...)` "throws if faderRole returns a value outside 0..2". Two details are wrong on this branch:

- The file is `src/gui/ControlCommandsMixerView.cpp`, not `src/core/`. The `names.at(...)` call is now at line 70 (`src/gui/ControlCommandsMixerView.cpp:70`).
- `QList::at` does not throw. It is `noexcept` and only asserts on an out-of-range index (`/usr/include/qt6/QtCore/qlist.h:512-516`, `logs/002-qt-at.log`). The failure mode described is wrong as well as the reachability.

## Evidence

- **The only producer of `FaderRole` values is `roleOf()`, and it returns one of three enumerators.** `src/gui/MixerOverlays.cpp:74-79`: `Volume` (line 76), and `Send` or `NoSend` (line 78). The enum has exactly three values, `Volume`, `Send`, `NoSend` (`include/MixerOverlays.h:53-57`).
- **`faderRole(int)` is a pass-through to `roleOf`.** `src/gui/MixerOverlays.cpp:149-151`: `return roleOf(index, sendsOnFaders());`.
- **No int-to-`FaderRole` cast exists anywhere.** `rg` for `static_cast<FaderRole>` and `(FaderRole)` in `src`, `include` and `tests` returns nothing (`logs/001-source-audit.log`). Every `FaderRole` in the tree is an enumerator name.
- **The index into `names` is always 0..2.** `src/gui/ControlCommandsMixerView.cpp:65` declares a three-entry `QStringList names{"volume","send","no_send"}`. The `.at()` is at line 70 and is fed only by `faderRole(i)`, which is `Volume`/`Send`/`NoSend` (0..2) in the loop bounded by `i < Engine::mixer()->numChannels()` (line 67). The second loop at line 80 does not use `faderRole`.
- **Other callers see the same three values.** `faderRole()` is also called by the test cases at `tests/src/gui/MixerSendsOnFadersTest.cpp:135` and `:165`, which compare it with `FaderRole::NoSend`.

Line citations are from `logs/004-line-citations.log`.

## Why there is no RED and no fix

The finding has no failing state to reproduce. The index can only be 0, 1 or 2, so a bounds check or a `.value()` fallback would be dead code. Under procedure step 2 this is a not-real verdict, so no product file was changed. `git diff --stat -- src include` is empty (`logs/005-product-diff.log`). Step 4b (revert-and-reapply of the product patch) therefore has nothing to revert, and no RED run exists to report.

The brief asks for a test in the closest area. I added one guard case, `theFaderAnswerNamesWhatEveryFaderDrives`, in `tests/src/gui/MixerSendsOnFadersTest.cpp`. It drives the real `window.mixer_view` command path that calls `faderRoles()`. It checks that:

- every `drives` entry is one of `volume`, `send`, `no_send`, with one entry per channel;
- with a send-mode return set, the sender drives `send`, the silent channel drives `no_send`, and the return and master drive `volume`;
- with `sends_on_faders` reset to empty, every entry drives `volume`.

**This is a guard, not a regression proof.** It passes on the unchanged product code (`logs/023-ctest-guard-verbose.log`), which is the expected result when there is no reachable bug. It pins the invariant so that a future change which adds a fourth role, or changes the role list, fails here instead of throwing in production.

## Commands and results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-10.r1/logs` (referred to as `logs/` below).

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Source audit | `rg -n 'faderRole\|roleOf\|FaderRole\|names\.at\|QStringList names' src/gui/ControlCommandsMixerView.cpp src/gui/MixerOverlays.cpp include/MixerOverlays.h src/gui/MixerChannelView.cpp > logs/001-source-audit.log 2>&1; echo EXIT=$? >> logs/001-source-audit.log` | 0 | Callers and definitions listed |
| Qt `QList::at` behaviour | `rg -n -A4 'const_reference at\(qsizetype i\) const' /usr/include/qt6/QtCore/qlist.h > logs/002-qt-at.log` | 0 | `noexcept`, `Q_ASSERT_X` only (line 512-516) |
| Feature gate script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/003-feature-check-1.log 2>&1` | 1 | `PermissionError` creating `<worktree>-feature-check` (see below) |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` (run from worktree) | 0 | `logs/30-gate-fork-sources.log` |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `logs/30-gate-all-sources-reproduce.log` |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `logs/30-gate-no-upstream-regression.log` |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 | `logs/30-gate-check-namespace.log` |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `logs/30-gate-no-tautology.log` (covers the new case) |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `logs/30-gate-doc-figures.log` |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check` | 1 | 7 `REGRESSION` lines, none in a changed file (see below) |
| Ratchet: file-length | `bash tests/file-length-gate.sh --check` | 1 | 22 `REGRESSION` lines, none in a changed file (see below) |
| Changed-file set | `git diff --name-only 7641e9493d7fb70d0ac063022f3df9117869a99b` | 0 | only `tests/src/gui/MixerSendsOnFadersTest.cpp` (`logs/031-changed-files.log`) |
| Product diff | `git diff --stat -- src include` | 0 | empty (`logs/005-product-diff.log`) |
| Build (first attempt) | `cmake --build build --target MixerSendsOnFadersTest -j4 > logs/020-build-guard.log 2>&1` | 2 | ccache could not write its temp file outside the write-jail |
| Build (retry) | `CCACHE_TEMPDIR=$TMPDIR/ccache-new-p23-10 cmake --build build --target MixerSendsOnFadersTest -j4 > logs/021-build-guard-retry.log 2>&1` | 0 | Built |
| ctest family (from `build/tests`) | `ctest -R MixerSendsOnFadersTest --output-on-failure > logs/022-ctest-guard.log 2>&1` | 0 | 1/1 passed |
| ctest verbose | `ctest -R MixerSendsOnFadersTest -V > logs/023-ctest-guard-verbose.log 2>&1` | 0 | 9 cases passed, 0 failed, including `theFaderAnswerNamesWhatEveryFaderDrives` |

Test count in the family: 9 test cases in one binary (`MixerSendsOnFadersTest`, ctest #416). Zero tests were not run.

### Gate notes

`feature_check.py` could not run inside the write-jail. It creates `<worktree>-feature-check` next to the worktree (`feature_check.py:69-71`), and that directory is not writable by this lane (`PermissionError`, `logs/003-feature-check-1.log`). I did not edit the gate. I ran the same commands it runs (`feature_check.py:22-29`) from the worktree, and logged them in the table above.

Ratchet classification follows `feature_check.py:83-86`: a ratchet fails only on a `REGRESSION` line whose item lies in a changed file. The changed-file set is only `tests/src/gui/MixerSendsOnFadersTest.cpp`. None of the 7 complexity and 22 file-length `REGRESSION` lines names that file or any product file this lane touched (`logs/30-gate-complexity.log`, `logs/30-gate-file-length.log`; `grep MixerSendsOnFaders` on both returned nothing). Those lines are the train's existing red items. Under the gate's rule both ratchets classify as PASS, but their own script exit is 1, and I report it as 1.

## Self-check

Results of `submit_check.py`: see the final run recorded below.

1. **Regression test fails with the product change reverted and passes with it.** Not applicable: there is no product change to revert (`logs/005-product-diff.log`, empty). The added case passes on the unchanged product code (`logs/023-ctest-guard-verbose.log`). It is a guard for an unreachable state, so no RED run is possible, and I do not claim it is regression proof.
2. **Deterministic.** The case has no timers, sleeps or races. It reads `window.mixer_view`'s answer, which `mixerViewState()` builds synchronously (`src/gui/ControlCommandsMixerView.cpp`, `mixerViewState`). The channels it uses (`m_sender`, `m_silent`, `m_return`) are created in `initTestCase`. The deferred `syncMixerViewSoon()` is a GUI refresh, and no assertion depends on it.
3. **Fix at the cause, every caller and sibling checked.** No fix. The callers and producers are listed in the Evidence section: the one production caller (`src/gui/ControlCommandsMixerView.cpp:70`), the one producer (`roleOf`, `src/gui/MixerOverlays.cpp:74-79`), and the three-value enum (`include/MixerOverlays.h:53-57`). No int-to-enum cast exists.
4. **No collateral damage.** No product file changed, so no saved project, load path or limit is affected. The test change only adds one helper and one case to an existing test file. It adds no new file, so no registration is needed.
5. **Verdict evidence.** FALSE-POSITIVE: `src/gui/MixerOverlays.cpp:74-79` (only producer, returns 0..2 by construction), `include/MixerOverlays.h:53-57` (three enumerators), `src/gui/ControlCommandsMixerView.cpp:65` (three-entry name list), `:67` and `:70` (loop bound and index). The finding's path and failure mode are also wrong, as stated above.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p23-10.md` and `tests/src/gui/MixerSendsOnFadersTest.cpp`.
