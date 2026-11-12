FIXED

# cluster new-p21-238: Microtuner::keyToFreq has no guard for an empty interval list

Finding (Luna patrol, BUG 4): a scale with no intervals reaches `Microtuner::keyToFreq`, where `octaveDegree` becomes -1 and is used in division, modulo and as an index into `intervals`. Suggested fix: return 0 when the interval list is empty.

## Verdict and decision

REAL, and FIXED in this commit. My first report (round 1) said ALREADY-FIXED. That was wrong, and the review disproved it: the public `Scale` constructor and `Scale::setIntervals` accept an empty vector, and `Song::setScale` (public, `include/Song.h:674`) stores it. With a mapped key, `keyToFreq` then indexed the empty list. The loader fallback from commit `7c5051c63` only covers the file-load path; it does not cover this API path.

The fix is the one the finding proposes, at the function: `keyToFreq` returns 0 when the interval list is empty, before any degree arithmetic or indexing.

## Evidence (file:line, branch `bugs/qwen-new-p21-238`)

- `src/core/Microtuner.cpp:65-112` `keyToFreq`. Line 74 takes `intervals` from `scale->getIntervals()`. Line 75 (new) returns 0 when it is empty. Before the fix, line 83 `const int octaveDegree = intervals.size() - 1;` was -1 for an empty list (size_t 0 minus 1 converted to int), and line 107 `intervals[octaveDegree]` read out of bounds. The doc comment at line 63 now also says that an empty scale gives 0.
- Reachable public path: `include/Scale.h:76` (constructor `Scale(QString, std::vector<Interval>)`) and `include/Scale.h:82` (`setIntervals`) accept an empty vector. `include/Song.h:674` and `src/core/Song.cpp:3178` (`setScale`) store it. `src/core/Microtuner.cpp:73` (`getScale`) returns it. `src/core/NotePlayHandle.cpp:688-695` calls `keyToFreq` for a mapped key when the Microtuner is enabled.
- The loader fallback (`src/core/Scale.cpp:186-188`, commit `7c5051c63`) still stands. It covers a different path (a project file whose intervals are all refused) and is unchanged.
- Other `Scale` construction sites, checked in round 2: `src/core/Scale.cpp:127-130` (default constructor, `1/1`), `src/gui/MicrotunerConfig.cpp:400-401,428` (dialog, seeds `1/1`), `src/core/SessionTuningScala.cpp:387-400` (the `.scl` path, seeds `1/1` at line 387 and feeds the SESSION table at `:400` onward, not the Song scale), and `src/core/Song.cpp:219` (defaults).
- Sibling not changed: `Microtuner::octaveSize()` (`src/core/Microtuner.cpp:115-125`) still computes `intervals.size() - 1` and, for an empty scale with keymap size 0, returns -1. Its only caller is `src/gui/editors/PianoRoll.cpp:954,1359`, which uses it as a shift amount. It does not index anything, so it is not the reported fault. Its right value for an empty scale is a contract question, so it is NEEDS-CONTEXT and left unchanged.

## Regression test

`tests/src/core/SessionTuningTest.cpp`, new slot `emptyStoredScaleReadsSilenceForAMappedKey` (lines 448-465, between `negativeBaseDegreeUsesFloorOctaveDivision` and the transport-retune proof). It:

1. clears the project, then calls `song->setScale(0, std::make_shared<Scale>("empty scale", std::vector<Interval>{}))` (the public constructor and the public `Song::setScale`);
2. installs a keymap with map `{0, 2}` over keys 0-127 (base key 69, 440 Hz);
3. checks that key 70 is mapped (`getDegree(70) != -1`), so `keyToFreq` takes the scale path;
4. calls `Microtuner::keyToFreq(70, 69)` and requires exactly `0.0f`.

Determinism: pure in-process values; no timing, sleeps, threads or randomness.

