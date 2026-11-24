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

Delegation: Haiku ran the build, the ctest green, red-control and restore runs, and feature_check, and reported their exit codes. I spot-checked the log tails. Sonnet and DeepSeek were not used: no code needed changing, so there was nothing to engineer.

## Other findings

None.
