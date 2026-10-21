# Cluster 2258: sample-rate changes / retired buffers / invalid range

2258: ALREADY-FIXED
new-p14-101: FIXED
new-p16-72: FIXED

## Evidence

- 2258: `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:105` `processImpl()` calls `applyPreparedBuffers()` (swap only, no resize); `:283` `sampleRateNeedsUpdate()` prepares/replaces the ring buffer off the audio thread; `:312` swaps prepared state and retires the old buffer; `:68` reserves grain capacity in the constructor. No `m_sampleRateNeedsUpdate` flag or `changeSampleRate()` remains in the product file (see `docs/050-features/p0fix-rt.md`, Q050-005). Regression `GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift` passes (exit 0).
- new-p14-101: retired buffers were only reclaimed in `sampleRateNeedsUpdate()` (`:285`) or the destructor. Fix adds `m_retiredPending` set in `applyPreparedBuffers()` after retiring (`:344`) and a zero-interval control-thread `QTimer` (`:73-77`) that calls `reclaimRetiredBuffersIfPending()` (`:348-352`), so a retired long buffer is freed without a later update. `reclaimRetiredBuffers()` (`:355-359`) performs the delete off the audio thread.
- new-p16-72: `sampleRateNeedsUpdate()` (`:288-289`) now clamps the range to `[0, std::size(RangeSeconds)-1]` before indexing `RangeSeconds[range]` (`:290`).

## Commands

- Build: `cmake --build build --target granularpitchshifter GranularPitchShifterRealtimeTest` (exit 0)
- Tests: `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^GranularPitchShifterRealtimeTest$' --output-on-failure` (exit 0, 1/1 passed)
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-2258` (exit 0, all PASS)
