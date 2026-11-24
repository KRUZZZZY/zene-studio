FIXED

# cluster ci-b71ed146fd535ea5-port - port the accepted Qt 5 QStringList count+value check

ci-b71ed146fd535ea5-port: FIXED

## Verdict

The hosted `build / linux-x86_64` finding is REAL at the pinned base
`fa4a5f2e785c73e32216931e33cdbe140c06647d`. Its product-side line was already
replaced on this branch by `421f6f13ae` (an ancestor of `HEAD`), but the
accepted stand-alone regression check was missing. This round ports it exactly
as the diagnostic checkpoint required: `tests/Qt5QStringListSizeCheck.py` from
`4b0b200ba6`, its ctest registration, the fork/upstream ledger entries, and
this report. The check is proved RED on the historical constructor and GREEN
on the append loop below.

## Evidence

- Pinned base has the defect: `git show fa4a5f2e:...MixerSendsOnFadersTest.cpp`
  prints `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));`
  (`24-code-evidence.log`).
- Qt 5.15.19's `QStringList` declares only default, single-`QString`, `QList`,
  initializer-list and iterator-pair constructors; there is no
  `QStringList(count, value)` (`/usr/include/qt/QtCore/qstringlist.h:111-123`,
  `24-code-evidence.log`).
- The branch's fix is present and is an ancestor of `HEAD`: the same file now
  builds the expected list with an append loop at
  `tests/src/gui/MixerSendsOnFadersTest.cpp:277-282`, keeping the exact-list
  assertion (`git merge-base --is-ancestor 421f6f13ae HEAD` exits 0,
  `24-code-evidence.log`).
- The checker scans first-party C/C++ for a non-iterator two-argument
  `QStringList(...)` (`tests/Qt5QStringListSizeCheck.py:75,196`) and, where
  pkg-config finds Qt 5, recompiles the real translation unit from
  `build/compile_commands.json` against the Qt 5 headers
  (`tests/Qt5QStringListSizeCheck.py:309-360`). Its own bad/good fixtures prove
  both directions (`tests/Qt5QStringListSizeCheck.py:203-261`).

## Changes

- `tests/Qt5QStringListSizeCheck.py` - new file, byte-exact from `4b0b200ba6`
  (sha256 `02b14d9de9380b7b0743b1784c542bacf36a4b24454a20a60f90d5e2d596d688`).
- `tests/CMakeLists.txt:9744-9756` - registers ctest `Qt5QStringListSizeCheck`.
- `tests/fork-sources.txt:4076-4084` - registers the checker and this report.
- `tests/upstream-modifications.txt:6141` - records the `tests/CMakeLists.txt`
  registration.
- `docs/qwen-fixes-2026-10/cluster-ci-b71ed146fd535ea5-port.md` - this report.

## Test matrix

- Normal path: current append-loop source, direct checker exit 0 and ctest
  exit 0 (`01`, `05`, `10`, `11`).
- Failure path: historical `QStringList(channels, ...)` restored, source scan
  finds it and the Qt 5 compile probes rejects it; direct exit 1 and ctest
  exit 8 (`07`, `08`, `17`, `18`).
- Exhaustion/reconfiguration: not applicable; the check is a static source
  scan plus a `-fsyntax-only` compile and holds no runtime state.
- Audio thread: no runtime code is added; the checker is Python and the
  compile is syntax-only, so nothing runs on the audio thread.
- Platforms: local Qt 5.15.19 and Qt 6.12.0 are both installed; the Qt 5 probe
  confirms the hosted compiler input, and the file uses no Qt-6-only API.

