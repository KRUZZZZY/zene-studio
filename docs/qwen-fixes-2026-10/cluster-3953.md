# Cluster 3953 / 4875 / 1234 — `render.stems`: the child's error, the change indicator and the partial stems

Verdict: FIXED

3953: FIXED

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
