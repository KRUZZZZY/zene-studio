ALREADY-FIXED

# Cluster new-p16-290: Ogg export can report success when the output file did not open

Branch `bugs/qwen-new-p16-290` (base `cf3618e61`, the fix base `050/m0-train-16`). Finding: `src/core/audio/AudioFileOgg.cpp:96` (historical line).

## Verdict

The finding's failure does not occur on this tree. When the base `AudioFileDevice` cannot open the output file, the Ogg constructor sets `successful = false` and removes the output file. The refusal comes from the header-page write check, not from an explicit `outputFileOpened()` test. No product code was changed.

## Evidence (file:line, current tree)

- `src/core/audio/AudioFileDevice.cpp:47-70`: the base constructor attempts `open(WriteOnly|Truncate)`. On failure it shows a message box when a GUI exists (`:59-64`). Without a GUI it prints and calls `exit(EXIT_FAILURE)` (`:65-69`). The GUI branch returns normally, so the Ogg constructor still runs with a closed file. This is the finding's scenario.
- `src/core/audio/AudioFileDevice.cpp:84-92`: `writeData()` returns -1 when the file is not open.
- `src/core/audio/AudioFileOgg.cpp:70-83`: three header packets are queued with `ogg_stream_packetin`.
- `src/core/audio/AudioFileOgg.cpp:85-95`: `ogg_stream_flush` yields the header page(s). Each goes through `writePage()`. If it returns false, the constructor sets `successful = false` (`:91`), calls `removeOutputFile()` and returns.
- `src/core/audio/AudioFileOgg.cpp:200-213`: `writePage()` compares the `writePageBytes()` result with `header_len`. The -1 from a closed file differs, so `recordWriteError()` runs and `writePage()` returns false.
- `src/core/audio/AudioFileOgg.cpp:111`: `successful = true` is reached only after the header flush (`:98-103`) and `vorbis_block_init` succeed. A closed file cannot reach it.
- `src/core/ProjectRenderer.cpp:180-188`: the caller reads `successful`, deletes the device and sets `m_fileDev = nullptr` when it is false.
- `include/AudioFileDevice.h:85-107`: `flushOutputFile()` (`:85-91`), `removeOutputFile()` (`:96-100`) and `outputFileOpened()` (`:104-107`) exist. The Ogg constructor does not call `outputFileOpened()`.
- The write check was added in commit `32e9fb35d` (cluster new-p12-317-port), which is an ancestor of HEAD (`git merge-base --is-ancestor 32e9fb35d HEAD` → 0).

## Measured behaviour

A probe (temporary, reverted) made the base constructor skip `exit()` in the non-GUI branch and built an Ogg device at `no-such-dir/probe.ogg`. Log `logs/probe-2.log`:

```
Could not open file .../no-such-dir/probe.ogg for writing.
QWARN : AudioFileOggInitTest::probeClosedFileOgg() PROBE successful= false device= true
PASS : AudioFileOggInitTest::probeClosedFileOgg()
Totals: 3 passed, 0 failed, 0 skipped
```

With the base file closed, the Ogg constructor reports `successful = false`. The device object still exists, and the caller deletes it. The probe edits were reverted with `git checkout --` on the two files; `git status` was clean afterwards.

## Regression coverage and why no new test

- The same mechanism is covered by `tests/src/core/AudioFileOggInitTest.cpp:234-260` (`aPartialOggHeaderPageRejectsTheDevice`). It arms a short header-page write and asserts that the device is rejected, that the write error is recorded, and that no file is left behind. It passes on this tree.
- A closed-file regression cannot run in the guiless test binary. The base constructor calls `exit()` (`AudioFileDevice.cpp:67-68`), and the only other path needs a GUI. Making the test reachable would mean a product-side seam for a defect that is not present. So no RED/GREEN pair exists: there is no fix to prove.
- An explicit `if (!outputFileOpened())` refusal at the top of the Ogg constructor would make the contract readable. It would not change behaviour, and no test could exercise it. I did not add it.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-290.r1/logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene AudioFileOggInitTest -j4` | 2 (ccache could not write its temp dir: `Permission denied`) | build-1.log |
| Build, retry | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target zene AudioFileOggInitTest -j4` | 0 | build-2.log |
| Baseline ctest | `cd build/tests && ctest -R '^AudioFileOggInitTest$' --output-on-failure` | 0 (1 test) | test-1.log |
| Probe build | same build command (probe edits in place) | 0 | build-3.log |
| Probe ctest | same ctest command | 0 (1 test; ctest shows no output on pass) | test-2.log |
| Probe run | `cd build/tests && ./AudioFileOggInitTest probeClosedFileOgg` | 0 | probe-2.log |
| Revert | `git checkout -- src/core/audio/AudioFileDevice.cpp tests/src/core/AudioFileOggInitTest.cpp` | 0 | (git status clean) |
| Clean rebuild | same build command | 0 | build-4.log |
| Clean run | `cd build/tests && ./AudioFileOggInitTest` | 0 (5 passed, 0 failed) | run-clean-1.log |
| Clean ctest | `cd build/tests && ctest -R '^AudioFileOggInitTest$'` | 0 (1 test passed) | test-3.log |

