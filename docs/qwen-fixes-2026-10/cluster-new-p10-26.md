FIXED

# cluster new-p10-26 — stopping an asio context from its own callback

BUG 6: `src/3rdparty/ableton-link/link/include/ableton/platforms/asio/Context.hpp`
(the vendored Ableton Link 4.1 asio port; historical `Context.hpp:~112`).

## Verdict: REAL, fixed at the cause

`Context::stop()` joined the worker unconditionally, and the destructor joined it
only while the work object was still set:

```cpp
// before (HEAD of this branch, Context.hpp:113-129)
~Context()
{
  if (mpService && mpWork) { mpWork.reset(); mThread.join(); }
}

void stop()
{
  if (mpService && mpWork) { mpWork.reset(); mpService->stop(); mThread.join(); }
}
```

Three facts make that a process terminator, all read in this session:

1. `mThread` is the context's own worker (`Context.hpp:81-95`: the thread body is
   `service.run()` in a loop that catches only
   `typename ExceptionHandler::Exception`). A thread that calls
   `std::thread::join()` on itself is refused by libstdc++/glibc with
   `std::system_error` "Resource deadlock avoided" — not the handler's exception
   type — so it escapes the thread body and `std::terminate` runs.
2. Handlers posted with `async()` run on that worker (`Context.hpp:238-240`:
   `::asio::post(*mpService, ...)`), so "a Link callback calls stop()" is exactly
   a self-join. Link itself creates the context with its own handler type
   (`link/Controller.hpp:154`: `mIo(IoContext{UdpSendExceptionHandler{this}})`) and
   its callbacks are posted onto the same service
   (`link/Controller.hpp:553-556`, `RtClientStateSetter::CallbackDispatcher`), so
   no exception type other than that one is caught on the worker.
3. After a *successful* `stop()` the destructor's guard is already false
   (`mpWork` was reset), so a stop() that did not join (the self-stop case, but
   also any stop() whose join was skipped) leaves `mThread` joinable and
   `~Context` skipped the join — a `std::thread` destroyed while joinable calls
   `std::terminate` as well. That is the "ensure a different thread eventually
   joins the worker" half of the finding.

Reproduced before any edit: the new regression test aborted the process with

```
terminate called after throwing an instance of 'std::system_error'
  what():  Resource deadlock avoided
Received signal 6 (SIGABRT)
```

(`logs/test-1-red.log`, and again after the product change was reverted,
`logs/test-4-red-patch-reverted.log`).

## The fix

`src/3rdparty/ableton-link/link/include/ableton/platforms/asio/Context.hpp`
(the only product file changed):

* `stop()` (`Context.hpp:137-149`) still resets the work object and stops the
  service, then returns without joining when there is no joinable thread or when
  the caller *is* the worker (`std::this_thread::get_id() == mThread.get_id()`).
  The service is stopped either way, so the worker's `run()` returns as soon as
  the handler returns, and the join is left to the next `stop()` from another
  thread or to the destructor.
* `~Context()` (`Context.hpp:113-125`) now decides the join on
  `mThread.joinable()` instead of on `mpWork`, so a worker left joinable by a
  self-stop is still joined, by the thread that destroys the context. Its
  behaviour is otherwise unchanged (it still does not call `mpService->stop()`,
  it only releases the work guard and joins).

CCN of both functions stays small (stop: work-guard test, joinable/self test;
destructor: two tests), and nothing on the audio path is touched: this header is
used only by `src/core/AbletonLinkSession.cpp` through `ableton::Link`, whose
audio path is `captureAudioSessionState`/`commitAudioSessionState` (no thread
call, no start/stop there).

### Siblings and callers checked

* Callers of `Context::stop()`: none in this repository — `grep` for `stop()` in
  `src/3rdparty/ableton-link/link/` finds only Link's own start/stop *transport*
  state names; `~Context()` (via `util::Injected<IoContext> mIo`,
  `link/Controller.hpp:845`) is the only in-tree user. The defect is in the
  public API of the vendored port, which is why the test drives it directly.
