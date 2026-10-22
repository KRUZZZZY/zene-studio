FIXED

# Cluster 23-port — Audio port edits race with audio routing

The port of the accepted `bugs/qwen-23` fix to the newest line
(`07d6d02bd`). The fix was re-applied by hand (no merge, no cherry-pick, no
rebase), and the one thing the train's bisect found — that the accepted fix
breaks `ScriptDawModelRegistrationTest` — is reproduced here, explained and
fixed.

## Decision: REAL

The writer and the reader are both real, and they are on different threads.

The writer: `AudioPortsModel::Matrix::setPin()`
(`src/core/AudioPortsModel.cpp:471`) writes the packed `std::vector<bool>` pin
matrix (`m_pins[trackChannel][processorChannel] = value;`, `:475`) and then
refreshes the caches the router reads — `updateUsedTrackChannels(trackChannel)`
(`:478`), `updateUsedChannels(processorChannel)` (`:479`) and
`m_parent->updateDirectRouting()` (`:480`, which recomputes
`m_trackChannelsUpperBound` and `m_directRouting`).

The reader: the render thread. `AudioEngine::renderNextPeriod()`
(`src/core/AudioEngine.cpp:973`) takes `m_changeMutex` for the whole period
(`:975`, a `std::recursive_mutex`, `include/AudioEngine.h:793`), and inside that
period the router reads exactly this state:

- `directRouting()` — `include/AudioPorts.h:186`
- `trackChannelsUpperBound()` — `include/AudioPorts.h:431`, `:494`, `:634`, `:724`
- `in().enabled(...)` — `include/AudioPorts.h:447`, `:448`, `:684`-`:687`
- `out().enabled(...)` — `include/AudioPorts.h:544`, `:546`, `:562`, `:573`, `:583`, `:783`-`:786`
- `out().usedTrackChannels()` — `include/AudioPorts.h:600`, `:601`

`AudioBus` reads the same caches (`src/core/AudioBus.cpp:92`, `:100`, `:101`).

Before the fix the edit took no lock at all: `src/core/AudioPortsModel.cpp` had
no `requestChangesGuard()` / `requestChangeInModel()` call, so the GUI
(`src/gui/PinConnector.cpp:481`), the `port.set_pin` control command
(`src/core/ControlCommandsPorts.cpp:275`) and the project loader
(`src/core/AudioPortsModel.cpp:317`) could rewrite the matrix and its caches
while the render thread was reading them. `std::vector<bool>` is bit-packed, so
a concurrent write and read of one entry is a data race on the same word: the
router can read a torn value and route the wrong channel, not merely a stale
one.

The contract the fork already uses for the sibling write is the engine's
model-change guard: `RemotePlugin::applyDeferredClientRequests()`
(`src/core/RemotePlugin.cpp:691`) takes `Engine::audioEngine()->requestChangeInModel()`
before `setChannelCounts()`, precisely because re-sizing the ports moves the
shared buffer the audio thread's router reads. The pin path was the missing
sibling of that write. The finding's second remedy — "synchronize edits at a
safe audio boundary" — is exactly that guard.

## Why the accepted fix could not go on the train, and what this port does about it

