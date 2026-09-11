# Integration verification — `post-alpha/integration` @ `ccd07f490`

Verification-only pass over the tree with **eight merged lanes** on top of the v0.1.0-alpha
commit `0c23587d2`. Nothing was merged, rebased, pushed, or resolved here; the eight merges
are frozen. This run measures what the tree actually does.

- Worktree: `projects/lmms-fl-research/zene-pa-integration`
- Branch / HEAD: `post-alpha/integration` / `ccd07f490` (`Merge branch 'post-alpha/clip-capture-spec' …`)
- Pre-merge base: `0c23587d2` (`test: drive the remote-plugin host<->client contract end to end`)
- Binary built: `build/lmms`, reported version `LMMS 0.1.0-alpha.28+ccd07f4`
- Run date: 2026-09-11/12, on `Linux x86_64`, g++ 13.3.0, cmake 3.28.3, Qt 6.4.2

**VERDICT — the merged tree BUILDS and its tests PASS in both configurations. Two of the three
fixed gates FAIL on this tree** (Gate 6 and Gate 9 of `run-all-gates.sh`). No source was changed
to make anything pass; the failures are reported as found.

---

## 1. Build with the CI's exact flags

Command (the repo's own reproduction command, workspace rule 6 — exit code read unpiped):

```
JOBS=4 tools/local-ci.sh --build-dir build --jobs 4
```

**Deviation, stated plainly:** `tools/local-ci.sh` is committed **non-executable**
(`git ls-files -s` → mode `100644`), so invoking it directly fails before it does anything:

```
$ JOBS=4 tools/local-ci.sh --build-dir build --jobs 4
bash: tools/local-ci.sh: Permission denied
LOCAL_CI_EXIT=126
```

It was therefore run as `JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4`. Same script,
same argument vector; only the interpreter invocation differs. The flag set is the CI
`linux-x86_64` job's, byte for byte, plus the script's own printed Qt6 deviation:

```
CI CMAKE_OPTS: -DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON
qt flags    : -DWANT_QT6=ON
```

Qt6 is the documented deviation: the CI runner installs `qtbase5-dev`, this box has Qt6 only.

Results (`/tmp/pa-ci-off.log`):

```
--- [1/3] configure (cmake -S . -B build ...) ---
configure EXIT=0   (log: build/configure.log)
--- [2/3] build (cmake --build build -j4) ---
build EXIT=0   (log: build/build.log)
--- [3/3] ctest (from build/tests, -j2) ---
ctest EXIT=0   (log: build/ctest.log)
ctest totals: 100% tests passed, 0 tests failed out of 27
…
local-ci: overall exit=0 (0 = every executed step passed)
LOCAL_CI_EXIT=0
```

`WANT_SESSION_VIEW` is **OFF** (the CMake default) for this run.

### Did any warning become an error?

**No.** `-DUSE_WERROR=ON` is in force and the build exited 0. `build/build.log` contains four
warnings, all in the vendored portsmf tree of the MIDI importer:

```
plugins/MidiImport/portsmf/src/allegro.cpp:792:24: warning: 'index' may be used uninitialized [-Wmaybe-uninitialized]
plugins/MidiImport/portsmf/src/allegro.cpp:792:24: warning: 'track_ptr' may be used uninitialized [-Wmaybe-uninitialized]
plugins/MidiImport/portsmf/src/allegro.cpp:1935:9: warning: 'prev_units_are_seconds' may be used uninitialized [-Wmaybe-uninitialized]
plugins/MidiImport/portsmf/src/allegrord.cpp:408:26: warning: 'next' may be used uninitialized [-Wmaybe-uninitialized]
```

These are pre-existing vendored third-party warnings, deliberately exempted from `-Werror` by
`plugins/MidiImport/CMakeLists.txt` (`-Wno-error=maybe-uninitialized`, with a comment naming
`-Werror` as the reason). No warning from any fork-new file was emitted, and none became an error.

---

## 2. Full ctest suite — `WANT_SESSION_VIEW=OFF`

Run from `build/tests` (workspace rule 7; the top-level dir reports 0 tests, which is an error):

```
$ cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure > /tmp/ctest-off.log 2>&1
CTEST_EXIT=0
100% tests passed, 0 tests failed out of 27
Total Test time (real) =  30.64 sec
```

**27 tests, 0 failures, 0 skipped.** The 27, in ctest order:

```
 1 ArrayVectorTest             8 LufsMeterTest             15 RelativePathsTest        22 SlideNotesTest
 2 AudioBufferTest             9 MathTest                  16 RemotePluginAudioPortsTest 23 TimelineTest
 3 AudioBusHandleTest         10 MidiLearnTest             17 RemotePluginClientE2ETest  24 TwoTrackRecordingHarness
 4 AudioBusTest               11 MultiTrackRecorderTest    18 RoutingGraphTest         25 AutomationTrackTest
 5 AudioPortsModelTest        12 PluginAudioPortsTest       19 PdcMixerTest             26 PluginPortsMigrationTest
 6 AudioPortsTest             13 ProjectVersionTest        20 ScriptBindingsTest       27 AudioPluginTest
 7 AutomatableModelTest       14 RecordRingBufferTest      21 ScriptEngineTest
```

**Count reconciliation:** the brief quoted 25 pre-merge and expected "that neighbourhood, or one
higher". The merged tree reports **27 = 25 + LufsMeterTest + MidiLearnTest** — the two new feature
suites this branch adds. Not fewer than 25, so no finding. No test failed.

---

## 3. The three fixed gates — **one of the three does not pass**

Every exit code below was read unpiped.

| gate | expected | actual |
|---|---|---|
| `tests/fork-sources-gate.sh` | 0 | **1 — FAIL** |
| `tests/run-all-gates.sh` | 3 (`PASS-WITH-SKIPS`) | **1 — FAIL** |
| `tests/test-verification-debt.sh` | 0 | 0 — PASS |

### 3a. `tests/fork-sources-gate.sh` → **EXIT 1**

```
$ bash tests/fork-sources-gate.sh > /tmp/gate9.log 2>&1; echo "FORK_SOURCES_GATE_EXIT=$?"
FORK_SOURCES_GATE_EXIT=1
```

Tail:

```
tests/src/core/LufsMeterTest.cpp                         NOT IN tests/fork-sources.txt
                                                         (and not known upstream in tests/all-sources.txt)
tests/src/core/MidiLearnTest.cpp                         NOT IN tests/fork-sources.txt
                                                         (and not known upstream in tests/all-sources.txt)
tests/src/core/SessionModelTest.cpp                      NOT IN tests/fork-sources.txt
                                                         (and not known upstream in tests/all-sources.txt)

scanned 1104 tracked source file(s) under src/, include/, plugins/, tests/;
  110 fork-sources entry(ies), 992 inherited upstream, 0 stale entry(ies).

FAIL: 3 tracked source file(s) are not in tests/fork-sources.txt
      (and are not known upstream in tests/all-sources.txt):
  tests/src/core/LufsMeterTest.cpp
  tests/src/core/MidiLearnTest.cpp
  tests/src/core/SessionModelTest.cpp
```

**Cause (attributed by first-parent merge):** the three new test files were never registered in
*either* manifest by their own lanes, and none of those three lanes contains the gate that would
have told them.

| merge | file it added unregistered |
|---|---|
| `6e8847d13` `post-alpha/lufs-meter` | `tests/src/core/LufsMeterTest.cpp` |
| `e2689e62b` `post-alpha/midi-learn` | `tests/src/core/MidiLearnTest.cpp` |
| `8900efbb0` `post-alpha/pr594` | `tests/src/core/SessionModelTest.cpp` |

`git cat-file -e post-alpha/{lufs-meter,midi-learn,pr594}:tests/fork-sources-gate.sh` → **gate
ABSENT** on all three lanes, so those lanes never saw the check; and none of the three files was
placed in their `tests/all-sources.txt` either. This is an inherited omission that the new gate
correctly catches — but it means the gate is **red on the integrated tree**. Note that
`tests/fork-sources.txt` contains **zero** `tests/` entries at all (older fork test files such as
`tests/src/core/AudioBusTest.cpp` sit in `all-sources.txt` instead, a pre-existing looseness).

### 3b. `tests/run-all-gates.sh` → **EXIT 1**, not 3

```
$ bash tests/run-all-gates.sh > /tmp/runall.log 2>&1; echo "RUN_ALL_GATES_EXIT=$?"
RUN_ALL_GATES_EXIT=1
```

Summary:

```
gate   name                     result
1      ctest                    PASS
2      coverage                 SKIP
3      no-tautology             PASS
4      complexity               PASS
5      mutation                 PASS
6      upstream-regression      FAIL
7      file-length              PASS
8      duplication              PASS
9      fork-sources             FAIL

skipped: 1 of 9 gates did not run
  gate 2 (coverage): --with-coverage was not passed — to run it: pass --with-coverage

RESULT: FAIL — see the failing gate above
```

Gate 2 skipping alone would have produced the expected exit 3. Gates 6 and 9 both failed, which
fails the whole run **before** the skip path is reached — so `PASS-WITH-SKIPS` never appears.

### 3c. Gate 6 — four undeclared changes to upstream-inherited code

```
changed file                                             verdict
include/MainWindow.h                                     VIOLATION: undeclared change to upstream-inherited code
include/MidiController.h                                 VIOLATION: undeclared change to upstream-inherited code
src/core/midi/MidiAlsaSeq.cpp                            VIOLATION: undeclared change to upstream-inherited code
src/core/midi/MidiClient.cpp                             VIOLATION: undeclared change to upstream-inherited code

FAIL: Gate 6 violation(s) above — either revert the change, or declare it in
      tests/upstream-modifications.txt with a reason and ship a regression test.
```

Cause, proven two ways:

1. **Attribution:** the merge `e2689e62b` (`post-alpha/midi-learn`) is the one that introduces all
   four files into the tree — `git diff --name-only e2689e62b^1 e2689e62b` lists exactly those four.
2. **Pre-merge comparison:** replaying Gate 6's own classification rules (script lines 41–88)
   against the gate base `01148947e`:

   ```
   PRE-MERGE(0c23587d2): changed=111 violations=0
   HEAD(post-merge):     changed=147 violations=1
     VIOLATION: include/MainWindow.h
     VIOLATION: include/MidiController.h
     VIOLATION: src/core/midi/MidiAlsaSeq.cpp
     VIOLATION: src/core/midi/MidiClient.cpp
   ```

   Gate 6 was **green** at the pre-merge base and is **red** at HEAD. The merged lanes introduced
   the regression; it is not inherited. None of the four appears in
   `tests/upstream-modifications.txt`.

### 3d. `tests/test-verification-debt.sh` → EXIT 0

```
$ bash tests/test-verification-debt.sh > /tmp/debt.log 2>&1; echo "DEBT_HARNESS_EXIT=$?"
DEBT_HARNESS_EXIT=0
…
  OK   defect 3 GREEN  new gate exits 0 once the files are registered   exit 0
  OK   defect 3 GREEN  and reports the registered counts                'PASS: every tracked source in scope is registered' present
================ SUMMARY ================
RESULT: PASS — every red/green assertion held.
```

All three defects' red/green assertions hold. (The harness passes because it proves the *gate
mechanism* against synthetic fixtures; it does not assert that this tree is registered.)

