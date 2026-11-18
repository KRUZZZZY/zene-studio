# Bunny core lane — batch 12

Six findings, each read against the current tree before any code was touched.
Three are real and fixed with a regression test that failed first; one is a
false positive, one is real but out of this lane's scope, and one was already
fixed on this branch by an earlier batch commit.

### Cluster 3739 — Reaching the directory-depth limit is not reported as truncation

Verdict: FIXED

Evidence: `walkMayDescend` (`src/core/BrowserCatalog.cpp:241`) returns false once
`depth + 1 > kMaxDepth` (`kMaxDepth = 8`, line 61), and `readDirectory` just
`continue`d past a directory it would not descend into — so a matching file
nested below the limit was omitted while the answer still reported
`truncated: false`, telling the caller its search had seen everything. A readable
directory skipped for the depth limit alone now sets `truncated`
(`skippedForDepth`, `src/core/BrowserCatalog.cpp:251`, applied at line 286).
Hidden and symlinked directories deliberately do NOT: nothing under a
dot-directory or behind a link was ever going to be in the answer, so flagging
them would make the flag meaningless.

Commit: 20073880e
Test: BrowserCatalogTest::reachingTheDepthLimitIsReportedAsTruncation
Red: `cmake --build build --target zene BrowserCatalogTest` → EXIT=0, then
`cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=8
(`FAIL!` on `result.truncated` returning FALSE, with `matched == 1` — the file at
the limit was found — already passing, so the assertion failed for exactly the
reported reason and not on fixture setup)
Green: same commands → build EXIT=0, ctest EXIT=0

### Cluster 4650 — An unknown browser kind disables kind filtering

Verdict: FALSE-POSITIVE

Evidence: the report reads `kindFiltered` as false both for an empty `kind` and
for a non-empty kind that fails to parse, so a typo like `kind="audoi"` would
query every kind. But the distinction the finding asks for is already made, one
layer above: `readBrowserQueryArguments` validates a non-empty `kind` through
`browserKindFromName` and returns `ControlErrorKind::InvalidArgs` naming the
valid kinds (`src/core/ControlBrowserSupport.cpp:324-334`). `runBrowserQuery` is
reached from production in exactly one place — `browserQueryHandler`
(`src/core/ControlCommandsBrowser.cpp:126`) — and that handler calls the
validator first, so an unparseable kind never arrives. The remaining
`runBrowserQuery` callers are all test code constructing a `BrowserQuery`
directly, which is not the wire path the finding describes. No product change.

Commit: none
Test: none
Red/green: none

### Cluster 3742 — Feature analysis decodes the entire audio file into memory

Verdict: SKIPPED-SCOPE

Evidence: `BrowserFeatures::analyse` (`src/core/BrowserFeatures.cpp:118`) does
call `SampleDecoder::decode(path)` and then mixes only the first
`FeatureSeconds` (20 s) window — so the report's characterisation of the
measurement is right, but the decode it objects to is the whole file. Fixing it
needs a windowed decode the decoder does not have: `include/SampleDecoder.h:51`
exposes only `decode(const QString&)` returning a `std::optional<Result>` with
the full `std::vector<SampleFrame>`, so a bounded API would have to be added
first, plus a metadata-only duration read to replace the
`data.size() / rate` duration the analysis reports today. That is a decoder API
and schema change, beyond this lane's scope; the code is left alone.

Commit: none
Test: none
Red/green: none

### Cluster 3741 — Invalid feature records can compare as perfect matches

Verdict: FIXED

Evidence: `distance` (`src/core/BrowserFeatures.cpp:138`) never consulted either
record's `valid` flag, and two default-constructed records score 0 in every
term — `logRatio(0,0)` and `tempoDistance(0,0)` both return 0 — so
`distance(blank, blank)` returned exactly 0, the strongest possible match. A
similar-sound search over a library holding undecodable files would rank those
first. `distance` now returns `InvalidDistance` (1e9, declared at
`include/BrowserIndex.h:62`) when either record was never measured
(`src/core/BrowserFeatures.cpp:146`), so such an entry sorts last instead of
tying for first. The only caller (`src/core/ControlCommandsBrowserIndex.cpp:184`)
already filters on `entry.features.valid`, so the ranking of measured files is
unchanged — confirmed by `theClosestToneComesFirst`, which ranks three measured
tones and still passes.

Commit: 0127573a8
Test: LibraryBrowserTest::anUnmeasuredSoundIsNotAPerfectMatch
Red: `cmake --build build --target zene LibraryBrowserTest` → EXIT=0, then
`cd build/tests && ctest -R '^LibraryBrowserTest$' --output-on-failure` → EXIT=8
(`FAIL!` — `distance(blank, blank) > 0.0` returned FALSE: two unmeasured records
compared as identical)
Green: same commands → build EXIT=0, ctest EXIT=0

### Cluster 5494 — A mid-file decode failure is reported as a successful peak map

Verdict: FIXED

Evidence: `accumulatePeaks` returned on a zero or failed read without telling
`compute` the read was incomplete, so the buckets the missing frames would have
filled stayed at their initial ±infinity and `peaksFromBounds` turned them into
a flat line presented as the file's own silence — with `peaksFor` reporting
success. Verified against libsndfile directly: a FLAC whose STREAMINFO declares
240000 frames reads back only 101376 from a truncated file, and FLAC is the
right fixture because RIFF/WAVE cannot express the shape (libsndfile recomputes
a WAV's length from the bytes present, so a lying `data` chunk still reads
exactly what it holds). `accumulatePeaks` now returns the frames it read
(`src/core/BrowserPeakCache.cpp:99`) and `compute` refuses a read short of the
header's promise (`src/core/BrowserPeakCache.cpp:185`) with a reason naming both
counts.

Commit: d1b6f7fd4
Test: BrowserCatalogTest::peaksAreRefusedWhenTheFileIsShorterThanItsHeader
Red: `cmake --build build --target zene BrowserCatalogTest` → EXIT=0, then
`cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=8
(`FAIL!` — `peaksFor` accepted a file whose header declares 160 frames it could
not read and returned 32 buckets for it)
Green: same commands → build EXIT=0, ctest EXIT=0
Note: the fixture is a 42-byte hand-written FLAC (`fLaC` plus a STREAMINFO block,
no audio frames) built in `BrowserCatalogTest.cpp`, because the tree holds no
binary audio fixtures. Its first two versions were wrong — a dropped nibble of
the sample rate made the packed field 33 bytes, and the metadata length was
appended to the wrong buffer — and both were caught by asserting the failure was
about the reported behaviour rather than a rejected file.

