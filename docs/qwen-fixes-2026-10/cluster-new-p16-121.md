Verdict: FIXED

new-p16-121: FIXED. A MIDI track body larger than the 32-bit SMF chunk length was written with a wrapped length header.

## Finding status

The finding cites `plugins/MidiExport/MidiFile.hpp:290`, which has moved. On the base commit `639e1d23e` the defect is in `MIDITrack::writeToBuffer`: the body is written and the chunk length is `writeBigEndian4(uint32_t(body.size()), out)` with no size comparison anywhere in the path. I confirmed the absence of a check by reading the whole function at base (`plugins/MidiExport/MidiFile.hpp:295-310`, with the unchecked cast at `:308`).

The defect is real in the code. How often a real project reaches it is not measured: a body over 4 GiB is on the order of 10^8 events. The "high" confidence in the finding applies to the code path, not to practical frequency.

## Fix

- `plugins/MidiExport/MidiFile.hpp:42` `SmfMaxChunkBody = 0xFFFFFFFF`, the largest value the 32-bit chunk length field can record.
- `plugins/MidiExport/MidiFile.hpp:301` `MIDITrack::writeToBuffer(out, maxBody = SmfMaxChunkBody)` returns `bool`. `:310` refuses a body above `maxBody` before any byte of the chunk is appended. `:315` the cast is now always in range.
- `plugins/MidiExport/MidiExport.cpp:95-102` the chunk writer records a refusal and writes nothing further into the file. `:281` folds the refusal into `writeOk`, so the export reports `Incomplete` (`src/core/Song.cpp:3032`), and the caller removes the partial file (`src/core/ControlCommandsProjectLifecycle.cpp:469`).
- `plugins/MidiExport/MidiFile.hpp:100-110` `MIDIHeader::writeToBuffer` returns `bool` (always `true`) so the shared chunk writer stays uniform.

Commit split: `bcbaf5395` is a behaviour-preserving seam (bool return, the limit parameter, no check). The fix, test and this report are in the following commit. The split means that reverting only the fix in step 4b is a behavioural failure, not a compile error.

## Regression test

`tests/src/core/MidiExportTest.cpp:332` `aTrackBodyBeyondTheChunkLengthIsRefused`. The test includes the header by relative path (`tests/src/core/MidiExportTest.cpp:57`), the same pattern as `tests/src/plugins/VstEffectControlsTest.cpp:24`. It:

1. checks `SmfMaxChunkBody == 0xFFFFFFFF`;
2. builds a real track and checks the chunk header records the real body length;
3. calls the real `writeToBuffer` with a limit one byte under the body: it must return false and leave the output vector exactly as it was (`{0xAA}`);
4. calls it with a limit equal to the body: the output must equal the unlimited write byte for byte.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal track, no limit | step 2 and the 10 existing `MidiExportTest` functions (export output unchanged) |
| Exact limit boundary | step 4 |
| One byte over the limit | step 3 (refused, output untouched) |
| Header records the real length | step 2 |
| Export-level refusal (`tryExport` returns `Incomplete`, caller removes the file) | **Not covered by a test.** A 4 GiB track cannot be built inside a unit test. Verified by reading `Song.cpp:3031-3032` and `ControlCommandsProjectLifecycle.cpp:469`. |
| Realtime / audio thread | Not applicable: MIDI export runs on the command/UI path. Nothing on the audio thread is touched. |
| Platforms | The limit is `uint64_t`, so the comparison is correct where `size_t` is 32 bits. |

Determinism: no wall-clock time, sleep or race. The only varying input is the injected `maxBody` value, and the body size is derived from the same track in the same test.

## Registration

No new files. `MidiExportTest.cpp` is already in `tests/fork-sources.txt:2446` and `tests/all-sources.txt:2784`. The two product files are already in `tests/upstream-modifications.txt:2198` (`MidiExport.cpp`) and `:2200` (`MidiFile.hpp`), with reasons that cover this writer.

## Evidence

