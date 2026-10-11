# Cluster new-p9-45 — FIXED

## Change and evidence

`src/core/ControlCommandsMixerSurround.cpp:211` now gives a surround-format source a note explaining that `mixer.surround_pan` does not pan sends from that source. A stereo source with no surround sends retains the existing no-sends note. A stereo source with a surround send receives no warning.

`tests/src/core/SurroundTest.cpp:233` covers all four cases: surround-format source with a send, surround-format source without a send, stereo source without a surround send, and stereo source with a surround send. The panner coordinates differ between changes so each command produces a reply rather than taking the unchanged-state early return.

## Build and test record

All ctest commands ran from `build/tests`. `ctest -R '^SurroundTest$'` selected and executed the affected family target (1/1). Build and test output went to new numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs`; each shell command captured its exit status and appended it to its log.

The first two test runs exposed a test setup issue: the surround-format call reused the stereo call's pan coordinates and returned through the unchanged-state path. I changed the second call's coordinates. The corrected run passed before the required reversal proof; the proof below uses that corrected regression.

| Stage | Command | Exit |
| --- | --- | ---: |
| Initial build | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/build-1.log 2>&1` | 0 |
| Initial regression run, before production edit | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/test-1.log 2>&1` | 8 |
| Build after first production edit | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/build-2.log 2>&1` | 0 |
| Test with the initial, unchanged-coordinate setup | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/test-2.log 2>&1` | 8 |
| Build with corrected regression inputs | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/build-3.log 2>&1` | 0 |
| Corrected regression, fix applied | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/test-3.log 2>&1` | 0 |
| Reverse only the production change | `git diff -- src/core/ControlCommandsMixerSurround.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/fix.patch` | 0 |
| Rebuild with production change reversed | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/build-4.log 2>&1` | 0 |
| RED with only production change reversed | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/test-4.log 2>&1` | 8 |
| Restore production change | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/fix.patch` | 0 |
| Rebuild after restoring production change | `cmake --build build --target zene SurroundTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/build-5.log 2>&1` | 0 |
| GREEN after restore | `ctest -R '^SurroundTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/test-5.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/gate-3.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-45.r2/logs/gate-4.log 2>&1` | 0 |

The reversal run failed at `theSurroundPannerExplainsWhichSourcesItAffects` on the surround-format note assertion. The restored run passed the selected `SurroundTest` target (1/1). The final build and gate log searches for `error`, `FAILED`, and `Assertion` found no matches. The `fff` grep tool was not available in this environment, so logs were searched with `rg`, one term per search.

## Files in commit

- `src/core/ControlCommandsMixerSurround.cpp` — split the response note by source format and sends.
- `tests/src/core/SurroundTest.cpp` — cover the four response cases.
- `docs/qwen-fixes-2026-10/cluster-new-p9-45.md` — this report.