Registration (same commit): `tests/upstream-modifications.txt` (last line) registers the inherited product file `src/core/Microtuner.cpp` with a reason. `SessionTuningTest.cpp` is already registered in `tests/CMakeLists.txt:329` and needs no new line. The changed-file set is therefore `src/core/Microtuner.cpp`, `tests/src/core/SessionTuningTest.cpp`, `tests/upstream-modifications.txt` and this report.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-238.r2/logs`. Script: `proof.sh`, summary `proof-summary.log`.

Build note: every build uses `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p21-238`. The default ccache temp directory is outside the write-jail and gives "Permission denied".

| Step | Command (cwd: worktree; ctest from `build/tests`) | EXIT | Log |
|---|---|---|---|
| Build with fix | `cmake --build build --target zene SessionTuningTest -j4` | 0 | build-1-green.log |
| GREEN ctest | `ctest -R '^SessionTuningTest$' --output-on-failure` | 0 | test-1-green.log |
| Save product hunk | `git diff -- src/core/Microtuner.cpp > logs/fix.patch` | 0 | fix.patch |
| RED: reverse product hunk only | `git apply -R logs/fix.patch` | 0 | apply-R (proof-summary.log) |
| RED build | `cmake --build build --target zene SessionTuningTest -j4` | 0 | build-2-red.log |
| RED ctest | `ctest -R '^SessionTuningTest$' --output-on-failure` | 8 | test-2-red.log |
| Restore fix | `git apply logs/fix.patch` | 0 | (proof-summary.log) |
| GREEN build | `cmake --build build --target zene SessionTuningTest -j4` | 0 | build-3-green.log |
| GREEN ctest | `ctest -R '^SessionTuningTest$' --output-on-failure` | 0 | test-3-green.log |
| New function alone, fixed build | `./SessionTuningTest emptyStoredScaleReadsSilenceForAMappedKey` (in `build/tests`) | 0 | func-green.log |

RED evidence (`test-2-red.log`): ctest reports `87 - SessionTuningTest (SEGFAULT)`, `0% tests passed, 1 tests failed out of 1`. The QTest run stops inside `SessionTuningTest::emptyStoredScaleReadsSilenceForAMappedKey()` (backtrace frame `#6`), after the four earlier slots passed. The crash is the out-of-bounds read the finding describes, now reached through the public API.

GREEN evidence: `test-1-green.log` and `test-3-green.log`: `1/1 Test #87: SessionTuningTest ... Passed`, `100% tests passed out of 1`. `func-green.log`: `PASS : SessionTuningTest::emptyStoredScaleReadsSilenceForAMappedKey()`, `Totals: 3 passed, 0 failed` (initTestCase, the new slot, cleanupTestCase).

Tests executed in the affected family: `SessionTuningTest`, 1 ctest test (the binary contains every SessionTuningTest slot; all passed on GREEN). `ScaleIntervalOverflowTest` (round 1) and `MicrotunerScaleFileTest` / `Sf2PlayerRealtimeTest` were not rebuilt or run in this round; the product change is confined to `keyToFreq`'s empty-list early return.

Whitespace: `git diff --check HEAD` EXIT 0 (`diff-check.log`).

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exit 1. It fails on `PermissionError`: it writes its logs to `<worktree>/../zene-qwen-new-p21-238-feature-check/`, outside the write-jail (`feature-check-attempt.log`). This is a sandbox limit, not a gate result.

Each gate command that the script runs was run from the worktree instead (`gates-run.sh`, `gates-summary.log`):

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchets are red on the train by design. Their listed items are in files this change does not touch (`WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommands*.cpp`, `Vst3Host.cpp`, `TakeLane.cpp`, `ArrangeToolsTest.cpp`, `CollabTransportTest.cpp`). None of the three changed files appears in their output (`gate-complexity.log`, `gate-file-length.log`), so there is no new item in my files.

## Self-check

Run: `submit_check.py <worktree> new-p21-238` (round 1 log `submit-check-2.log`; this round's result is in the final report).

- **Regression fails with the product change reverted, passes with it:** yes. RED ctest exit 8 (SEGFAULT inside the new slot); GREEN ctest exit 0; the new slot alone passes on the fixed build (`func-green.log`).
- **Deterministic:** yes. Pure values built in-process, no timing, sleeps, threads or randomness; no seam is needed.
- **Fix at the cause and complete:** the empty list is checked once, at the only function that indexes it (`keyToFreq`), before any use. The siblings were checked: the loader and default paths already keep at least one interval; the `.scl` path seeds `1/1`; `octaveSize()` does not index and is NEEDS-CONTEXT, reported above.
- **No collateral damage:** non-empty scales take the same path as before (the early return is not reached). The only new behaviour is 0 Hz for a state that crashed before. Project files and the `.scl` path are unchanged.
- **Realtime / undo:** the check is a `.empty()` test on a vector already in use. No allocation, lock or log is added, and no command is added.
- **Registration:** `src/core/Microtuner.cpp` registered in `tests/upstream-modifications.txt`; test file already in `tests/CMakeLists.txt`.