The accepted fix (`c2eb7fecf`, `c45dd98ba`) put the guard inside the model:
every mutator of the routing state takes `Engine::audioEngine()->requestChangesGuard()`.
On the newest line that fix deadlocks `ScriptDawModelRegistrationTest`
(ctest **#316**), which is what the train's bisect found.

Reproduced here with the accepted fix applied verbatim
(`.../logs/build-2.log`, `.../logs/test-scriptdaw-1.log`):

```
1/1 Test #316: ScriptDawModelRegistrationTest ...***Timeout  60.08 sec
0% tests passed, 1 tests failed out of 1
```

The cause is a product contract, not a flaky test.
`tests/src/core/ScriptDawModelRegistrationTest.cpp:105` —
`aRecordedDeviceIsBuiltWithoutTheAudioChangeLock` — holds the audio change
mutex on a second thread and requires the recorded-device build to complete
anyway. The build creates the device (`Effect::instantiate`), and every device
owns an `AudioPortsModel` (`include/AudioPlugin.h:222` → `include/AudioPorts.h:291`
→ `include/AudioPortsModel.h:67`), whose constructor edits the routing state
(`src/core/AudioPortsModel.cpp:134` onward). With the guard inside the model,
that edit waits for the audio change mutex the other thread holds, and the build
never finishes: the holder waits for the build, the build waits for the holder.

The fork already states the rule that makes the contract right, in the comment
at `src/core/ControlStructuralSupport.cpp:402-408`:

> The device is not in any chain yet, so the render thread never runs it: no
> audio change lock is taken here. […] a model the render thread cannot see is
> written without the change lock the render thread takes once per period.

So the rule is: **an edit of a model the render thread cannot see takes no
change lock.** This port keeps the guard inside the model — the fix at the
cause — and makes the model know when it is one of those invisible models.

## The fix

### `include/AudioPortsModel.h`

- `AudioPortsModel::DetachedBuildScope` (`:275`): the scope in which a device is
  built outside every chain. It records the audio-ports models that are created
  while it is alive, and only those may skip the engine's change guard.
- `Matrix::setPinBatch()` moved out of the header into the `.cpp`, so its
  definition can take the same guard a single `setPin()` takes. It was the one
  sibling write that stayed outside the guard. Its only product caller is
  `Matrix::setPinFromAttributeName()` (`include/AudioPortsModel.h:179`), reached
  only from the guarded `Matrix::loadSettings()`.

### `src/core/AudioPortsModel.cpp`

- `takeRoutingEditGuard(model)` (`:64`): returns the engine's
  `requestChangesGuard()` — the recursive guard the render period already owns.
  It returns an empty guard when
  - there is no engine (a model built before `Engine::init`, the unit tests) — no
    render thread to synchronise with,
  - the edit is made inside a realtime scope (`RealtimeScope::isActive()`): the
    render thread itself and the render workers already own the guard, and a lock
    there would be a realtime violation — the same rule
    `AutomatableModel`'s `modelChangeGuard()` follows,
  - the model is one the render thread cannot see: it was created inside a
    `DetachedBuildScope` (`:68`).
- The guard now covers every public mutation of the routing state:
  `AudioPortsModel::setAllChannelCounts()` (`:153`), `setTrackChannelCount()`
  (`:196`), `setChannelCounts()` (`:214`),
  `AudioPortsModel::loadSettings()` (`:317`), `Matrix::setPin()` (`:473`),
  `Matrix::setPinBatch()` (`:490`), `Matrix::setTrackChannelCount()` (`:496`),
  `Matrix::setChannelCount()` (`:517`) and `Matrix::loadSettings()` (`:641`).
  `AudioPortsModel::setChannelCountIn()` / `setChannelCountOut()` delegate to
  `setChannelCounts()`, so they are covered by it.
- `DetachedBuildScope` (`:85`-`:120`) keeps the scopes that are alive on the
  calling thread; the `AudioPortsModel` constructor registers itself with the
  scopes that are alive (`:134`), before it edits its own routing state.

### `src/core/ControlStructuralSupport.cpp`

- `instantiateRecordedDevice()` runs the device build and the state restore
  inside an `AudioPortsModel::DetachedBuildScope` (`:416`), next to the
  `DeferredModelRegistrationScope` that already marks the same build. The
  device's audio-ports model is therefore invisible to the render thread while
  it is written, and the build still completes while another thread owns the
  audio change lock.

Nothing on the render path changed: the router still reads the same fields
without taking a new lock, and no allocation, free, lock, wait, log or signal
was added to any render path. The guard is taken only by edit entry points
(GUI, control command, project load, plug-in layout change), and it is the
engine's own existing recursive mutex, so an edit that already runs under it (a
command handler, the project loader, the `RemotePlugin` path above) takes no
second, different lock. Undo stays one honest step: `setPin` still journals
nothing, and `port.set_pin` still records the single inverse from the value it
read before the write (`src/core/ControlCommandsPorts.cpp` unchanged). No
control command was added.

## The regression tests

