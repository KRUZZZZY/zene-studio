FIXED

# Cluster 2163 — Asset hashing can repeatedly read gigabytes on the UI thread

**Verdict: FIXED.** The finding is REAL: `project.hash_assets` hashed one file per
reference, so N references that resolved to the same file read it N times, and the 1 GiB
per-file cap still allowed up to 10,000 references × 1 GiB in a single call on the
control/UI thread. The fix has two parts at the cause:

1. **One read per distinct file.** `controlHashProjectAssets()` now memoises the hash
   answer by resolved file identity (`QFileInfo::canonicalFilePath()`, falling back to the
   absolute path) for the duration of one command, so a repeated reference reuses the
   `hashed`/`sha256`/`error` it already produced.
2. **A total-work bound per command.** A new
   `MaxProjectAssetTotalHashBytes` (4 GiB) bounds the bytes one command may read across
   all distinct files. References past it come back `hashed=false` with an error naming
   the total cap — the same reported state an over-cap file already has — so the command
   still succeeds with an honest, partial answer rather than failing or blocking forever.

A structural split keeps the 500-code-line ratchet honest: the bounded document reader
moved unchanged into `src/core/ControlProjectAssetsDocument.cpp`; the scan/hash half and
the relink write half keep their existing homes.

## File evidence

- `include/ControlProjectAssets.h:162` — `MaxProjectAssetTotalHashBytes = 4 GiB`, with the
  reason and the "reported unhashed, never silently skipped" contract in its comment.
- `src/core/ControlProjectAssets.cpp:496` — `controlHashProjectAssets()`.
  - `:505` takes the command budget (`:352`, `totalHashBudget()`).
  - `:511` keys the per-command cache by `hashIdentity(ref.path)` (`:276`).
  - `:513-519` copies the cached answer; a cache hit performs no read.
  - `:520` otherwise hashes once under both bounds (`hashOneWithin`, `:374`), which
    charges the bytes actually read (`:399-400`) to the remaining budget.
- `src/core/ControlProjectAssets.cpp:306` — `hashFileWithin()` is the one streaming
  reader: it reads no more than `readCap` plus the one byte that proves there is more, and
  returns the bytes consumed for budget accounting. `hashReference()` (`:475`) keeps its
  old per-file-cap contract and message.
- `src/core/ControlProjectAssets.cpp:289` — test-only read log seam
  (`ZENE_TEST_PROJECT_ASSET_HASH_READ_LOG`, compiled only under `LMMS_TESTING`); records
  each file the hasher actually opens. `:352` is the test-only budget seam
  (`ZENE_TEST_PROJECT_ASSET_HASH_TOTAL_BYTES`).
- `src/core/ControlProjectAssetsDocument.cpp:241` — `readProjectDocument()`, moved
  unchanged out of `ControlProjectAssets.cpp` for the file-length ratchet; `:288`
  `serialiseDocument()`, `:310` `inflateQtCompressedWithinCap()`.
- `tests/src/core/ControlProjectArchiveTest.cpp:659` —
  `aRepeatedReferenceIsHashedOnce()`; `:713` —
  `aCommandStopsAtItsTotalHashBudgetAndSaysSo()`.
- Docs updated to state the new bound: `docs/CAPABILITIES-0.3.0.md:1100`,
  `docs/KNOWN-LIMITATIONS.md:2069`.

Before the fix, the hash loop was `src/core/ControlProjectAssets.cpp:642-655` on HEAD
(`git show HEAD:src/core/ControlProjectAssets.cpp`): every present reference was passed to
`hashReference()` with no cache, and nothing in `collectReferences()` or `fillReference()`
de-duplicated by path. The per-file cap at `include/ControlProjectAssets.h:151` was the
only bound.

## Test matrix

