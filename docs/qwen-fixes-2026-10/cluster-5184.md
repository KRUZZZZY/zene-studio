FIXED

# Cluster 5184 — a route's address that is not a number loads as 0

Verdict: **FIXED** on `bugs/qwen-5184`. A route whose `channel`, `chain`, `effect` or `track` is present and is not a
number now refuses its modulator. Before the fix the same block loaded, and the modulator was routed to channel,
chain or effect 0, an address nobody authored. The file-length compression in the follow-up commit changes no
validation order. The RED and GREEN runs for the address check are under Evidence.

## The real decision

Before the fix, `readRoute` (pre-fix `src/core/ModulationLayer.cpp`, HEAD) read the address with unchecked `toInt()`:

- `route.channel = element.attribute("channel").toInt();` (HEAD `:177`)
- `route.chain = ...toInt();` (HEAD `:178`), `route.effect = ...toInt();` (HEAD `:179`)
- `route.track = std::max(-1, ...toInt());` (HEAD `:182`)

`QString::toInt()` returns 0 for `"bad"`, so a corrupt address became a legal-looking address. `saveRoute` writes
integers, so a file with `channel="bad"` was never written by the engine.

The documented contract says a corrupt block must not be repaired. `docs/MODULATION.md:162-168` states that a corrupt
or empty block loads as an EMPTY layer, and that nothing is guessed. The address case broke that contract silently.

## The fix

- `intAttribute` (`src/core/ModulationLayer.cpp:139`) returns the result of `toInt(&ok)` when the attribute is present,
  and gives the fallback when it is absent. It mirrors the existing `boolAttribute` and `floatAttribute` helpers.
- `readRoute` (`:182`) validates the four address fields with `intAttribute` (`:186-189`) and returns false when any is
  malformed. Absent fields keep their documented defaults: channel 0, chain 0, effect 0, track -1. The track clamp
  `std::max(-1, ...)` (`:191`) is kept.
- The route's per-sample flag is checked in `readRoute` by one `boolAttribute` line (`:190`), after the address checks.
  It is reached only for a named route. A nameless route returns true earlier (`:185`).
- `readRoutes` (`:199`) refuses the whole modulator (`return false`) when `readRoute` fails (`:207`).
- A nameless route still returns true from `readRoute` (`:185`), and `readRoutes` drops it at `:208` without
  inspecting its other fields. A nameless route with `channel="bad"` or `persample="bad"` therefore still loads.

## Compression (follow-up commit)

The file-length gate counts code lines, and the fix pushed `ModulationLayer.cpp` over 500. Two reductions, with no
change to any validation order:

- `intAttribute` body reduced from 9 to 7 code lines. The failure path now writes `toInt`'s return (0) into `*out`
  before returning false. The only caller is `readRoute`, which returns false at once and discards the route, so no
  written value is observed.
- The route's per-sample flag had its own 4-line helper and a call in `readRoutes`. Both are deleted, and the flag is
  checked in `readRoute` by one line (`:190`).

The gate's `--check` log lists only the four files over 500 (`file-length-check.log`); `ModulationLayer.cpp` is not in
that list. The gate does not print a per-file count.

## Callers and siblings checked

Grep of `src/` for `readRoute(` and `readRoutes(`/`loadModulator(`:

- `readRoute` is called only from `readRoutes` (`:207`).
- `readRoutes` is called only from `loadModulator` (`:241`).
- `loadModulator` is called only from `loadSettings` (`:593`).
- `intAttribute` in `ModulationLayer.cpp` is called only from `readRoute` (`:186-189`). The other `intAttribute`
  functions in `src/core/SessionModelPrivate.h` and `DawProjectRead.cpp` are different helpers and are not touched.

**Changed:** the four address fields (channel, chain, effect, track), and the route's per-sample flag, which is now
checked in the same function.

**Deliberately unchanged:**

- `depth` (`:192`) still uses `boundedValue`, as before.
- The phase and the source flags already refuse a malformed value (`readSourcePhase` `:149`, `readSourceFlags` `:156`).
- `trigger` (`readSource`, `:173`) still uses `std::max(-1, ...toInt())`. It has the same unchecked pattern. It is
  not in this cluster and is listed under Other findings.

## Tests

`tests/src/core/ModulationLayerTest.cpp` (unchanged by this commit):

- `aMalformedRouteAddressIsRefused_data` (`:524`) — four in-memory rows: `channel`, `chain`, `effect`, `track`.
- `aMalformedRouteAddressIsRefused` (`:533`) — builds a `QDomDocument` in memory with one modulator whose route has
  the row's field set to `"bad"`. It asserts `loadSettings` returns false and `modulatorCount()` is 0.
