FIXED
# new-p15-253: FIXED (round 9)

REAL verdict: FIXED. A bus constructed on the audio path no longer takes the model lock, grows the bus list, or calls the audio device. The engine links it onto an intrusive, lock-free control-thread stack, and the control thread registers it and applies its port on the next service tick. The render owner never promotes a bus.

## Cause

Before this fix, the audio-path constructor registered the bus inline (model lock and bus-list growth in `addAudioBusHandle`), and `setExtOutputEnabled` called the device on the audio path. Both allocate or block, so both are unsafe on the realtime thread.

## Evidence (file:line, HEAD = 1c36c59c16 with product commits bd268757cb and 951bf3cace)

- Realtime constructor branch: `src/core/AudioBusHandle.cpp:88-95`. Under `RealtimeScope` it calls `addAudioBusHandle(this)` and `setExtOutputEnabled(true)` and returns. It does not call `prepareStorage()`. Storage is prepared on the control thread (`:98`, reserve at `:104-107`, control-thread `addAudioBusHandle` at `:108`).
- Link path: `src/core/AudioEngine.cpp:1173-1176` (`linkOnce`: `enqueued.exchange(true, seq_cst)`, then push). `:1181-1196` (`addAudioBusHandle` on the audio path: state set to Pending at `:1191`, linked onto `m_registrationHead` at `:1193-1194`, counter at `:1195`). The control-thread branch sets Registered at `:1204` under the model lock.
- Engine stacks: `include/AudioEngine.h:731-733` (`m_registrationHead`, `m_retirementHead`, `m_portUpdateHead`), `:740` (deferred-registration counter), `:701`, `:704`, `:707` (service phase declarations), `:260` (`serviceAudioBusHandlesOnControlThread`).
- Single control-thread activation: `src/core/AudioEngine.cpp:1314-1322` runs `serviceRegistrations()`, then `serviceRetirements()`, then `servicePortUpdates()`. It is called once per tick from the timer at `:380`.
- `serviceRegistrations()` (`:1325-1355`): exchanges the head (`:1328`), clears `enqueued` with seq_cst (`:1334`), CAS Pending→Registered (`:1336`), `prepareStorage()` (`:1339`), appends the bus only if it is not listed, and marks a Retired bus Handled (`:1349`).
- `serviceRetirements()` (`:1357`) and `servicePortUpdates()` (`:1386`, `enqueued` cleared at `:1397`).
- Render owner: `renderNextPeriod()` starts at `:930` (realtime scope at `:934`). A grep over `:930-1010` finds no registration, service, pending or bus-list call. Nothing is promoted on the render path.
- Destructor contract: `include/AudioBusHandle.h:66-70` (a bus built on the audio path must go through `AudioEngine::destroyAudioBusHandle`; a plain delete frees an object a link still names). `~AudioBusHandle` is at `src/core/AudioBusHandle.cpp:142-146`.
- Destroy path: `src/core/AudioEngine.cpp:1249-1267`. Pending→Retired CAS at `:1254`. Control thread releases at `:1260`. Render path links `RetirementRemoveAndFree` at `:1266`.
- Remove path: `src/core/AudioEngine.cpp:1222-1244` (Pending→Retired CAS at `:1227`; a render worker links `RetirementRemove`).
- Port path: `src/core/AudioBusHandle.cpp:151-162`. Wanted state is stored with `exchange` (`:153`). A realtime caller requests a port update (`:158`). The control thread applies it (`:161`).
- Enum and fields: `include/AudioBusHandle.h:82-88` (RegistrationState Registered/Pending/Retired/Handled), `:254` (state atomic), `:258-263` (ControlLink `next`, `enqueued`; `m_registrationLink` at `:263`).

## Every-caller audit

- `new AudioBusHandle` appears once in `src/`: `src/core/SamplePlayHandle.cpp:78`, the standalone sample bus. It is the only heap bus, and its only destroy call is `src/core/SamplePlayHandle.cpp:445` (`destroyAudioBusHandle`).
- Stack and track buses are constructed on the control thread and have no links. They do not go through the audio-path branch.
- `src/core/SessionAudioCells.cpp:55` and the other `audioBusHandle()` users only receive an existing bus. They do not construct one.
- No plugin under `plugins/` constructs a bus (grep over `src`, `include`, `plugins`).

## State sequences (realtimeBusRegistrationStatesFollowTheControlThreadHandoff, tests/src/core/AudioBusHandleTest.cpp:521)

- Pending→Registered: a bus built on the audio path is Pending until `serviceAudioBusHandlesOnControlThread()` registers it.
- Pending→Retired→Handled: a bus retired while Pending is marked Retired. The control-thread service then consumes its entry and marks it Handled, and it is never registered.

## Commands and exit codes

Exit codes for p1 to p4 are the ones recorded in the p1–p4 run records. They are not written into the log files, and I did not re-derive them this round.

