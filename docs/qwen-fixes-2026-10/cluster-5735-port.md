FIXED

# cluster 5735-port — Failed renders can leave a partial output file

5735-port: FIXED

## Verdict: REAL, fixed (round 3)

The finding is real. Before round 2, `render.render` handed the caller's own `out` to
the CLI render child, so a child that could not finish left its truncated WAV at `out`
(round-2 RED, before any product change: 524288 bytes of truncated RIFF where the
caller's 544-byte file had been). Round 2 staged the child's output and published it
only on success. Review sent round 2 back for three defects, all addressed here:

1. **[regression] Symlink chains.** Round 2 followed one symlink hop
   (`publishDestination`) and renamed the render onto the intermediate link. That
   replaced `middle` with a file and left the terminal target unchanged. Now
   `resolveRenderTarget` (`src/core/ControlRenderStaging.cpp:173`) follows the whole
   chain one hop at a time. It reads each link's raw text (`readSymLink` on Qt >= 6.6,
   `symLinkTarget` on older Qt, `:70`) and resolves relative text against the folder
   of the link holding it. It refuses after `MaxRenderSymlinkHops = 40`
   (`include/ControlRenderStaging.h:41`) and accepts a missing final file.
   `runCliRender` resolves the chain before staging
   (`src/core/ControlCommandsProject.cpp:372`). The staging file (`stagedRenderPath`,
   `:165`) and the `.zene-backup-` file (`publishStagedRender`, `:111`) both go in the
   final target's folder, so publishing is a same-filesystem rename onto the real file
   and every link stays a link. The loudness report is still published beside `out`
   (`publishRenderSidecar`, `:85`).
2. **[not-fixed] Timeout cleanup raced the child.** Round 2 called `kill()` and then
   removed the staging file at once. On Windows the child may still hold the file open
   at that point. Now `abandonCliRender` (`:149`) runs `kill()` and then
   `waitForFinished(30000)` (`:153-154`), and calls `discardStagedRender` only when
   `state() == NotRunning`. `discardStagedRender` (`:53`) now returns whether it
   succeeded and retries the removal 5 times, 100 ms apart. A staging file that stays,
   or a child that cannot be confirmed gone, is named in the refusal: "a partial render
   could not be removed: <path>" (`:158`). A render is published only when the child
   exited normally with code 0: `renderExitCode` (`:135-139`) never treats a crash as
   0. The hard-coded 600000 ms timeout is now `renderTimeoutMs()` (`:189`).
   `ZENE_CONTROL_RENDER_TIMEOUT_MS` replaces it only when set to a positive integer;
   that is the test seam for the timeout path.
3. **[weakened-check] Runtime failures were skips.** In
   `tests/control-render-output-publish.py`, `CannotInject` is raised only by
   `file_size_limit` when `resource`, `RLIMIT_FSIZE` or `SIGXFSZ` is unavailable, and
   only it maps to exit 77. `H.Timeout`/`H.Blocked`, connection errors and startup
   failures now return 1 and print the app log (`log_on_error`).

The staging helpers moved from `ControlCommandsProject.cpp` into the new
`src/core/ControlRenderStaging.cpp` / `include/ControlRenderStaging.h`. Their bodies
are unchanged by the move; the move keeps `ControlCommandsProject.cpp` under the
file-length gate. Both callers of `runCliRender` get the same behaviour:
`renderSession` (`render.render`, which `render.sections` also goes through) and
`controlRenderProjectFile` (`src/core/ControlCommandsProject.cpp:586`, used by
`project.subproject_add` and `project.subproject_refresh`).

## Test matrix (tests/control-render-output-publish.py, ctest `ControlRenderOutputPublish`)

| Path | Check |
|---|---|
| normal | render over an existing file publishes RIFF/WAVE; the reply's bytes/sha256 match; no worker file is left |
| normal + sidecar | the loudness report sits beside `out` and names `out` |
| chain, absolute | `out -> middle -> target`: both links keep their text, target is RIFF/WAVE matching the reply's sha256/bytes, no worker file in either folder |
| chain, relative | the same with relative link texts in different folders |
| failure (RLIMIT_FSIZE 512 KiB) | refused; the previous file is byte-identical; nothing is created when `out` was absent; no worker file |
| failure through a chain | target byte-identical, both links unchanged, no worker file |
| timeout (`ZENE_CONTROL_RENDER_TIMEOUT_MS=1`, 64-bar song) | refused; `out` byte-identical; no `.zene-render-*` file; no child of the instance (running, zombie or render command line) in `/proc` when the reply arrives |
| harness failure | missing binary → exit 1 (not 77) |

The failures are injected, not timed: a file-size rlimit, and a 1 ms bound on a render
of many megabytes. Audio-thread constraints: none apply. The changed code runs in the
synchronous control handler, which already blocks on a child process. Platforms: the
Qt API is guarded for Qt5 (`QT_VERSION` check around `readSymLink`). Hosted Qt5/MSVC
builds were not run here.

## Commands and EXIT codes (logs in `~/.config/kads/zene/state/runs/5735-port.r3/logs/`)

The final 4b proof, on the final product layout:

```
git diff -- src/core/CMakeLists.txt src/core/ControlCommandsProject.cpp src/core/ControlRenderStaging.cpp include/ControlRenderStaging.h > logs/fix.patch   EXIT=0 (493 lines)
git apply -R logs/fix.patch                                              EXIT=0
cmake --build build --target zene -j4 > logs/build-5-red.log             EXIT=0
(build/tests) ctest -R ControlRenderOutputPublish --output-on-failure > logs/test-7-red.log   EXIT=8
git apply logs/fix.patch                                                 EXIT=0
cmake --build build --target zene -j4 > logs/build-6-green.log           EXIT=0
(build/tests) ctest -R 'ControlRenderOutputPublish|ControlBounceRender|ControlRenderSections|ControlRenderSurround' --output-on-failure > logs/test-8-green.log   EXIT=8 (only ControlRenderSurround failed: pre-existing, see Other findings)
```