- `tests/src/gui/PinConnectorTest.cpp:141`
  `pinEditsRunUnderTheEngineModelChangeGuard` (ctest **#438**, the three-digit
  number the negative control's `Test #\d+: (\S+)` pattern matches).
- `tests/src/core/AudioPortsModelTest.cpp:524`
  `routingEditsHoldTheEngineChangeGuard` (ctest **#14**).

The seam is the one the newest line already ships:
`AudioEngine::isChangeInModelHeld()` (`include/AudioEngine.h:562`, compiled only
with `LMMS_TESTING`) reports whether the **calling** thread holds the engine's
model-change guard. Each edit emits its model signal from inside the edit, so a
`Qt::DirectConnection` slot reads the guard while the edit is still running. No
threads, no sleeps, no wall clock, no injected timestamps, no race.

Both tests look the seam up through a `requires` expression, so the test still
compiles against a tree without the fix and reports the missing seam instead
of failing to build — that is what makes the negative control a real proof.

What is asserted:

- `Matrix::setPin()` (the reported function) must change the routing while the
  guard is held. Without the fix the guard is not held at the signal, so the
  render thread could be inside a period reading the same bits — the test
  fails.
- The other entry points of the same state are held to the same rule:
  `setTrackChannelCount()`, `setChannelCounts()`, `setAllChannelCounts()` and
  `loadSettings()`.
- The edit still does its work: the pin is set and the routing caches are
  refreshed with it (the extra pin rules the direct routing of that processor
  out, so the cached decision must be gone). The guard must not swallow the
  edit.

## The negative control (the fix reverted, the test still compiled and failed)

```
$ git diff -- include/AudioPortsModel.h src/core/AudioPortsModel.cpp src/core/ControlStructuralSupport.cpp \
      > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/23-port.r1/logs/fix.patch \
  && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/23-port.r1/logs/fix.patch
EXIT=0
$ cmake --build build --target AudioPortsModelTest PinConnectorTest > .../logs/build-5-red.log 2>&1
EXIT=0
$ cd build/tests && ctest -R '^(AudioPortsModelTest|PinConnectorTest)$' --output-on-failure --timeout 120 \
      > .../logs/test-3-red.log 2>&1
EXIT=8
```

```
FAIL!  : AudioPortsModelTest::routingEditsHoldTheEngineChangeGuard() 'guardAtSignal == 1' returned FALSE.
          (Matrix::setPin() changed the routing outside the engine model-change guard, so the render
          thread can read the matrix while it is rewritten)
FAIL!  : PinConnectorTest::pinEditsRunUnderTheEngineModelChangeGuard() 'guardAtSignal == 1' returned FALSE.
          (Matrix::setPin() changed the routing outside the engine model-change guard, so the render
          thread can read the matrix while it is rewritten)
0% tests passed, 2 tests failed out of 2
```

And with the fix put back:

```
$ git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/23-port.r1/logs/fix.patch
EXIT=0
$ cmake --build build --target AudioPortsModelTest PinConnectorTest ScriptDawModelRegistrationTest AudioPortsTest \
      > .../logs/build-6-green.log 2>&1
EXIT=0
$ cd build/tests && ctest -R '^(AudioPortsModelTest|PinConnectorTest|ScriptDawModelRegistrationTest|AudioPortsTest)$' \
      --output-on-failure --timeout 120 > .../logs/test-4-green.log 2>&1
EXIT=0
100% tests passed out of 4
```

## Evidence table

| Item | Command | Exit | Result |
| --- | --- | --- | --- |
| RED (fix reverted) | `ctest -R '^(AudioPortsModelTest\|PinConnectorTest)$' --output-on-failure --timeout 120` (`.../logs/test-3-red.log`) | 8 | 0/2 passed — both new regressions FAIL on the assertion |
| GREEN (fix applied) | `ctest -R '^(AudioPortsModelTest\|PinConnectorTest\|ScriptDawModelRegistrationTest\|AudioPortsTest)$' --output-on-failure --timeout 120` (`.../logs/test-4-green.log`) | 0 | 4/4 passed |
| Affected family, final | `ctest -R '^(AudioPortsModelTest\|AudioPortsTest\|PluginAudioPortsTest\|RemotePluginAudioPortsTest\|RemotePluginClientE2ETest\|PinConnectorTest\|ScriptDawModelRegistrationTest\|...)$'` (`.../logs/test-10-family.log`) | 0 | 39/39 CTest targets passed |
| Control surface | `ctest -R '^(ControlPortsCommands\|ControlRoutingCommands\|ControlDevices\|ControlUndoStructuralTranscript\|ControlPluginBuses\|ControlMixingRouting\|ControlHeadlessProjectOpen)$'` (`.../logs/test-8-control.log`) | 0 | 6/6 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`.../logs/gates-1.log`) | 1 | 6 PASS, 2 FAIL (`fork-sources`, `all-sources-reproduce` — pre-existing, see below) |
| Gates on the unmodified tree | the same command with every change reverted (`.../logs/gates-2-base.log`) | 1 | the identical 6 PASS / 2 FAIL — the two failures are not mine |

The two failing gates are the source-manifest gates, and they fail on the
**unmodified** tree as well (`feature-check zene-qwen-23-port (base 07d6d02bd,
0 changed files)`): the diff the gate prints names only
`plugins/SfzPlayer/sfizz/external/abseil-cpp/**`, i.e. the vendored sfizz tree
that drifted from the manifest's own recipe. No gate baseline and no gate
limit was touched.

Two further CTest failures in the first family run
(`.../logs/test-5-targeted.log`: `PluginPortsMigrationTest`, `AudioPluginTest`)
were missing build artifacts in this tree, not behaviour: the `decimator_1202`
LADSPA fixture, the `nam_harness` executable and the `gate` module had never
been built here. Building them (`.../logs/build-10-plugins.log`,
`.../logs/build-11-fixture.log`) made both tests pass
(`.../logs/test-6-plugins.log`, `.../logs/test-7-fixture.log`).

## Files committed

- `include/AudioPortsModel.h` — the `DetachedBuildScope`; `setPinBatch()` moved to the `.cpp`.
- `src/core/AudioPortsModel.cpp` — the routing edits take the engine's model-change guard; the scope's bookkeeping.
- `src/core/ControlStructuralSupport.cpp` — the recorded-device build runs inside the scope.
- `tests/src/core/AudioPortsModelTest.cpp` — `routingEditsHoldTheEngineChangeGuard`.
- `tests/src/gui/PinConnectorTest.cpp` — `pinEditsRunUnderTheEngineModelChangeGuard`.
- `tests/upstream-modifications.txt` — the registration of the five changed files.
- `docs/qwen-fixes-2026-10/cluster-23-port.md` — this report.

No new file was added, so `tests/fork-sources.txt` is unchanged.

## Self-check

- **Is the finding real?** Yes — the writer and the reader are named above with
  file:line, on two different threads, on a packed `std::vector<bool>`.
- **Is the fix at the cause?** The guard is inside the model, at the nine
  mutators of the routing state; the sibling write `setPinBatch()` was moved
  into the `.cpp` so it is guarded too. Every writer of the state was checked:
  `src/gui/PinConnector.cpp:481` (GUI), `src/core/ControlCommandsPorts.cpp:275`
  (`port.set_pin`), `src/core/AudioPortsModel.cpp:549`-`:561`
  (`setDefaultConnections`) and `:657` (`loadSettings` → `setPinFromAttributeName`)
  all reach a guarded `setPin()` / `setPinBatch()`.
- **Does anything on the audio thread change?** No: the render period already
  holds `m_changeMutex`; `requestChangeInModel()` returns immediately on the
  rendering thread (`src/core/AudioEngine.cpp:1450`), and `RealtimeScope::isActive()`
  covers the render workers. No allocation, free, lock, wait, log or signal was
  added to a render path.
- **Does the fix break the recorded-device build?** No: the build is inside the
  scope, and `ScriptDawModelRegistrationTest` (#316) passes — with the accepted
  fix it timed out (60 s) in this tree.
- **Does the fix change behaviour outside the bug?** No: no value, no file
  format and no command changed; old projects load as before; the pin matrices,
  the caches and the direct-routing decision are computed exactly as before,
  only under the guard.
- **Undo and commands:** `setPin` still journals nothing and `port.set_pin`
  still records one inverse; no control command was added (the
  `ControlCommandsSnapshot` canary is untouched).
- **Registration:** the five changed inherited files are registered in
  `tests/upstream-modifications.txt` with their reason, in the same commit.
- **Gates:** `feature_check.py` exits 1, with `fork-sources` and
  `all-sources-reproduce` failing — proven pre-existing by the identical run on
  the unmodified tree (`.../logs/gates-2-base.log`). `no-upstream-regression`,
  `check-namespace`, `no-tautology`, `doc-figures`, `complexity` and
  `file-length` all pass.
