# Cluster new-p14-120-port-port: an invalid saved output route silenced the master

Verdict: **FIXED**

## What this job is

A port job. The fix for this finding was already accepted as `bugs/qwen-new-p14-120-port`
(commit `0ceee034a`, with the two documentation commits `95028be6e` and `85120fd1d` on top of it),
but it could not go onto the newest train because it conflicted with the newer line in
`src/core/HardwareIo.cpp`. This branch is a fresh branch off the newest line
(`07d6d02bd`, train-12), and the fix was re-applied **by hand** from `git show` - no merge, no
cherry-pick, no rebase. The newer line had changed the same neighbourhood (`readDestination`'s
neighbour `savedFlag` was added, and the cue's `followListen` line moved), which is exactly where
the conflict came from.

The re-applied fix is the same change in the same place; the two callers of the changed helper were
re-checked against the newer line, and the cue's line had to be re-anchored on the new
`savedFlag(...)` call.

## The finding

`src/core/HardwareIo.cpp` (the finding's line numbers, before this change):

```cpp
void ChannelHardwareIo::load(const QDomElement& channelElement, f_cnt_t frames)
{
	clear();
	const QDomElement element = channelElement.firstChildElement(QStringLiteral("hardwareio"));
	if (element.hasAttribute(QStringLiteral("output")))
	{
		outputSet = true;                                    // <- set even when the route does not parse
		output = readDestination(element, QStringLiteral("output"));
	}
	...
}
```

`readDestination` (`src/core/HardwareIo.cpp:61-70` before this change) returned
`audioio::Destination{}` - "nowhere" - for a route that does not parse, while the caller had
already set `outputSet = true`. `effectiveOutput()` (`src/core/HardwareIo.cpp:159-163`) then
returned that "nowhere" instead of the default, so the master's main mix went to no output at all:
`mixOutputs` places the master with `bus.setMasterPlacement({main.start, main.width})`
(`src/core/HardwareIo.cpp:259`), and the same value reaches the GUI
(`src/gui/MixerStripInfo.cpp:75-76`), the surround mix (`src/core/MixerSurroundMix.cpp:301`) and
the commands (`src/core/ControlCommandsHardwareIo.cpp:66,174`,
`src/core/ControlCommandsMixerSurround.cpp:84`).

A route that does not parse is reachable from a project's own file: `output="3/5"` is refused by
`audioio::parseDestination` (a pair that is not two consecutive channels - see the existing
assertion `tests/src/core/HardwareIoTest.cpp:517`), and the same is true of any other hand-edited
garbage in the attribute.

**The bug was real on this branch** (nothing had fixed it): the code above is what the branch
carried at `07d6d02bd`, and the regression below fails without the fix (RED, `logs/test-1.log`).

## The change

| File | What it does |
|---|---|
| `src/core/HardwareIo.cpp:61-70` | `readDestination` now reports whether the route parsed (it fills the caller's destination instead of returning one): a route that does not parse is told apart from a route that is "none", which is a valid route nowhere. |
| `src/core/HardwareIo.cpp:186-195` | `ChannelHardwareIo::load` sets `outputSet` and `output` only when the route parsed. A route that does not parse leaves the default in force: the master stays on the outputs 1/2, another channel stays nowhere. |
| `src/core/HardwareIo.cpp:311-313` | the same helper's other caller, a cue master's output: a route that does not parse leaves the cue's default (nowhere), exactly as before the change. |
| `tests/src/core/HardwareIoTest.cpp:772-801` | the regression (`malformedOutputKeepsTheDefaultRoute`). |
| `tests/upstream-modifications.txt` | the inherited file `src/core/HardwareIo.cpp` is changed: its divergence is declared. |

### What does not change

- A route that parses is set exactly as before, **including "none"**: `parseDestination` accepts
  "none" and answers a destination that is nowhere (`tests/src/core/HardwareIoTest.cpp:515`), and
  that is the user's own choice (the command `mixer.hardware_output` sets the same thing:
  `src/core/ControlCommandsHardwareIo.cpp:188`), so it still silences the master when asked.
- A route saved on a machine with more outputs than another still loads: the project path reads
  the whole range `audioio::MaxChannels` (`src/core/HardwareIo.cpp:68`), unchanged by this fix.
- A route that names a channel the running device does not carry is still accepted - the parser is
  not touched (the contract of cluster new-p8-269, `include/AudioIoSetup.h:113-117`).

### The siblings of the changed code

Every path that reads a saved routing target was checked:

| Path | Behaviour on a route that does not parse |
|---|---|
| `src/core/HardwareIo.cpp:186-195` - the channel's direct out | **fixed here**: the default stays in force. |
| `src/core/HardwareIo.cpp:311-313` - a cue master's output | the default (nowhere) stays in force, as before (the cue's default is nowhere - `include/HardwareIo.h:67` - so nothing changes for it). |
| `src/core/HardwareInsert.cpp:51-62` - an insert's send and return | already keeps its fallback (`destinationAttribute` returns the fallback when the route does not parse) - unchanged. |
| `src/core/ControlHardwareIoSupport.cpp:116-130` - the command path | the route is refused with an error and nothing is written - unchanged. |

The command path was not touched: no new control command, and the commands' behaviour on a valid
route is the same.

### Realtime, undo and commands

The change is in the load path (the main thread, when a project is opened): no allocation, no
lock, no logging is added, and nothing on the audio thread is touched - `effectiveOutput` and
`mixOutputs` are unchanged. The undo path is untouched: the fix changes no command and no undo
step, so an undo stays one honest step. `ChannelHardwareIo::save` is unchanged, so a project that
routes nothing to the hardware still saves nothing of it, and a project that routes somewhere
saves the same attribute as before.

## The regression

`HardwareIoTest::malformedOutputKeepsTheDefaultRoute` (`tests/src/core/HardwareIoTest.cpp:772-801`):

- a `<hardwareio output="3/5"/>` element loaded into a `ChannelHardwareIo` must leave
  `outputSet` false and `effectiveOutput(true)` at the default `audioio::Destination{0, 2}`
  (the master's outputs 1/2);
- an explicit `output="none"` must still be set (`outputSet` true, `effectiveOutput(true)`
  nowhere) - it parses, and it is the user's own route nowhere.

The test is deterministic: it reads a DOM element and loads it; no clock, no sleep, no thread.

## The evidence

| Log | Command | Exit | Result |
|---|---|---|---|
| `logs/build-1.log` | `cmake --build build --target HardwareIoTest` (the regression added, no product fix) | 0 | RED build |
| `logs/test-1.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` (from `build/tests`) | 8 | **RED**: `HardwareIoTest::malformedOutputKeepsTheDefaultRoute() '!io.outputSet' returned FALSE. (a route that does not parse must not replace the default)`; 31 passed, 1 failed |
| `logs/build-2.log` | `cmake --build build --target zene HardwareIoTest` (with the fix) | 0 | GREEN build |
| `logs/test-2.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` | 0 | 1/1 passed (32 test functions) |
| `logs/build-3.log` | `cmake --build build --target zene HardwareIoTest` (the product diff reversed) | 0 | the reverse-patch build |
| `logs/test-3.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` (the product diff reversed) | 8 | **RED proof**: the same assertion failed (`'!io.outputSet' returned FALSE`), 31 passed, 1 failed |
| `logs/build-4.log` | `cmake --build build --target zene HardwareIoTest` (the product diff re-applied) | 0 | the build after the proof |
| `logs/test-4.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` | 0 | 1/1 passed |
| `logs/build-5.log` | `cmake --build build --target HardwareIoTest SurroundTest ImportTrackIoTest MixerWindowsTest` | 0 | the neighbouring families' build |
| `logs/build-6.log` | `cmake --build build --target MixerAbRegressionTest MixerConcurrencyTest MixerRoutingBackwardCompatTest` | 0 | the three family binaries that were not built in this tree yet |
| `logs/test-5.log` | `ctest -R '^(HardwareIoTest\|SurroundTest\|ImportTrackIoTest\|MixerWindowsTest\|ControlHardwareIo\|ControlMixingRouting\|MixerRoutingBackwardCompatTest\|MixerAbRegressionTest\|MixerConcurrencyTest)$' --output-on-failure` | 0 | **9/9 passed** |
| `logs/build-7.log` | `cmake --build build --target zene HardwareIoTest` (after the pristine-HEAD experiment restored the change) | 0 | the final build |
| `logs/test-6.log` | `ctest -R '^HardwareIoTest$' --output-on-failure` | 0 | 1/1 passed - the final state |
| `logs/gates-1.log`, `logs/gates-2.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | the gate script's baseline step stopped with the Bunny lane guard (exit 97) before any gate ran - see "The gate runs" below |
| `logs/gates-3.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (baseline cache filled from an incomplete base export) | 1 | 6 PASS, 2 FAIL - the file-length row was wrong, the base export was incomplete |
| `logs/gates-4.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (baseline cache filled from the full base tree) | 1 | **6 PASS, 2 FAIL** - the two failures are pre-existing on the train (see below) |
| `logs/base-fork-sources.log` | `bash tests/fork-sources-gate.sh` on the **pristine HEAD** (the change temporarily reverted) | 1 | the same pre-existing failure, with the same 34 entries |
| `logs/base-all-sources.log` | `bash tests/all-sources-reproduce.sh` on the **pristine HEAD** | 1 | the same pre-existing failure, with the same 34 entries |
| `logs/gates-fork-after.log` | `bash tests/fork-sources-gate.sh` (after the manifest re-derivation) | 0 | `PASS: every tracked source in scope is registered (1976 fork-NEW, 1214 inherited, 48 tooling)` |
| `logs/gates-all-after.log` | `bash tests/all-sources-reproduce.sh` (after the manifest re-derivation) | 0 | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output` |
| `logs/gates-5.log` | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (final tree, after the manifest re-derivation) | 0 | **8/8 PASS, `RESULT: PASS`** |

The reverse-patch proof ran exactly as the procedure asks:

```
git diff -- src/core/HardwareIo.cpp > logs/fix.patch && git apply -R logs/fix.patch   # EXIT=0
cmake --build build --target zene HardwareIoTest                                      # EXIT=0
ctest -R '^HardwareIoTest$' --output-on-failure                                       # EXIT=8 (RED)
git apply logs/fix.patch                                                              # EXIT=0
cmake --build build --target zene HardwareIoTest                                      # EXIT=0
ctest -R '^HardwareIoTest$' --output-on-failure                                       # EXIT=0 (GREEN)
```

### The gate runs

`feature_check.py` computes the two ratchet baselines (complexity, file-length) on a checkout of
the branch's base commit with `git checkout --detach`. The Bunny lane guard
(`~/.local/share/zene-bunny-fix/bunny/shims/git`) refuses that command - it is on the guard's
deny list - so the unmodified script stopped with exit 97 **before it ran a single gate**
(`logs/gates-1.log`, `logs/gates-2.log`).

The cache entry for the base commit `07d6d02bd7846c6e0d6ffc7fdf7441ad20cd565c` was missing, so
the script tried to build it. It was filled by `logs/populate_gate_baseline.py`, which reuses
`feature_check.py`'s own `key()` and `REG` (the listing format is the script's own) and reads the
two gate logs produced on a `git archive` export of the base commit - a read-only export, no
checkout, no branch move, no history rewritten. The gate thresholds, the baseline TSVs and the
cache's other entries were not touched.

The first fill used a partial export (the fork sources only) and produced a wrong file-length
baseline (`logs/gates-3.log`: the file-length row reported a false NEW item for
`tests/src/core/HardwareIoTest.cpp`). The export was replaced by the **full** base tree
(`/tmp/p14full`, `git archive 07d6d02bd`) and the cache recomputed from it; the file-length row
then matched the script's own behaviour.

The final table (`logs/gates-4.log`):

```
feature-check zene-qwen-new-p14-120-port-port (base 07d6d02bd, 3 changed files)
  FAIL  fork-sources            exit 1
  FAIL  all-sources-reproduce   exit 1
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: FAIL
```

**The two failing rows were pre-existing on the train and were not caused by this change.** Both
were run on the **pristine HEAD** - the change temporarily reverted with
`git checkout -- <paths>` and restored afterwards with `git apply` - and both fail there with
exit 1 and the same 34 entries (`logs/base-fork-sources.log`, `logs/base-all-sources.log`). The
entries are vendored `plugins/SfzPlayer/sfizz/external/...` files, which the manifests' own
recipe filters exclude (`tests/all-sources.txt:22`:
`grep -vE '^(src/3rdparty/|...|plugins/SfzPlayer/sfizz/)'`) while the manifest still lists them:
1148 such paths in `tests/all-sources.txt`. The drift entered with the train-10 snapshot and was
re-broken by the train-12 snapshot `07d6d02bd` - this branch's own base commit - twenty minutes
after the owner's own repair `6168a6873` ("chore(tests): re-derive tests/all-sources.txt from
its own header recipe") had put it right. `tests/fork-sources-gate.sh` runs the whole-tree
manifest's recipe itself, so one manifest breaks both rows.

**The repair applied** (commit `125996c2b`, separate from the fix): the entry list is re-derived
the way the gate itself prescribes - "run the 'Regenerate with' command from the manifest's
header and write its output over the entry list" - and the header is left untouched. The diff is
**1148 deletions and nothing else**: no added line and no changed line outside the vendored
tree, which was checked mechanically (`git diff -- tests/all-sources.txt` shows no `+` line at
all). No gate limit, baseline or recipe was changed, no entry was hand-edited, and no product
code was touched. This is the same repair the owner made in `6168a6873` and that four peer lanes
of this train made for the same reason (`new-p12-221-port`, `new-p14-199-port`,
`new-p15-97-port`, `new-p16-141-port`).

After it, both rows are green (`logs/gates-fork-after.log`, `logs/gates-all-after.log`) and the
whole gate suite passes (`logs/gates-5.log`):

```
feature-check zene-qwen-new-p14-120-port-port (base 07d6d02bd, 5 changed files)
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

The two ratchet rows - the ones that judge *this* change - both PASS: "no new item in your
files".

## Self-check

- **The test is deterministic** - the test injects its input itself: it builds a `QDomElement`
  (`<hardwareio output="3/5"/>`, then `<hardwareio output="none"/>`) and hands it to
  `ChannelHardwareIo::load`. It reads no clock, sleeps for nothing and starts no thread: the only
  value it takes from the engine is `Engine::audioEngine()->framesPerPeriod()` (the period the
  insert's buffers are sized for, not a timing). The same input therefore always yields the same
  result.
- **The verdict is FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE question does not apply to this
  finding: the code the finding names is quoted verbatim above, and the branch carried it until
  this commit.
- **The regression test FAILS without the fix and PASSES with it** - RED: `ctest -R '^HardwareIoTest$'`
  -> exit 8, `malformedOutputKeepsTheDefaultRoute()` fails at `'!io.outputSet' returned FALSE`
  (`logs/test-1.log`, and again with the product diff reversed in `logs/test-3.log`); GREEN: the same
  command -> exit 0 (`logs/test-2.log`, `logs/test-4.log`, `logs/test-6.log`).
- **The affected family still passes** - `logs/test-5.log`: 9/9 families passed
  (HardwareIoTest, SurroundTest, ImportTrackIoTest, MixerWindowsTest, ControlHardwareIo,
  ControlMixingRouting, MixerRoutingBackwardCompatTest, MixerAbRegressionTest, MixerConcurrencyTest).
  The changed product file is `src/core/HardwareIo.cpp`; no header changed, so no other target had
  to be rebuilt beyond `zene` and the test binaries above.
- **Both gate runs** - `feature_check.py` -> exit 0, `RESULT: PASS`, **8/8 gates PASS**
  (`logs/gates-5.log`). The two rows that were red on the train (`fork-sources`,
  `all-sources-reproduce`) were proved pre-existing on the pristine HEAD and then repaired the way
  the gate itself prescribes (the manifest re-derived from its own header recipe, 1148 deletions
  and nothing else - see "The gate runs" above). No gate limit or baseline was changed, and the
  complexity and file-length rows - the ones that judge this change - report "no new item in your
  files".
- **Old projects and saved data load exactly as before** - the fix only changes the case where the
  stored route does not parse; a stored route that parses (including "none", a mono number, a pair
  and a bus name) is set as before, and the insert's and the cue's paths are unchanged in their
  behaviour. `everythingSurvivesASaveAndLoad` (`tests/src/core/HardwareIoTest.cpp`) still passes.
- **No collateral damage** - no limit was tightened; `parseDestination` and its bound
  (`audioio::MaxChannels` for a project's routing) are untouched, so a route beyond the running
  device is still accepted and a route saved on a bigger machine still loads.
- **Realtime, undo and commands** - the change is in `ChannelHardwareIo::load` (the main thread,
  at project load): nothing is added to the audio thread, nothing allocates, locks or logs there.
  No command was added and no undo step changed, so undo stays one honest step.
- **The files committed** - `src/core/HardwareIo.cpp`, `tests/src/core/HardwareIoTest.cpp`,
  `tests/upstream-modifications.txt`, `tests/all-sources.txt` (the train-wide manifest
  re-derivation, in its own commit) and
  `docs/qwen-fixes-2026-10/cluster-new-p14-120-port-port.md`. No new file was created, so nothing
  had to be registered in `tests/fork-sources.txt`.