---

## 4. Composition proof — `WANT_SESSION_VIEW` OFF → ON → OFF

Same build directory throughout (`build/`), reconfigured explicitly each time; no second build dir.

### ON

```
$ cmake -S . -B build … -DWANT_SESSION_VIEW=ON
SV_ON_CONFIGURE_EXIT=0
$ cmake --build build -j4
SV_ON_BUILD_EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure
CTEST_ON_EXIT=0
100% tests passed, 0 tests failed out of 28
```

**28 = 27 + `SessionModelTest`**, which appears as ctest **Test #26: SessionModelTest**. The
opt-in layer composes: enabling the flag adds the Session View data layer and its test, and
nothing else regresses.

The two new feature suites, run by name from their own binaries:

```
$ QT_QPA_PLATFORM=offscreen ./LufsMeterTest
LUFS_EXIT=0
********* Start testing of LufsMeterTest *********
PASS   : LufsMeterTest::kWeightingMatchesThePublishedTable()
PASS   : LufsMeterTest::kWeightingGainAt997HzIsTheCalibrationOffset()
PASS   : LufsMeterTest::integrationLoudnessReadsTheTestSignalLevel()
PASS   : LufsMeterTest::relativeGateExcludesTheQuietPassage()
PASS   : LufsMeterTest::singleChannelSignalIsThreeLuQuieter()
PASS   : LufsMeterTest::planarFeedFollowsTheFiveOneWeightings()
PASS   : LufsMeterTest::windowsFillInOrderAndBlockSizeDoesNotMatter()
PASS   : LufsMeterTest::silenceReadsMinusInfinityAndKeepsTheRunningValue()
PASS   : LufsMeterTest::truePeakOversamplesTheSignal()
PASS   : LufsMeterTest::processingABlockAllocatesNothing()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 2255ms
```

