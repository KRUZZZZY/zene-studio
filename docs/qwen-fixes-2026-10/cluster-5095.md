Verdict: FALSE-POSITIVE

5095: FALSE-POSITIVE — `addBlock()` forms `frames + at` on a null pointer, but that is `nullptr + 0` (well-defined in C++), the meter refuses null input whole, and the report is unchanged. No product change; one characterization test added.

Evidence:

- The finding's premise is that `addBlock(nullptr, 0)` evaluates `nullptr + 0` and that this is undefined behaviour. In C++ it is not: [expr.add] paragraph 4.1 exempts adding zero to a null pointer value from undefined behaviour. I checked this with a web search; the sources were secondary (an isocpp liaison thread, <https://lists.isocpp.org/liaison/2023/10/1324.php>, and a Filament PR discussion, <https://github.com/google/filament/pull/1566>), not the current draft text. The claim that a non-zero offset on a null pointer is undefined is the part that matters, and it never occurs here (below).
- `src/core/LoudnessReport.cpp:107-109`: when `frames == nullptr`, `piece` is `std::numeric_limits<f_cnt_t>::max()`.
- `src/core/LoudnessReport.cpp:61-75` (`inPieces`): the do-while makes `count = min(piece, frameCount - done)`, so with `piece == max` the first call is `feed(0, frameCount)` and `done` becomes `frameCount`, ending the loop. The offset `at` is therefore always `0` for a null block, so line 110's `frames + at` is always `nullptr + 0`. A positive count with a null pointer behaves the same way.
- `src/core/LufsMeter.cpp:150`: `processBlock()` returns immediately on `frames == nullptr`, so nothing is read and no state changes.
- `src/core/LufsMeter.cpp:163`: `processPlanar()` returns immediately on `channels == nullptr`, and the planar path at `src/core/LoudnessReport.cpp:123-129` routes null channels there.
- `include/LoudnessReport.h:110-119` documents `addBlock` as reading `frames` only; the null-input contract is the meter's refusal, which the code comment at `src/core/LoudnessReport.cpp:107` also states.

Why no product change: a rewrite such as `frames == nullptr ? nullptr : frames + at` would produce bit-identical values and identical meter behaviour. No test can fail before it and pass after it, so a fix would be unprovable and would only change code style. I did not make it.

Regression test: `tests/src/core/LoudnessReportTest.cpp`, slot `aNullBlockIsIgnored()`, declared in the `LoudnessReportTest` class in that same file. It is a characterization test: it feeds a real 0.5 s sine, records `integratedLufs()` and `shortTermMaxLufs()`, then calls `addBlock(nullptr, 0)`, `addBlock(nullptr, BlockFrames)` and `addPlanarBlock(nullptr, DEFAULT_CHANNELS, BlockFrames)`, and asserts both values are unchanged. It passes on the unmodified product code, so it is not RED-proof; it pins the null-input contract that the finding asks about, so a later refactor that breaks it would be caught.

Commands and exit codes (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5095.r1/logs/`):

| Step | Command | EXIT |
| --- | --- | --- |
| Build, first attempt (ccache temp dir not writable; `build-1.log`) | `cmake --build build --target LoudnessReportTest -j4 > .../build-1.log 2>&1` | 2 |
| CTest after the failed build (`test-1.log`: target not built) | `ctest -R '^LoudnessReportTest$' --output-on-failure` from `build/tests` | 8 (Not Run) |
| Build, retry with ccache disabled for this command only (`build-2.log`) | `CCACHE_DISABLE=1 cmake --build build --target LoudnessReportTest -j4 > .../build-2.log 2>&1` | 0 |
| CTest, original product code plus new test (`test-2.log`) | `ctest -R '^LoudnessReportTest$' --output-on-failure` from `build/tests` | 0 (1/1 passed) |
| Test binary, verbose (`run-2-verbose.log`) | `./LoudnessReportTest` from `build/tests` | 0 (`PASS : LoudnessReportTest::aNullBlockIsIgnored()`; `Totals: 12 passed, 0 failed`) |

RED/GREEN: not applicable. The product code is unchanged, so step 4b (revert, expect FAIL, restore, expect PASS) has no product hunk to revert. The new test passes on the original code, as recorded above.

Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 1 before any gate ran: it writes its logs to `<worktree>/../<name>-feature-check`, i.e. in `projects/zene-studio-program/`, which the write-jail forbids. Its log is `gates-1.log`. I ran the same gate commands directly in the worktree instead, logging each to the LOG FOLDER:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (train-wide red; 7 REGRESSION lines, none in `LoudnessReport*` or `LoudnessReportTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (train-wide red; 22 lines, none in `LoudnessReport*`) |

With the runner's rule (ratchet items fail only when they name a file the branch changed), the two ratchets pass for this branch: the only changed file is `tests/src/core/LoudnessReportTest.cpp`, and no ratchet line names it.

Files committed: `tests/src/core/LoudnessReportTest.cpp`, `docs/qwen-fixes-2026-10/cluster-5095.md`.

## Self-check

1. **RED fails / GREEN passes with the product change reverted:** not applicable. There is no product change, so there is no hunk to revert. The new test `aNullBlockIsIgnored()` passes on the original product code (`test-2.log`, `run-2-verbose.log`, both EXIT=0), so it is not regression proof. It pins the null-input contract the finding names; the finding's behaviour does not change.
2. **Deterministic:** yes. Fixed inputs only: `makeSine(-23.0, 0.5)` (a pure generated sine), `BlockFrames`, and `SampleRate`. No clocks, sleeps, threads or races. It compares values that cannot change when no frames are read.
3. **Fix at the cause and complete:** no fix was made, because the finding is not a defect. Checked every null-input path: `addBlock` → `inPieces` (`src/core/LoudnessReport.cpp:61-75`, offset always 0 for null) → `LufsMeter::processBlock` (`src/core/LufsMeter.cpp:150`, returns on null); and `addPlanarBlock` → `LufsMeter::processPlanar` (`src/core/LufsMeter.cpp:163`, returns on null).
4. **No collateral damage:** no product file changed, so no saved data, project load, limit or caller behaviour can change. The only changed file is `tests/src/core/LoudnessReportTest.cpp`, which is an existing registered test (`tests/CMakeLists.txt:436`, `tests/all-sources.txt:2769`).
5. **Verdict evidence (FALSE-POSITIVE):** `src/core/LoudnessReport.cpp:107-110` (null piece and `frames + at`), `src/core/LoudnessReport.cpp:61-75` (single feed of offset 0), `src/core/LufsMeter.cpp:150` and `:163` (meter refusals), `include/LoudnessReport.h:110-119` (passive read-only contract); C++ [expr.add] ¶4.1 via the secondary sources cited above.

**Gates row FAIL (environment, not content):** `feature_check.py` exits 1 before running any gate with `PermissionError` creating `projects/zene-studio-program/zene-qwen-5095-feature-check`, a directory outside this worktree that the write-jail forbids. Its log is `gates-1.log`. I ran each gate command directly in the worktree (table above): all six simple gates EXIT=0; the two ratchets EXIT=1 for train-wide reasons, with no REGRESSION line naming a file this branch changed. The runner itself needs a writable log directory before it can produce its PASS table here.
