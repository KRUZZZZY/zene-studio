FIXED

# Cluster 1876 — A failed WAV write can delete an existing destination

`ClipConsolidate::render()` wrote its WAV by opening the destination itself with
`sf_open(path, SFM_WRITE, ...)`. `SFM_WRITE` empties the path at open time, before
`sf_writef_float()` or `sf_close()` can fail; the failure branch then called
`QFile::remove(path)`. A short write (full disk, quota, failed device) therefore
destroyed both the failed output and the file that was already at the destination.

Pre-change evidence in this worktree (reverted state, `fix.patch` reversed):
`src/core/ClipConsolidate.cpp:112` `sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info)`
and `src/core/ClipConsolidate.cpp:129` `QFile::remove(path)`.

The call is reachable with a user-named destination: `clip.consolidate` passes an
explicit `out` through `uniqueOutPath()` unchanged
(`src/core/ControlCommandsConsolidate.cpp:147-166` only invents a fresh name for the
*default* path), and `browser.save_clip` passes its `overwrite` target through
`ControlCommandsLibraryClips::writeClip` for sample clips
(`src/core/ControlCommandsLibraryClips.cpp:183`).

## The fix

`src/core/ClipConsolidate.cpp:105-175`:

- `writeWav()` now opens a unique sibling, `<out>.part.<uuid>.wav`
  (`ClipConsolidate.cpp:150-152`). The destination is never opened, so it cannot be
  emptied by a failed write.
- Every failure (open, short write, failed close) removes only the staged file
  (`ClipConsolidate.cpp:155`, `:170`).
- `installWav()` (`ClipConsolidate.cpp:105-129`) moves the completed file over the
  destination. `QFile::rename()` does not replace an existing name, so an existing
  destination is first renamed to a unique `.backup.<uuid>` and restored if the
  install fails; the backup is removed only after the new file has landed. This is
  the same staged-install shape `BounceInPlace::installPreparedOutput()`
  (`src/core/BounceInPlace.cpp:421-452`) uses.
- An existing *directory* at the destination is refused before anything is touched
  (`ClipConsolidate.cpp:138-142`); previously it failed at `sf_open`, and the
  backup path would otherwise have been able to rename a directory aside.

No header, control command, undo step, schema, or save format changed.

## Regression test

`tests/src/core/ClipConsolidateTest.cpp:447` —
`ClipConsolidateTest::aFailedWriteKeepsTheFileThatWasAtTheDestination()`:

1. Renders a real 0.5 s WAV onto the destination and keeps its bytes.
2. Caps `RLIMIT_FSIZE` at 1024 bytes for one scope (ignoring `SIGXFSZ`), so the
   kernel cuts the next render's write short — the full-disk / failed-device case.
3. Requires the render to fail, the destination to still exist and hold **exactly
   the bytes it held before**, and no `*.part.*` file left behind.
4. Restores the limit (scope exit) and requires a later successful render to
   replace the destination wholly, with no `*.part.*`/`*.backup.*` leftovers.

`RLIMIT_FSIZE` is POSIX-only; the test `QSKIP`s elsewhere, exactly like
`ScriptBindingsTest::projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort()`
(same defect class) and `VideoTrackTest`'s `ScopedFileSizeLimit`. The product code
uses only Qt (`QFile`, `QUuid`), no `std::filesystem`, so it stays portable.

## Commands and exit codes (all run in this session)

| # | command | exit | log |
|---|---|---|---|
| 1 | `cmake --build build --target ClipConsolidateTest -j4` (test added, product unchanged) | 0 | `001-build-red.log` |
| 2 | `cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure` | **8** — `FAIL! : aFailedWriteKeepsTheFileThatWasAtTheDestination() 'kept.exists()' returned FALSE. (the failed render removed the file that was at the destination)`; 14 passed, 1 failed | `002-test-red.log` |
| 3 | `cmake --build build --target zene ClipConsolidateTest -j4` | 0 | `003-build-green.log` |
| 4 | `cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure` | **0** — 1/1 passed | `004-test-green.log` |
| 5 | `git diff -- src/core/ClipConsolidate.cpp > logs/fix.patch` | 0 | `fix.patch` |
| 6 | `git apply -R logs/fix.patch` | 0 | — |
| 7 | `cmake --build build --target zene ClipConsolidateTest -j4` (product reverted) | 0 | `005-build-reverted.log` |
| 8 | `cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure` | **8** — same `kept.exists()` failure; 14 passed, 1 failed | `006-test-reverted.log` |
| 9 | `git apply logs/fix.patch` | 0 | — |
| 10 | `cmake --build build --target zene ClipConsolidateTest -j4` | 0 | `007-build-reapplied.log` |
| 11 | `cd build/tests && ctest -R '^ClipConsolidateTest$' --output-on-failure` | **0** | `008-test-reapplied.log` |
| 12 | `cmake --build build --target RangeEditsTest ArrangeToolsTest SessionLibrarySaveTest -j4` | 0 | `009-build-family.log` |
| 13 | `cd build/tests && ctest -R '^(RangeEditsTest\|ArrangeToolsTest\|SessionLibrarySaveTest)$' --output-on-failure` | **0** — 3/3 passed | `010-test-family.log` |
| 14 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** — all eight rows PASS | `011-feature-check.log` |