```
$ QT_QPA_PLATFORM=offscreen ./MidiLearnTest
MIDI_EXIT=0
********* Start testing of MidiLearnTest *********
QDEBUG : MidiLearnTest::initTestCase() Lv2 plugin SUMMARY: 73 of 87  loaded in 188 msecs.
PASS   : MidiLearnTest::SyntheticCcBindsFocusedControl()
PASS   : MidiLearnTest::BindingSurvivesProjectRoundTrip()
PASS   : MidiLearnTest::LearnOffIgnoresTheSameCc()
PASS   : MidiLearnTest::ArmedWithoutFocusTargetDoesNotBind()
PASS   : MidiLearnTest::NonControlChangeDoesNotBind()
PASS   : MidiLearnTest::AudioPeriodDoesNotLearn()
Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 1703ms
```

`LufsMeterTest` includes a no-allocation assertion (`processingABlockAllocatesNothing`), which is
the real-time rule's gold standard.

### Back to OFF

```
$ cmake -S . -B build … -DWANT_SESSION_VIEW=OFF
SV_OFF2_CONFIGURE_EXIT=0
$ cmake --build build -j4
SV_OFF2_BUILD_EXIT=0
$ cd build/tests && QT_QPA_PLATFORM=offscreen ctest -N | grep -cE '^\s+Test\s+#'
27
$ QT_QPA_PLATFORM=offscreen ctest -N | grep -ci session
0
$ QT_QPA_PLATFORM=offscreen ctest --output-on-failure
CTEST_OFF2_EXIT=0
100% tests passed, 0 tests failed out of 27
```

