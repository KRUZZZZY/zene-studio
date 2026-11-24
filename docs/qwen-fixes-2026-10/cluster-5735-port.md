FIXED

# cluster 5735-port — Failed renders can leave a partial output file

## Verdict: REAL (fixed)

The finding is real on this branch. `render.render` handed the caller's own `out`
path to the CLI render child (`src/core/ControlCommandsProject.cpp:375` before this
change; the function now starts at `:465` and stages at `:472`). A child that could
not finish writing therefore left its truncated file at `out`, replacing any good
file that was already there. `renderSession` removed only the temporary project
file, never the child's partial output (`src/core/ControlCommandsProject.cpp:565-574`).

The same bug reached the other caller of `runCliRender`:
`controlRenderProjectFile` (`src/core/ControlCommandsProject.cpp:684`). That is the
render behind `project.subproject_add` and `project.subproject_refresh`
(`src/core/ControlCommandsSubprojects.cpp:157`); both now take the same staged rule.
`render.sections` goes through `render.render` itself
(`src/core/ControlCommandsRenderSections.cpp:291-295`), so it is covered too.

The sibling render paths already staged their output and were used as the model:
`RenderQueue` writes `.zene-render-<uuid>.<format>` beside the destination and
commits only on success (`src/core/RenderQueue.cpp:109-123`, `:169-192`, `:246-256`),
and `BounceInPlace` renders to `prepared.renderPath` and discards it on failure
(`src/core/BounceInPlace.cpp:498-503`). The control command was the one render path
that did not.

## Cause and fix

Before the fix, `runCliRender` built the child command line with the caller's `out`
directly:

```
QStringList arguments = controlRenderCliArguments(projectPath, out, ...);
```

The fix gives the child a unique staged name beside the destination and publishes it
only after a normal, zero exit:

- `stagedRenderPath` (`src/core/ControlCommandsProject.cpp:385`) creates
  `.zene-render-<uuid>.<format>` in the destination's own directory. The extension is
  the format's own, because the CLI appends that extension to `-o`
  (`src/core/main.cpp:1571-1575`), so the child writes exactly the path it was given.
- `discardStagedRender` (`:393`) removes the staged audio and the child's loudness
  sidecar when the child failed or timed out.
- `publishStagedRender` (`:442`) renames the finished file over `out` only on success.
  An existing file is moved aside and restored if the rename fails; a non-file at the
  destination is never moved. A symlink at `out` is resolved (`publishDestination`,
  `:402`) so a render through a link still writes the link's target, as before.
- `publishRenderSidecar` (`:417`) moves `<out>.loudness.txt` beside the caller's path
  and rewrites its `file` line from the staged name to `out`.
- `runCliRender` (`:465-520`) stages, discards on failure, publishes on success, and
  rewrites the child's quoted staged path in a refusal back to the caller's path.

No argument, schema, reply field, command id, undo step or data format changed. The
change runs only in the control handler, which already waits for a child process off
the audio path; it adds no allocation, lock, wait or log to any realtime function.

## Test matrix

| path | coverage |
| --- | --- |
| normal render | a successful `render.render` publishes over an existing file; the reply's `path`, `bytes` and `sha256` describe the file at `out`; it is RIFF/WAVE; no worker file is left |
| failure, existing destination | `RLIMIT_FSIZE` makes the child fail at a fixed size; the refusal is typed; the caller's file is byte-identical; no worker file is left |
| failure, no destination | the same injected failure leaves nothing at `out`; no worker file is left |
| reconfiguration | the loudness report on: `<out>.loudness.txt` is published beside `out`, its `file` line names `out`, and no `.zene-render-` name reaches it; report off restores the setting |
| other caller | `ControlBounceRenderTest` drives `project.subproject_add` through the stand-in render child, including the render-failure path |
| audio thread | the change is confined to the control handler and a child process; no realtime code changed |
| platforms | product code uses `QFile`/`QFileInfo`/`QUuid` only; the Python test uses POSIX `RLIMIT_FSIZE` and reports ctest `skipped` (exit 77), never passed, where it cannot inject it |

## RED / GREEN evidence

RED before the product fix (direct run, `test-1-red.log`, **EXIT=1**):

```
QT_QPA_PLATFORM=offscreen python3 tests/control-render-output-publish.py build/zene
```

The failing checks were explicit:

```
FAILED: the file the caller already had is byte-identical after a failed render
    524288 bytes on disk, 544 expected, head=b'RIFF\x08\x00\x00\x00WAVEfmt '
FAILED: a failed render left no file at the requested output path
    size=524288
```