Every log is in `/home/kruzzzzy/.config/kads/zene/state/runs/1876.r1/logs/`.

## Test matrix (written before editing)

| path | result |
|---|---|
| normal | two renders onto one explicit path: the second replaces the destination wholly (new test, step 4); default paths still never reuse a name (`aDefaultPathIsNeverReused`, passing) |
| failure | kernel short write -> `render` fails, destination bytes unchanged, no `.part` leftovers (new test, RED before the fix) |
| exhaustion | whole-song / whole-tick ranges still refused before allocation (`aRangeTooLargeToRenderIsRefusedBeforeItIsAllocated`, `theWholeSongRangeOfACommandIsRefusedAndChangesNothing`, passing) |
| reconfiguration | the capped scope is restored, and a later render succeeds on the same path (new test, tail) |
| audio thread | `render()`/`writeWav()` run on the control-command thread, not the audio thread; no engine callback or play handle was touched by this change |
| platforms | product code is Qt-only and builds on Windows/macOS/MSVC; the fault injection is POSIX behind `Q_OS_UNIX`, like the existing `RLIMIT_FSIZE` tests |

## Self-check

- **Regression fails reverted, passes restored:** reverting only
  `src/core/ClipConsolidate.cpp` (steps 6-8) gives EXIT=8 with
  `'kept.exists()' returned FALSE`; restoring it (steps 9-11) gives EXIT=0. The
  test also failed EXIT=8 before any product edit (step 2), so it is anchored to
  the defect, not to a later change.
- **Deterministic:** the fault is a 1024-byte `RLIMIT_FSIZE` cap enforced by the
  kernel for one scope; `SIGXFSZ` is ignored so the write returns short instead of
  killing the test process, and the original limit/handler are restored in the
  destructor. No clock, sleep, timing or race is involved.
- **Cause and complete — callers/siblings:** `ClipConsolidate::render()`'s two
  callers (`ControlCommandsConsolidate.cpp:231`, `ControlCommandsLibraryClips.cpp:183`)
  both get the fix. The sibling MIDI path of `writeClip` already commits through
  `QSaveFile` (`ControlCommandsLibraryClips.cpp:203-209`) and is unchanged.
  `BounceInPlace` already stages its render before installing it, so no other WAV
  writer in this command family has the defect; `TrackRecorder` streams live
  recording to a new take and is out of this command family. `tests/fork-sources.txt`
  already lists `src/core/ClipConsolidate.cpp`; `tests/upstream-modifications.txt`
  already lists the test, so registration needed no edit (gate PASS).
- **No collateral damage:** no save format, project load path, schema, argument
  validation, undo step or command id changed. The only new refusal is an existing
  *directory* at the destination, which previously also failed (at `sf_open`) with
  a different message. Successful renders replace the destination wholly, which the
  new test asserts, and a previous file is preserved on every failure. A destination
  that is a symbolic link or hard link is now replaced at that path rather than
  written through, which is the documented atomic-replace behaviour of
  `QSaveFile`/`BounceInPlace`; `browser.save_clip` already refuses symlink targets
  (`ControlCommandsLibraryClips.cpp:235-239`).
- **Not ALREADY-FIXED/FALSE-POSITIVE:** verdict is FIXED; the reverted-state run
  above reaches the old truncating `sf_open`/`QFile::remove(path)` pair.
- **Undo/realtime:** no control command added, no undo behaviour changed; no
  allocation, lock, wait, log or I/O was added to any audio-thread function.
- **Sibling commands:** my commit touches no `ControlCommands*` file, so the
  sibling list the pre-submit check prints (`app.*`, `arrangement.*`, ...) belongs
  to earlier commits on this branch, not to cluster 1876. The only commands that
  reach this code are `clip.consolidate` and `browser.save_clip`, both reached
  through `ClipConsolidate::render()` and both covered above.

## Other findings

- `src/core/audio/TrackRecorder.cpp:171` opens its take file with `SFM_WRITE`
  directly, but that path creates a new take file rather than replacing an
  existing user file, so it is outside this finding.
- `clip.consolidate` with an explicit `out` naming a symlink replaces the link
  itself (atomic-install semantics) instead of writing through it; not changed
  here because it matches the existing `QSaveFile`/`BounceInPlace` contract.
