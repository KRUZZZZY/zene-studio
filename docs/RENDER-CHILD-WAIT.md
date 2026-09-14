# A render's wait: one engine start, a dead control surface, and the arm64 bound

**Measured 2026-09-14 in `zene-030` at `e20f94971`.** This is the record of why
`ControlFreezeCommandsTranscript` and `ControlTrackFolderTranscript` reported a
HANG on `linux-arm64` while the same commands passed on `linux-x86_64` in the
same run, and of what was and was not fixed.

## The CI evidence, and what it does not say

The job's own words, at a `render.render` issued from
`tests/freeze_bounce_evidence.py`'s `render_measure`:

```
control_socket_harness.Blocked: no response line inside 30.0s (socket timed out)
diagnosis: the instance is STILL RUNNING (pid 40056) - a HANG, not a crash
cpu: burned 0% of one core over 1.0s (3553 -> 3553 ticks)
5 threads, all state=S: poll_schedule_timeout, futex_do_wait x3, hrtimer_nanosleep
```

Every thread asleep, 0% CPU, instance alive. That is *consistent with* a lost
wake-up, which is what it was read as. It is not one. Two facts decide it:

* the futex waiters are the idle audio-engine worker pool
  (`AudioEngineWorkerThread::run` parks in `QWaitCondition::wait` with the
  bounded `kQuitRecheckMs` re-check that exists precisely to bound a lost
  wake-up), and an IDLE instance has exactly that thread set: main thread, N-1
  workers, the audio-dummy thread (`hrtimer_nanosleep`). On a 4-vCPU runner that
  is 5 threads, and nothing about the profile distinguishes "idle" from
  "waiting";
* the ONE thread that matters - the main thread, which serves the control
  socket - is in `poll_schedule_timeout`, not in a futex. A thread that lost a
  wake-up sleeps on a futex. A thread waiting for a CHILD PROCESS polls.

That is what it is doing. Measured on this box, with the same sources:

```
$ ps -o pid=,stat=,args= --ppid <instance>
939374 R  .../build/zene render /tmp/zene-render-939046-*.mmp -o /tmp/...wav -f wav -s 44100
$ cat /proc/<instance>/wchan
poll_schedule_timeout.constprop.0
```

and the backtrace of a failing run names the chain end to end (see
"the diagnosis now names it" below):

```
#2  QProcess::waitForFinished(int)
#3  lmms::runCliRender                                  src/core/ControlCommandsProject.cpp:305
#4  lmms::renderSession                                 src/core/ControlCommandsProject.cpp:344
#18 lmms::ControlServer::dispatchLine                   src/core/ControlServer.cpp:422
#20 lmms::ControlServer::onClientReadable               src/core/ControlServer.cpp:258
```

## The mechanism

`render.render` does not render in this process. It serialises the session to a
temp project and runs the product's own CLI render **in a child process**
(`ControlCommandsProject.cpp:297-309`), because rendering in-process would drive
the running instance's audio engine (`ProjectRenderer` swaps the audio device).
`renderSession` then waits for that child with

```c++
const bool finished = renderer.waitForStarted(30000) && renderer.waitForFinished(600000);
```

on the thread that serves the control socket. While it waits:

* the instance answers NOTHING. Measured: `control.ping` on a second connection,
  3s into a 35s render, is not answered inside a 10s budget, while the child is
  in state `R`;
* the child pays a **full engine construction** (`Engine::init`, once per
  process), which the project has measured at **~34 s on the linux-arm64 runner**
  against ~0.3 s here (`docs/control-arm64-cluster-logs/EVIDENCE.md`, item
  `9d15bd7e9`, still open).

So `render.render`'s cost on that job is one engine start plus the audio, and the
harness bounded it at `SOCKET_TIMEOUT` = 30 s - a bound written for a socket
round trip. The SAME command passes in `ControlSocketIntegration`, whose client
waits 60 s (`tests/control-socket-integration.py:42`), which is the contrast that
makes this a bound defect and not an arm64 mystery.

`bounce.in_place`, `freeze.track` and `freeze.region` call the same helper
(`src/core/BounceInPlace.cpp:179`), so they carry the same cost.

## What was fixed

1. **`tests/freeze_bounce_evidence.py`** - `RENDER_TIMEOUT = 180.0` and
   `RENDER_COMMANDS`, applied in `Session.call`. A render gets the treatment
   readiness already has (`control_socket_harness.STARTUP_BOUND`: "a declared
   budget, never one socket read"); every other command keeps the 30 s that makes
   a genuine hang cost seconds. Not a raise of `SOCKET_TIMEOUT`.
2. **`tests/control_instance_diagnosis.py`** - the failing test's own evidence now
   (a) prints the MAIN thread's backtrace first (a bare `bt` before
   `thread apply all bt`, which lists threads in descending order and pushed the
   main thread past `_DEBUGGER_LINES`, so the frame that explained the stall was
   never in the log), and (b) lists the instance's CHILD PROCESSES with their
   command lines. A render child in flight is what the CI log could not see.