* Platform siblings of the same class: the ESP32 port's `stop()` is `void stop() {}`
  (`platforms/esp32/Context.hpp:157`) and its task is deleted by
  `~ServiceRunner`; no join exists there, so nothing to align.
  `plugins/CarlaBase/.../hylia/link/ableton/platforms/asio/Context.hpp` is a
  third-party plugin's own copy, not built by any Zene target (listed under
  "Other findings").
* Thread factories: the Windows, macOS/`darwin` and default factories all return
  `std::thread`, so `joinable()`/`get_id()` are available on every supported
  platform build; no POSIX-only call is used.

## Regression test

New: `tests/src/core/AbletonLinkContextStopTest.cpp` (registered in
`tests/CMakeLists.txt` under `LMMS_HAVE_ABLETON_LINK`, linked to `ableton_link`,
listed in `tests/fork-sources.txt`). Five slots:

| path | what it proves |
| --- | --- |
| normal | `stopFromAnotherThreadJoinsTheWorker`: stop() from the application thread joins; a second stop() is a no-op |
| failure (the bug) | `stopFromTheWorkerThreadReturnsInsteadOfJoiningItself`: a handler on the worker calls `stop()`; the call returns and the process lives |
| destructor | `theDestructorJoinsAWorkerThatStoppedItself`: after a self-stop, destroying the context joins the worker instead of terminating |
| failure path of the loop | `theWorkerSurvivesAnExceptionItHandles`: the worker catches its handler's own exception type and keeps running |
| reconfiguration | `aMovedFromContextStopsAsANoOp`: a moved-from context has no service and no joinable thread, so stop() stays a no-op |
| exhaustion | not applicable: this file creates no resource that can run out; a failed `std::thread` construction throws from the factory before any of this code runs |
| audio thread | not applicable: nothing in `Context.hpp` runs per audio period (the audio half is `captureAudioSessionState`/`commitAudioSessionState`, untouched) |
| platform builds | `std::thread::joinable/get_id` only — no new platform-specific API; the same change compiles on the Windows/macOS factories listed above |

Determinism: no sleep and no wall-clock assertion. The context is built with a
test `ThreadFactory` (its documented template seam) that counts "the worker
function returned"; a `std::promise`/`std::future` handed to the posted handler
says when the self-stop returned. `std::thread::join` synchronises with the
worker's exit, so after a joining `stop()`/destructor the count is 1, which a
detach or a skipped join would not guarantee. The only duration in the file
(`deadlockGuard`, 30 s) bounds `future::wait_for` so a deadlock fails the test
instead of hanging the suite; no assertion reads it.

## Commands and exit codes (all logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-26.r1/logs/`)

Configuration note: `cmake -S . -B build` is refused in a lane
(`config-1.log`, `EXIT=97`, "bunny-guard: configuring is the parent's job"). The
new test target was registered by building an existing target, which runs
CMake's own `cmake_check_build_system` regeneration:
`cmake --build build --target AbletonLinkWorkDispatcherTest -j4` →
`build-2-regenerate.log`, last line `[100%] Built target AbletonLinkWorkDispatcherTest`;
`build/tests/CTestTestfile.cmake` then lists `AbletonLinkContextStopTest`.

RED (test first, product code untouched):

```
cmake --build build --target AbletonLinkContextStopTest -j4      -> build-3-red.log  (Built target)
cd build/tests && ctest -R AbletonLinkContextStopTest --output-on-failure
                                                                 -> test-1-red.log   EXIT=8
```

`test-1-red.log`: `Subprocess aborted`, `terminate called after throwing an
instance of 'std::system_error'` / `what(): Resource deadlock avoided`,
`Received signal 6 (SIGABRT)`, `0% tests passed, 1 tests failed out of 1`.

GREEN (fix applied):

```
cmake --build build --target AbletonLinkContextStopTest zene -j4 -> build-7-restored.log
                                                                 -> build-9-final.log, EXIT=0
cd build/tests && ctest -R AbletonLinkContextStopTest --output-on-failure
                                                                 -> test-6-green-restored.log EXIT=0
                                                                 -> test-9-final-green.log    EXIT=0
./AbletonLinkContextStopTest  (five slots, direct run)           -> Totals: 7 passed, 0 failed
```

