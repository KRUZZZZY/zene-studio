# Cluster new-p10-357-port: spectrum view decay accumulator

**Verdict: ALREADY-FIXED** on the newest line (`050/m0-train-5` tip `b02a0ff2b`, this branch's base).

## Finding

`plugins/SpectrumAnalyzer/SaSpectrumView.cpp:182` reads `m_decaySum` in `drawSpectrum()` before any
write on the first paint. The original branch (`bugs/qwen-new-p10-357`, commit `328542ee8`) fixed this by
adding `= 0.0f` to the member declaration.

## Evidence on this branch

| Item | Location |
| --- | --- |
| Member is zero-initialised at its declaration | `plugins/SpectrumAnalyzer/SaSpectrumView.h:107` (`float m_decaySum = 0.0f;`) |
| First-paint read | `plugins/SpectrumAnalyzer/SaSpectrumView.cpp:182` |
| Per-refresh reset before accumulation | `plugins/SpectrumAnalyzer/SaSpectrumView.cpp:254` (`m_decaySum = 0;`), accumulated at `:345` |
| Fix already in history | commit `9a03fc741` "Fix spectrum decay initialization (cluster new-p9-412)", an ancestor of `HEAD` |
| Regression test (already present) | `tests/src/core/SaSpectrumViewTest.cpp:6-19` (`decayAccumulatorStartsAtZero`) |
| Regression test registered | `tests/CMakeLists.txt:1774-1779` (ctest name `SaSpectrumView`), `tests/fork-sources.txt:2390`, `tests/upstream-modifications.txt:2190` |

The port was not needed, so no code was changed. I did not port `328542ee8`'s `AudioAnalysisTest::spectrumViewDecayStartsAtZero`
because it would duplicate the existing `SaSpectrumView` regression.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-357-port.r1/logs`

| Step | Command (run from the worktree / `build/tests` as noted) | Exit | Result |
| --- | --- | --- | --- |
| Build 1 | `cmake --build build --target SaSpectrumViewTest -j2 > build-1.log 2>&1` | 2 | ccache could not create temp files under `/run/user/1000` (environment, not code) |
| Build 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p10-357 cmake --build build --target SaSpectrumViewTest -j2 > build-2.log 2>&1` | 0 | target built, 0 errors |
| Count | `ctest -N -R '^SaSpectrumViewTest$'` (from `build/tests`) | - | 0 tests: the registered name is `SaSpectrumView`, not `SaSpectrumViewTest` |
| Count | `ctest -N -R '^SaSpectrumView$'` | - | 1 test |
| GREEN (fix present) | `ctest -R '^SaSpectrumView$' --output-on-failure > test-1-green-with-fix.log 2>&1` | 0 | 1/1 passed |
| Revert | `git show 9a03fc741 -- plugins/SpectrumAnalyzer/SaSpectrumView.h > fix.patch && git apply -R fix.patch` | 0 | line 107 back to `float m_decaySum;` |
| RED (fix removed) | `ctest -R '^SaSpectrumView$' --output-on-failure > test-2-red-without-fix.log 2>&1` | 8 | `FAIL! : SaSpectrumViewTest::decayAccumulatorStartsAtZero() 'source.contains("float m_decaySum = 0.0f;")' returned FALSE.` 0% passed |
| Re-apply | `git apply fix.patch` | 0 | line 107 is `float m_decaySum = 0.0f;` again |
| GREEN (re-applied) | `ctest -R '^SaSpectrumView$' --output-on-failure > test-3-green-reapplied.log 2>&1` | 0 | 100% passed |
| Zene rebuild | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p10-357 cmake --build build --target zene -j2 > build-3-zene.log 2>&1` | 0 | 0 errors after the header was touched and restored |
| Complexity gate | `bash tests/complexity-gate.sh --check > gate-complexity-1.log 2>&1` | 1 | FAILS on the newest line (see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > gate-filelength-1.log 2>&1` | 1 | FAILS on the newest line (see below) |

Notes on the RED step: the regression reads `SaSpectrumView.h` from the source tree at runtime
(`SA_SPECTRUM_VIEW_HEADER`, `tests/CMakeLists.txt:1778-1779`), so the RED run did not need a rebuild of the test
binary. The `zene` target was rebuilt afterwards (exit 0).

The affected test family (`ctest -R 'Spectrum|SaControls'`) contains one test, `SaSpectrumView`: 1 run, 1 passed.

## Gate failures (pre-existing; not caused by this cluster)

After the revert and re-apply, `git diff HEAD` is empty, so the gates measured the same content as `b02a0ff2b`.
Both gates fail on it:

- complexity: 68 functions over CCN 10; `REGRESSION` lines name `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
  `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`.
- file length: 19 files over 500 code lines; `REGRESSION` lines name `SmfInterchangeReader.cpp`, `TakeLane.cpp`, several tests.

None of these files is touched by this cluster. I did not change gate limits or baselines, and I did not refactor
unrelated files to make the gates pass. These need an owner decision on the newest line.

## Not checked

- `tests/reference/SpectrumAnalyzer/SaSpectrumView.h:107` still declares `float m_decaySum;` with no initializer.
  That is the pinned pre-migration reference copy (`tests/reference/ORIGIN.tsv`), not product code, and changing it
  would break its recorded blob ids. I did not check whether the reference renderer's first paint reaches that read.
- The regression is a source-text scan (`source.contains(...)`), not a behavioural test. It would also pass if the
  same text appeared in a comment. Strengthening it is a separate task.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-357-port.md` (this report only; no product or test file changed)
