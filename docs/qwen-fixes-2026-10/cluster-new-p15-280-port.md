FIXED

# cluster new-p15-280-port — Failed WAV trimming can destroy an existing target

## Verdict: REAL
`BounceInPlace::trimWav` (`src/core/BounceInPlace.cpp`, was line ~588) opened the target with
`sf_open(target, SFM_WRITE, ...)`, which truncates an existing file immediately, and only then copied
frames. Any failure after that (a write error, a failed close, a short copy) returned false with the
previous good target already destroyed and a partial file in its place. `trimWav` is public, takes an
arbitrary target and its header contract is "returns false and fills error", so a failure must not cost
the caller the file that was there.

Callers: the only product caller is `BounceInPlace::finishPreparedRender` (`BounceInPlace.cpp:411`),
which targets a staged path, so the bounce's own destination was already protected by the staging; the
bug is in `trimWav` itself and applies to every caller. The same-file guard (`namesTheSameFile`) is kept.

## Fix (`src/core/BounceInPlace.cpp`)
New anonymous-namespace helper `writeTrimmed()` writes through a `QSaveFile` (sibling temporary) using
`sf_open_fd(staged.handle(), SFM_WRITE, ...)`, and `staged.commit()` replaces the target only when every
frame was copied and `sf_close` succeeded. On any failure the temporary is discarded and the target is
untouched. `trimWav` calls it (CCN stays low). Note: `sf_open_fd` resets `info->frames` to 0 in write
mode, so the frame count is passed separately. Success results (frames written, file contents) are the
same as before. Behaviour change to note: the target's directory must now allow creating a temporary file.
No header change; no audio-thread code (this runs in the render/bounce path, not the audio callback).

## Test matrix
normal path (`trimWavCopiesTheRangeToAnotherFile`, unchanged, passes); same-file/symlink/hard-link
refusals (existing, pass); failure after open with an existing target (new); exhaustion = write cap
(new); reconfiguration/audio thread: not applicable; platform: new test is POSIX-only (QSKIP on Windows),
production code uses Qt/libsndfile APIs available on all platforms.

## Regression test
`tests/src/core/BounceInPlaceTest.cpp`: `trimWavFailureLeavesAnExistingTargetIntact`. Writes a source and
an existing target, caps the process file size with `RLIMIT_FSIZE` (1024 bytes, SIGXFSZ ignored,
restored by an RAII guard) so the copy fails deterministically after the target is opened, then asserts
trimWav fails with an error, the target's sha256 and frame count are unchanged, and no stray temp file
remains. No timing, sleeps or races.

## Commands (LOGS = /home/kruzzzzy/.config/kads/zene/state/runs/new-p15-280-port.r4/logs)
- RED (test added, no fix): `cmake --build build --target BounceInPlaceTest -j4 > build-1.log` EXIT=0;
  `cd build/tests && ctest -R BounceInPlaceTest --output-on-failure > test-1.log` EXIT=8. test-1.log:
  `FAIL! : trimWavFailureLeavesAnExistingTargetIntact() Compared values are not the same`
  (target sha256 changed), BounceInPlaceTest.cpp:364.
- First GREEN attempt: build-2.log EXIT=0, test-2.log EXIT=8 (my helper read `info->frames` after
  `sf_open_fd` zeroed it; trimWavCopiesTheRangeToAnotherFile got written=0). Fixed by passing the frame
  count; not a test problem.
- GREEN: `cmake --build build --target zene BounceInPlaceTest -j4 > build-3.log` EXIT=0;
  `ctest -R BounceInPlace --output-on-failure > test-3.log` EXIT=0.
- 4b: `git diff -- src/core/BounceInPlace.cpp > $LOGS/fix.patch && git apply -R $LOGS/fix.patch` EXIT=0;
  build-4.log EXIT=0; `ctest -R BounceInPlaceTest -V > test-4.log` EXIT=8 (10 passed, 1 failed: the new
  test, same sha mismatch at line 364). `git apply $LOGS/fix.patch` EXIT=0; rebuild build-5.log EXIT=0;
  `ctest -R BounceInPlace -V > test-5.log` EXIT=0 (11 passed, 0 failed, 0 skipped).
- Gates: `python3 .../feature_check.py <worktree> > gate-1.log` EXIT=0, `RESULT: PASS` (all rows PASS,
  including complexity and file-length).
- Registration: `tests/upstream-modifications.txt` entry for `src/core/BounceInPlace.cpp`; the test file
  is already in `tests/fork-sources.txt`.

Delegation: Haiku ran every build and ctest/gate run (build-1..5, test-1..5, gate-1) and reported
exit codes and slot results; I wrote the test and the fix, did the patch revert/re-apply myself and
checked the logs' outcomes against Haiku's reports.

## Unverified
Local TSan not available; no concurrency is involved. Windows build not run (code uses portable Qt and
libsndfile calls; the new test is skipped there).

## Other findings
- `finishPreparedRender` trims into a staged path, then `installPreparedOutput` swaps it in; that path was
  already safe, no change needed.

## Self-check
- Red/green: with `src/core/BounceInPlace.cpp` reverted (`git apply -R fix.patch`) the new test fails
  (ctest EXIT=8, test-4.log, sha256 mismatch at BounceInPlaceTest.cpp:364); with the fix it passes
  (ctest EXIT=0, test-5.log, 11 passed / 0 failed / 0 skipped).
- Deterministic: the failure is injected with `setrlimit(RLIMIT_FSIZE)` plus `SIGXFSZ` ignored, so the
  write fails by a fixed byte cap; no clocks, sleeps, threads or races. Limit and signal handler are
  restored by an RAII guard.
- Cause and completeness: the cause is opening the final target for writing (truncation) before the copy
  succeeds. `trimWav` is the only writer of that kind (`rg trimWav src`: one product caller,
  `BounceInPlace.cpp:411`, plus the tests); the self/symlink/hard-link refusals and clamping are unchanged
  and their tests pass. The sibling staged/backup install path (`installPreparedOutput`) already uses
  rename-based replacement.
- Collateral: no saved data or project format is touched. Successful trims produce the same WAV (same
  frames, format and error behaviour). One deliberate difference: the target's directory must allow a
  temporary file to be created next to it; a target already in an unwritable directory used to be
  rewritten in place and now fails cleanly. No size limit or check was added.
- ALREADY-FIXED / FALSE-POSITIVE: not applicable, the verdict is FIXED.
- Sibling commands: this change touches no command file (only `src/core/BounceInPlace.cpp`, its test, and
  the registration list). The `app.*` / `arrangement.*` commands the check lists come from the branch's
  earlier merged work, not from this fix, and are not affected: `rg` shows none of them call `trimWav`.
