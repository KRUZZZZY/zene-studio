FIXED

# cluster 5735 — Failed renders can leave a partial output file

## Verdict: REAL (fixed)

The finding is real on this branch, and it is worse than reported: the child wrote
straight to the caller's own path, so a render that did not finish normally did not
merely leave a `partial` file behind — it **replaced a file the caller already had**
with a truncated WAV.

Cause, before this fix (`src/core/ControlCommandsProject.cpp`, pre-change lines):

- `runCliRender` handed the caller's `out` to the render child:
  `controlRenderCliArguments(projectPath, out, …)` — the child's `-o` was the
  caller's own path.
- `renderSession` then removed only the temp PROJECT and stopped on failure
  (`QFile::remove(tempProject)` the only cleanup), so a child that was killed
  mid-write left whatever it had written at `out`.

Failure classes that reach it: a child killed on the 600 s timeout
(`renderer.waitForFinished(600000)` then `renderer.kill()`), a crashing child, and
any exit path where the child never reaches `ProjectRenderer`'s own
`QFile(f).remove()` (`src/core/ProjectRenderer.cpp:362`). The sibling render path
already knew this: `RenderQueue` stages every job (`.zene-render-<uuid>.<format>`,
`src/core/RenderQueue.cpp:109-123`) and commits it only on success
(`:169-196`), and `BounceInPlace` renders to `prepared.renderPath` and discards it
on failure (`src/core/BounceInPlace.cpp:492-498`). The control command was the
one render path that did not.

Measured before the fix (log `repro-2.log`, an `render.render` whose child was
killed mid-write): `out` held 240 bytes of the caller's file before the call and
524288 bytes of header-only RIFF after it (`head=b'RIFF\x08\x00\x00\x00WAVEfmt '`),
while the command answered `refused: the render failed (exit 25)`.

## The fix (one place: the child's own output path)

`src/core/ControlCommandsProject.cpp`:

- `stagedRenderPath` (`:385`) — the child is given `.zene-render-<uuid>.<format>`
  **beside** the destination (same filesystem, so publishing is one rename; same
  prefix `RenderQueue` uses). The extension is the format's own, because the CLI
  names a single-file render `<base name of -o> + <that format's extension>`
  (`src/core/main.cpp:1573`), so the name the child writes is the name it was given.
- `discardStagedRender` (`:393`) — a failed render removes the staged file and the
  loudness sidecar the child wrote beside it.
- `publishStagedRender` (`:442`) — the staged file is moved over `out` only after a
  clean exit; a file already at `out` is moved aside and put back if the move fails
  (`RenderQueue::commitStaged`'s rule); a destination that is not a file is never
  moved aside. `publishDestination` (`:402`) resolves a symlink at `out`, so
  rendering through a link still writes the file the link points at, as before.
- `publishRenderSidecar` (`:417`) — the loudness report rides along: it is named
  `<out>.loudness.txt` and its own `file` line names `out`, exactly as it did when
  the child rendered straight to `out`.
- `runCliRender` (`:465`) — stages (`:472`), rewrites the child's quoted path back
  to the caller's path in the refusal text (`:507`, so
  `Could not open file <out> for writing` still names the path the caller can fix),
  discards on failure (`:510`) and publishes on success (`:513`).
- `renderSession` (`:522`) and `controlRenderProjectFile` (`:684`) — both callers of
  `runCliRender`, so `render.render`, `render.sections` (which renders through
  `render.render`, `src/core/ControlCommandsRenderSections.cpp:291`) and
  `project.subproject_add/refresh` (which call `controlRenderProjectFile`,
  `src/core/ControlCommandsSubprojects.cpp:157`) now obey the same rule. No command,
  schema, argument or reply field changed; no header changed.

## Regression test

`tests/control-render-output-publish.py`, registered as `ControlRenderOutputPublish`
(`tests/CMakeLists.txt`, `SKIP_RETURN_CODE 77` for a host without POSIX resource
limits — the reason is printed and ctest says "skipped", never "passed").

Test matrix, all driven through the real binary's control socket:

| path | checks |
| --- | --- |
| normal render / publish over an existing file | reply ok; `path`, `bytes` and `sha256` describe the file at `out`; the file is RIFF/WAVE and is not the previous content; no worker file left |
| loudness report on | `<out>.loudness.txt` sits beside `out`, its `file` line names `out`, no worker name in the text, no worker file left |
| failure with a file already at `out` | typed `refused`; the caller's file is still there and byte-identical; no worker file left |
| failure with nothing at `out` | typed `refused`; no file at the requested path; no worker file left |

Determinism: the failure is **injected, not timed** — the test lowers the soft
`RLIMIT_FSIZE` around the instance's spawn, so the app and the child it spawns hit a
fixed FILE SIZE (512 KiB against a song that renders megabytes), not a wall-clock
delay. No sleeps, no polling races, no dependence on a timeout. The test does not
depend on how the child dies: whether it is killed at the limit (what happens here,
exit 25) or fails its own write and tidies up, the caller's file must survive either
way.

### RED, before the product change (`test-8-red-final.log`, EXIT=1)

```
  FAILED: the file the caller already had is byte-identical after a failed render
      524288 bytes on disk, 544 expected, head=b'RIFF\x08\x00\x00\x00WAVEfmt '
  FAILED: a failed render left no file at the requested output path
      size=524288
```

### GREEN, with it (`test-9-green-final.log`, EXIT=0, 19 checks ok)

```
  a render that finished is reported ok                                    ok
  the reply's path is the path the caller asked for                        ok
  the file at the caller's path is the render, not what was there before   ok
  the published file is a RIFF/WAVE file                                   ok
  the reply's sha256 is the sha256 of the file at that path                ok
  the reply's bytes is the size of the file at that path                   ok
  a render that finished leaves no worker file beside the destination      ok
  a render with the loudness report on is reported ok                      ok
  the loudness report sits beside the file the caller asked for            ok
  the report's own file line names the caller's path                       ok
  no worker name reaches the report                                        ok
  the loudness report's render leaves no worker file                       ok
  a render that cannot finish is refused, not reported ok                  ok
  the file the caller already had is still there after a failed render     ok
  the file the caller already had is byte-identical after a failed render  ok
  a failed render leaves no worker file beside the destination             ok
  a render that cannot finish is refused, not reported ok                  ok
  a failed render left no file at the requested output path                ok
  a failed render that writes nothing new leaves no worker file            ok
```

## Commands and EXIT codes (all logs in the run's log folder)

Build and test (ctest always from `build/tests`):

| command | log | EXIT |
| --- | --- | --- |
| `cmake --build build --target zene -j 4` | `build-5.log` | 0 |
| `cmake --build build --target ControlRenderJobTest ControlRenderOutputPathTest -j 3` | `build-2.log` | 0 |
| `ctest -R "^ControlRenderOutputPublish$" --output-on-failure` (fix in place) | `test-6-green.log` | 0 |
| `python3 tests/control-render-output-publish.py build/zene` (fix in place) | `test-9-green-final.log` | 0 |
| `ctest -R "^ControlRender(Presets|Sections|FormatCapability|OutputPublish|JobTest|OutputPathTest)$"` | `test-10-family.log` | 0 (6/6 passed) |
| `ctest -R "^ControlSuccessPaths$"` (subproject renders: the other caller) | `test-11-subprojects.log` | 8 — the two `project.import/export_midi` checks fail, pre-existing and unrelated (see Other findings); both `project.subproject_add`/`refresh` render checks pass |

Step 4b (product change reversed, then restored — `git diff -- src/core/ControlCommandsProject.cpp`):

```
git diff -- src/core/ControlCommandsProject.cpp > …/logs/fix.patch      EXIT=0
git apply -R …/logs/fix.patch                                            EXIT=0
cmake --build build --target zene -j 4                                   EXIT=0   (build-6-reverted.log)
python3 tests/control-render-output-publish.py build/zene                EXIT=1   (test-8-red-final.log, the two FAILED checks above)
git apply …/logs/fix.patch                                               EXIT=0
cmake --build build --target zene -j 4                                   EXIT=0   (build-7-restored.log)
python3 tests/control-render-output-publish.py build/zene                EXIT=0   (test-9-green-final.log, 19 checks ok)
ctest -R "^ControlRenderOutputPublish$"                                  EXIT=8 → after restore EXIT=0 (test-4b-red.log / test-4b-green.log)
```