| Path | Test | What it proves |
| --- | --- | --- |
| Normal / duplicate references | `aRepeatedReferenceIsHashedOnce` | The same file referenced three times is opened once (read log has exactly one line for it, one for the other file), all four references are hashed, and each hash equals the file's own sha256. |
| Exhaustion | `aCommandStopsAtItsTotalHashBudgetAndSaysSo` | With a test-lowered total budget for two of four distinct files, exactly two are hashed with correct sha256, the other two are `hashed=false` with a "total cap" error and no hash, and the command still succeeds. |
| Failure / growth | `fileGrowthAfterScanRespectsHashCap` (existing) | A file that grows after the scan is still refused by the streaming cap, with the cap in the error. |
| Reconfiguration / stability | `hashingIsStableAndChangesWithTheReferenceSet`, `relink*`, `collectMedia*` (existing) | Digest stability, relink and media collection still work; the four-test family below passes. |
| Audio thread | none touched | The change is entirely in the control-command path; no process/render/play function is on the diff. |
| Supported platforms | compile + test | Only `QFile`/`QFileInfo`/`QHash`/`QCryptographicHash`; the Qt 5 `addData` overload is kept under `QT_VERSION`, and the test normalises path separators with `QDir::fromNativeSeparators`. |

## RED / GREEN (procedure step 4 and 4b)

The test-only seams are compiled only into the test build (`tests/CMakeLists.txt:13`
defines `LMMS_TESTING` on `lmmsobjs`), and the regression test needs no timing, sleeps or
races: it counts reads from the log and sets an injected byte budget.

`fix.patch` is the product diff
(`include/ControlProjectAssets.h`, `src/core/ControlProjectAssets.cpp`,
`src/core/ControlProjectAssetsDocument.cpp`, `src/core/CMakeLists.txt`); tests are not in
it.

