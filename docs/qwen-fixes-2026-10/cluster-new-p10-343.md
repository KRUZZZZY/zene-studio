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

- Gates: `python3 .../feature_check.py <worktree>` -> gates.log, EXIT=0 (first run FAILED fork-sources/all-sources-reproduce because tests/all-sources.txt lacked the new test; added in sorted place, rerun all 8 PASS):
```
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check
- Test fails reverted, passes fixed: test-4b-red.log RED_EXIT=8 (3 cases fail, e.g. actual 0 vs expected 4294967296),
  test-4b-green.log GREEN_EXIT=0. The fix patch contains only the `base64SourceBytes` body in src/core/SampleBuffer.cpp.
- Deterministic: pure arithmetic on injected frame counts (300,000,000; 2^29; SIZE_MAX/8; 0; 1000) plus a 16-frame
  round trip. No clock, sleeps, threads or large allocations. The seam is `SampleBuffer::base64SourceBytes`.
- Cause and siblings: the only int cast was in `toBase64()`; its callers (AudioFileProcessor.cpp:314,
  SlicerT.cpp:611, Sample.h:115) all go through it. The inverse `fromBase64` (SampleBuffer.cpp, `bytes.size()` /
  `std::memcpy`) already uses qsizetype/size_t, so it is consistent. include/base64.h and the Base64 users for VST/CLAP
  state take a QByteArray directly and never cast through int in this path (not changed, not part of this finding).
- Collateral: output for any sample that fit before is byte-identical (round-trip test; SlicerTWaveformTest and
  SampleBuffer* tests pass). The new limit is 3/16 of qsizetype max (about 1.7e18 bytes on 64-bit), so no
  legitimate 64-bit caller reaches it; it only refuses a sample whose Base64 text a QString could not hold, which the old code
  silently truncated. Loading is untouched, so old projects load as before. Not audio-thread code.
- Not applicable: ALREADY-FIXED/FALSE-POSITIVE (verdict is FIXED).
- Sibling commands item: I changed no command files and added no control command. The tests/control-*.py files the
  check lists came in through the train merges on this branch, not from me.

## Other findings
- None noticed.
