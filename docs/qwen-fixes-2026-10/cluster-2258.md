# Cluster 2258: sample-rate changes / retired buffers / invalid range

Verdict: ALREADY-FIXED (2258), FIXED (new-p14-101, new-p16-72)

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

## Self-check

- Regression RED/GREEN: reverting only `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp` (`git apply -R logs/fix.patch`) and rebuilding made `GranularPitchShifterRealtimeTest` fail the allocation assertion (`callbackAllocations` 6 vs 0, exit 8) for the retired-buffer scheduling; restoring the fix (`git apply logs/fix.patch`) returned exit 0 (1/1 passed). The invalid-range clamp is covered by `invalidRangeIsClamped`, which passes with the fix.
- Determinism: the test uses `AllocationProbe` (`test::tlCountAllocations`/`test::tlAllocationCount`) and `RealtimeScope` around `effect->processAudioBuffer`, no sleeps, wall-clock thresholds only bound the render loop's completion, not correctness.
- Cause/consistency: `applyPreparedBuffers()` is the only audio-thread consumer of prepared state; `sampleRateNeedsUpdate()` remains the only producer; `reclaimRetiredBuffers()` remains the only deleter, now driven by `m_retiredPending` + the control-thread timer. `RangeSeconds` indexing is clamped at the sole use site.
- Collateral: valid ranges 0-4 are unchanged; the clamp only affects out-of-range values, which previously indexed out of bounds. No project format change.
- ALREADY-FIXED (2258): `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:105` swaps prepared buffers without resize; `:283` builds them off-thread; see `docs/050-features/p0fix-rt.md` (Q050-005).
