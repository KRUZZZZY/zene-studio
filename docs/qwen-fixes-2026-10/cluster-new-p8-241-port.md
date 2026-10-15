# Cluster new-p8-241-port: partial WAV writes (port of 14a0cdd21)

Verdict: **FIXED** (port of `14a0cdd21` from `bugs/qwen-new-p8-241` onto `bugs/qwen-new-p8-241-port`).

The finding is real on this branch. The fix was re-applied by hand against the current code; nothing was merged, cherry-picked or rebased.

Caveat: both ratchet gates exit 1, but they exit 1 on unmodified HEAD too, for failures this bug does not touch. See "Gates" below.

## Evidence

- Original defect: `WavSink::write` ignored the return value of `sf_writef_float` in [`VideoAudioExtract.cpp:132`](../../src/core/VideoAudioExtract.cpp) (pre-fix line 129). `decodeAll` ignored `write`'s result, so a short write was counted into `frames` and extraction still returned success.
- Fix in [`src/core/VideoAudioExtract.cpp`](../../src/core/VideoAudioExtract.cpp):
  - `writeFailed` flag at line 62.
  - `write()` now returns `bool`; it compares `sf_writef_float`'s return with the requested count at line 133, sets `writeFailed` and `error` ("only N of M frames were written") on a short write, and returns false (lines 123-141).
  - `decodeAll` stops on a failed write at line 170 and checks the final flush at line 173.
  - `extract` reports a failed write as `cannot write '<wav>': <reason>` at line 191. The `QSaveFile` output is discarded on return, so the incomplete WAV is never published and an existing destination is kept.
- Regression: [`tests/src/core/VideoTrackTest.cpp`](../../tests/src/core/VideoTrackTest.cpp) `aPartialWavWriteFailsAndRemovesTheFile` (line 594). It caps `RLIMIT_FSIZE` to 4096 bytes through `ScopedFileSizeLimit` (line 84) while extracting the generated two-second clip. It asserts the extraction fails, the error is non-empty, and the output file does not exist.
- Registers: both changed files are already product-new entries in `tests/fork-sources.txt:1839` and `:2445`. No new file, so no register change. No header changed.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-241-port.r1/logs`.

Environment note: the default ccache temp dir (`/run/user/1000/ccache-tmp`) was not writable in this session. Builds from `build-2` onward set `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p8`. No config file was changed for this.

| # | Purpose | Command (abbreviated; `$L` = logs folder) | EXIT | Log |
| - | ------- | ------------------------------------------ | ---: | --- |
| 1 | First build attempt (ccache permission failure, not a code error) | `cmake --build build --target zene VideoTrackTest > $L/build-1.log 2>&1` | 2 | build-1.log |
| 2 | Build regression on original product code | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen-p8 cmake --build build --target zene VideoTrackTest > $L/build-2.log 2>&1` | 0 | build-2.log |
| 3 | **RED** on original code | `cd build/tests && ctest -R '^VideoTrackTest$' --output-on-failure > $L/test-1.log 2>&1` | **8** | test-1.log |
| 4 | Build with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene VideoTrackTest > $L/build-3.log 2>&1` | 0 | build-3.log |
| 5 | **GREEN** with fix | `cd build/tests && ctest -R '^VideoTrackTest$' --output-on-failure > $L/test-2.log 2>&1` | **0** | test-2.log |
| 6 | Save product patch | `git diff -- src/core/VideoAudioExtract.cpp > $L/fix.patch` | 0 | fix.patch |
| 7 | Revert product fix | `git apply -R $L/fix.patch` | 0 | |
| 8 | Rebuild without fix | `CCACHE_TEMPDIR=... cmake --build build --target zene VideoTrackTest > $L/build-4.log 2>&1` | 0 | build-4.log |
| 9 | **RED** without fix (proof) | `cd build/tests && ctest -R '^VideoTrackTest$' -V > $L/test-3.log 2>&1` | **8** | test-3.log |
| 10 | Reapply fix | `git apply $L/fix.patch` | 0 | |
| 11 | Rebuild with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene VideoTrackTest > $L/build-5.log 2>&1` | 0 | build-5.log |
| 12 | **GREEN** with fix (proof) | `cd build/tests && ctest -R '^VideoTrackTest$' -V > $L/test-4.log 2>&1` | **0** | test-4.log |
| 13 | Complexity gate, final tree | `bash tests/complexity-gate.sh --check > $L/gate-1.log 2>&1` | **1** | gate-1.log |
| 14 | File-length gate, final tree | `bash tests/file-length-gate.sh --check > $L/gate-2.log 2>&1` | **1** | gate-2.log |
| 15 | Save full diff, revert everything to HEAD | `git diff > $L/full.patch`; `git apply -R $L/full.patch` | 0; 0 | full.patch |
| 16 | Complexity gate on HEAD (comparison) | `bash tests/complexity-gate.sh --check > $L/gate-3.log 2>&1` | **1** | gate-3.log |
| 17 | File-length gate on HEAD (comparison) | `bash tests/file-length-gate.sh --check > $L/gate-4.log 2>&1` | **1** | gate-4.log |
| 18 | Restore full diff | `git apply $L/full.patch` | 0 | |
| 19 | Video family, first run (2 targets not built) | `cd build/tests && ctest -R 'Video' -V > $L/test-5.log 2>&1` | 8 | test-5.log |
| 20 | Build the two unbuilt targets | `CCACHE_TEMPDIR=... cmake --build build --target VideoTestClipTest ArrangerOverlaysVideoLaneTest > $L/build-6.log 2>&1` | 0 | build-6.log |
| 21 | Video family, after build | `cd build/tests && ctest -R 'Video' -V > $L/test-6.log 2>&1` | **0** | test-6.log |
| 22 | Ctest listing for the family | `cd build/tests && ctest -N -R 'Video' > $L/ctest-list-1.log 2>&1` | 0 | ctest-list-1.log |