Step 4b, the test catches the bug (product hunk reverted, tests kept):

```
git diff -- src/3rdparty/.../asio/Context.hpp > logs/fix.patch   DIFF_EXIT=0
git apply -R logs/fix.patch                                      REVERSE_APPLY_EXIT=0
cmake --build build --target AbletonLinkContextStopTest AbletonLinkSessionTest -j4
                                                                 -> build-6-reverted.log (Built target)
cd build/tests && ctest -R AbletonLinkContextStopTest             -> test-4-red-patch-reverted.log RED_EXIT=8
git apply logs/fix.patch                                         RESTORE_APPLY_EXIT=0
cmake --build build --target AbletonLinkContextStopTest zene -j4 -> build-7-restored.log
cd build/tests && ctest -R AbletonLinkContextStopTest             -> test-6-green-restored.log GREEN_EXIT=0
```

The reversed run aborts on the same `std::system_error` / "Resource deadlock
avoided" as the original RED, so the test fails without the product change and
passes with it.

Family (`ctest -R AbletonLink`, rebuilt against the fixed header —
`build-8-family-fixed.log`): `test-8-final-family.log` EXIT=8, 5 of 6 passed:

```
AbletonLinkSessionTest ....... ***Exception: SegFault   (see Other findings)
AbletonLinkAllocatorTest ..... Passed
AbletonLinkInteropTest ....... Passed   (constructs ableton::Link twice, so it also
                                         exercises ~Context on teardown)
AbletonLinkEsp32RunnerTest ... Passed
AbletonLinkWorkDispatcherTest  Passed
AbletonLinkContextStopTest ... Passed
```

`AbletonLinkSessionTest` fails identically **without** this fix
(`test-5-session-without-fix.log`, SEGFAULT on the reverted tree), so it is not a
regression from this change; it is a pre-existing defect of that test on this
host (no multicast delivery).

Gates:

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   -> gates-1.log EXIT=0
  PASS fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
       no-tautology, doc-figures, complexity (no new item), file-length (no new item)
  RESULT: PASS
```

That first run was taken while the new test source was still untracked, so the
scope recipes (which read `git ls-files`) did not yet derive it. Once the test was
committed the reproduce gate asked for it in the whole-tree manifest: `gates-2.log`
EXIT=1, `FAIL fork-sources` and `FAIL all-sources-reproduce`, each naming the one
missing entry `tests/src/core/AbletonLinkContextStopTest.cpp`. The entry was added
to `tests/all-sources.txt` at the recipe's sorted position (between
`AbletonLinkAllocatorTest` and `AbletonLinkEsp32RunnerTest`) and both gates then
passed on their own:

```
bash tests/all-sources-reproduce.sh  -> gate-all-sources.log  EXIT=0
   "REPRODUCES: the entry list in all-sources.txt is the recipe's own output."
bash tests/fork-sources-gate.sh      -> gate-fork-sources.log EXIT=0
   "PASS: every tracked source in scope is registered (2769 fork-NEW, 1213 inherited, 49 tooling)."
```

Registration and documentation in the same commit: the changed vendored file is
declared in `tests/upstream-modifications.txt:2087` (its entry now states the
local modification), the new test file is in `tests/fork-sources.txt` and
`tests/all-sources.txt`, the modification list in
`src/3rdparty/ableton-link/README.lmms` says "three" and names this change with
its test, and `docs/LINK-SYNC.md` no longer claims "Local modifications: none" (it
points at those lists). `MANIFEST.tsv` still records the upstream blob shas, as it
does for the two ESP32 files that were already modified. No command id, control
command, GUI file or `commands_snapshot.json` was touched, so the
`ControlCommandsSnapshot` canary is unchanged.

Commits on `bugs/qwen-new-p10-26`: `a4591cdcb5` (product change, test, registration
and this report) and `5fe19db01c` (the whole-tree manifest entry).

Completion check:

```
python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p10-26
   -> submit-check-2.log EXIT=0, "RESULT: PASS - submit"