3. **`tools/local-ci.sh`** - `-DWANT_VST3_TEST_INSTRUMENT=ON` added to
   `CI_CMAKE_OPTS`, which claimed to be the linux-x86_64 job's options "byte for
   byte" while omitting it: the local bar ran 137 tests where the job runs 140.

## The proof (before / after, with the arm64 cost injected)

This box cannot reproduce the arm64 engine start, so it is injected: a busy loop
at the top of `Engine::init`, uncommitted, `ZENE_SLOW_ENGINE_START_MS=<ms>` (all
processes) / `ZENE_SLOW_RENDER_CHILD_MS=<ms>` (render-only children). A burn, not
a sleep, because the arm64 observation is one core burning inside the engine
start. Exit codes unpiped, logs in `/home/kruzzzzy/zene-030-render/`:

| run | command | result |
|---|---|---|
| BEFORE | `ZENE_SLOW_ENGINE_START_MS=35000 python3 tests/control-freeze-commands-transcript.py build/zene` | **EXIT=1, 70.7 s** - `Blocked: no response line inside 30.0s (socket timed out)`, `the instance is STILL RUNNING (pid 937694) - a HANG, not a crash`, from `freeze_bounce_evidence.py:121 render_measure -> render.render`, main thread `wchan=poll_schedule_timeout` (the CI reported 67.19 s / 68.10 s) |
| AFTER | the same, with `RENDER_TIMEOUT` | **EXIT=0, 499.9 s** - 13 `slow render: <cmd> took 35.5s` lines, every check held |
| NEGATIVE CONTROL | `ZENE_SLOW_RENDER_CHILD_MS=300000` (child outlasts the 180 s bound) | **EXIT=1, 185.7 s** - `no response line inside 180.0s`, and the new diagnosis names `child 979790: state=R ... zene render ... -f wav -s 44100` |

The control is what says the bound was not weakened into vacuity: a child that
never finishes still fails, inside the 900 s ctest `TIMEOUT` the transcripts
carry.

## What is NOT fixed, and is a product defect

**The control surface is dead for the whole lifetime of the render child.** An
agent driving `render.render` (or a bounce/freeze) gets no answer to anything -
not even `control.ping` - for up to `waitForFinished(600000)`, so a slow render
and a hung instance are indistinguishable from outside. That is why the arm64
job's diagnosis said "a HANG, not a crash" and cost this line cycles.

The fix is not a bound: the dispatch thread must not block on the child at all.
`ControlServer::dispatchLine` already treats an EMPTY reply as "nothing to send
for this request", which is exactly the shape a deferred reply needs:

* `renderSession` starts the child and returns a deferred result; a
  `QProcess::finished` handler, delivered by the ordinary event loop, builds the
  result (frames, sha256, bytes) and delivers it;
* `ControlServer` hands the handler a reply sink bound to `(fd, request id)`;
* replies for requests that arrive while a deferred reply is outstanding must be
  QUEUED behind it (the `Client::pending` tail already exists for a partial
  write; this is the same idea for whole lines), and a second render refused with
  `busy`;
* `control.ping` stays answerable throughout - it is the probe whose whole
  purpose is to be answerable when everything else is stuck.

That is a change to the dispatch core with a lifetime to get right (the client
disconnecting mid-render, the instance quitting mid-render, reply order), so it
belongs in its own lane with the full 140-test suite behind it - not in the
defect-hunt lane that measured this.

## Residual risks, stated

* The renders on linux-arm64 will now be ~35 s EACH (13 of them in
  `ControlFreezeCommandsTranscript`), so that test's measured wall time there is
  ~500-650 s against its 900 s ctest `TIMEOUT`. It should pass, with less margin
  than is comfortable. The honest fix for that is the arm64 engine-start cost
  (`9d15bd7e9`), which is untouched.
* `render.render` still spawns a second engine. In-process rendering of
  `render.render` was implemented and MEASURED here (a real render: 226560
  frames, -11.04 dBFS, `QEventLoop` + `RenderManager`), and it works - but
  `bounce.in_place`/`freeze.*` cannot follow: they need the serialised copy and
  the mute pass that only a separate process gives them, so the arm64 cost would
  merely move to the next command. Removing the child for `render.render` alone
  buys nothing on that job.
