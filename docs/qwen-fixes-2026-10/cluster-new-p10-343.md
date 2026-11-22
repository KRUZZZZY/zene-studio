FIXED

# cluster new-p10-343: saving a large embedded sample could truncate its Base64 data

## Evidence the bug was real
- Before: `src/core/SampleBuffer.cpp` `toBase64()` did `static_cast<int>(m_data.size() * sizeof(SampleFrame))` and
  built `QByteArray{data, size}`. Past INT_MAX bytes (about 268M stereo frames of 8 bytes) the count goes negative
  or wraps; exactly 2^29 frames wraps to 0, so the project saved an empty `sampledata`.
- Callers: `plugins/AudioFileProcessor/AudioFileProcessor.cpp:314`, `plugins/SlicerT/SlicerT.cpp:611`
  (via `Sample::toBase64`, `include/Sample.h:115`). Both go through the one function, so both are fixed.
- Load side `SampleBuffer::fromBase64` already uses `qsizetype` sizes (`bytes.size()`), no change needed.

## Fix
- `include/SampleBuffer.h`, `src/core/SampleBuffer.cpp`: new `static base64SourceBytes(size_t frames)
  -> std::optional<qsizetype>`. It returns the exact byte count in `qsizetype`, or nullopt if the count or its
  Base64 text (as a QString) could not be held (limit 3/16 of qsizetype max; only reachable on 32-bit builds).
  `toBase64()` uses it, warns and returns an empty string on refusal instead of silently wrapping, and views the
  frames with `QByteArray::fromRawData` instead of copying them (saves a full second copy of a huge buffer).
- Small samples: byte-identical output (round-trip case in the test).
- No audio-thread code, no control command, no undo change.

## Regression test
`tests/src/core/SampleBufferBase64Test.cpp` (registered in tests/CMakeLists.txt, tests/fork-sources.txt; product
files in tests/upstream-modifications.txt). The size arithmetic is a static function, so 300M frames and 2^29
frames are tested without allocating gigabytes. No timing.
The seam was committed first with the old int cast (commit "Add SampleBuffer::base64SourceBytes seam..."), so the
4b reverse of the fix patch yields a compiling RED build.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p10-343.r1/logs)
- RED (seam with int cast), `cd build/tests && ctest -R SampleBufferBase64Test --output-on-failure` -> test-red.log, EXIT=8
  (countAboveIntMaxIsNotWrapped, countThatWrapsToSmallIsNotTruncated, countThatCannotBeHeldIsRefused fail; actual 0 vs expected 4294967296).
- GREEN, same command -> test-green.log, EXIT=0.
- 4b: `git diff -- src/core/SampleBuffer.cpp > fix.patch && git apply -R fix.patch` (REVERT=0), rebuild (EXIT 0),
  ctest -> test-4b-red.log RED_EXIT=8; `git apply fix.patch` (APPLY=0), rebuild (EXIT 0), ctest -> test-4b-green.log GREEN_EXIT=0.
- Header changed: `cmake --build build --target zene SampleBufferBase64Test SampleBufferNoEngineTest SampleBufferResampleTest SlicerTWaveformTest -j4` -> build-9.log EXIT=0.
  A full `cmake --build build` is refused by the bunny-guard (build-8-all.log), so only these targets were rebuilt; other
  test targets that include SampleBuffer.h were NOT rebuilt or run (unverified, they only see an added static declaration).
- Family: `ctest -R "SampleBuffer|SlicerTWaveform"` -> test-family.log EXIT=0, 4/4 passed.