- p1 test-1d-red: EXIT 8 (record).
- p2 build-2-green, build-2a-green, test-2a-green: EXIT 0 (record).
- p3 build-3-family: EXIT 0 (record). test-2-family: 20/20 passed, EXIT 0 (record; `logs/test-2-family.log` shows "100% tests passed out of 20").
- p4 build-4-red and test-3-red: EXIT 8 (record). build-5-green and test-4-green: EXIT 0 (record).
- p4 targeted build-6 and test-5: EXIT 1 (record). Verified in `logs/test-5-target-red.log`: `FAIL! engineBusRegistrationOnTheRealtimePathIsDeferred`, `Actual (handles.size()): 4`, `Expected (registered): 3`.
- p4 build-7 and test-6: EXIT 0 (record). Verified in `logs/test-6-target-green.log`: `Totals: 3 passed, 0 failed`.

Checks run in this round:

- `python3 feature_check.py <worktree> > logs/gate-1-feature-check.log 2>&1; echo EXIT=$?` gave `EXIT=0`, `RESULT: PASS`, and eight rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). Before the test-file fix the same gate exited 1 on `file-length` (`NEW: new file over 500 lines: tests/src/core/AudioBusHandleTest.cpp (503)`).
- Test-file fix, commit 1c36c59c16: `AudioBusHandleTest.cpp` had four identical local `BusDeleter`/`BusPtr` blocks. They now share one definition. No assertion changed.
- `cmake --build build --target AudioBusHandleTest` gave exit 0 (`build-8-dedupe`). `cmake --build build --target AudioBusTest` gave exit 0 (`build-9-audiobus`).
- `ctest -R AudioBus` from `build/tests`, first run before AudioBusTest was built: `EXIT=8`, AudioBusTest "Not Run" (`logs/test-8-dedupe-full.log`). Superseded by the next run.
- `ctest -R AudioBus` from `build/tests`, after the build: `EXIT=0`, `100% tests passed out of 3` (AudioBusHandleTest, AudioBusTest, AudioBusPlayHandleQueueTest; `logs/test-9-dedupe-family.log`).
- `git diff --check`: `EXIT=0`, empty output (`logs/diff-check.log`).
- `bash tests/file-length-gate.sh --check`: `EXIT=1`, 15 REGRESSION lines, all outside the files this cluster changed (for example `tests/src/gui/ArrangerTimelineTest.cpp` 716 and `tests/src/core/VideoTrackTest.cpp` 651). `AudioBusHandleTest.cpp` is no longer listed (`logs/file-length-recheck.log`).

## Self-check (submit_check ASK items)

- RED/GREEN: RED is shown by p1 test-1d-red EXIT 8 and p4 build-4-red/test-3-red EXIT 8, and by the targeted run `FAIL` at size 4 vs 3 (test-5). GREEN is shown by p4 build-5-green/test-4-green EXIT 0 and build-7/test-6 EXIT 0.
- Determinism: the realtime cases run inside `RealtimeScope` and drive the control-thread service explicitly (`flushOnControlThread`, `releaseOnControlThread`, `serviceAudioBusHandlesOnControlThread`). `grep -c -E "sleep|usleep|msleep|wait_for" tests/src/core/AudioBusHandleTest.cpp` returns 0, so no test waits on time.
- Caller completeness: see the every-caller audit above. The only heap bus is SamplePlayHandle, and it is destroyed through `destroyAudioBusHandle`.
- No collateral damage: `git diff --stat b61e5f965 HEAD` shows 9 files. The product changes are in `include/AudioBusHandle.h`, `include/AudioEngine.h`, `src/core/AudioBusHandle.cpp`, `src/core/AudioEngine.cpp`, `src/core/SamplePlayHandle.cpp`, and the two audio-bus test files. The test-file commit is a dedupe with no assertion change. The gate reports no new item in the changed files.
- Verdict-specific items: N/A. The verdict is FIXED, so the items that apply only to a NOT FIXED verdict do not apply.

## Other findings

- Destructor contract: `include/AudioBusHandle.h:66-70`. A bus built on the audio path has engine links and must be destroyed through `destroyAudioBusHandle`. The engine destructor (`src/core/AudioEngine.cpp:394-437`) stops the timer, joins the workers (`:421`), and reclaims MPE snapshots (`:436`). It does not service the bus stacks. Any link still on `m_registrationHead`, `m_retirementHead` or `m_portUpdateHead` at engine destruction is not drained there. This is an open question and was not changed in this round.
- Port ordering: the seq_cst `enqueued` exchange in `linkOnce` (`src/core/AudioEngine.cpp:1175`) orders the wanted store (`src/core/AudioBusHandle.cpp:153`) before the service load in `servicePortUpdates` (`:1386-1397`). A toggle that happens after the control thread clears `enqueued` (`:1397`) re-links the bus, so it is applied on the next tick.
