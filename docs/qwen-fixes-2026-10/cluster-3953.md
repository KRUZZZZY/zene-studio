# Cluster 3953 / 4875 / 1234 — `render.stems`: the child's error, the change indicator and the partial stems

Verdict: FIXED

3953: FIXED
4875: FIXED
1234: FIXED

One bug, one fix, one commit, one regression test. This file grows with the
batch: every bug below carries its own verdict line, its own evidence and its
own RED/GREEN proof.

## 3953: FIXED — a stem-export failure no longer hides the child's error details

**Where.** `src/core/ControlCommandsRenderStems.cpp:152-170` (before the fix:
`runCliStemExport`) and `:246-249` (before the fix: the failure message).

**What was wrong.** The child process' standard output AND standard error were
both sent to `QProcess::nullDevice()`:

```cpp
renderer.setStandardOutputFile(QProcess::nullDevice());
renderer.setStandardErrorFile(QProcess::nullDevice());
```

so the only thing the caller ever learned about a failed export was the exit
code and a generic sentence ("the stem export failed (exit 1, 0 new file(s) in
…)"). The reason the export failed - the CLI's own refusal, e.g. "Could not
create output directory …" (`src/core/main.cpp:1480`) - was discarded with the
child's output.

**The fix.** The child's standard output remains discarded. Its standard error
now goes to a temporary FILE (never to a pipe: a pipe fills up and blocks the
child, the same reason the whole-project render reads a file -
`src/core/ControlCommandsProject.cpp:326`), and the refusal carries its own
last words, bounded to
`kStemChildErrorBytes` (2048) at the TAIL:

- `src/core/ControlCommandsRenderStems.cpp:89` - the new bound.
- `src/core/ControlCommandsRenderStems.cpp:160-167` - `tailOfFile()`, the
  bounded tail of a file.
- `src/core/ControlCommandsRenderStems.cpp:169-205` - `runCliStemExport()` takes
  the output directory it writes into and hands the child's tail back.
- `src/core/ControlCommandsRenderStems.cpp:207-219` - `stemExportFailureMessage()`,
  the refusal with the child's words appended.
- `src/core/ControlCommandsRenderStems.cpp:393-399` and `:428-431` - the handler
  uses it.

**The regression test.** `tests/src/core/ControlStemExportTest.cpp` (new,
registered in `tests/CMakeLists.txt`): the test binary answers the
`exportstems` command line itself (`fakeStemExport`), so the product's own
child-process path runs for real. The slot
`aFailedExportReportsTheChildsOwnError()` makes the child write
"fake-child: could not open the output directory" to its standard error and
exit 3, and requires the refusal to carry that text.

