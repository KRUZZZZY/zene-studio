ALREADY-FIXED

# cluster-ci-8764ffa3b495466c-port — Port: CI regression: build / windows-arm64

ci-8764ffa3b495466c-port: ALREADY-FIXED

## What the original cluster was

The original repair for cluster ci-8764ffa3b495466c is commit `69b1830a4b`
("use a Qt 5/6 portable expected QStringList in MixerSendsOnFadersTest"), mirrored by the CI
repair commit `f22042415b` (`CI-Repair-Id: ci-repair-ci-8764ffa3b495466c-69b1830a4bdd`). The
hosted Qt 5.15 jobs (windows-arm64 among them) failed to compile
`tests/src/gui/MixerSendsOnFadersTest.cpp:278`:

    QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));

`QStringList(size, value)` is Qt 6's inherited `QList(qsizetype, const T&)` constructor. Qt 5.15's
`QStringList` lacks it. The fix builds the expected list with an append loop.

Neither `69b1830a4b` nor `f22042415b` is an ancestor of this branch (`git merge-base --is-ancestor`
printed "NOT ancestor" for both), so this port job asked whether the fix is needed here.

## Evidence that this branch already has the fix

- `git merge-base --is-ancestor 421f6f13ae HEAD` printed `ANCESTOR`. Commit `421f6f13ae`
  ("make the mixer faders answer compile on Qt 5.15 (cluster ci-ec26fd9bddef0dd4)") made the
  same change to the same line.
- `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282` on this branch now reads:
  `QStringList expected;`, then
  `for (int i = 0; i < channels; ++i) { expected.append(QStringLiteral("volume")); }`, then
  `QVERIFY(faders == expected);`.
- A search for `QStringList(channels` under `tests/` finds 0 matches. The Qt 6-only spelling is
  gone from this branch.
- Regression guard: `tests/train-51-portability-check.py` `mixer_faders_probe` (added by `421f6f13ae`)
  extracts the faders assertion block from the test source and compiles it against Qt5Core, and
  against Qt6Core when that is installed. It is registered as ctest `Train51Portability`
  (`tests/CMakeLists.txt:6535-6540`).

The original port would also have added `tests/Qt5QStringListSizeConstructorCheck.py`. This branch
does not need it: `Train51Portability` already guards the same line against the same Qt 5 failure
(see the red control below), and a second checker would duplicate it. No product or test code was
changed in this job.

## Commands and EXIT codes (logs in ~/.config/kads/zene/state/runs/ci-8764ffa3b495466c-port.r2/logs)

| step | command | EXIT |
|---|---|---|
| build | `cmake --build build --target zene MixerSendsOnFadersTest -j4 > logs/build-1.log 2>&1` | 0 |
| green | `cd build/tests && ctest -R '^(Train51Portability\|MixerSendsOnFadersTest)$' --output-on-failure > logs/test-1.log 2>&1` | 0 ("100% tests passed out of 2") |
| red control | temporarily put back the original line `QVERIFY(faders == QStringList(channels, QStringLiteral("volume")));` in place of lines 278-282, then `cd build/tests && ctest -R '^Train51Portability$' --output-on-failure > logs/test-2-red.log 2>&1` | 8 (`mixer-faders-qt5`: "no matching function for call to 'QStringList::QStringList(const int&, QString)'"; "0% tests passed, 1 tests failed out of 1") |
| restore + green | file restored from a byte copy (`git status --porcelain` empty), then `ctest -R '^Train51Portability$' --output-on-failure > logs/test-3-green.log 2>&1` | 0 ("100% tests passed out of 1") |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 0 (RESULT: PASS, all 8 rows PASS) |

The red control shows that the existing guard reproduces the exact hosted compiler failure from
the original cluster. With the branch's current source, it passes.

## Self-check

- Verdict backed by evidence: yes. See the ancestry check, the file lines, and the red and green runs above.
- New regression test: none. The fix and its regression guard (`Train51Portability` /
  `mixer_faders_probe`) were already on the branch. The red control above shows the guard fails
  without the fix (EXIT 8) and passes with it (EXIT 0).
- Product code changed: none. The only change is this report.
- Realtime, undo, control commands: not touched.
- `git status` clean after the work: yes, apart from this report, which is committed.

Answers to the submit_check "?" items (`submit_check.py` run 1: EXIT=0, "RESULT: PASS - submit"):

- Fails with the fix reverted and passes with it: yes. Reverting `MixerSendsOnFadersTest.cpp:278-282`
  to the original Qt 6-only line makes `ctest -R '^Train51Portability$'` exit 8 (`test-2-red.log`).
  With the branch source it exits 0 (`test-1.log`, `test-3-green.log`).
- Deterministic: yes. `mixer_faders_probe` is a pure compile check. It runs the C++ compiler on a
  fixture derived from the test source, with `channels = 3` hard-coded. It uses no timing, sleeps
  or threads.
- At the cause and complete: the only use of the Qt 6-only `QStringList(count, value)` shape was
  `MixerSendsOnFadersTest.cpp:278`. A search for `QStringList(channels` in `tests/` now returns 0
  matches. This job changed no code, so it has no callers or siblings to keep consistent.
- No collateral damage: this job changes no product code, file format or limit. Only this report
  was added.
- ALREADY-FIXED evidence: `tests/src/gui/MixerSendsOnFadersTest.cpp:280-282` (append loop), from
  commit `421f6f13ae` (an ancestor of HEAD). The guard is at `tests/train-51-portability-check.py`
  (`mixer_faders_probe`), registered at `tests/CMakeLists.txt:6535-6540`.
- "Tests added or changed" (`control-audio-test-tone.py` etc.) and the "sibling commands" list
  (`app.*`, `arrangement.*`): these come from the merge-train commits already on this branch
  relative to the check's base `fa4a5f2e7`, not from this job. This job's only commit adds this
  report (`git show --stat HEAD`), so none of those commands or tests are affected by it.

Delegation: Haiku ran the build, the ctest green, red-control and restore runs, and feature_check, and reported their exit codes. I spot-checked the log tails. Sonnet and DeepSeek were not used: no code needed changing, so there was nothing to engineer.

## Other findings

None.
