FIXED

# Cluster new-p14-272: a short SoundFile read is refused instead of padded

## Evidence

### The defect (before the fix)

`src/core/SampleDecoder.cpp:144-151` sized the sample buffer from the header
(`sfInfo.channels * sfInfo.frames`) and called `sf_read_float` with the buffer
size, ignoring its return value. When the file held fewer frames than its header
declared, libsndfile read what it could, the rest of the buffer stayed zero, and
the decoder returned those zeros as real audio. A header-only FLAC that declares
160 frames therefore decoded to 160 silent frames.

### The fix

`src/core/SampleDecoder.cpp:145-151` now calls
`sf_readf_float(sndFile, buf.data(), sfInfo.frames)`, keeps the returned
`framesRead`, and when `framesRead != sfInfo.frames` it closes the libsndfile
handle, closes the file and returns `std::nullopt`. Nothing else in the decode
path changes.

### Precedent

`src/core/BrowserPeakCache.cpp:190-198` already refuses a short read
(`if (read < frames)`): the header promised more frames than the file gave, so it
does not present the missing frames as the file's own silence. The fix follows
the same rule in the decoder.

### Fixture

`tests/src/core/BrowserCatalogTest.cpp:99-150` is cited by the packet as the FLAC
fixture precedent. This run did not re-read those lines, and it did not compare
the header-only FLAC writer against them.

### Callers

- `src/core/SampleBuffer.cpp:95`: `SampleDecoder::decode(absolutePath)`
- `src/core/ImportDetection.cpp:128`: `SampleDecoder::decode(path)`

Both callers pass through `decode`. This run did not re-read how either caller
handles `nullopt`; the evidence that they still work is the passing
`BrowserCatalogTest` and `ImportDetectionTest` runs below.

### Test slots

`tests/src/core/SampleDecoderShortReadTest.cpp`:

- `:134` `completeWavStillDecodesItsFrames`: a complete 1000-frame WAV still
  decodes all its frames, and at least one frame carries audio.
- `:151` `oneFrameWavStillDecodes`: a one-frame complete WAV is still accepted.
- `:163` `shortSoundFileReadIsNotPadded`: a 160-frame header-only FLAC must be
  refused (`!full.has_value()`). It first checks with `decodeLimited(file, 1.0)`
  that libsndfile opened the file and reports 160 frames, so the refusal comes
  from the decoder and not from an unopenable file.

## Commands and exit codes