GREEN with the fix (`test-2-green.log`, **EXIT=0**): the same command printed
`PASS: a failed render never leaves the caller a partial file`.

Rebuild after the fix: `cmake --build build --target zene -j4`
(`build-1.log`, **EXIT=0**).

## 4b: the test catches the bug

```
git diff -- src/core/ControlCommandsProject.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch
```

**EXIT=0**. Rebuilt `zene` (`build-3-red-final.log`, **EXIT=0**). ctest failed:

```
ctest -R '^ControlRenderOutputPublish$' --output-on-failure   (build/tests)
```

**EXIT=8** (`test-6-red-final.log`), with the same two bug checks FAILED and
`0% tests passed, 1 tests failed out of 1`. Then:

```
git apply .../logs/fix.patch
```

**EXIT=0**. Rebuilt `zene` (`build-4-green-final.log`, **EXIT=0**). ctest passed:

```
ctest -R '^ControlRenderOutputPublish$' --output-on-failure   (build/tests)
```

**EXIT=0** (`test-7-green-final.log`), `100% tests passed out of 1`.

## Family tests

- `ctest -R '^(ControlRenderPresets|ControlRenderSections|ControlRenderOutputPublish|ControlRenderFormatCapability)$' --output-on-failure`
  from `build/tests`: **EXIT=0**, 4/4 passed (`test-3-family.log`).
- `cmake --build build --target ControlRenderJobTest ControlRenderOutputPathTest -j4`:
  **EXIT=0** (`build-2-tests.log`); `ctest -R '^(ControlRenderJobTest|ControlRenderOutputPathTest)$'`:
  **EXIT=0**, 2/2 passed (`test-5-cpp-family.log`).
- `cmake --build build --target ControlBounceRenderTest -j4`: **EXIT=0**
  (`build-5-bounce-test.log`); `ctest -R '^ControlBounceRenderTest$'`: **EXIT=0**,
  1/1 passed (`test-8-bounce-caller.log`). This is the `controlRenderProjectFile` /
  subproject caller.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
(`gates-1.log`, **EXIT=0**, unpiped):

```
feature-check kads-5735-port (base fa4a5f2e7, 654 changed files)
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

## Self-check

- **REAL, not already fixed.** The RED run above measured a 524288-byte truncated RIFF
  at `out` where the 544-byte caller file had been, before any product change.
- **Deterministic test.** The failure is injected with a soft `RLIMIT_FSIZE` of 512 KiB
  around the instance's spawn; there are no sleeps, wall-clock budgets or retry loops
  in the failure checks. The only retry is the tree's printed guard on the separate
  successful-render path. The song renders megabytes, so the child cannot finish
  inside the limit. A host without `resource` reports skipped (77), never passed.
- **Cause, callers and siblings.** Both `runCliRender` callers now stage:
  `renderSession` (`render.render`) and `controlRenderProjectFile`
  (`project.subproject_add`/`refresh`); `render.sections` renders through
  `render.render`. `ControlBounceRenderTest`, `ControlRenderSections` and the C++
  render tests pass. `RenderQueue` and `BounceInPlace` already staged and are
  unchanged.
- **No collateral damage.** Successful renders still replace the file at `out`
  (the normal-path checks pass and the reply matches the file's own bytes/sha256); a
  directory at `out` is still left alone; a symlink at `out` still writes its target;
  the loudness sidecar keeps its name and `file` line; no command, schema, reply or
  undo step changed.
- **Realtime, undo and commands.** The changed code is in the synchronous control
  handler, which already waits for a child; no audio-thread code, undo step or
  control command was added.
- **Registration.** The new test is registered in `tests/CMakeLists.txt`
  (`ControlRenderOutputPublish`, skip code 77); `tests/CMakeLists.txt` already has an
  inherited-file entry in `tests/upstream-modifications.txt`. The product file
  `src/core/ControlCommandsProject.cpp` is already a registered fork source. The gates
  above pass.

## Other findings

- `ControlRenderSurround` fails on this host on two CLI-stderr checks
  (`--channels 9.1` and `exportstems --channels surround`): the CLI output is empty, so
  the test cannot find the expected text (`test-4-surround.log`, **EXIT=8**). This test
  invokes the CLI directly and does not call `runCliRender`, so the staging change
  cannot affect it; it is the pre-existing CLI-stderr condition noted by the earlier
  5735 lane.
- A successful `render.render` to a path without the format's extension now succeeds
  (the staged name always carries the extension and is published to `out`). Before,
  the child wrote `<out>.<ext>` and the command refused because `out` did not exist.
  No test or documented contract depended on the old refusal; it is a consequence of
  fixing the cause.