`SessionModelTest` disappears from the suite (0 registrations) and the count returns to 27. The
stale `SessionModelTest` executable remains on disk — CMake does not delete a de-registered
target's binary — but it is not in `ctest -N`, so it cannot run. Both feature suites were re-run
under OFF and still pass: `LufsMeterTest` 12/12 (exit 0), `MidiLearnTest` 8/8 (exit 0).

| configuration | ctest | result |
|---|---|---|
| OFF (default, and the CI run) | 27 | 100% passed, exit 0 |
| ON | 28 (+`SessionModelTest`) | 100% passed, exit 0 |
| OFF again | 27 (`SessionModelTest` gone) | 100% passed, exit 0 |

---

## 5. Real headless render on the merged tree

The integrated `build/lmms` binary, `QT_QPA_PLATFORM=offscreen`, rendering projects that ship in
the repo. Nothing here merely proves a file exists: each WAV was decoded and measured.

```
$ QT_QPA_PLATFORM=offscreen ./build/lmms render data/projects/tutorials/editing_note_volumes.mmp \
      -o /tmp/render/tutorial.wav -f wav -s 44100
RENDER_EXIT=0
```

```
file        : /tmp/render/tutorial.wav
bytes       : 2720856
frames      : 680192
channels    : 2
sample rate : 44100
sample width: 16 bit
duration    : 15.424 s
peak        : 32767 (0.999969 full-scale)
rms         : 7211.558 (-13.15 dBFS)
OK: WAV is non-empty and non-silent
```

Second, independent render from the shipped demo set:

```
$ QT_QPA_PLATFORM=offscreen ./build/lmms render "data/projects/shorties/Crunk(Demo).mmp" \
      -o /tmp/render/crunk.wav -f wav -s 44100
RENDER2_EXIT=0
```

```
file        : /tmp/render/crunk.wav
bytes       : 2930776
frames      : 732672
channels    : 2
sample rate : 44100
duration    : 16.614 s
peak        : 32767 (0.999969 full-scale)
rms         : 8029.393 (-12.22 dBFS)
OK: WAV is non-empty and non-silent
```