All commands were run from the worktree or from `build/tests` as stated. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-121.r1/logs/`. `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp` was set on every build after the first (see Self-check).

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Baseline build | `cmake --build build --target MidiExportTest midiexport -j4 > build-0-baseline.log` | 2 | ccache could not create a temp file in the sandboxed cache path (`Permission denied`). Environmental, no code error. |
| Baseline build (retry) | same, with `CCACHE_TEMPDIR` set, `> build-1-baseline.log` | 0 | 0 `error:` lines |
| Baseline test | `ctest -R MidiExportTest --output-on-failure > test-0-baseline.log` | 0 | 1/1 passed |
| Seam (commit A) | build `build-2-seam.log`; `ctest -R MidiExportTest > test-2-seam.log` | 0 / 0 | 1/1 passed, behaviour unchanged |
| **RED** | build `build-3-red.log`; `ctest -R MidiExportTest --output-on-failure > test-3-red.log` | build 0 / **test 8** | `FAIL! MidiExportTest::aTrackBodyBeyondTheChunkLengthIsRefused() '!track.writeToBuffer(out, body - 1)' returned FALSE`, `Loc: MidiExportTest.cpp(348)`. Totals: 10 passed, 1 failed. |
| **GREEN** | build `build-4-green.log`; `ctest -R MidiExportTest --output-on-failure > test-4-green.log` | build 0 / **test 0** | 1/1 passed |
| 4b: write fix patch | `git diff -- plugins/MidiExport/MidiFile.hpp plugins/MidiExport/MidiExport.cpp > logs/fix.patch` | 0 | 43 lines |
| 4b: revert fix | `git apply -R logs/fix.patch` | 0 | working tree returned to commit A (`git diff` empty) |
| 4b: RED | build `build-5-4b-reverted.log`; `ctest -R MidiExportTest -V --output-on-failure > test-5-4b-reverted.log` | build 0 / **test 8** | same FAIL at the same assertion; 10 passed, 1 failed |
| 4b: reapply fix | `git apply logs/fix.patch` | 0 | |
| 4b: GREEN | build `build-6-4b-reapplied.log`; `ctest -R MidiExportTest -V --output-on-failure > test-6-4b-reapplied.log` | build 0 / **test 0** | `PASS MidiExportTest::aTrackBodyBeyondTheChunkLengthIsRefused()`; Totals: 11 passed, 0 failed |
| Affected family | `ctest -R MidiExportTest` | 0 | 1 ctest test (#346), 11 QTest functions |
| Neighbours build | `cmake --build build --target ControlSuccessPathsTest SafeStartLoadPathTest -j4 > build-7-neighbours.log` | 0 | 0 `error:` lines |
| Neighbours test | `ctest -R "ControlSuccessPathsTest\|SafeStartLoadPathTest\|MidiExportTest" --output-on-failure > test-7-family.log` | 8 | `SafeStartLoadPathTest` passed (#95); `MidiExportTest` passed (#346); `ControlSuccessPathsTest` failed (#152), 15 passed and 3 failed. See Self-check. |
| Neighbour rerun | `ctest -R ControlSuccessPathsTest --output-on-failure > test-8-csp-rerun.log` | 8 | the same 3 failures (deterministic) |
| Gate, as written | `python3 .../feature_check.py <worktree> > gate-1-feature-check.log` | 1 | Crashed before any check: `PermissionError` creating `<worktree>-feature-check`, outside the write-jail. |
| Gate, same checks, logs redirected | `python3 -I $TMPDIR/gate-driver-p16-121/run_gate.py <worktree> > gate-2-driver.log` | 0 | `RESULT: PASS`. Six simple gates exit 0 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures). Complexity and file-length: no new item in the changed files. |

## Self-check

1. **RED fails, GREEN passes (exit codes).** RED: `ctest` exit 8 with the fix reverted by `git apply -R`, failing at `MidiExportTest.cpp:348`. GREEN: `ctest` exit 0 with the fix applied, 11/11 functions. The procedure's 4b revert was run on the product diff only. The test file was not changed between RED and GREEN.

2. **Deterministic.** No timing, sleep or race. The limit is an injected parameter (`maxBody`, `MidiFile.hpp:301`). Every value in the test is computed from a track built in the test.

3. **Fix at the cause; callers and siblings checked.**
   - Callers of `MIDITrack::writeToBuffer`: one, `MidiExport.cpp:101` (`writeChunk`). Checked with `rg -n "writeToBuffer" --glob '!build/**'`.
   - `Event::writeToBuffer` (`MidiFile.hpp:133`) is a different class and is unchanged.
   - `MIDIHeader::writeToBuffer` is called through the same `writeChunk` (`MidiExport.cpp:109`). It now returns `bool` and is always `true`.
   - **Not fixed (sibling, different module):** `src/core/SmfInterchange.cpp:293` writes `appendU32(bytes, static_cast<quint32>(body.size()))` with no check. That is the SMF interchange export (`zene_interchange_smf_export`), a separate writer in `src/core`, not the function the finding names. It has the same defect and should be a follow-up cluster.
   - **Not fixed (different field, not tested):** `writeVarLength` (`MidiFile.hpp:48-69`) can emit 5 bytes for a delta of 2^28 ticks or more, which is not valid SMF. A delta that large is about 559,000 quarter notes of gap inside one track. I did not measure whether any project can reach it.
   - **Not fixed (not tested):** `MIDIHeader` stores track count as `uint16_t` (`MidiFile.hpp:93`). More than 65,535 tracks would wrap. Unlikely; not changed.

4. **No collateral damage.** The check fires only for a body above `0xFFFFFFFF` bytes. For every smaller body the bytes written are identical: the boundary test asserts exact equality, and the 10 existing `MidiExportTest` functions still pass. Export is the only consumer of this writer, so saved projects and project load paths are not touched.

5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** Not applicable beyond the code citation in "Finding status".

**Open items in this sandbox (not hidden):**
- `feature_check.py` as written exits 1 here, because it must create `<worktree>-feature-check` next to the worktree, which the write-jail forbids. It runs no check before failing. I ran the same six simple checks and the two ratchet checks through the script's own functions in `gate-driver-p16-121/run_gate.py`, with logs in the LOG FOLDER (`gates/`). The gate logic, limits and baselines are unchanged. The `submit_check.py` gate item will fail here for the same permission reason.
- `ControlSuccessPathsTest` fails 3 of 18 functions, deterministically, in `controller.template_save` (`tests/src/core/ControlSuccessPathsTest.cpp:239`) and in `removeRecursively` on `document-templates` (`:516`, `:549`). These run before any export. The template write targets `~/Documents/Zene Studio/presets/controller-templates/`, outside the worktree. I did not run this test on the base commit, so I cannot say whether it fails there. The assertions do not pass through `MidiExport.cpp` or `MidiFile.hpp`.
- The first baseline build failed on a ccache temp-file permission error. Environmental; the rebuild with `CCACHE_TEMPDIR` set succeeded with no code change.