### Cluster 3683 — A null error output crashes on cache failures

Verdict: FIXED

Evidence: `compute` and `entryFor` wrote through `*error` unconditionally, so
`peaksFor(path, buckets, &answer, nullptr)` dereferenced null on every failure
path — a missing file, a non-audio file, a truncated read. Both now write
through a `setError(QString*, const QString&)` helper
(`src/core/BrowserPeakCache.cpp:129`), and `include/BrowserPeakCache.h`
documents the parameter as an optional "tell me why". The public API still
requires `answer`; only `error` is nullable, which is what the callers want.

Commit: facf24b32
Test: BrowserCatalogTest::peaksSurviveANullErrorOutput
Red: `cmake --build build --target zene BrowserCatalogTest` → EXIT=0, then
`cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=8
(`Received signal 11 (SIGSEGV)` in `peaksSurviveANullErrorOutput`)
Green: same commands → build EXIT=0, ctest EXIT=0

## Verification

`cmake --build build --target zene BrowserCatalogTest LibraryBrowserTest
ControlBrowserCommandsTest BrowserTagMenuTest LibraryProjectsTest LibraryBrowserTest`
→ EXIT=0, then
`cd build/tests && ctest -R '^(BrowserCatalogTest|LibraryBrowserTest|ControlBrowserCommandsTest|BrowserTagMenuTest|LibraryProjectsTest)$' --output-on-failure`
→ EXIT=0, "100% tests passed, 0 tests failed out of 5".

`bash tests/complexity-gate.sh --check` → EXIT=0. `readDirectory` grew a branch
and a new `skippedForDepth` helper was factored out rather than the ratchet
moved.
`bash tests/file-length-gate.sh --check` → EXIT=0.

Leftovers: cluster 3742 is still unfixed in code — it needs a windowed decode on
`SampleDecoder` plus a metadata-only duration read, a decoder API change this
lane should not make. Cluster 4650 needs nothing: the report was written against
a revision where the validator's placement is easy to miss, and the behaviour it
describes cannot be reached from the socket. Nothing else is half-done.