## Test results

- RED on original code (rows 3 and 9), identical failure both times:
  `FAIL! : VideoTrackTest::aPartialWavWriteFailsAndRemovesTheFile() '!extracted' returned FALSE. (a short WAV write was reported as a successful extraction)`.
  Totals: 23 passed, 1 failed, 1 skipped.
- GREEN with fix (rows 5 and 12): `PASS : VideoTrackTest::aPartialWavWriteFailsAndRemovesTheFile()`. Totals: 24 passed, 0 failed, 1 skipped. The skip is `withoutFfmpegEveryCommandIsUnavailable` ("this build has FFmpeg"), not the new test.
- Affected family, `ctest -R 'Video'` (row 21), 3 of 3 passed:
  - `VideoTrackTest`: 24 passed, 0 failed, 1 skipped.
  - `VideoTestClipTest`: 5 passed, 0 failed.
  - `ArrangerOverlaysVideoLaneTest`: 9 passed, 0 failed.

  Total executed: 38 passed, 0 failed, 1 skipped. Row 19 had two "Not Run" entries because those executables were not built; that was a build gap, not a result.

## Gates

Both required gates exit 1 with the fix (rows 13, 14). They also exit 1 on unmodified HEAD (rows 16, 17). The failure sets are the same except one line:

- Complexity gate: the same six `REGRESSION` lines and the same `improved` lines in both runs. None are in `VideoAudioExtract.cpp`, `VideoTrackTest.cpp` or the new functions (for example `dotCentreAlong` in `tests/src/gui/XyPadPairTest.cpp`, `parseNotes` in `src/core/DawProjectReadTracks.cpp`).
- File-length gate: the same list, except `tests/src/core/VideoTrackTest.cpp` is 589 code lines at HEAD and 647 with this fix. That file is already flagged as "new file over 500 lines" on HEAD. The fix adds about 58 code lines of regression test to it.

I did not change any gate, limit or baseline. The gates are red on this branch independently of this fix; clearing them is outside this one-bug port.

## Files committed

- `src/core/VideoAudioExtract.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-241-port.md`
