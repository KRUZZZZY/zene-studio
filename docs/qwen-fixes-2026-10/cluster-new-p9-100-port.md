# Cluster new-p9-100-port: DrumSynth envelope point overflow

**Verdict: FIXED** (a port of `4f2593bfb`, which was accepted on `bugs/qwen-new-p9-100`). The
defect is still present on the newest line, so this is not ALREADY-FIXED.

## Defect and evidence

- `envpts` is `float envpts[8][3][32]` (`src/core/DrumSynth.cpp:56`): 32 slots per row, and the
  sentinel needs one of them.
- Before the fix, `GetEnv` advanced `ep` on every space with no capacity check
  (`src/core/DrumSynth.cpp` pre-fix line 159). With 32 points, `ep` reaches 31 and the sentinel
  write `envpts[env][0][ep + 1] = -1` (pre-fix line 180) lands on `envpts[env][0][32]`, which is
  the same memory as `envpts[env][1][0]`. The first level of the envelope is overwritten with -1.
  Each point past 31 writes further along the same row, and past the end of the row into the
  next envelope.
- RED on the unfixed code: `DrumSynthTest::ignoresEnvelopePointsBeyondSupportedCapacity()` fails at
  `oversizedSamples == prefixSamples`, so a 32-point envelope renders differently from its 31-point
  prefix.

## Fix

- `src/core/DrumSynth.cpp:130-144`: `maxEnvelopePoints = 31` (32 slots, the last reserved for the
  sentinel) and a helper `storeEnvelopeValue` that drops any point at or past the capacity and
  keeps the existing malformed or overlong token handling (a bad token becomes 0).
- `GetEnv` (`src/core/DrumSynth.cpp:146-196`) calls the helper for each stored value, stops
  advancing `ep` at the capacity, and places the sentinel at `min(ep, 30) + 1`, so it always fits.
  Envelopes of 31 or fewer points are parsed exactly as before.
- The helper keeps `GetEnv` at CCN 6 and the helper at CCN 4 (`lizard`, measured below). Inline
  bounds checks at each of the four write sites would have added four branches and pushed `GetEnv`
  past the CCN 10 target.

## Regression test

- `tests/src/core/DrumSynthTest.cpp`: new slot `ignoresEnvelopePointsBeyondSupportedCapacity`.
  It writes a `.ds` file whose `[Tone] Envelope` has 31 points and another with 32, renders both
  through `DrumSynth::GetDSFileSamples`, and requires identical output.
- The fixture uses the `[Tone] Envelope` (env 1), which is read on every render with `On=1`. The
  port's `FilterEnv` fixture was not used: `MainFilter` is `2 * Filter` (`src/core/DrumSynth.cpp:381`),
  so with `Filter=1` the filter-envelope branch (`MainFilter == 1`, line 818) is not taken.
- The test file is already registered (`tests/CMakeLists.txt:811-812`, `tests/fork-sources.txt:2272`),
  so no new file registration was needed. `tests/upstream-modifications.txt` gets one line for the
  inherited `src/core/DrumSynth.cpp`.

## Commands and exit codes

Build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9` because the default ccache temp directory
(`/run/user/1000/ccache-tmp`) is not writable from this session. The first attempt without that
override is `build-1-red.log`, EXIT=2, a ccache permission error and not a code error.

| Step | Command (run from the worktree / `build/tests`) | Log | EXIT |
| --- | --- | --- | --- |
| RED build (unfixed) | `cmake --build build --target zene DrumSynthTest -j2` | `build-2-red.log` | 0 |
| RED test (unfixed) | `ctest -R '^DrumSynthTest$' --output-on-failure` | `test-1-red.log` | 8 |
| GREEN build (fixed) | `cmake --build build --target zene DrumSynthTest -j2` | `build-3-green.log` | 0 |
| GREEN test (fixed) | `ctest -R '^DrumSynthTest$' --output-on-failure` | `test-2-green.log` | 0 |
| Proof: save fix | `git diff -- src/core/DrumSynth.cpp > logs/fix.patch` | `fix.patch` | 0 |
| Proof: revert fix | `git apply -R logs/fix.patch` | | 0 |
| Proof: RED build | `cmake --build build --target zene DrumSynthTest -j2` | `build-4-unfixed-check.log` | 0 |
| Proof: RED test | `ctest -R '^DrumSynthTest$' --output-on-failure` | `test-4-proof-red.log` | 8 |
| Proof: re-apply fix | `git apply logs/fix.patch` | | 0 |
| Proof: GREEN build | `cmake --build build --target zene DrumSynthTest -j2` | `build-5-reapplied.log` | 0 |
| Proof: GREEN test | `ctest -R '^DrumSynthTest$' --output-on-failure` | `test-5-proof-green.log` | 0 |
| Family build | `cmake --build build --target DrumSynthLevelTest SampleBufferResampleTest ImportDetectionTest -j2` | `build-6-family.log` | 0 |
| Family test | `ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest\|ImportDetectionTest)$' -V` | `test-7-family.log` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` | `gate-complexity-1.log` | **1** |
| File-length gate | `bash tests/file-length-gate.sh --check` | `gate-filelength-1.log` | **1** |
| Base complexity gate | same command on a `git archive HEAD` copy in /tmp | `gate-complexity-base-head.log` | **1** |
| Base file-length gate | same command on a `git archive HEAD` copy in /tmp | `gate-filelength-base-head.log` | **1** |

## Counts

- `DrumSynthTest`: 4 QTest entries executed (`initTestCase`, `rejectsOverlongEnvelopeLevel`,
  `ignoresEnvelopePointsBeyondSupportedCapacity`, `cleanupTestCase`), all PASS in GREEN
  (`test-6-green-verbose.log`).
- Family (`DrumSynthTest`, `DrumSynthLevelTest`, `SampleBufferResampleTest`, `ImportDetectionTest`):
  4 of 4 ctest tests passed, 27 QTest PASS lines (`test-7-family.log`).

## Gates: red, and red before this change

Both ratchet gates exit 1 on this branch. The same gates on an unmodified `git archive HEAD` also
exit 1, and the regression lists are identical line for line (`diff` of the `REGRESSION` lines
matched for both gates). The failures are in files this change does not touch: six complexity
regressions (for example `XyPadPairTest.cpp dotCentreAlong` CCN 16, `DawProjectReadTracks.cpp
parseNotes` CCN 13) and 14 file-length entries (for example `VideoTrackTest.cpp` 589 lines). No
gate entry mentions `DrumSynth.cpp`, `GetEnv` or `storeEnvelopeValue`. Fixing those files is out
of scope for this bug, and changing gate baselines is not allowed, so they remain as the branch base
left them.

## Notes

- `build-1-red.log` is the ccache permission failure described above, not a test result.
- The compiler warning at `src/core/DrumSynth.cpp:226` (unchecked `QFile::open`) predates this change
  and is not in the edited lines.
- The Luna round-0 review text was not in this brief, and this branch (`b02a0ff2b` tip) has no
  earlier cluster commit to amend. The work follows the procedure and the QA lessons from the brief.
