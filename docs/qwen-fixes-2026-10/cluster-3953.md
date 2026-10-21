# Cluster 3953 / 4875 / 1234 — `render.stems`: the child's error, the change indicator and the partial stems

Verdict: FIXED

3953: FIXED
4875: FIXED

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

**The fix.** The child's output now goes to two temporary FILES (never to a
pipe: a pipe fills up and blocks the child, the same reason the whole-project
render reads a file - `src/core/ControlCommandsProject.cpp:326`), and the
refusal carries the child's own last words, bounded to
`kStemChildErrorBytes` (2048) at the TAIL:

- `src/core/ControlCommandsRenderStems.cpp:87-91` - the new bound.
- `src/core/ControlCommandsRenderStems.cpp:158-176` - `tailOfFile()`, the
  bounded tail of a file.
- `src/core/ControlCommandsRenderStems.cpp:178-222` - `runCliStemExport()` takes
  the output directory it writes into and hands the child's tail back.
- `src/core/ControlCommandsRenderStems.cpp:224-236` - `stemExportFailureMessage()`,
  the refusal with the child's words appended.
- `src/core/ControlCommandsRenderStems.cpp:300-306` - the handler uses it.

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

- `src/core/ControlCommandsRenderStems.cpp:250-256` - `StemFileState`, the
  digest and the stamp.
- `src/core/ControlCommandsRenderStems.cpp:258-274` - `digestOfFile()`, the
  SHA-256 of a file's content, hashed in 64 KiB chunks so a long stem is never
  held in memory whole.
- `src/core/ControlCommandsRenderStems.cpp:276-292` - `listingOf()`, name ->
  state.
- `src/core/ControlCommandsRenderStems.cpp:294-325` - `writtenStems()`: a file
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
