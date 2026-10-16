# Cluster new-p12-315

## Verdict
FIXED

## Summary
Audio playback (`AudioFileProcessor::playNoteImpl`) previously emitted `isPlaying` directly from the render thread on every period. A signal delivered to a GUI slot from the render thread is queued by Qt, which allocates and accumulates. The render thread now publishes the playhead to an atomic, and the GUI poll timer reads/coalesces it before emitting `isPlaying`.

## Evidence
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:161-175` (after fix) calls `publishPlayhead(...)` instead of `emit isPlaying(...)`.
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp:186-198` implements `publishPlayhead` / `pollPlayhead` with an atomic + GUI-thread-only last-emitted value.
- `plugins/AudioFileProcessor/AudioFileProcessorView.cpp:142-164` creates a GUI-thread `QTimer` (`audioFileProcessorPlayheadPoll`, 20 ms) invoking `pollPlayhead`, plus common-name aliases.
- `plugins/AudioFileProcessor/AudioFileProcessor.h` adds `std::atomic<f_cnt_t> m_playheadFrame`, `m_playheadShown`, `publishPlayhead`, `pollPlayhead`, a forward declaration and `friend class gui::AudioFileProcessorView`.

## Proof (temporary regression probe)
Temporary test `tests/src/core/AudioFileProcessorPlayheadProbeTest.cpp` was added and registered, then removed after proof.

RED (buggy code):
- Command: `ctest -R '^AudioFileProcessorPlayheadProbeTest$' --output-on-failure` (run from `build/tests`)
- Exit code: `8` (`FAIL ... Compared values are not the same` because the render thread emitted `isPlaying` during `renderPeriod()`)

GREEN (fixed code):
- Command: `ctest -R '^AudioFileProcessorPlayheadProbeTest$' --output-on-failure` (run from `build/tests`)
- Exit code: `0` (`1/1 Test ... Passed`; `100% tests passed`)

Reproduction of the fix catching the bug (step 4b):
- Save patch: `git diff -- plugins/... > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-315.r1/logs/fix.patch`
- Revert: `git apply -R .../fix.patch` (product files reverted; CMake/test registration kept), rebuild `audiofileprocessor` + test, run ctest → exit code `8`
- Restore: `git apply .../fix.patch`, rebuild, run ctest → exit code `0`

## Gates
- `bash tests/complexity-gate.sh --check`: exit code `1`. No AudioFileProcessor function is newly flagged; failures are pre-existing branch violations (`lmms::control::rotateProjectRevision`, `lmms::importedOverrides`, and 66 other grandfathered entries).
- `bash tests/file-length-gate.sh --check`: exit code `1`. No AudioFileProcessor file is newly over the limit; failures are pre-existing branch entries not present in `tests/file-length-baseline.tsv`.

## Files committed
- `plugins/AudioFileProcessor/AudioFileProcessor.cpp`
- `plugins/AudioFileProcessor/AudioFileProcessor.h`
- `plugins/AudioFileProcessor/AudioFileProcessorView.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-315.md`