RED (`test-7-red.log`) failing checks: for both the absolute and the relative chain,
"middle.wav is still a link with the text it had", "the final target is the render, a
RIFF/WAVE file", "the reply's sha256 …" and "the reply's bytes …". Also "a render that
timed out is refused, not reported ok" and "the file the caller already had is
byte-identical after a timed-out render".

GREEN (`test-8-green.log`): ControlBounceRenderTest Passed, ControlRenderSections
Passed, ControlRenderOutputPublish Passed.

Earlier runs in this round, on the same product code before the file move:
`test-1-green.log` (3 of 3 passed, EXIT=0), `test-2-red.log` (EXIT=8, the same
failures) and `test-3-green.log` (EXIT=0).

```
QT_QPA_PLATFORM=offscreen python3 tests/control-render-output-publish.py build/zene > logs/test-4-green-direct.log   EXIT=0
python3 tests/control-render-output-publish.py /tmp/qa5735-no-such-zene > logs/test-5-missing-binary.log         EXIT=1
```

In `test-4-green-direct.log` every check reads `ok`, including "no render child of the
instance is running or unreaped when the reply arrives". The missing-binary run prints
`FAILED: the run did not complete: Timeout: no lmms binary at …`. Before this round it
printed SKIPPED and exited 77.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-3.log`: **EXIT=0**

```
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

An earlier run (`gates-1.log`, EXIT=1) failed file-length on
`ControlCommandsProject.cpp` (552 counted lines). That is why the helpers moved to
`ControlRenderStaging.cpp`.

## Self-check

- **REAL, not already fixed.** The round-2 RED reproduced the truncated file at
  `out`. The round-3 RED (`test-7-red.log`, EXIT=8) shows the round-2 product still
  replacing the intermediate link and ignoring the timeout path.
- **Fails without the fix, passes with it.** With the product patch reversed,
  `ctest -R ControlRenderOutputPublish` gave EXIT=8 (`test-7-red.log`). With it
  re-applied, the test Passed (`test-8-green.log`); the direct run gave EXIT=0
  (`test-4-green-direct.log`).
- **Old projects and limits.** No project load or save code changed; the change is
  confined to where a control render writes its output file. The new 40-hop limit
  matches the kernel's own MAXSYMLINKS on Linux, so a chain the OS would follow is
  never refused. The timeout seam applies only when the env var is set.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not claimed (verdict FIXED).
- **Deterministic test.** No sleeps and no wall-clock waits in the test. Failures are
  injected with RLIMIT_FSIZE and the timeout seam (1 ms against a 64-bar render); the
  `/proc` scan runs right after the reply. Exit 77 is reserved for a host that cannot
  inject RLIMIT_FSIZE. The only retry is the existing printed retry on the
  successful-render path, which works around a known shutdown abort of the render
  child. A real failure fails both attempts.
- **Cause, callers and siblings.** Both callers of `runCliRender` go through
  `resolveRenderTarget` and `finishCliRender`. `RenderQueue` and `BounceInPlace` keep
  their own staging, unchanged. `render.sections` uses `render.render`.
- **Timeout race design (no local TSan).** On timeout the parent process is the only
  one that touches the staging file after `kill()`, and it does so only after
  `waitForFinished` confirms the child is gone (`state() == NotRunning`). If the child
  cannot be confirmed gone, the file is not removed and its path goes into the
  refusal. This ordering matters on Windows, where an open file cannot be deleted. On
  Linux the test sees the child reaped before the reply arrives. The Windows behaviour
  is reasoned from the code, not run.
- **No collateral damage.** Renders to plain paths behave as in round 2. A symlink at
  `out` now updates the file the whole chain ends at; that is where a direct write
  through the link would land. Without `ZENE_CONTROL_RENDER_TIMEOUT_MS` the timeout is
  still 600000 ms. A crashed child whose exit code reads 0 is now reported as `-1` and
  never published. A crashed child with a non-zero code keeps that code in the
  message. No command, schema or undo step changed.
- **Realtime, undo and commands.** No audio-thread code was touched and no control
  command was added. The env var is a test seam, not a command.
- **Registration.** `src/core/ControlRenderStaging.cpp` and
  `include/ControlRenderStaging.h` are in `tests/fork-sources.txt` and
  `tests/all-sources.txt`. `src/core/CMakeLists.txt` already has its entry in
  `tests/upstream-modifications.txt`. The gates pass.
- **Sibling commands listed by the completion check** (`app.*`, `arrangement.*`):
  these are as answered in round 2. Their handlers are in
  `src/gui/ControlCommandsShell.cpp`, `src/gui/ControlCommandsArrangerView.cpp` and
  `src/core/ControlCommandsArrangementState.cpp`, and none of them renders through
  `runCliRender` or writes a render output.

Delegation: Sonnet wrote the extended regression test (chains, timeout, exit-code
split) and then moved the staging helpers into `ControlRenderStaging.*` with their
registration. Haiku ran both build/RED/GREEN sequences. I wrote the product fix,
reviewed both diffs, ran the direct test and the gates, and wrote the report.

## Other findings

- `ControlRenderSurround` fails on this host (`test-8-green.log`): the CLI's refusals
  for `--channels 9.1` and `exportstems --channels surround` lack the expected text.
  The test invokes the CLI directly, not `runCliRender`. It also failed in round 2
  before this round's changes, so the cause predates this work.
- A `render.render` to a path without the format's extension succeeds, as it did after
  round 2: the staged name carries the extension and is published to `out`.