Rows marked **observed** were run in this session through the quiet runner and
their exit code was printed by the runner. Rows marked **log only** exist only as
text in the earlier run logs under
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-272.r1/logs/`. Those logs
record no exit codes, so for them the table gives the success or failure marker
the log shows, not an exit code.

| Step | Command | EXIT | Source |
|---|---|---|---|
| build-1-red-toplevel-attempt | `make` at the top level for `SampleDecoderShortReadTest` | not captured | log only: "No rule to make target"; test targets live under `build/tests` |
| build-1-red | build of the test target, fix reverted | not recorded | log only: "Built target SampleDecoderShortReadTest" |
| test-1-red | ctest `SampleDecoderShortReadTest`, fix reverted | not recorded | log only: 0/1 passed, 160-padded-frames assertion |
| build-2-green | build of `zene`, fix present | not recorded | log only: "Built target zene" |
| build-2-green-tests | build of test targets, fix present | not recorded | log only: "Built target BrowserCatalogTest" |
| test-2-green | ctest `SampleDecoderShortReadTest` | not recorded | log only: 1/1 passed |
| test-3-family | ctest `SampleDecoder` | not recorded | log only: 4/4 passed |
| test-4-callers | ctest `BrowserCatalogTest`, `ImportDetectionTest` | not recorded | log only: 2/2 passed |
| p5-green-zene | `cmake --build build --target zene` | 0 | observed |
| p5-green-test-build | `make -C build/tests SampleDecoderShortReadTest` | 0 | observed |
| p5-green-test | ctest `SampleDecoderShortReadTest` | 0 | observed: 1/1 (100%) |
| p5-green-family-build | make of the three family test targets | 0 | observed |
| p5-green-family | ctest `SampleDecoder` | 0 | observed: 4/4 (100%) |
| p5-green-callers-build | make `BrowserCatalogTest`, `ImportDetectionTest` | 0 | observed |
| p5-green-callers | ctest `BrowserCatalogTest|ImportDetectionTest` | 0 | observed: 2/2 (100%) |
| fix.patch | `git diff -- src/core/SampleDecoder.cpp > p5-fix.patch` | 0 | observed |
| git apply -R | reverse the fix patch | 0 | observed (`REVERSE_OK`) |
| p5-red-zene | `cmake --build build --target zene`, fix reverted | 0 | observed |
| p5-red-test-build | make the test target, fix reverted | 0 | observed |
| p5-red-test | ctest `SampleDecoderShortReadTest`, fix reverted | **8** | observed: 0/1 (0%), `FAIL! : ...shortSoundFileReadIsNotPadded() '!full.has_value()' returned FALSE. (a 160-frame FLAC with no audio decoded to 160 padded frames)` |
| git apply (restore) | re-apply the fix patch | 0 | observed (`RESTORE_OK`); `git diff` then `cmp` against the saved patch: identical |
| p5-restored-zene | `cmake --build build --target zene`, fix restored | 0 | observed |
| p5-restored-test-build | make the test target, fix restored | 0 | observed |
| p5-restored-test | ctest `SampleDecoderShortReadTest` | **0** | observed: 1/1 (100%) |
| gates-1-feature-check | feature check | not recorded | log only: 8 items PASS, `RESULT: PASS` |
| gates-2-all-sources-selftest | all-sources reproduce self-test | not recorded | log only: `SELF-TEST PASS` |
| gates-3-status | status snapshot | not recorded | log only: the staged/unstaged entries listed in the log |
| gates-4-feature-check-postcommit | `feature_check.py` on committed tree `bee761508b` | 0 | observed (packet command): `RESULT: PASS`; log `gates-4-feature-check-postcommit.log` |
| check-1-whitespace | `git diff --check` | 0 | observed; log `check-1-whitespace.log` is empty |

The packet's "build-5", "test-7" and "test-8" steps correspond to the
`p5-green-*` rows above, which were run in this session.

## Gates

The feature-check log lists eight items: `fork-sources`, `all-sources-reproduce`,
`no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`,
`complexity` and `file-length`. The `gates-1` log's last line is `RESULT: PASS`,
but its exit code was not recorded. The post-commit feature check on the
committed tree `bee761508b` (`gates-4-feature-check-postcommit.log`) exited
`EXIT=0` with `RESULT: PASS`. That is the observed gate result for the product
tree this report describes. The all-sources and status gates were not re-run.

## Self-check

The five `submit_check` asks, as listed in the packet instructions, answered
with evidence:

1. **Regression fails with the product change reverted and passes with it.**
   RED: `p5-red-test` EXIT=8, 0/1, `shortSoundFileReadIsNotPadded()` failed on
   `!full.has_value()` (a 160-frame FLAC with no audio decoded to 160 padded
   frames). GREEN: `p5-restored-test` EXIT=0, 1/1. The family run
   (`p5-green-family` EXIT=0, 4/4) and callers run (`p5-green-callers` EXIT=0,
   2/2) also pass with the fix in place.
2. **Deterministic.** The fixtures are a fixed 1000-frame WAV
   (`completeWavStillDecodesItsFrames`, `tests/src/core/SampleDecoderShortReadTest.cpp:137`),
   a fixed 1-frame WAV (`oneFrameWavStillDecodes`, `:154`) and a fixed 160-frame
   header-only FLAC (`shortSoundFileReadIsNotPadded`, `:166`), written by
   `writeTone` (`:50`) and `writeHeaderOnlyFlac` (`:75`) into a `QTemporaryDir`
   (`:127`). A grep of the whole test file finds no `sleep`, clock, timer or
   thread use.
3. **Fix is at the cause and complete.** The `sf_readf_float` return value at
   `src/core/SampleDecoder.cpp:145` was not checked against the requested frame
   count, so a short read was padded. The fix refuses the short read at
   `SampleDecoder.cpp:145-151` and returns `std::nullopt`. The sibling
   `boundedSF` (`:273`) reads its return count at `:292` and is not changed by
   this fix. Callers `SampleBuffer.cpp:95` and `ImportDetection.cpp:128` both
   call `SampleDecoder::decode`. `BrowserCatalogTest` and `ImportDetectionTest`
   pass (`p5-green-callers` EXIT=0, 2/2).
4. **No collateral damage.** The controls `completeWavStillDecodesItsFrames`
   and `oneFrameWavStillDecodes` are slots in the same binary as the regression
   test, which exits 0 with the fix in place (`p5-restored-test` EXIT=0). Only a
   file shorter than its header changes behavior, because the refusal fires only
   when `framesRead != sfInfo.frames`.
5. **ALREADY-FIXED / FALSE-POSITIVE ask: not applicable.** The verdict is FIXED.
   The RED run fails on the unfixed tree and the GREEN runs pass on the fixed tree.

## Other findings

* The earlier run logs record no exit codes, so the packet's EXIT values for the
  log-only rows are not verified. The rerun rows above are the observed evidence.
* The gate exit codes (`gates-1`, `gates-2`, `gates-3`) were not re-run here.
* The five `submit_check` asks are the (1)-(5) list in the packet instructions;
  the self-check above answers that list in order.
* The call lines `SampleBuffer.cpp:95` and `ImportDetection.cpp:128` were checked;
  how each caller handles a `nullopt` result was not read in this session.