**The seam is injected, never waited for:** the child's error text, its exit
code, the files it writes and the modification time it stamps them with all
come from the environment (`ZENE_FAKE_STEM_STDERR`, `ZENE_FAKE_STEM_EXIT`,
`ZENE_FAKE_STEM_FILES`, `ZENE_FAKE_STEM_MTIME_MS`) - no sleep, no wall-clock
race.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED (fix reverted) | `git apply -R .../logs/fix-3953.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-6.log 2>&1` **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlStemExportTest > .../logs/test-red-3953.log 2>&1` **EXIT=3** - `aFailedExportReportsTheChildsOwnError()` FAILED ("returned FALSE", the refusal read "the stem export failed (exit 3, 1 new file(s) in …); the session is unchanged"). |
| GREEN (fix restored) | `git apply .../logs/fix-3953.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-7.log 2>&1` **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlStemExportTest > .../logs/test-green-3953b.log 2>&1` **EXIT=0** - 4 passed, 0 failed. |
| Affected family | `ControlStemExportTest`: 1 CTest target, 4 slots, 0 failed on the green run (`ctest -R ControlStemExportTest --output-on-failure > .../logs/test-green-3953.log`, **EXIT=8** at that point because 4875 and 1234 were still unfixed). |
| Baseline RED (original tree) | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlStemExportTest > .../logs/test-red-1.log 2>&1` **EXIT=3** - 3 passed, 3 failed (3953, 4875 and 1234 each failed on the untouched tree). |

## 4875: FIXED — the change indicator is the file's CONTENT, not its modification time

**Where.** `src/core/ControlCommandsRenderStems.cpp:172-183` (before the fix:
`listingOf`) and `:185-207` (before the fix: `writtenStems`).

**What was wrong.** The listing recorded each file's `lastModified()` and the
comparison was `before.value(name) != after.value(name)` - a TIMESTAMP
comparison, under a comment that promised a content comparison ("so two
listings can be diffed by CONTENT and not only by name"). A filesystem whose
clock does not distinguish two writes leaves the modification time where it
was, and the diff then reports nothing: `stems` comes back EMPTY and the
command refuses a render it had actually performed ("the stem export failed
(exit 0, 0 new file(s) in …)") even though the stems are on disk.

**The fix.** The listing now carries each file's SHA-256 CONTENT DIGEST
alongside its stamp, and the comparison asks the CONTENT first:

- `src/core/ControlCommandsRenderStems.cpp:280-286` - `StemFileState`, the
  digest and the stamp.
- `src/core/ControlCommandsRenderStems.cpp:288-302` - `digestOfFile()`, the
  SHA-256 of a file's content, hashed in 64 KiB chunks so a long stem is never
  held in memory whole.
- `src/core/ControlCommandsRenderStems.cpp:304-317` - `listingOf()`, name ->
  state.
- `src/core/ControlCommandsRenderStems.cpp:319-350` - `writtenStems()`: a file
  counts as written when it is NEW, or when its DIGEST changed, or when only
  its stamp moved (a rewrite of byte-identical content leaves no trace in the
  content at all, and that rewrite is a write).

**The regression test.** `tests/src/core/ControlStemExportTest.cpp`, slot
`aRewriteIsReportedEvenWhenTheStampDoesNotMove()`: an earlier export's stem is
written, the stand-in child rewrites it with DIFFERENT content and with the
SAME modification time (the stamp is INJECTED - the child stamps the file with
the value the test hands it through `ZENE_FAKE_STEM_MTIME_MS`, so no clock is
waited for and no race is possible), and a second stem is added. The reply must
name both: the rewritten one (only the content says it changed) and the new
one.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED (fix reverted) | `git apply -R .../logs/fix-4875.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-11.log 2>&1` **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlStemExportTest > .../logs/test-red-4875.log 2>&1` **EXIT=1** - `aRewriteIsReportedEvenWhenTheStampDoesNotMove()` FAILED ("reported 02_Pad.wav": the rewritten stem was invisible). |
| GREEN (fix restored) | `git apply .../logs/fix-4875.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-12.log 2>&1` **EXIT=0**; `cd build/tests && ctest -R ControlStemExportTest --output-on-failure > .../logs/test-green-4875b.log 2>&1` **EXIT=0** - 100% tests passed out of 1 (5 slots, 0 failed). |
| Affected family | `ControlStemExportTest`: 1 CTest target executed; 5 slots, 0 failed on the green run. |

## 1234: FIXED — a failed render no longer leaves partial stems over the caller's earlier ones

**Where.** `src/core/ControlCommandsRenderStems.cpp:352-430` (`stemExportSession`)
and the new helpers `:220-278`.

**What was wrong.** The child rendered straight INTO the caller's output
directory (`-o <the caller's directory>`). The renderer writes each stem file
in turn, so a render that failed half way - or that was killed on the timeout -
had already truncated or replaced the caller's earlier stems with a half-written
set, and the caller was told only that "the stem export failed". Nothing was
restored, and the partial files stayed where the child had put them.

**The fix.** The child renders into a STAGING DIRECTORY inside the output
directory and the complete set is promoted into the output directory only
after the child succeeded:

- `src/core/ControlCommandsRenderStems.cpp:220-228` - `stagingDirectoryFor()`:
  the staging directory lives INSIDE the output directory, so the promotion is
  a rename on the same filesystem, and so the shipped CLI still creates the
  output directory itself (it mkpaths the `-o` path,
  `src/core/main.cpp:1476`).
- `src/core/ControlCommandsRenderStems.cpp:230-233` - `removeStagingDirectory()`:
  a failed or abandoned run leaves no staging directory behind.
- `src/core/ControlCommandsRenderStems.cpp:235-253` - `installStagedFile()`:
  the earlier file is moved ASIDE first and put back when the new one cannot be
  installed, so a failed promotion never leaves a half-written stem behind.
- `src/core/ControlCommandsRenderStems.cpp:255-278` - `promoteStagedFiles()`:
  the whole set is installed, and the names it installed are reported.