Both renders are real audio: stereo 44.1 kHz, ~15–17 s, RMS around −12 to −13 dBFS. The renderer
runs offline end to end (project load → plugin scan → PERFLOG → write) and exits 0.

Two honest notes, neither a regression signal:

- The tutorial render logs one missing sample —
  `Sample not found: samples/shapes/smooth_inv_saw.ogg` — a relative sample path the project
  carries; the render still produced full-level audio. Crunk(Demo).mmp logged no such error.
- `tutorial.wav` has **391 samples at full scale (0.0287%)** — negligible, consistent with the
  project's own mix level, not a systematic clipping regression.

---

## 6. The `tests/fork-sources.txt` ledger merge check

The two lanes that touched the ledger (`lufs-meter`, `midi-learn`) resolved as an entry union.
Re-verified independently:

**Well-formedness**

```
entries                    : 110
duplicates (sort|uniq -d)  : 0
conflict markers           : 0 (no <<<<<<<, =======, >>>>>>>)
trailing newline           : yes (final byte 0x0a)
```

**The merge is exactly the union — nothing lost, nothing invented.** Sorted entry sets compared
byte for byte:

```
union of the 8 lanes           : 110
post-alpha/integration ledger  : 110
in integration but in NO lane  : (empty)
in a lane but not in integration: (empty)
diff -q union integration      : IDENTICAL
```

Per lane, no entry is missing from the integration ledger:

| lane | lane entries | missing from integration |
|---|---|---|
| `post-alpha/gate-debt` | 100 | — |
| `post-alpha/docs-security` | 100 | — |
| `post-alpha/lufs-meter` | 102 | — |
| `post-alpha/pr594` | 104 | — |
| `post-alpha/midi-learn` | 104 | — |
| `post-alpha/instrument-hosting` | 100 | — |
| `post-alpha/clip-capture-spec` | 100 | — |
| `post-alpha/readme-truth` | 100 | — |

**So the ledger's own merge is clean.** But "nobody's source is missing from it" is **not** true in
the wider sense: the gate finds three fork-new test sources registered in *neither* manifest
(§3a). Those are omissions inherited from the lanes, not merge losses — the union is provably
lossless.

---

## Summary of failures

| # | what | where | exact evidence |
|---|---|---|---|
| 1 | `tests/fork-sources-gate.sh` exits **1**, not 0 | Gate 9 | 3 test files in neither manifest (§3a) |
| 2 | `tests/run-all-gates.sh` exits **1**, not 3 | whole run | Gate 6 + Gate 9 FAIL (§3b) |
| 3 | 4 undeclared upstream modifications | Gate 6 | `MainWindow.h`, `MidiController.h`, `MidiAlsaSeq.cpp`, `MidiClient.cpp`; green pre-merge, red at HEAD (§3c) |
| 4 | `tools/local-ci.sh` committed non-executable (mode `100644`) | `tools/` | `Permission denied`, exit 126; ran via `bash` (§1) |

**Not fixed here, deliberately.** No file under `src/`, `include/` or `plugins/` was modified, and
no gate was relaxed. The Gate 6 failure in particular cannot be "fixed" mechanically: declaring a
divergence asserts the upstream behaviour change is intended and requires a reason plus a
regression test, and reverting `MainWindow.h`/`MidiController.h`/`MidiAlsaSeq.cpp`/`MidiClient.cpp`
would remove the MIDI-learn feature that merge `e2689e62b` exists to add. Both are decisions for
the integration owner. The mechanical part of the Gate 9 fix is registering the three test files in
`tests/fork-sources.txt` — left undone for the same reason: the report is the deliverable.

### Reality check on what IS green

- Build: configure / compile / ctest all exit 0 under the CI flag set with `USE_WERROR=ON`; no
  warning became an error.
- Tests: 27/27 OFF, 28/28 ON, 27/27 OFF again — the ON↔OFF toggle composes exactly as designed.
- New features: `LufsMeterTest` 12/12, `MidiLearnTest` 8/8, `SessionModelTest` present only when ON.
- Render: two shipped projects render headlessly to real, non-silent audio.
- Debt harness: exit 0, all red/green assertions hold.
