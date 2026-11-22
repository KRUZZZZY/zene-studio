FIXED

# cluster new-p10-343: saving a large embedded sample could truncate its Base64 data

## Evidence the bug was real
- Before: `src/core/SampleBuffer.cpp` `toBase64()` did `static_cast<int>(m_data.size() * sizeof(SampleFrame))` and
  built `QByteArray{data, size}`. Past INT_MAX bytes (about 268M stereo frames of 8 bytes) the count goes negative
  or wraps; exactly 2^29 frames wraps to 0, so the project saved an empty `sampledata`.
- Callers: `plugins/AudioFileProcessor/AudioFileProcessor.cpp:314`, `plugins/SlicerT/SlicerT.cpp:611`
  (via `Sample::toBase64`, `include/Sample.h:115`). Both go through the one function, so both are covered (on Qt 5 and Qt 6, see round 2).
- Load side `SampleBuffer::fromBase64` already uses `qsizetype` sizes (`bytes.size()`), no change needed.

## Fix
- `include/SampleBuffer.h`, `src/core/SampleBuffer.cpp`: new `static base64SourceBytes(size_t frames)
  -> std::optional<qsizetype>`. It returns the exact byte count in `qsizetype`, or nullopt if the count or its
  Base64 text (as a QString) could not be held (Qt 6: limit 3/16 of qsizetype max, unreachable on 64-bit; Qt 5: limit INT_MAX/4*3 bytes, because Qt 5's QByteArray is int-counted on 64-bit too).
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

## Round 2 (Luna's review: Qt 5 still narrowed the count)
Finding: on Qt 5.15 `QByteArray::fromRawData(const char*, int)` narrows the qsizetype count, so 2^29 frames still saved an
empty `sampledata` (base64SourceBytes returned 4294967296, QByteArray size 0). Qt 5 is the CMake default.
- Fix (`src/core/SampleBuffer.cpp`, base64SourceBytes): `#if QT_VERSION >= 6.0.0` keeps the 3/16-of-qsizetype cap;
  otherwise the cap is `INT_MAX / 4 * 3` source bytes (so the 4/3 Base64 text is still an int) and nullopt above it.
  toBase64() therefore never passes a count above INT_MAX to Qt 5's fromRawData; it warns and returns an empty string.
  Unverified: whether a Qt 5 QString can hold a text near 2^31 chars (QString is 2 bytes/char); I followed the requested
  INT_MAX/4*3 cap and did not lower it, so a text above INT_MAX/2 chars may still fail inside Qt 5 as it did before.
- Test (`SampleBufferBase64Test.cpp`): 300M-frame and 2^29-frame slots assert the exact wide value on Qt 6 and nullopt
  on Qt 5; new slot `qt5CapRefusesWhatAnIntCountedByteArrayCannotHold` (Qt 5 only, boundary frames accepted, +1 refused;
  QSKIP on Qt 6); new slot `anyAcceptedCountLeavesRoomForTheBase64Text` probes around the cap and checks the 4/3 text fits
  the Counter type (int on Qt 5, qsizetype on Qt 6). Small-count and round-trip slots run on both.
- Qt 6 build (this tree): `cmake --build build --target SampleBufferBase64Test -j4` -> build-1.log EXIT=0;
  `ctest -R SampleBufferBase64Test` -> test-green.log EXIT=0.
- RED/GREEN with the old int-cast helper: `git diff ee55360d90 -- src/core/SampleBuffer.cpp > fix.patch` (that commit is
  the seam with the int cast); `git apply -R fix.patch` REVERT=0, rebuild build-2-oldcast.log EXIT=0, ctest test-red.log
  RED_EXIT=8 (3 slots fail); `git apply fix.patch` APPLY=0, rebuild build-3-fixed.log EXIT=0, ctest test-green2.log GREEN_EXIT=0.
  The literal reversal of every product hunk (header too) does not compile, as Luna noted; the helper body is the seam.
- Qt 5 branch verified against Qt 5.15.19 headers (`pkg-config Qt5Core`), with a harness that includes the extracted text
  of `base64SourceBytes` from the tree (the test file itself cannot be built against Qt 5 here: no Qt 5 build of the project).
  The harness calls `QByteArray::fromRawData(buf, static_cast<int>(count)).size()` the way toBase64() does. Logs:
  - HEAD of round 1 (qt5-head-run.log, run exit 1): 2^29 frames -> helper=4294967296 qbytearray_size=0 (the bug);
    300M frames -> qbytearray_size=-1894967296; cap+1 frame not refused.
  - Round-2 text (qt5-new-run.log, run exit 0): both sizes refused, cap frames accepted, cap+1 refused, 1000 frames = 8000 exact.
  Both compiled with `g++ -std=c++20 -Wall -Werror`, exit 0 (qt5-*-build.log).
- toBase64()'s own wiring line is not covered by the test (it needs a >1.6 GB buffer); it is a two-line caller of the helper.
- Refusal is a qWarning plus an empty string, as before the change the audio was lost silently; no user-visible dialog added.
- 32-bit Qt 6 builds: the 3/16 cap refuses roughly 400 MB+ samples the old int cast could represent; no CI platform is 32-bit.

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
  SampleBuffer* tests pass). On Qt 6 the limit is 3/16 of qsizetype max (about 1.7e18 bytes on 64-bit), so no
  legitimate 64-bit caller reaches it; on Qt 5 it is INT_MAX/4*3 = 1,610,612,733 bytes, which only refuses what the old int cast already lost (see round 2); it only refuses a sample whose Base64 text a QString could not hold, which the old code
  silently truncated. Loading is untouched, so old projects load as before. Not audio-thread code.
- Not applicable: ALREADY-FIXED/FALSE-POSITIVE (verdict is FIXED).
- Sibling commands item: I changed no command files and added no control command. The tests/control-*.py files the
  check lists came in through the train merges on this branch, not from me.

## Other findings
- None noticed.