## Commands and EXIT codes

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-b71ed146fd535ea5-port.r3/logs`.

| Step | Command | EXIT | Log |
|---|---|---|---|
| Direct green baseline | `python3 tests/Qt5QStringListSizeCheck.py --root . --build-dir build` | 0 | `01-check-green-direct.log` |
| Registration visible | `cd build/tests && ctest -N -R '^Qt5QStringListSizeCheck$'` | 0 (1 test) | `04-ctest-list.log` |
| ctest green baseline | `cd build/tests && ctest -R '^Qt5QStringListSizeCheck$' --output-on-failure` | 0 | `05-ctest-green-baseline.log` |
| Build with historical line (Qt 6 accepts) | `cmake --build build --target MixerSendsOnFadersTest -j4` | 0 | `06-build-red.log` |
| Direct RED | `python3 tests/Qt5QStringListSizeCheck.py --root . --build-dir build` | 1 | `07-check-red-direct.log` |
| ctest RED | `cd build/tests && ctest -R '^Qt5QStringListSizeCheck$' --output-on-failure` | 8 | `08-ctest-red.log` |
| Build restored | `cmake --build build --target MixerSendsOnFadersTest -j4` | 0 | `09-build-green-restored.log` |
| Direct GREEN restored | `python3 tests/Qt5QStringListSizeCheck.py --root . --build-dir build` | 0 | `10-check-green-restored.log` |
| ctest GREEN restored | `cd build/tests && ctest -R '^Qt5QStringListSizeCheck$' --output-on-failure` | 0 | `11-ctest-green-restored.log` |
| Affected product test | `cd build/tests && ctest -R '^MixerSendsOnFadersTest$' --output-on-failure` | 0 | `12-ctest-mixer-sends.log` |
| Portability family | `ctest -R '^(MixerSendsOnFadersTest\\|Qt5QStringListSizeCheck\\|Qt5QStringListSizeConstructorCheck\\|Qt5QListApiCheck\\|Qt5QStringListAssignmentCheck\\|Qt5QStringListBraceCheck\\|Qt5QStringListAssignCheck)$'` | 0 (7/7) | `13-ctest-family.log` |
| Generate step-4b patch | `git diff 421f6f13ae^ 421f6f13ae -- tests/src/gui/MixerSendsOnFadersTest.cpp > <LOGS>/fix.patch` | 0 | `fix.patch` |
| Reverse-patch applies | `git apply -R --check <LOGS>/fix.patch` | 0 | `14-reverse-check.log` |
| Reverse apply | `git apply -R <LOGS>/fix.patch` | 0 | `15-reverse-apply.log` |
| Build without fix | `cmake --build build --target MixerSendsOnFadersTest -j4` | 0 | `16-build-red-via-patch.log` |
| Direct RED via patch | `python3 tests/Qt5QStringListSizeCheck.py --root . --build-dir build` | 1 | `17-check-red-via-patch.log` |
| ctest RED via patch | `cd build/tests && ctest -R '^Qt5QStringListSizeCheck$' --output-on-failure` | 8 | `18-ctest-red-via-patch.log` |
| Restore fix | `git apply <LOGS>/fix.patch` | 0 | `19-forward-apply.log` |
| Build with fix | `cmake --build build --target MixerSendsOnFadersTest -j4` | 0 | `20-build-green-via-patch.log` |
| Direct GREEN via patch | `python3 tests/Qt5QStringListSizeCheck.py --root . --build-dir build` | 0 | `21-check-green-via-patch.log` |
| ctest GREEN via patch | `cd build/tests && ctest -R '^Qt5QStringListSizeCheck$' --output-on-failure` | 0 | `22-ctest-green-via-patch.log` |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (`RESULT: PASS`) | `23-feature-check.log` |
| Final gate table (with report) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (`RESULT: PASS`) | `30-feature-check-final.log` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> ci-b71ed146fd535ea5-port` | 0 (`RESULT: PASS - submit`) | `31-submit-check-final.log` |

The RED output is the hosted failure verbatim:
`tests/src/gui/MixerSendsOnFadersTest.cpp:278:81: error: no matching function
for call to 'QStringList::QStringList(const int&, QString)'` with
`1 finding(s), 1 confirmed by a Qt 5 compile` (`17-check-red-via-patch.log`).
Qt 6 still builds the historical line, which is why the Qt 6 `ctest` family
could pass before this checker existed.

## Self-check

- Regression test fails with the product change reverted and passes with it:
  yes. `git apply -R fix.patch` (exit 0) then direct checker exit 1 and ctest
  exit 8; `git apply fix.patch` (exit 0) then direct checker exit 0 and ctest
  exit 0 (`15`, `17`, `18`, `19`, `21`, `22`).
- Deterministic: yes. The check reads checked-in sources and the recorded
  compile command, retargets the Qt 6 include directories at the pkg-config
  Qt 5 tree, generates the Qt 5 moc in a temporary directory, and compiles
  `-fsyntax-only`. No clock, sleep, thread or race is involved. A host without
  Qt 5 reports `probe: skipped`, never a pass.
- At the cause and complete: the expected list is built with `append()` on
  both Qt generations, the exact-list assertion is unchanged, and the scan
  finds no other first-party non-iterator two-argument `QStringList(...)`.
- No collateral damage: only the test source's expected-list construction is
  involved. No product source, audio path, undo state, command, project file
  or saved-data format changes; no limit or check is added for other callers.
  `MixerSendsOnFadersTest` and the 7-test portability family pass.
- ALREADY-FIXED/FALSE-POSITIVE evidence: not the verdict here. The pinned base
  source and the Qt 5 constructor list above show the defect was real; the
  branch-side product fix is `421f6f13ae`, and this job adds the accepted
  regression guard that was missing.
- Realtime: nothing added runs on the audio thread; the only new executable
  code is a Python source scan and a syntax-only compiler probe.
- Undo/commands: no command, undo step, reversibility class or snapshot
  canary is touched.
- Sibling commands (`app.*`, `arrangement.*` and the rest listed by
  `submit_check`): they belong to merge-train commits already between the base
  `fa4a5f2e` and this branch's `HEAD`, not to this job. This job's only commit
  `c7e66c4af` changes `tests/Qt5QStringListSizeCheck.py`,
  `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
  `tests/upstream-modifications.txt` and this report (`git show --stat HEAD` in
  `28-sibling-evidence.log`). It registers no command, handler, reversibility
  class or snapshot entry, so those commands are unaffected.

## Other findings

- The branch already carried the equivalent
  `tests/Qt5QStringListSizeConstructorCheck.py` from cluster
  `ci-8764ffa3b495466c-port`. This port adds the accepted stand-alone
  `Qt5QStringListSizeCheck` from `4b0b200ba6` as the diagnostic checkpoint
  required; the two checkers are independent and both pass.
- The lane guard refuses `cmake -S . -B build` (exit 97: configuring is the
  parent's job). The allowed `cmake --build build --target
  MixerSendsOnFadersTest` regenerated the cache and registered the new test,
  as `04-ctest-list.log` shows.
