Verdict: ALREADY-FIXED

new-p14-94: DrumSynth profile reads use uninitialized and potentially unterminated buffers (`src/core/DrumSynth.cpp:257-349`, finding line 292 is now `else if (!is.eof())` in the scan loop at :292).

The finding describes three defects in `GetPrivateProfileString`. All three are fixed on this branch. Both fixes are ancestors of HEAD (`git merge-base --is-ancestor` exits 0 for each):

| Finding claim | Current code | Introduced by |
| --- | --- | --- |
| The output buffer is not initialised, so `buffer[0]` is read for an absent key | `src/core/DrumSynth.cpp:278` `std::memset(buffer, '\0', size);` runs before the scan loop at :280, on every path; the default check at :345 reads initialised memory | `0fcd67f91` (#7709) |
| Copies can fill the buffer with no terminator | `:261` `maxlen = max(1, size) - 1` bounds every copy; `:336` `strncpy(buffer, valueToken, maxlen)` is followed by `:337` `buffer[maxlen] = '\0'` | `3bb99a2ac` |
| `strlen` can scan past the buffer | `:345` the default copy is also bounded by `maxlen`; `strncpy` never writes index `maxlen`, and the memset at :278 leaves it `'\0'`; `:347` `strlen(buffer)` therefore stops at or before `size - 1` | `0fcd67f91`, `3bb99a2ac` |

Callers (all pass `sizeof` of a fixed array, so none passes `size == 0`):

- `GetEnv`: `char en[256]` at `src/core/DrumSynth.cpp:178`, call at :181 (`en[255] = 0` follows at :184).
- Section and version/comment reads: `char ver[32]` at :389 (call :427, `ver[9] = 0` and `ver[11]` read at :428-430 stay inside 32 bytes); `char comment[256]` at :390 (call :436, `comment[commentLen + 1] = 0` at :447 stays inside 256 bytes).
- `GetPrivateProfileInt` and `GetPrivateProfileFloat`: `char tmp[16]` at :353 and :367, calls at :356 and :370. Both go through the same function, so they get the same memset and terminator.

## Commands and results

| Step | Command (run from the worktree unless noted) | EXIT |
| --- | --- | --- |
| Build, default environment | `cmake --build build --target DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4 > .../logs/build-1.log 2>&1` | 2 (ccache could not write to `CCACHE_DIR=/home/kruzzzzy/.cache/ccache-bunny`'s temp path: `Permission denied`; environment, not code) |
| Build, ccache off | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4 > .../logs/build-2.log 2>&1` | 0 |
| Baseline family, unmodified tree | `cd build/tests && ctest -R '^(DrumSynthTest|DrumSynthLevelTest|SampleBufferResampleTest)$' --output-on-failure > .../logs/test-1.log 2>&1` | 0 (3/3 passed) |
| Feature gates via the shared script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | 1: `PermissionError` on `mkdir` of the sibling `zene-qwen-new-p14-94-feature-check`, which is outside the write jail |
| Same script, log dir redirected in a scratch copy | copy of `feature_check.py` in `$TMPDIR` with `logs` pointed at the LOG FOLDER, run with `python3 -I` | 1: `FileNotFoundError` on `ROOT / "FIX-BASE"`, because `ROOT` is taken from the script's own directory. The script could not run here. |
| Gate commands run directly (after the test edit) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` | 0, 0, 0, 0, 0, 0 (logs `gate2-1..6.log`) |
| Gate commands run directly, ratchets | `bash tests/complexity-gate.sh --check`; `bash tests/file-length-gate.sh --check` | 1, 1. `gate2-7.log` lists 7 REGRESSION lines and `gate2-8.log` lists 22, in `WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsMixerTools.cpp` and others. None is in `DrumSynth.cpp` or `DrumSynthTest.cpp`. The counts (7 and 22) are the same before and after the test edit (`gate-7.log`, `gate-8.log`). These ratchets are red on the train on purpose. The ratchet baseline for this base is not in the shared gate-baselines cache, so the script's own ratchet pass could not run here: it would create a worktree in `zene-cachyos`, outside the write jail. |
| Guard test, direct | `build/tests/DrumSynthTest boundedProfileValuesKeepTheirSettings -v2` | 0 (`PASS`, logged in `test-3-direct.log`) |
| Family after the test edit | `cd build/tests && ctest -R '^(DrumSynthTest|DrumSynthLevelTest|SampleBufferResampleTest)$' --output-on-failure > .../logs/test-4.log 2>&1` | 0 (3/3 passed) |

Tests executed in the family: 3 (DrumSynthTest, DrumSynthLevelTest, SampleBufferResampleTest). Not zero.

## What changed

- `tests/src/core/DrumSynthTest.cpp`: one new guard test, `boundedProfileValuesKeepTheirSettings`. A Version value padded to 78 bytes must be cut to the 32 byte buffer and still render the same samples as the unpadded file, and a file without a Version key must render nothing. Both checks go through the public `GetDSFileSamples` path.
- `docs/qwen-fixes-2026-10/cluster-new-p14-94.md`: this report.

No product code changed. The test file is already registered (it is an existing test file), so no entry was needed in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`.

## Why there is no RED run

The defect is absent on this branch, so the procedure's RED step (fail with the fix reverted) does not apply to a product change. The guard passes on the current tree, which is expected. It is a lock-in for the padded-value and missing-key paths, not a proof that the original uninitialised read is caught. Catching that read needs a sanitiser or valgrind, and neither is available here: `which valgrind` finds nothing, and a full ASan build is outside the disk budget. I did not claim a RED result I did not see.

## Residual observations (not changed)

- `src/core/DrumSynth.cpp:273` `QByteArray dat = f.readAll().constData();` builds the copy through the `const char*` constructor, so the profile text ends at its first NUL byte. The result is deterministic and not memory-unsafe, and it is outside this finding. It is left for a separate report.
- `size == 0` would make `buffer[0]` in the memset and default check out of range. No caller passes 0, so this is not reachable today.

## Self-check

- **Regression FAILS with the product change reverted and PASSES with it:** not applicable. There is no product change, so there is no reverted state to compare. The guard passes on the current tree (`test-3-direct.log`, `test-4.log`). It does not demonstrate the pre-fix failure, as explained above.
- **Deterministic:** yes. `boundedProfileValuesKeepTheirSettings` has no timing, sleeps, threads or races. Its fixtures are built in memory from `drumSynthFixture(100, false, false)` and written to a `QTemporaryDir`. Its padding is a fixed 64 `x` characters, and its missing-key case removes the exact `Version=DrumSynth v2.0\n` line.
- **The fix is at the cause and complete:** no fix was made. Every caller of `GetPrivateProfileString` was checked (see Callers above), and each passes a `sizeof` of a fixed array. The shared memset and terminator cover the Int and Float wrappers too.
- **No collateral damage:** no product change, so no saved data or limit is affected.
- **ALREADY-FIXED evidence:** `src/core/DrumSynth.cpp:278` (memset), `:261` (maxlen), `:336-337` (bounded copy and terminator), `:345-347` (default copy and strlen), with the introducing commits `0fcd67f91` and `3bb99a2ac`, both ancestors of HEAD.
- **Feature gates:** the shared `feature_check.py` could not run in this sandbox (see the table). The commands it runs were run directly. Gates 1 to 6 pass. Gates 7 and 8 (ratchets) fail with entries in files this lane did not touch, with the same counts before and after the edit. The completion check's `gates (feature_check.py)` item is therefore red for an environment reason, not a result of this change.
- **Completion check:** see the final message. Its "commits" and "clean tree" items reflect the state at the time it was run; the commit for this report is made after the table above.