```

(An earlier run, `submit-check-1.log` EXIT=1, failed only the gate row above for
the missing manifest entry; its ASKs are answered below.)

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with
  it (exit codes).** Reverted: `test-4-red-patch-reverted.log`, `RED_EXIT=8`
  (`Subprocess aborted`, `std::system_error`, "Resource deadlock avoided");
  restored: `test-6-green-restored.log` and `test-9-final-green.log`,
  `EXIT=0`. Both after a rebuild of the target in each state
  (`build-6-reverted.log`, `build-7-restored.log`).
* **The test is deterministic: no wall-clock timing, sleeps or races.** It uses
  two seams: a test `ThreadFactory` (the `Context` template parameter) that
  counts returned worker functions, and a `std::promise`/`std::future` pair that
  reports when the self-stopping handler returned. `std::thread::join`
  synchronises with the worker's exit, so the counter reads 1 only after a real
  join. The single duration (`deadlockGuard`, 30 s) bounds `wait_for` as a
  deadlock guard; no assertion compares times.
* **The fix is at the cause and complete; callers and siblings checked.** Cause:
  the unconditional self-join in `stop()` plus the `mpWork`-guarded join in
  `~Context()` (evidence above). No in-tree caller of `Context::stop()`; its only
  user is `~Context()` through `util::Injected<IoContext>` (`link/Controller.hpp:845`).
  Siblings: the ESP32 port's `stop()` is a no-op (no join to fix);
  the Carla copy is a different, unbuilt third-party tree (Other findings); the
  Windows/darwin/default thread factories all hand back `std::thread`, so the new
  `joinable()`/`get_id()` calls hold on every supported platform.
* **No collateral damage; old projects and saved data unchanged.** No file
  format, model, command, undo or GUI code changed — one header's two member
  functions and their test. The destructor's other behaviour is unchanged (it
  still does not stop the service; it only releases the work guard and joins).
  No limit or check was added, so nothing else can be rejected by it.
* **Realtime, undo, commands.** Nothing added runs on the audio thread: the
  change is in the control-thread teardown of Link's io context, and
  `AbletonLinkSession`'s audio entry points are untouched. No undo step, no
  command id, so `ControlCommandsSnapshot` and its canary are unaffected (both
  gate rows pass).
* **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE** — the pre-fix code is
  quoted above with the `std::system_error` abort reproduced on this branch,
  both before the change and with the change reverted.
* **Sibling commands (the ASK's list: app.*, arrangement.fit_tracks,
  arrangement.get_state, ...)** — none of them is affected, because this fix
  changes no command file: `git diff --name-only` over my two commits
  (`a4591cdcb5`, `5fe19db01c`) is exactly the eight paths named above (the vendored
  header, the test, the two scope manifests, `upstream-modifications.txt`,
  `README.lmms`, `LINK-SYNC.md`, this report). `ControlRegistry*`,
  `ControlCommands*`, `ControlReversibilityTable*` and
  `tools/mcp-zene-control/zene_control/commands_snapshot.json` are untouched, and
  the pre-submit check's own sibling computation ran against command files that
  other lanes committed between the base and HEAD, not against mine.

## Other findings

* `tests/src/core/AbletonLinkSessionTest.cpp:219` (`joinBoth()`): `QSKIP` inside
  the helper returns only from `joinBoth`, so the caller continues and dereferences
  the never-created session (`m_a`/`m_b` are null) — a SIGSEGV on any host whose
  multicast probe is not delivered, this one included. Pre-existing and unrelated
  to this fix (`test-5-session-without-fix.log` shows it on the unmodified tree);
  not fixed here.
* `plugins/CarlaBase/carla/source/modules/hylia/link/ableton/platforms/asio/Context.hpp`
  is a third-party plugin's own Link copy with the same unconditional `join()`.
  It is not built by any Zene target and was left alone.
* `stop()` from two different threads at once can still join the same
  `std::thread` twice (pre-existing; this fix neither widens nor narrows it). A
  lane that stops a context from several threads would need an
  `std::atomic<bool> mStopped` exchange before the join.
* Destroying a `Context` from its own worker thread is still not supported: the
  destructor cannot join itself and the service it destroys is the one the
  worker is running on. Only `stop()` (and the destructor used from another
  thread) is covered by this fix, which is what the finding describes.