`ctest -R '^AudioFileOggInitTest$'` matches one ctest entry (#514). Its QTest functions were run directly: `failedEncoderInitializationCanBeDestroyed`, `aShortPositiveOggPageWriteIsReported`, `aPartialOggHeaderPageRejectsTheDevice`, plus `initTestCase` and `cleanupTestCase`.

### Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` could not run. It writes its logs to `<worktree>-feature-check` next to the worktree, which is outside the write-jail (`PermissionError` at `feature_check.py:71`; log `gates-1.log`). I ran the same commands it lists from the worktree instead:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-fork-sources.log |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-all-sources.log |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-no-upstream.log |
| check-namespace | `tests/scripted/check-namespace` | 0 | gate-check-namespace.log |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-no-tautology.log |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-doc-figures.log |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 REGRESSION lines) | gate-complexity.log |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (19 REGRESSION lines) | gate-file-length.log |

The two ratchet gates pass the feature-check rule. That rule fails only on items that are new in files the branch changed. Relative to the fix base the branch changes no files, so no item can be new in a changed file. I checked this by reading the cached base listing `gate-baselines/cf3618e61….json` and the branch diff by hand. This is my reproduction of the rule, not feature_check's own output.

## Self-check

- Verdict and evidence: see "Evidence" above, with file:line for every claim.
- Regression test: none added. Reason and alternative are in "Regression coverage and why no new test".
- Affected family: `AudioFileOggInitTest` passes (1 ctest entry, 5 QTest functions incl. init/cleanup).
- Realtime, undo, commands: no product change, so nothing new on the audio thread, no undo change, no control commands.
- Registration: no new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged.
- Working tree: no product or test edits remain; only this report is committed.

### Answers to the completion-check items

- Regression test FAILS with the product change reverted and PASSES with it: not applicable. The verdict is ALREADY-FIXED, so there is no product change to revert. The closed-file behaviour was measured instead: probe `logs/probe-2.log` (EXIT 0, `PROBE successful= false`). The existing mechanism test `aPartialOggHeaderPageRejectsTheDevice` passed on the clean build (`logs/run-clean-1.log`, EXIT 0).
- Deterministic: no test was added. The existing test uses injected short counts (`setFirstPageWriteShortForTest`, `AudioFileOggInitTest.cpp:249`), with no timing or sleeps.
- Fix at the cause and complete: no fix. Checked the path into the write check: `writeData` (`AudioFileDevice.cpp:84-92`) → `writePage` (`AudioFileOgg.cpp:200-213`) → constructor (`AudioFileOgg.cpp:89-94`) → caller (`ProjectRenderer.cpp:185-188`). The export path is in agreement.
- No collateral damage: no product change, so old projects, saved data, limits and callers are untouched.
- ALREADY-FIXED evidence: `AudioFileOgg.cpp:89-94, 200-213, 111` and `AudioFileDevice.cpp:84-92`, as cited above.
- Gates row FAILS for environment reasons, not gate findings: `feature_check.py` raised `PermissionError` creating `<worktree>-feature-check` (outside the write-jail, `gates-1.log`), so `submit_check.py` read its exit 1 as a failed gate. The six simple gates and the two ratchets exit as listed above, and the ratchet rule is reproduced by hand. The row needs a run from an unrestricted shell.