Reproduction before any change: `repro-1.py`/`repro-2.log` (probe, EXIT=0) — the
killed child left a 524288-byte truncated RIFF where a 240-byte file had been.

Gates, `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
(`gates-2.log`, **EXIT=0**, unpiped):

```
feature-check kads-5735 (base fa4a5f2e7, 431 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

`src/core/ControlCommandsProject.cpp` is already a registered fork source
(`tests/fork-sources.txt:1850`), the new file is a test script registered in
`tests/CMakeLists.txt`, and no inherited file was touched — so no list file changed.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it
  (exit codes).** Reverted: `test-8-red-final.log`, EXIT=1, both bug checks FAILED.
  Restored: `test-9-green-final.log`, EXIT=0, 19/19 checks ok. Same result through
  ctest: `test-4b-red.log` EXIT=8 (`***Failed`), `test-4b-green.log` EXIT=0
  (`Passed`). Both interrupted by a rebuild of `zene` with the same patch state.
- **The test is deterministic: no wall-clock timing, sleeps or races.** The injected
  value is a resource limit, not a delay: the soft `RLIMIT_FSIZE` is set to 512 KiB
  around `H.start_instance(...)` and restored immediately afterwards (hard limit and
  the previous SIGXFSZ disposition untouched). The song renders megabytes, so the
  child cannot finish inside the limit at any tempo this test sets. The assertions
  are on file contents, sizes and hashes, and the only waits are the harness's
  bounded socket reads. No `sleep`, no `time.time()` budget, no retry loop that could
  turn a failure into a pass (the single retry in the success path is the tree's
  established render-child-abort flake guard and prints when it fires).
- **The fix is at the cause and complete: every caller and sibling checked.** Both
  callers of `runCliRender` go through the new staging (`render.render`,
  `controlRenderProjectFile`); `render.sections` renders through `render.render`
  (`ControlCommandsRenderSections.cpp:291`) and `ControlSuccessPaths`'s
  `project.subproject_add`/`refresh` checks pass, which is the `controlRenderProjectFile`
  path. The siblings that already stage (`RenderQueue` `.zene-render-` staging +
  `commitStaged`; `BounceInPlace`'s `prepared.renderPath` + `discardPreparedRender`)
  are unchanged and now consistent with the control command. `mastering.run` and
  `render.stems` write their own paths through their own contracts and are untouched.
- **No collateral damage.** No argument, schema, reply field, command id, undo step
  or data format changed. A file already at `out` is still replaced by a successful
  render (`ControlRenderOutputPublish` proves it byte-wise, and
  `ControlRenderPresets`/`Sections`/`FormatCapability` still pass, `test-10-family.log`
  EXIT=0 — including the refusal text check that needs the child's
  `Could not open file <path>` to name the caller's path, which `:507` guarantees).
  A directory at `out` is still left alone. A symlink at `out` still gets its target
  written (`publishDestination`). The loudness report keeps its name and its `file`
  line. Nothing new runs on the audio thread: the whole change is in the control
  handler, which spawns and waits for a child process off the audio path — no
  allocation, lock, signal or I/O was added to any realtime function.
- **ALREADY-FIXED / FALSE-POSITIVE**: not claimed; the bug was reproduced on this
  branch before the change (`repro-2.log`).
- **Other findings (not fixed here).** (1) `ControlRenderSurround` fails on this host
  on two CLI-stderr checks (`--channels 9.1`, `exportstems --channels surround`): the
  shipped CLI exits 1 with EMPTY stderr, so `usageError`'s `qCritical()`
  (`src/core/main.cpp:341-346`) is swallowed here — reproduced with the product patch
  REVERTED (`test-4b-surround-reverted.log`, EXIT=8, identical two failures) and by
  running the CLI by hand, so it is pre-existing and unrelated. (2) `ControlSuccessPaths`
  fails `project.import` / `project.export_midi` because this build has no loadable
  MIDI import/export filter (`no import filter in this build takes …`), unrelated to
  renders. (3) `render.render` onto a path without the format's extension used to be
  a failure and now succeeds (the staged name always carries the format extension);
  no test or documented contract depends on the old failure, and it is a consequence
  of fixing the cause rather than a separate change.