- `aRouteAddressAbsentKeepsItsDefault` (`:560`) — an absent address keeps 0/0/0/-1, and a nameless route with a bad
  channel is dropped while the valid modulator still loads.

Determinism: all data are in-memory `QDomDocument` rows. There are no sleeps, no `qWait`, no threads and no
randomness in lines 520-586 (re-checked with grep for sleep, wait, timer, thread and rand: no matches).

## Evidence

Logs are outside the repository, in `/home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/`. The RED run reverses the full product change with
`logs/fix-5184-red.patch`, a diff of `src/core/ModulationLayer.cpp` over `b85f94a76e^..HEAD`, which includes the address
check. `logs/fix.patch` is the older compression-only patch; it is not the address-check reversal and is not used as the
RED evidence.

RED and GREEN for the address check (the packet p2/p3 run; ctest runs from `build/tests`):

| Step | Command | Exit | Result |
|---|---|---|---|
| Patch for RED | diff of `src/core/ModulationLayer.cpp` over `b85f94a76e^..HEAD` → `logs/fix-5184-red.patch`; non-empty check | 0; 0 | non-empty |
| Reverse | `git apply -R logs/fix-5184-red.patch` | 0 | address check removed from the worktree |
| RED build | build of the `ModulationLayerTest` target → `build-10-red.log` | 0 | `Built target ModulationLayerTest` |
| RED test | `ctest -R '^ModulationLayerTest$'` → `test-11-red.log` | **8** | `Totals: 16 passed, 4 failed`; `0% tests passed, 1 tests failed out of 1` |
| Reapply | `git apply logs/fix-5184-red.patch` | 0 | fix restored |
| GREEN build | build of the `ModulationLayerTest` target → `build-12-green.log` | 0 | `Built target ModulationLayerTest` |
| GREEN test | `ctest -R '^ModulationLayerTest$'` → `test-13-green.log` | 0 | `100% tests passed out of 1` |
| Family | `ctest -R '^Modulation'` → `test-14-modulation-family.log` | 0 | `100% tests passed out of 5` |

Exact RED/GREEN command sequence, as run in the p2/p3 shell runs. Paths are absolute. Each command has a `# cwd:` comment giving the directory it starts in. The ctest lines start in the worktree root and `cd` into `build/tests` for their own command; a standalone `cd <root>` line follows each of them, so the next command starts in the root.

The p3 green build (`cmake --build build --target zene ModulationLayerTest`, the line marked "green build") was run through the quiet-run helper (`/home/kruzzzzy/.local/share/zene-bunny-fix/duet/quiet-run`), which wrapped that cmake command. The line shows the underlying command with its redirect. `build-12-green.log` holds the full build output, and the `0` on that line is the wrapper's exit code. The red build was a direct redirect, not wrapped.

Each line's exit code is the value printed by the p2/p3 shell run; exit codes were printed by the shell runs and are not embedded in the logs:

```sh
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
git diff b85f94a76e^ HEAD -- src/core/ModulationLayer.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/fix-5184-red.patch; echo EXIT=$?   # patch generation: 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
test -s /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/fix-5184-red.patch; echo EXIT=$?   # test -s: 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
git apply --check -R /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/fix-5184-red.patch; echo EXIT=$?   # 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/fix-5184-red.patch; echo EXIT=$?   # 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/build-10-red.log 2>&1; echo EXIT=$?   # red build (direct redirect): 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184 (the line enters build/tests itself)
cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/test-11-red.log 2>&1; echo EXIT=$?   # red ctest: 8
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184   # reset: cwd is the root again
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
git apply /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/fix-5184-red.patch; echo EXIT=$?   # 0
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184
cmake --build build --target zene ModulationLayerTest > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/build-12-green.log 2>&1; echo EXIT=$?   # green build: run via quiet-run wrapper of this command; 0 = wrapper's exit code; full output in build-12-green.log
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184 (the line enters build/tests itself)
cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/test-13-green.log 2>&1; echo EXIT=$?   # focused green ctest: 0
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184   # reset: cwd is the root again
# cwd: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184 (the line enters build/tests itself)
cd build/tests && ctest -R '^Modulation' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/test-14-modulation-family.log 2>&1; echo EXIT=$?   # family ctest: 0
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184   # reset: cwd is the root again
```

The four RED failures are the four address rows, all at `tests/src/core/ModulationLayerTest.cpp:553`:

- `ModulationLayerTest::aMalformedRouteAddressIsRefused(channel)` — `Compared values are not the same`
- `ModulationLayerTest::aMalformedRouteAddressIsRefused(chain)` — same message
- `ModulationLayerTest::aMalformedRouteAddressIsRefused(effect)` — same message
- `ModulationLayerTest::aMalformedRouteAddressIsRefused(track)` — same message

Earlier compression-only checks (`build-5-green.log`, `test-5-green.log`, `build-6-reversed.log`, `test-6-reversed.log`,
`build-7-reapplied.log`, `test-7-reapplied.log`) reversed only the uncommitted compression. They leave the `b85f94a`
address check in place, so they show no failure for the address check. They are not the RED evidence above.

Other gates:

| Step | Command | Exit | Result |
|---|---|---|---|
| File length | `bash tests/file-length-gate.sh --check` → `file-length-check.log` | 0 | `ModulationLayer.cpp` not in the exceed list; PASS |
| Feature check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184 > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/feature-check-3.log 2>&1; echo EXIT=$?` → `feature-check-3.log` | 0 | all eight gates PASS; `RESULT: PASS` |
| Submit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5184 5184 > /home/kruzzzzy/.config/kads/zene/state/runs/5184.r1/logs/submit-check-3.log 2>&1; echo EXIT=$?` → `submit-check-3.log` | 0 | `RESULT: PASS - submit`; no FAIL line |

Exit codes for the RED, GREEN and gate rows come from the p2/p3/p4 shell run records. The `.log` files hold the test output,
and the exit codes are not written into them.

## Realtime and undo

The change is on the load path only (`loadSettings` → `loadModulator` → `readRoutes` → `readRoute`). It adds no
audio-thread work, no commands and no undo change.

## Registration

`tests/fork-sources.txt:2131` lists `src/core/ModulationLayer.cpp`. The change is to an existing source file and the
existing test file. No new source and no CMake change.

## Other findings

- `trigger` (`src/core/ModulationLayer.cpp:173`, `readSource`) is still read with `std::max(-1, ...toInt())`. A
  malformed `trigger` loads as track 0. This is the same defect class as this cluster, outside its scope. It needs its
  own cluster and its own RED test.

## Self-check

- **RED/GREEN exit codes:** RED: the `ModulationLayer.cpp` reversal of `b85f94a76e^..HEAD` applied with exit 0, the
  RED build exit 0 (`build-10-red.log`), and the RED ctest exit 8 (`test-11-red.log`). Four address rows failed at
  `ModulationLayerTest.cpp:553`. The reapply exit 0. GREEN: build exit 0 (`build-12-green.log`), focused ctest exit 0
  (`test-13-green.log`), and family ctest exit 0 (`test-14-modulation-family.log`). The exit codes are from the p2/p3
  record, not from the logs.
- **Determinism:** the new tests use in-memory `QDomDocument` rows (`aMalformedRouteAddressIsRefused_data` `:524`,
  `aRouteAddressAbsentKeepsItsDefault` `:560`). The tests use no sleeps, timers, threads or randomness (grep of
  lines 520-586: no match).
- **Caller chain:** `readRoute` (`:182`) ← `readRoutes` (`:199`, call at `:207`) ← `loadModulator` (`:228`, call at
  `:241`) ← `loadSettings` (`:582`, call at `:593`). `intAttribute` (`:139`) is used only by `readRoute` (`:186-189`).
  The `trigger` sibling (`readSource` `:173`) has the same unchecked pattern and is listed under Other findings.
- **Collateral, absent defaults and nameless routes:** an absent address keeps 0/0/0/-1, and a nameless route with a
  bad channel is dropped without inspection while the valid modulator still loads. Both are asserted by
  `aRouteAddressAbsentKeepsItsDefault` (`:560`).
- **Collateral, round-trip family:** the five-test modulation family passes 5/5 (`test-14-modulation-family.log`),
  including `ModulationLayerProjectRoundTripTest`.
- **Verdict evidence:** commit `b85f94a76e` adds the refusal in `readRoute` and the tests. Commit `038fe08e23` makes the
  file-length compression with no validation change. The contract is `docs/MODULATION.md:162-168`, which says a corrupt
  block loads as EMPTY and nothing is guessed. The comment above `readSourcePhase` (`:147-148`) states the same rule for
  the phase, and the address check follows it.
- **Command files:** no command files changed. The cluster commits `b85f94a76e`, `038fe08e23` and `23adf82e06` touch
  only `docs/MODULATION.md`, the cluster report, `src/core/ModulationLayer.cpp` and
  `tests/src/core/ModulationLayerTest.cpp`. A path filter over `b85f94a76e^..23adf82e06` for command, arrangement and
  app files returns 0 matches. So `app.*` and `arrangement.*` are unaffected.
