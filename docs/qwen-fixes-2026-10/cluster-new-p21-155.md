FALSE-POSITIVE

new-p21-155: `ExportDither::ditherFrames` "hard-codes DEFAULT_CHANNELS" (src/core/ExportDither.cpp:105)

Evidence:

- The finding's premise, that a `SampleFrame` can carry a channel count other than `DEFAULT_CHANNELS`, cannot hold for this type. `SampleFrame` stores exactly `DEFAULT_CHANNELS` samples in a fixed array: [include/SampleFrame.h:189](include/SampleFrame.h:189) (`std::array<sample_t, DEFAULT_CHANNELS> m_samples;`), indexed directly by `operator[]` at [include/SampleFrame.h:96](include/SampleFrame.h:96) and [include/SampleFrame.h:101](include/SampleFrame.h:101). `DEFAULT_CHANNELS` is `2` at [include/lmms_constants.h:38](include/lmms_constants.h:38). So the loop bound at [src/core/ExportDither.cpp:105](src/core/ExportDither.cpp:105) equals the frame's full extent: every slot of every frame is dithered, and no slot is skipped or read out of bounds.
- `ditherFrames` takes no channel count, and the frame has none of its own to derive one from. Its signature at [src/core/ExportDither.cpp:99](src/core/ExportDither.cpp:99) and [include/ExportDither.h](include/ExportDither.h) matches that.
- The only caller that reaches `ditherFrames` is the 16-bit stereo-or-mono path, [src/core/audio/AudioFileWave.cpp:220](src/core/audio/AudioFileWave.cpp:220). It is reached only through `writeBuffer`, and `ProjectRenderer` routes every render with more than `DEFAULT_CHANNELS` channels away from `writeBuffer` ([src/core/ProjectRenderer.cpp:317-319](src/core/ProjectRenderer.cpp:317): `m_channels > DEFAULT_CHANNELS ? writeSurround(...) : m_fileDev->writeBuffer(...)`).
- Surround output (more than 2 channels) takes `writeInterleaved`, which dithers the whole interleaved array by `frames * channels()` samples: [src/core/audio/AudioFileWave.cpp:264](src/core/audio/AudioFileWave.cpp:264) and [src/core/audio/AudioFileWave.cpp:269](src/core/audio/AudioFileWave.cpp:269), via `ditherInterleaved` (which has no per-frame loop). So the surround path is not affected either.
- Mono (`channels() == 1`) goes through `writeBuffer` too. `ditherFrames` then also dithers the unused second slot, which consumes RNG draws. `convertDitheredToS16` reads only `_channels` slots ([src/core/audio/AudioFileWave.cpp:85-93](src/core/audio/AudioFileWave.cpp:85)), so the output bytes stay deterministic and no channel is skipped. Changing that would alter dithered mono output, which is outside this finding, so I did not change it. It is noted here as an observation, not a defect.
- The intended contract is already pinned by the existing test: `tests/src/core/ExportDitherTest.cpp:372-383` asserts that `ditherFrames` applies exactly the offsets `nextOffset()` draws, one per slot in turn, for both `[0]` and `[1]` of each frame.

No product code was changed. A regression that fails without a fix does not apply, because there is no bug to reproduce, so step 4b (revert and re-run) does not apply.

Commands and results (all run from the worktree; logs in the LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-155.r1/logs`):

| Evidence | Command | Result |
| --- | --- | --- |
| Build the three dither test targets (first attempt) | `cmake --build build --target ExportDitherTest ExportNoiseShaperTest ExportWavDitherTest -j4 > .../001-build-tests.log 2>&1; echo EXIT=$?` | EXIT=2: ccache could not create its temp file (`Permission denied`) in the shared dsh ccache dir; no code error |
| Same build with ccache disabled for this invocation only | `CCACHE_DISABLE=1 cmake --build build --target ExportDitherTest ExportNoiseShaperTest ExportWavDitherTest -j4 > .../002-build-tests-noccache.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Baseline run of the dither test family, from `build/tests` | `ctest -R '^(ExportDitherTest\|ExportNoiseShaperTest\|ExportWavDitherTest)$' --output-on-failure > .../003-ctest-baseline.log 2>&1; echo EXIT=$?` | EXIT=0; 3/3 tests run and passed (ExportDitherTest, ExportNoiseShaperTest, ExportWavDitherTest) |
| Gate as shipped | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../004-feature-check-pre.log 2>&1; echo EXIT=$?` | EXIT=1 before any gate ran: `logs.mkdir` on the sibling directory `zene-qwen-new-p21-155-feature-check` raised `PermissionError` (outside the write-jail). Not a gate result. |
| Gate command list, run with logs in the LOG FOLDER | `python3 -I .../gate_equiv.py <worktree> .../005-gates > .../005-feature-check-equivalent.table 2>&1; echo EXIT=$?` (`gate_equiv.py` imports `feature_check` and runs its own `SIMPLE` and `RATCHET` lists and `run()` helper unchanged) | EXIT=0; all rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures exit 0; complexity and file-length have raw exit 1 but 0 items in changed files (table: `005-feature-check-equivalent.table`) |

Notes on the gate run: `feature_check.py` was not edited and its limits were not changed. The `gate_equiv.py` wrapper only changes where logs are written. Its ratchet filter uses the same changed-file set (`base..HEAD` plus working-tree status). Before this commit the set was empty, so no items were counted in changed files. After this commit the only changed file is this report, which contains no source items.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-155.md`. No product, test or manifest file changed.

## Self-check

Completion check: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-155` (pre-report run `006-submit-check-pre.log`, EXIT=1; final run recorded below).

- The regression test fails with the product change reverted and passes with it (exit codes): not applicable. There is no product change: the verdict is FALSE-POSITIVE, and the existing family run is 3/3 pass (`003-ctest-baseline.log`, EXIT=0).
- The test is deterministic: no product test was added or changed. The existing `ExportDitherTest` uses a fixed seed (`ExportDither` default seed, `include/ExportDither.h`), so its offsets are reproducible.
- The fix is at the cause and complete: not applicable (no fix). The callers of `ditherFrames` and `ditherInterleaved` were checked above; each covers all slots or uses the full interleaved count.
- No collateral damage: nothing changed in product code, so old projects and saved data load exactly as before.
- Verdict evidence: `src/core/ExportDither.cpp:105`, `include/SampleFrame.h:189`, `include/lmms_constants.h:38`, `src/core/ProjectRenderer.cpp:317-319`, `src/core/audio/AudioFileWave.cpp:220`, `tests/src/core/ExportDitherTest.cpp:372-383`.
- Gates: see the table above. The gate item in `submit_check` can fail for reasons outside this commit. That is why `feature_check.py` itself gave EXIT=1 before any gate ran (its log directory is outside the write-jail). The equivalent run is EXIT=0.
