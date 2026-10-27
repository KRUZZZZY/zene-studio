FALSE-POSITIVE

# Cluster new-p18-28: "Long DrumSynth settings can make `strlen` read past the buffer"

## Verdict

FALSE-POSITIVE. The buffer is always null-terminated inside its `size` bytes, so `strlen(buffer)` cannot read past it. No product change is made.

The branch also has an explicit terminator on the truncated-value path (`src/core/DrumSynth.cpp:337`). It was added by `3bb99a2ac` (cluster new-p11-92-port-port-port) and is not on `master`. It is redundant with the `memset`, so this is hardening, not a fix for a live defect.

## Evidence (file:line, HEAD `639e1d23e`)

`DrumSynth::GetPrivateProfileString` (`src/core/DrumSynth.cpp:257-349`):

- `:261` `maxlen = max(1, size) - 1`. Every copy is bounded by `size - 1`.
- `:278` `std::memset(buffer, '\0', size)`. The whole destination is zeroed before any copy, so `buffer[size - 1]` starts as `'\0'`.
- `:336-337` value path: `strncpy(buffer, valueToken.c_str(), maxlen)` writes at most `maxlen` bytes, then `buffer[maxlen] = '\0'` is set explicitly.
- `:345` default path: `strncpy(buffer, def, maxlen)` writes at most `maxlen` bytes. Index `maxlen` is still `'\0'` from `:278`.
- `:347` `strlen(buffer)`. Nothing writes at or after index `size - 1` except the `'\0'` stores, so the scan stops inside the buffer.

Callers all pass a real array with `sizeof`, so `size >= 16`:

- `:181` `char en[256]`
- `:356` and `:370` `char tmp[16]` (the `Int` and `Float` wrappers)
- `:427` `char ver[32]`
- `:436` `char comment[256]`

On `master`, the same invariant holds without the explicit terminator: `src/core/DrumSynth.cpp:207` (`memset`), `:235` (`strncpy(buffer, b, maxlen)`), `:243` (default path). So the finding is not real on `master` either.

## Commands run and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-28.r1/logs`

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target zene DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4` | 2 | `build-1.log` (ccache temp dir not writable: `Permission denied`) |
| 2 | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target zene DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4` | 0 | `build-2.log` (0 `error:` lines) |
| 3 | `cd build/tests && ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure` | 0 | `test-1.log` (3 of 3 passed: `DrumSynthLevelTest` #111, `SampleBufferResampleTest` #260, `DrumSynthTest` #322) |
| 4 | `python3 -I $TMPDIR/fc-new-p18-28/feature_check_relog.py <worktree>` | 0 | `gates-1.log` (`RESULT: PASS`) |

Notes on the runs:

- Command 1 failed in the environment, not in the code. ccache wrote its temp file under `zene-bunny-fix/dsh/home/ccache-tmp-r6`, which this session cannot write. Command 2 is the same build with `CCACHE_TEMPDIR` in the build tree. No source was changed for it.
- The gate was run from a copy of `feature_check.py` in `$TMPDIR`. The copy is identical except that `ROOT` points at the real gate directory and the log folder is the run's `logs/gates`. The original writes to a sibling of the worktree, `zene-qwen-new-p18-28-feature-check`, which this session cannot create (the original's first attempt: `PermissionError`, EXIT=1). Gate logic, limits and baselines are unchanged. The first, failed attempt's log was overwritten by the successful run.
- The gate reports `0 changed files` (base `639e1d23e`), because this branch has no diff of its own.

## Regression test

None added. The procedure says a not-real verdict skips the RED/GREEN step, and there is no product change to revert. The existing family passes at HEAD (command 3). The truncated-value path is not directly exercised by those tests: `tests/` has one `GetPrivateProfile` reference, and `DrumSynthTest.cpp:440-468` (`refusesAnEnvelopeWhoseRoundedLengthOverflows`) covers envelope overflow instead. I did not mutation-test the `:337` terminator, so I cannot say whether a test would catch its removal. A lock-in test is possible if the owner wants one.

## Self-check

`submit_check.py` (`submit-1.log`, EXIT 1) passed the commit, clean-tree, report, and claim checks. Its gate item failed for the environment reason in the notes above, not for a code reason. The same gate passed when run from the relocated copy (command 4, EXIT 0).

- **Revert-and-fail (RED/GREEN):** not applicable. The verdict is FALSE-POSITIVE and no product file changed, so there is nothing to revert. The family passes unmodified (command 3, EXIT 0).
- **Test fails with the change reverted and passes with it:** not applicable, since there is no product change and no new test. The cited code is the proof (`:261`, `:278`, `:336-337`, `:345-347`).
- **Test is deterministic (no timing, sleeps or races):** not applicable, no test added.
- **Fix at the cause and complete (callers and siblings checked):** not applicable, no fix. The callers that pass a buffer are listed above (`:181`, `:356`, `:370`, `:427`, `:436`), and all of them pass `sizeof` of a real array.
- **No collateral damage (old projects, saved data, limits):** no product file changed, so behaviour is unchanged.
- **If ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or contract:** `src/core/DrumSynth.cpp:261, 278, 336-337, 345-347`, plus the caller sizes `:181, 356, 370, 427, 436`.
- **Edit is one undo step / reversibility class:** no command or product edit made. Only this report is committed.
- **Audio thread:** no product code changed. Nothing new runs on the audio thread.
- **Qt 5.15 / MSVC / macOS portability:** no code added.
- **Sibling commands:** no command file changed, so no sibling set applies.
- **Gate:** `feature_check` PASS from the relocated copy (command 4, EXIT 0). `submit_check`'s own gate step FAILs with `PermissionError` on the sibling log folder `zene-qwen-new-p18-28-feature-check`, which this session cannot create. That is an environment limit, not a code failure. It needs the owner or the loop to run the gate from a location the session can write.