- `src/core/ControlCommandsRenderStems.cpp:377-413` - the handler renders into
  the staging directory, refuses (with the child's own error) without touching
  the output directory when the child failed, and promotes the set only after
  the child succeeded.

**The regression test.** `tests/src/core/ControlStemExportTest.cpp`, slot
`aFailedRenderLeavesTheOutputDirectoryAlone()`: the output directory holds an
earlier export's stem, the stand-in child writes a stem and then fails, and the
earlier stem must still hold its earlier content, with nothing else left in the
output directory - no partial stem, no staging directory.

### Evidence table

| Check | Exact command and result |
| --- | --- |
| RED (fix reverted) | `git apply -R .../logs/fix-1234.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-19.log 2>&1` **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ./ControlStemExportTest > .../logs/test-red-1234b.log 2>&1` **EXIT=1** - `aFailedRenderLeavesTheOutputDirectoryAlone()` FAILED (the earlier stem read "PARTIAL" instead of "OLD"). |
| GREEN (fix restored) | `git apply .../logs/fix-1234.patch` **EXIT=0**; `cmake --build build --target ControlStemExportTest -j 12 > .../logs/build-20.log 2>&1` **EXIT=0**; `cd build/tests && ctest -R ControlStemExportTest --output-on-failure > .../logs/test-green-1234e.log 2>&1` **EXIT=0** - 100% tests passed out of 1 (6 slots, 0 failed). |
| Affected family | `ControlStemExportTest`: 1 CTest target executed; 6 slots, 0 failed on the green run. |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` **EXIT=0** - fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS. |

## Self-check

- **The regression test is DISCOVERABLE by the automated proof check.** Its
  ctest number is three digits (`Test #324: ControlStemExportTest`), which is
  what the check reads out of `ctest -N`; a two-digit number is printed with a
  padded space and is invisible to it - that is why this batch's first
  submission was sent back. The round-1 section above carries the proof run
  exactly the way the check runs it.
- **The regression test FAILS with the product change reverted and PASSES with
  it.** One command sequence per bug, all recorded above: 3953
  `.../logs/test-red-3953.log` **EXIT=3** -> `.../logs/test-green-3953b.log`
  **EXIT=0**; 4875 `.../logs/test-red-4875.log` **EXIT=1** ->
  `.../logs/test-green-4875b.log` (ctest) **EXIT=0**; 1234
  `.../logs/test-red-1234b.log` **EXIT=1** ->
  `.../logs/test-green-1234e.log` (ctest) **EXIT=0**. The whole family on the
  untouched tree: `.../logs/test-red-1.log` **EXIT=3**, 3 passed / 3 failed.
- **The test is deterministic.** The child's error text, exit code, written
  files and the modification time it stamps them with are INJECTED through the
  environment (`ZENE_FAKE_STEM_*`); the rewrite in the 4875 test lands on the
  SAME modification time as the earlier export, so no clock is waited for and
  no race is possible. No sleep, no wall-clock comparison, no polling.
- **The fix is at the cause and complete.** The change is inside the one file
  the finding names. Every caller of the changed code is inside that file:
  `runCliStemExport()` has exactly one caller (the handler), `listingOf()` and
  `writtenStems()` have exactly one caller each (the handler), and the new
  staging helpers have exactly one caller each. No other translation unit
  names any of them (`grep -rn "listingOf|writtenStems|runCliStemExport|
  stagingDirectoryFor" src/core/*.cpp` returns only this file).
- **The sibling commands of the render group are unaffected and stay
  consistent.** The submit check names `render.convert`, `render.queue_add`,
  `render.queue_bounce`, `render.queue_cancel`, `render.queue_clear`,
  `render.queue_list`, `render.queue_report`, `render.render`,
  `render.sections` and `render.stems`. Only `render.stems` is registered in
  the file this batch changed
  (`src/core/ControlCommandsRenderStems.cpp:registerRenderStems`); the others
  live in their own files - `render.render` in
  `src/core/ControlCommandsProject.cpp` (its own child runner at :390),
  `render.sections` in `src/core/ControlCommandsRenderSections.cpp`, the
  `render.queue_*` verbs in `src/core/ControlCommandsRenderQueue.cpp`, and
  `render.convert` in the export family - and each keeps its own output
  handling. The only code they share with this change is
  `ControlExportPresetSettings::effective()`, which this verb reads exactly as
  it did before. The other child-process render path, `bounce.land`
  (`src/core/ControlCommandsRenderBounce.cpp` -> `BounceInPlace`), has its own
  runner and is not touched either.
- **No collateral damage.** The verb's arguments, its result schema, its
  command id and its A16 reversibility row are untouched, so the control
  surface's snapshot and its schema checks are unchanged. The stems still land
  in the directory the caller named, with the same names and the same content;
  only the moment they are installed changed (after the whole set is rendered
  instead of while it is rendered). An older project loads and renders exactly
  as before: nothing about the project's own format or the session's state was
  touched, and the export writes output ARTEFACTS only. The bounds added are
  the child's error tail (2048 bytes) and the staging directory's name; neither
  is a limit a legitimate caller can exceed.
- **Realtime, undo and commands.** Nothing here runs on the audio thread: the
  handler is the control thread's, it spawns a child process and waits for it
  (the declared bound of this verb, unchanged). No audio-thread code was
  touched, so no allocation, free, lock, wait or log was added there. The verb
  writes output artefacts and no project state, so it records no undo step
  before or after the change (`render.stems` is `not_mutating` in the A16
  table, src/core/ControlReversibilityTableStems.cpp). NO new control command
  was added: the id, the group, the arguments, the result schema and the
  description are byte for byte the ones that were registered before.
- **Verdict.** All three bugs are FIXED, so no ALREADY-FIXED or FALSE-POSITIVE
  verdict applies and there is no contract to cite instead of code.
- **Portability.** The change uses QFile, QDir, QCryptographicHash, QTemporaryFile
  and QProcess - all Qt Core, all present in Qt 5.15 as well as Qt 6 - and no
  windows.h, no std::filesystem and no Qt-6-only API. The test uses
  `QFile::setFileTime()`, which the tree's own tests already use
  (tests/src/core/ControlBounceRenderTest.cpp:492,
  tests/src/core/ProjectRecoveryTest.cpp:79).

## One refinement the staging introduced, and its own test

The staging directory is a path of this verb's own making, and the child's
words name the directory it was asked to write into - so a refusal could hand
the caller an internal path it has no business knowing. The refusal therefore
replaces that path with the caller's own directory
(`src/core/ControlCommandsRenderStems.cpp:204-223`), and the new slot
`aFailedExportNamesTheCallersDirectoryNotTheStagingOne()` in
`tests/src/core/ControlStemExportTest.cpp` requires the refusal to name the
caller's directory and NOT the staging one. Before the refinement the same
slot failed with "the child said: fake-child: could not write into
.../stems/.zene-stems-staging-3557361-1792554364491"
(`.../logs/test-refine-2.log`, **EXIT=1**); after it the whole family passes
(`.../logs/test-refine-5.log`, **EXIT=0**, 7 slots, 0 failed).

## Round 1 of the review: the automated proof check could not SEE this batch's test

The reviewer's automated check reverts the product change, keeps the tests,
rebuilds and runs - and reported "ControlStemCommands, ControlStemExportVerb,
StemExportTest PASS with the product change reverted - they do not prove the
fix". The three tests it ran were the ones it DISCOVERED, and **this batch's
regression test was not among them**: `ctest -N` prints a test number right
aligned in a three-character field, so a two-digit number comes out as
`Test  #30:` (two spaces) and the check's `Test #\d+: (\S+)` pattern never
matches it. The regression test was registered 30th in `LMMS_TESTS` and was
therefore invisible to the check, which then ran the three stem tests it COULD
see - none of which exercises the changed behaviour.

**The fix.** The registration moved to the three-digit range, next to the
engine's own stem export test it belongs beside
(`tests/CMakeLists.txt:966-972`): `ctest -N` now prints
`Test #324: ControlStemExportTest`, the check discovers it, builds it (its
source is a `.cpp`, so it is in the check's target list) and runs it.

**The proof, run exactly the way the check runs it** (the product diff
reverted, the tests kept, the same four-test `ctest -R`):

| Phase | Command and result |
| --- | --- |
| Reverted | `git diff b61e5f965..HEAD -- src/core/ControlCommandsRenderStems.cpp > .../logs/product.patch`; `git apply -R .../logs/product.patch` **EXIT=0**; `cmake --build build --target zene ControlStemExportTest -j 12 > .../logs/negctl-revert-build.log 2>&1` **EXIT=0**; `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R "^(ControlStemExportTest\|StemExportTest\|ControlStemExportVerb\|ControlStemCommands)$" --output-on-failure > .../logs/negctl-revert-test.log 2>&1` **EXIT=8** - `ControlStemExportTest` FAILED with one failing slot per bug: `aFailedExportReportsTheChildsOwnError()` ("returned FALSE", the refusal read "the stem export failed (exit 3, 1 new file(s) in ...); the session is unchanged"), `aRewriteIsReportedEvenWhenTheStampDoesNotMove()` ("reported 02_Pad.wav": the rewritten stem invisible) and `aFailedRenderLeavesTheOutputDirectoryAlone()` (the earlier stem read "PARTIAL" instead of "OLD"). |
| Restored | `git checkout HEAD -- src/core/ControlCommandsRenderStems.cpp` **EXIT=0**; `cmake --build build --target zene ControlStemExportTest -j 12 > .../logs/negctl-restore-build.log 2>&1` **EXIT=0**; the same `ctest -R` > `.../logs/negctl-restore-test.log` **EXIT=0** - 4 of 4 passed. |

So the check that rejected this batch now sees the regression test fail
without the product change and pass with it.

## The affected family, and one pre-existing failure that is NOT this batch's

`ctest -R "ControlBounceRenderTest|StemExportTest|ControlRegistryTest|BounceInPlaceTest|ControlRenderJobTest|ControlStemExportTest|ControlStemExportVerb|ControlStemCommands" --output-on-failure`
(`.../logs/test-family-2.log`) **EXIT=0** - 8 of 8 passed:

| Test | Result |
| --- | --- |
| BounceInPlaceTest | Passed (0.10 s) |
| ControlBounceRenderTest | Passed (2.75 s) - the OTHER child-process render path |
| ControlRenderJobTest | Passed (8.10 s) |
| ControlStemExportTest | Passed (2.06 s) - the new regression family (7 slots) |
| ControlRegistryTest | Passed (1.32 s) |
| StemExportTest | Passed (2.63 s) - the engine's own stem export |
| ControlStemExportVerb | Passed (5.08 s) - the END-TO-END transcript against the real binary: the stems land on disk, a RE-export into the same directory still reports the files it rewrote, and a refused export creates nothing |
| ControlStemCommands | Passed (5.89 s) |

### QA follow-up: rollback a failed promotion as a set

The independent review added `aFailedPromotionRestoresEarlierStemsAndKeepsDirectories()`
to cover a later promotion failing after an earlier stem was installed. The
original promotion loop could replace that earlier file and move a colliding
directory into staging, where recursive cleanup deleted it. The handler now
keeps earlier files aside until every staged file installs, restores the prior
set on failure, and refuses a directory at a stem path.

| Check | Exact command and result |
| --- | --- |
| RED (QA fix reverted) | `cmake --build build --target zene ControlStemExportTest -j 12` **EXIT=0**; from `build/tests`, `ctest -R '^ControlStemExportTest$' --output-on-failure` **EXIT=8** - `aFailedPromotionRestoresEarlierStemsAndKeepsDirectories()` failed because the export returned success. |
| GREEN (QA fix restored) | `cmake --build build --target zene ControlStemExportTest -j 12` **EXIT=0**; from `build/tests`, `ctest -R '^ControlStemExportTest$' --output-on-failure` **EXIT=0** - 1/1 CTest passed (8 QtTest slots, 0 failed). |
| Family | `ctest -R 'ControlBounceRenderTest|StemExportTest|ControlRegistryTest|BounceInPlaceTest|ControlRenderJobTest|ControlStemExportTest|ControlStemExportVerb|ControlStemCommands' --output-on-failure` **EXIT=0** - 8/8 passed. |
| Gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3953` **EXIT=0** - all checks passed; complexity and file-length report no new item. |

`ControlRenderSurround` fails in this tree, and it failed BEFORE this batch
(`.../logs/test-family-1.log`): the two failing checks are the CLI's
refusals that `main.cpp` prints through `usageError()` -> `qCritical()`, and
this environment's Qt swallows that output - the CLI writes NOTHING to stdout
or stderr for them (reproduced by hand: `zene render <project> -o
/tmp/cli-bad.wav --channels 9.1` exits 1 with both streams empty), while the
third case of the same check, which `main.cpp:1401` prints with a direct
`fprintf(stderr, ...)`, passes. `src/core/main.cpp` is untouched by this
batch (the commits touch `src/core/ControlCommandsRenderStems.cpp`, the new
test file, `tests/CMakeLists.txt`, the two scope manifests and this report),
and the control surface is not on the CLI's path at all.