| Step | Command (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/2163.r1/logs`) | EXIT | Observed |
| --- | --- | --- | --- |
| Patch out the fix | `git diff HEAD -- include/ControlProjectAssets.h src/core/ControlProjectAssets.cpp src/core/ControlProjectAssetsDocument.cpp src/core/CMakeLists.txt > logs/fix.patch` | 0 | 931-line product patch. |
| Reverse it | `git apply -R logs/fix.patch` | 0 | New document TU deleted, old monolithic `ControlProjectAssets.cpp` restored. |
| Build RED | `cmake --build build --target ControlProjectArchiveTest -j4 > logs/build-18-red.log 2>&1` | 0 | Compiles and links the unfixed code. |
| Test RED | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > logs/test-red-4.log 2>&1` (from `build/tests`) | 8 | 21 passed, 2 failed: `aRepeatedReferenceIsHashedOnce` — read log absent (`QFileInfo::exists(readLog)` false); `aCommandStopsAtItsTotalHashBudgetAndSaysSo` — 4 hashed instead of 2. |
| Reapply | `git apply logs/fix.patch` | 0 | Fix and split restored. |
| Build GREEN | `cmake --build build --target zene ControlProjectArchiveTest -j4 > logs/build-19-green.log 2>&1` | 0 | `zene` and the test target build. |
| Test GREEN | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > logs/test-green-9.log 2>&1` (from `build/tests`) | 0 | 1/1 ctest target passed (23 QtTest cases). |

Targeted run (`./ControlProjectArchiveTest aRepeatedReferenceIsHashedOnce
aCommandStopsAtItsTotalHashBudgetAndSaysSo`, `logs/test-green-targeted-final.log`): EXIT=0,
`4 passed, 0 failed` (including init/cleanup), both regression cases explicitly PASS.

## Family tests

After the final build:

- `cmake --build build --target ControlProjectMediaTest ControlProjectTemplateTest LibraryProjectsTest -j4 > logs/build-20-family-final.log 2>&1` — EXIT=0.
- `ctest -R '^(ControlProjectArchiveTest|ControlProjectMediaTest|ControlProjectTemplateTest|LibraryProjectsTest)$' --output-on-failure > logs/test-family-final2.log 2>&1` (from `build/tests`) — EXIT=0, `100% tests passed out of 4`.

`LibraryProjectsTest` covers `project.handoff` → `collectProjectMediaInto()` →
`controlHashProjectAssets()`; `ControlProjectMediaTest` covers the media inventory path.
Both callers of the changed function stay green.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> >
logs/feature-check-final2.log 2>&1` — EXIT=0, `RESULT: PASS`, all eight rows PASS
(`complexity` and `file-length`: "no new item in your files").

Registration: the new `src/core/ControlProjectAssetsDocument.cpp` is in
`tests/fork-sources.txt` and `tests/all-sources.txt`; `src/core/CMakeLists.txt` is a build
config file (Gate 6 allows it without a divergence reason) and lists the new TU next to
the original.

## Files committed

- `include/ControlProjectAssets.h`
- `src/core/ControlProjectAssets.cpp`
- `src/core/ControlProjectAssetsDocument.cpp` (new; the bounded document reader, moved unchanged)
- `src/core/CMakeLists.txt`
- `tests/src/core/ControlProjectArchiveTest.cpp`
- `tests/fork-sources.txt`, `tests/all-sources.txt`
- `docs/CAPABILITIES-0.3.0.md`, `docs/KNOWN-LIMITATIONS.md`
- `docs/qwen-fixes-2026-10/cluster-2163.md` (this report)

No control command was added or changed, so the ControlCommandsSnapshot canary is
untouched. No undo, transport, recording or audio-engine file was changed; `hash_assets`
remains a not-mutating read, so there is no new undo step.

## Self-check

1. **Regression fails with the product change reverted, passes with it.** RED:
   `git apply -R logs/fix.patch` EXIT=0; build `logs/build-18-red.log` EXIT=0; ctest
   `logs/test-red-4.log` EXIT=8, with both new tests failing for the reasons above.
   GREEN: `git apply logs/fix.patch` EXIT=0; build `logs/build-19-green.log` EXIT=0; ctest
   `logs/test-green-9.log` EXIT=0. The targeted run names both cases and shows PASS.
2. **Deterministic test.** No clock, sleep, thread or race. The cache test observes a
   test-only read log (`logTestHashRead`, compiled under `LMMS_TESTING`) and asserts exact
   line counts; the budget test injects a byte budget through
   `ZENE_TEST_PROJECT_ASSET_HASH_TOTAL_BYTES`, so only 250-byte files are needed. Both
   seams are read per call, never set by the product, and released by `ScopedTestEnv`
   even when a `QVERIFY` returns early.
3. **Cause and completeness; every caller and sibling.** The cause was the uncached loop
   plus the per-call work bound. Callers of `controlHashProjectAssets()` are
   `src/core/ControlCommandsProjectArchive.cpp:280` (`project.hash_assets`),
   `src/core/ControlCommandsCollectMedia.cpp:166` (`project.collect_media`), `:212`
   (`collectProjectMediaInto`, used by `project.handoff`) — all take the same
   `ProjectAssetScan` and are covered by the family run. `project.relink` uses
   `hashReference`-independent code (`BounceInPlace::sha256OfFile`) and is unchanged.
   `hashReference()` itself keeps its previous per-file cap and message; its only direct
   caller is the existing growth test. The scan/digest and document-read halves moved
   without edits and keep their shared-header declarations.
4. **No collateral damage.** Saved projects and media formats are unchanged (only reads
   were added). The new total bound is a REPORTED state, not a command failure, and it
   only limits how much media one call hashes: small/normal projects hash exactly as
   before, and duplicate references are now cheaper, never different. A reference over
   the per-file cap behaves exactly as before. The existing `fileGrowthAfterScanRespectsHashCap`,
   `hashingIsStableAndChangesWithTheReferenceSet`, relink and collect-media tests all pass.
5. **ALREADY-FIXED / FALSE-POSITIVE:** not used; the verdict is FIXED.
6. **Sibling commands** (the `submit_check.py` list: `app.*` and `arrangement.*`): this
   commit changes no `ControlCommands*` or `ControlReversibilityTable*` file — `git show
   --stat HEAD` lists only the asset read/write engine, its split document TU, the test,
   the two scope manifests, two docs and the report. The sibling list comes from the
   branch's `base..HEAD` diff, which carries other lanes' command files; none of them
   calls `controlHashProjectAssets()` or `hashReference()`. The only `project.*` commands
   affected are the three asset verbs, and their callers are the three sites named in
   item 3; all are covered by the family run.

## Other findings

- `ControlCommandsCollectMedia.cpp:fileSha256()` (`fileSha256(candidate)` in
  `destinationFor`) still hashes a candidate destination file without the streaming cap,
  unlike `hashReference`. It only reads files already inside the project's media folder and
  is not part of this finding; noted, not changed here.
- The command description string in `src/core/ControlCommandsProjectArchive.cpp:252` still
  mentions only the per-file cap. Changing it would move
  `commands_snapshot.json` (the ControlCommandsSnapshot canary); the docs and the header
  state the second bound instead. Not changed for that reason.
