ALREADY-FIXED

# Cluster new-p21-90: `triggerFrameCounter` iterating `s_controllers` on the audio thread

**Verdict: ALREADY-FIXED.**

Worktree: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p21-90`
(branch `bugs/qwen-new-p21-90`). The finding cites `src/core/Controller.cpp:180`; the loop it
describes is now at `src/core/Controller.cpp:203-215`. The finding text also names
`s_controllers` correctly (the field exists), but no function called `triggerFrameCounter`
*site* is unguarded any more: the finding was written against code that predates a fix already
on this branch.

No product file was changed by this report, so nothing in the diff and no control command is
touched (`ControlCommandsSnapshot` canary unchanged).

## What the finding claims

> the audio-thread loop reads the shared `std::vector<Controller*>` while GUI-thread
> construction/destruction may `push_back`/`erase` it; reallocation or mutation during
> iteration can dangle or crash.

That is exactly the upstream LMMS shape: the loop over `s_controllers` in
`triggerFrameCounter()` ran with no lock at all, and `Controller::Controller()` /
`~Controller()` pushed and erased without one. On this branch it is closed, at every one of the
four places that touch the vector.

## Evidence (file:line, read this session)

The exclusion is single-writer against single-reader through `AudioEngine::m_changeMutex`:

- `include/AudioEngine.h:888` — `std::recursive_mutex m_changeMutex;` (the change lock).
- `src/core/AudioEngine.cpp:1220` — `AudioEngine::renderNextPeriod()` opens with
  `const auto lock = std::lock_guard{m_changeMutex};`, so the render thread owns the change lock
  for the **whole** period, `renderStageNoteSetup()` … `renderStageMix()` (`:1236-1240`) included.
- `src/core/AudioEngine.cpp:1211` — `Controller::triggerFrameCounter();` is called from
  `renderStageMix()`, i.e. inside that lock, on the render thread. It is the only call site
  (`grep -rn "triggerFrameCounter" src include` → `src/core/AudioEngine.cpp:1211` and the
  declaration `include/Controller.h:135`).
- `src/core/Controller.cpp:44-51` — `controllerChangeGuard()` returns a real
  `AudioEngine::RequestChangesGuard` on any thread that is not inside a `RealtimeScope` and has an
  engine, and an empty guard otherwise.
- `src/core/AudioEngine.cpp:1718-1741` — `requestChangeInModel()`/`doneChangeInModel()` take and
  release that same `m_changeMutex`; both return immediately only when the **calling** thread is
  the render thread (`s_renderingThread`, declared `static thread_local bool` at
  `src/core/AudioEngine.cpp:471`), and `RealtimeScope`'s depth is likewise `thread_local`
  (`include/RealtimeScope.h:74`). A GUI thread is therefore never mistaken for the render thread.
- `src/core/Controller.cpp:203-215` — `triggerFrameCounter()` takes the guard (`:205`) before the
  loop (`:206`). On the render thread the guard is empty, so the audio thread takes **no** new
  lock, allocates nothing and logs nothing; it already holds the lock that excludes mutation.
- `src/core/Controller.cpp:72-75` (constructor: guard, then `s_controllers.push_back(this)` at
  `:75`) and `:104-110` (destructor: guard, then `s_controllers.erase(it)` at `:110`) are the only
  two mutators of the vector, and both run under the same lock. A GUI-thread construction or
  destruction therefore cannot interleave with the render period that iterates it.
- `src/core/Controller.cpp:221-227` — the sibling loop, `resetFrameCounter()`, takes the guard at
  `:223` before its `:224` loop over the same vector. `triggerFrameCounter` is not the only path
  into the shared vector, and the other one is guarded too.

Completeness check — every use of `s_controllers` in the tree
(`grep -rn "s_controllers" src include tests plugins modules`, recorded in
`evidence-2-blame-before.txt`): definition `src/core/Controller.cpp:59`, declaration
`include/Controller.h:174`, then `:75`, `:77`, `:84` (constructor), `:107`, `:108`, `:110`
(destructor), `:206` (`triggerFrameCounter`), `:224` (`resetFrameCounter`). All four functions
call `controllerChangeGuard()`. No test, plugin or GUI file touches the vector directly.

## The race was real before this branch's fix — and that fix is where it was closed

`git blame` puts the guard on this branch:

```
$ git blame -L 203,210 --date=short src/core/Controller.cpp
f4dbb2ff5c (Tobias Junghans     2009-08-24 203) void Controller::triggerFrameCounter()
3541af29df (Paul Giblock        2008-04-04 204) {
c28751cef2 (Zachariah Markusson 2026-10-09 205)     auto changeGuard = controllerChangeGuard();
da126bfb5c (Colin Wallace       2018-04-15 206)     for (Controller * controller : s_controllers)
```

and the commit that added it is `c28751cef2` *fix(automation): prepare model buffers before
worker jobs (cluster 2647)*, 2026-10-09 (`src/core/Controller.cpp | 60 +++++++-` in its stat).
The parent of that commit shows the unguarded shape the finding describes:

```
$ git show c28751cef2^:src/core/Controller.cpp | sed -n '84,92p'
Controller::~Controller()
{
        auto it = std::find(s_controllers.begin(), s_controllers.end(), this);
        if (it != s_controllers.end())
        {
                s_controllers.erase(it);
```

So the defect existed, and the branch already carries the fix. Verdict ALREADY-FIXED rather than
FALSE-POSITIVE, with `c28751cef2` as the change that closed it.

## Runtime evidence that the guarded loop executes

`ControllerSurfaceTest` drives `Controller::triggerFrameCounter()` and
`Controller::resetFrameCounter()` from a non-render thread, so it takes the change lock through
`controllerChangeGuard()` and would hang or corrupt if the guard were wrong:

```
$ cmake --build build --target ControllerSurfaceTest -j4 > .../logs/build-1-controller-surface.log 2>&1 ; echo EXIT=$?
EXIT=0

$ ctest -R "ControllerSurfaceTest" --output-on-failure > .../logs/test-2-controller-surface-rerun.log 2>&1 ; echo EXIT=$?
EXIT=8
8:PASS   : ControllerSurfaceTest::defaultPeakControllerWithoutEffectReturnsSilence()
9:PASS   : ControllerSurfaceTest::runningFrameCountDoesNotWrapAtUnsignedIntLimit()
10:PASS   : ControllerSurfaceTest::resettingFrameCounterRefreshesControllerBuffer()
```

The three frame-counter cases pass; the single failure in that binary is a pre-existing,
unrelated template-store failure — see "Other findings".

## Gates

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1-feature-check.log 2>&1 ; echo EXIT=$?
EXIT=0
feature-check kads-new-p21-90 (base fa4a5f2e7, 429 changed files; logs in .../build/feature-check)
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

## Completion check

```
$ python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-90 > .../logs/check-2-submit.log 2>&1 ; echo EXIT=$?
```

The first run (`check-1-submit.log`) printed `RESULT: PASS - submit` with `EXIT=0`; the second run
after the Self-check answer above is recorded in `check-2-submit.log`.

## Logs

All under `/home/kruzzzzy/.config/kads/zene/state/runs/new-p21-90.r1/logs/`:
`evidence-1-controller-guard.txt`, `evidence-2-blame-before.txt`,
`build-1-controller-surface.log`, `test-1-controller-surface.log`,
`test-2-controller-surface-rerun.log`, `gates-1-feature-check.log`, `git-commit-1.log`,
`check-1-submit.log`, `check-2-submit.log`.

## Self-check

**1. "The regression test FAILS with your product change reverted and PASSES with it (exit
codes)."** Not applicable: this report changes no product code, so there is no product change to
revert (see item 5 — `submit_check.py` records this as "claim-only closure"). The guarded code is
exercised at runtime by `ControllerSurfaceTest` (`EXIT=8` overall for the pre-existing template
failure, with `defaultPeakControllerWithoutEffectReturnsSilence`,
`resettingFrameCounterRefreshesControllerBuffer` and
`runningFrameCountDoesNotWrapAtUnsignedIntLimit` reported PASS in
`test-2-controller-surface-rerun.log:8-10`). I did not fabricate a red/green pair for a fix I did
not make.

**2. "The test is deterministic: no wall-clock timing, sleeps or races."** The run above uses no
timing, sleeps or threads: the three frame-counter cases call `Controller::triggerFrameCounter()`
/ `resetFrameCounter()` synchronously on the test thread and compare values. The verdict itself
rests on code + `git blame`, not on a timing measurement. Local TSan does not run on this box, so
the exclusion argument is a design argument (single writer under `m_changeMutex` at
`Controller.cpp:72-75`/`:104-110` versus a single reader that holds the same mutex for the whole
period, `AudioEngine.cpp:1220`) and I say so rather than claiming a sanitizer run.

**3. "The fix is at the cause and complete: every caller and sibling checked."** Every
`s_controllers` site is listed above and all four functions are guarded, including the sibling
loop `resetFrameCounter()` (`Controller.cpp:221-227`), which the finding does not mention. The
single caller of `triggerFrameCounter()` (`AudioEngine.cpp:1211`) is inside the locked period.
There is no second path into `s_controllers` — not in `src/`, `include/`, `tests/`, `plugins/` or
`modules/`.

**4. "No collateral damage."** No product file, test, schema, command or save format changed in
this session, so old projects and saved data load exactly as before; nothing new runs on the audio
thread (the guard resolves to an empty `RequestChangesGuard` under `RealtimeScope`, so
`Controller.cpp:205` adds no lock, allocation or log on the render thread); undo and the control
command surface are untouched.

**5. "If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it."** `src/core/Controller.cpp:205` (guard in `triggerFrameCounter`) plus
`src/core/Controller.cpp:72-75` and `:104-110` (guard around the only two mutators) plus
`src/core/AudioEngine.cpp:1220` (the render period holds `m_changeMutex` across
`renderStageMix()` at `:1240`, where `:1211` calls it), introduced by `c28751cef2`. Before that
commit the same file had no guard in the constructor or destructor (`evidence-2-blame-before.txt`).

**6. "Sibling commands in the command files you changed — confirm each is consistent with your
fix (or say why it is not affected): app.*, arrangement.fit_tracks, arrangement.get_state, …"**
The check lists those because its diff base is `fa4a5f2e78` (the merge-base with the train,
`050/m0-train-35`), and 18 control-command files arrive in that range from train merges that are
already on this branch — not from this session. My only commit on the branch is the report:

```
$ git show --stat --format=%H HEAD
7c369b38c66ec297fda4422f5bafa09a6137d638
 docs/qwen-fixes-2026-10/cluster-new-p21-90.md | 189 ++++++++++++++++++++++++++
 1 file changed, 189 insertions(+)
```

So no command, reversibility row or `commands_snapshot.json` entry is added, changed or removed by
this work, and no sibling command can disagree with it: the verdict changes no behaviour at all.

## Other findings

- `ControllerSurfaceTest::TemplatesSaveListReadAndApply` fails on this box, before and
  independently of anything in this report: `surface.saveTemplate(kTemplate)` returns false
  (`tests/src/core/ControllerSurfaceTest.cpp:484`, log `test-1-controller-surface.log:18`,
  reproduced in `test-2-controller-surface-rerun.log`, ctest `EXIT=8`). `saveTemplate()`
  (`src/core/ControllerSurface.cpp:115-133`) writes into
  `ConfigManager::userPresetsDir() + "controller-templates"`, and that preset tree does not exist
  under `~/.local/share` in this sandbox (`ConfigManager::createWorkingDir()`,
  `src/core/ConfigManager.cpp:319-326`, is what normally creates it). Environmental and
  out of scope here — not fixed, only reported.
