FALSE-POSITIVE

# new-p12-194: split crossovers can exceed the output Nyquist frequency

## Verdict
The filters are already limited below Nyquist. The finding says the rack does not limit crossovers to below Nyquist, but the limit lives one layer down, in the biquad design every split stage goes through.

## Evidence
- `src/core/Rack.cpp:142,149` passes `engine->outputSampleRate()` to `RackSplitFilter::configure`.
- `src/core/RackSplit.cpp:102`: every stage (low-pass, high-pass, all-pass) is made by `designBiquad(shape, hz, Q, sampleRate)`.
- `include/BiquadDesign.h:63`: `designBiquad` computes `w0` from `std::clamp(hz, 1.0, 0.45 * sampleRate)`. At 32 kHz that is 14.4 kHz, below the 16 kHz Nyquist, so a 20 kHz crossover is designed at 14.4 kHz. That is the "small safety margin" the finding asks for.
- The band sum stays flat because the low-pass, high-pass and all-pass of one crossover are all clamped to the same frequency. Two crossovers that both clamp (15 kHz and 20 kHz) give a zero-width middle band, and the sum is still an all-pass.
- Empirical check: the new test `DeviceToolsTest::crossoversAboveNyquistStillSumFlatAtLowOutputRate` (`tests/src/core/DeviceToolsTestParts1.h`) builds a 3-band split with crossovers 15000 and 20000 Hz (`RackSplit::MaxCrossoverHz`) at 32 kHz. Every output sample is finite, and the summed RMS at 500, 4000 and 9000 Hz is within 2% of the input. It passes with NO product change, which is why this is a FALSE-POSITIVE: there is no bug to fix and nothing for it to fail on.
- No product file was changed. The test is a guard against a future change to the clamp.

## Commands (from logs)
- `cmake --build build --target DeviceToolsTest -j4 > logs/build-1.log` : EXIT=0
- `cd build/tests && ctest -R DeviceToolsTest --output-on-failure > logs/test-1.log` : EXIT=8. My first version of the test compared the sampled peak, and at 4 kHz / 32 kHz (8 samples per cycle) the phase-shifted crest falls between samples (0.93). That was a test artifact, not a product defect.
- The test was changed to compare RMS. `cmake --build build --target DeviceToolsTest -j4 > logs/build-2.log` : EXIT=0
- `cd build/tests && ctest -R DeviceToolsTest --output-on-failure > logs/test-2.log` : EXIT=0
- `./DeviceToolsTest crossoversAboveNyquistStillSumFlatAtLowOutputRate > logs/test-3.log` : EXIT=0
- `python3 .../feature_check.py <worktree> > logs/gates-1.log` : EXIT=0, RESULT: PASS (all 8 gates)

## Other findings
- The 0.45 x sampleRate clamp is silent: the rack's stored and saved crossover is still the requested value. That is by design and unchanged.
