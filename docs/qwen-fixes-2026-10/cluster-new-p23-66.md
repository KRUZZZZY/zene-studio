FALSE-POSITIVE

# Cluster new-p23-66 - WAV writer overflow check (src/core/ControlStemWav.cpp:77-86)

Finding: "WAV writer checks overflow only after the multiplication has overflowed" - a frame count near 2^61 would wrap
`dataBytes` in `quint64` before the `36 + dataBytes > UINT32_MAX` test, so the bounds test would pass.

## Verdict: FALSE-POSITIVE

The multiplication cannot overflow for any buffer this function can receive, so the check order the finding asks for is
not needed. The 32-bit RIFF bound is already enforced before anything is written.

## Evidence (file:line)

- `src/core/ControlStemWav.cpp:77` - `frames = static_cast<quint64>(buffer.size())`.
- `include/SampleBuffer.h:47,88,99` - `SampleBuffer::size()` returns `std::vector<SampleFrame>::size_type`, and the
  storage is `std::vector<SampleFrame> m_data`. So `frames` is bounded by `std::vector<SampleFrame>::max_size()`.
- Probe (`$TMPDIR`, g++ -std=c++20, log `probe-run-1.log`): `std::vector<SampleFrame>::max_size()` = 1152921504606846975
  (2^60 - 1) on this toolchain. At that size `frames * 2 * (32/8)` = 9223372036854775800 < 2^64, and `36 + dataBytes`
  does not wrap. A frame count of 2^61 is not constructible as a `SampleBuffer`; the finding's premise does not occur.
- `src/core/ControlStemWav.cpp:81-86` - the RIFF bound: `36 + dataBytes > std::numeric_limits<quint32>::max()` refuses
  the write and sets `*error` before `QSaveFile` is opened (`:89`). The largest accepted frame count is 536870907
  (`(2^32-1-36)/8`, probe output), i.e. the check is the correct classic-RIFF limit.
- `src/core/ControlStemWav.cpp:100,107` - the `quint32` casts of `36 + dataBytes` and `dataBytes` happen only after that
  check passes, so the header cannot wrap.

## Callers (checked, consistent)

- `src/core/ControlStemSupport.cpp:374` (stem result write loop) - on `false` it returns `false` with the error set.
- `src/gui/StemSplitController.cpp:241` (GUI export of a clip) - on `false` it releases the temp file and returns `false`.

Both callers handle the refusal; nothing else writes a stem (`ControlStemWav.cpp:2-8`).

## Siblings

- `src/core/ControlStemWav.cpp:104` - `sampleRate * kWavChannels * (kWavBits / 8)` is `int` arithmetic. It overflows only
  for sample rates above 268435455 Hz. On the stem path the rate is the fixed `StemModelSampleRate = 44100`
  (`include/StemSeparation/StemTypes.h:74`, set at `src/core/ControlStemSupport.cpp:248`). The GUI path passes
  `buffer->sampleRate()` (`src/gui/StemSplitController.cpp:241`); I did not trace where that buffer's rate is bounded,
  so this is unverified for that path. It is not part of the finding and I did not change it.

## Commands run and exit codes

| Command | EXIT | Log (LOG folder) |
| --- | --- | --- |
| `g++ -std=c++20 -Wall -o probe probe.cpp` (in `$TMPDIR/p23-66-probe`) | 0 | `probe-build-1.log` |
| `./probe` | 0 | `probe-run-1.log` |
| `python3 .../qwen/feature_check.py <worktree>` (as given) | 1 | `gates-1.log`: `PermissionError` creating `<worktree>-feature-check` next to the worktree, outside the write jail; no gate ran |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce-2.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression-3.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace-4.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-5.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-6.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-7.log` (6 REGRESSION lines, none in a file this branch changes) |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-8.log` (22 REGRESSION lines, none in a file this branch changes; "baseline not written" in check mode) |
| `python3 -I feature_check_logpath.py <worktree>` (same script; only the ROOT and log path changed so the log folder is writable) | 0 | `feature-check-table-1.log`: all 8 rows PASS, `RESULT: PASS` |

The table in `feature-check-table-1.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures PASS; complexity and file-length PASS ("no new item in your files", 0 changed files).

The gates were run from the worktree after the probe. The report file was added after them, so `doc-figures` was re-run
(`gate-doc-figures-9.log`, EXIT 0) and the table re-run with the report present (`feature-check-table-2.log`, EXIT 0,
`RESULT: PASS`, 1 changed file: this report).

## Tests

No product code changed and no test was added. The finding is not real, so there is no fix for a regression test to
prove. No ctest run and no build were needed; the build tree was not touched.

## Self-check

Answers to the completion-check items. No product change, so the regression-test items are answered as not applicable.

- **Regression test FAILS with the change reverted and PASSES with it:** not applicable - no product change, and no test
  was added. The verdict is FALSE-POSITIVE, so there is no fix to revert (procedure step 2). Evidence that the code is
  already correct: `ControlStemWav.cpp:77,81-86` and `probe-run-1.log`.
- **Test is deterministic:** not applicable - no test added.
- **Fix is at the cause and complete (callers and siblings checked):** not applicable - no fix. Callers
  `ControlStemSupport.cpp:374` and `StemSplitController.cpp:241` handle the refusal (see Callers). Sibling
  `ControlStemWav.cpp:104` (int product) was checked and left unchanged; it is unreachable on the stem path
  (fixed 44100 Hz) and unverified for the GUI path (see Siblings).
- **No collateral damage:** holds by construction - no code changed, so no old project, saved data or other caller is
  affected.
- **Verdict evidence (file:line):** `ControlStemWav.cpp:77` (quint64 size from `buffer.size()`), `include/SampleBuffer.h:47,88,99`
  (size bounded by `std::vector<SampleFrame>`), `probe-run-1.log` (max_size 2^60-1; no wrap), `ControlStemWav.cpp:81-86`
  (32-bit RIFF bound checked before the write).

### Completion check result (`submit_check.py`, log `submit-check-1.log`, EXIT 1)

- PASS: commits on the branch; working tree clean; report present with the verdict in its first line; claim-only closure
  (tests row PASS as a claim-only closure).
- FAIL: `gates (feature_check.py)`. This is an environment blocker, not a gate failure. `feature_check.py` writes its logs
  to `<worktree>-feature-check`, next to the worktree in `projects/zene-studio-program/`, which is outside this session's
  write jail. Running it as given fails with `PermissionError` (`gates-1.log`, EXIT 1). I did not edit the gate script.
  The same gate logic, with only the ROOT and log path redirected into the run folder, passes all 8 rows with `RESULT: PASS`
  (`feature-check-table-2.log`, EXIT 0). The individual gate commands each exit 0 except the two ratchets, which exit 1 with
  REGRESSION lines only in files this branch does not change (`gate-complexity-7.log`, `gate-file-length-8.log`).
  Resolving the FAIL needs the gate run in an environment where its log folder is writable.
- The 5 "?" items are answered in the list above. Item 1 and item 2 are not applicable because no test was added.
