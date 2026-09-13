# Racks: macros and key/velocity zones, and the `rack.*` command surface

**Status: #599's second slice is in, on the 0.3.0 release line.** The first slice
(`docs/RACKS.md`, the `post-alpha/racks` lane) put parallel chains and a Chain
Selector under a mixer channel and said so in its §5: macros and key/velocity
zones were explicitly not in it, and the only way to have a rack at all was a
project file that already contained a `<rack>` element. This slice adds the two
missing halves **and the command surface that makes any of the rack drivable at
all** — because with no `rack.*` group, a macro or a zone would have been
unreachable from outside the process too.

Worktree: `projects/lmms-fl-research/zene-030-w8`, branch `030/w8-rack-macros`,
based on `post-alpha/integration` @ `70f2d087c3cb2da3a74c60ac74b08e6181d8b8c1`.

---

## 1. What the rack group is, and why it is part of this slice

The 0.3.0 scope contract (`NEXT-0.3.0-AGENT-PROMPT.md` §3.1) requires four things
of every feature that ships: the engine in the tree, a control-surface command
group with ids, argument/result schemas and SPEC A16 reversibility metadata, a
proof, and a written-down UI absence. The rack engine satisfied the first; this
slice supplies the other three, and the rack itself is the cheapest way to prove
the group is real, because the group's own entry point is the rack object:

| id | what it does | SPEC A16 class |
| --- | --- | --- |
| `rack.get_state` | the whole channel rack: chains, device counts, the selector, every macro with its targets, every zone | `not_mutating` |
| `rack.add_chain` | append an empty parallel chain | `true_inverse` (an action checkpoint removes the chain it created) |
| `rack.remove_chain` | drop a parallel chain (chain 0 refused) | `snapshot` (the chain's state XML is captured; recreating a chain *with* its effects is a documented manual fallback) |
| `rack.set_selected` | choose the chain the channel routes to, or `-1` for parallel | `true_inverse` (an action checkpoint restores the previous selection) |
| `rack.macro_add` | create a macro (a named scalar) | `true_inverse` |
| `rack.macro_remove` | drop a macro and its targets | `true_inverse` (the captured macro is re-inserted at its own index) |
| `rack.macro_target_add` | bind an existing parameter to a macro, with its window | `true_inverse` |
| `rack.macro_target_remove` | unbind one target | `true_inverse` |
| `rack.macro_set` | set the macro's value and drive every bound parameter in one step | `true_inverse` (ONE action checkpoint restores the macro and every parameter it wrote) |
| `rack.zone_add` | add a key/velocity zone | `true_inverse` |
| `rack.zone_remove` | drop a zone | `true_inverse` (re-inserted at its own index) |
| `rack.zone_resolve` | which zone a (key, velocity) falls into | `not_mutating` |

`rack.remove_chain` is the one mutating command with no automatic inverse, and
that is the same finding `mixer.remove_channel` records: the state is not lost
(it is in the transaction's before-state, bounded like every other snapshot) but
no *single* command recreates a container object with its devices. The row names
the fallback (`rack.add_chain`, then `plugin.load` + `plugin.state_load` per
device) rather than pretending. Editing the selector does not change a chain's
*latency* alignment — see §4.

Where the code lives: `src/core/ControlCommandsRack.cpp` (the rack object, the
selector and the shared helpers), `src/core/ControlCommandsRackMacros.cpp` and
`src/core/ControlCommandsRackZones.cpp` (the two halves, in their own translation
units for the same reason the automation and warp groups are split),
`include/ControlRackSupport.h` (the helpers the two share), registered from
`registerControlCommands()` in `src/core/ControlRegistry.cpp`.

## 2. Macros

**What a macro is.** A named scalar in `[0, 1]`, persisted with the rack, that
drives a set of *existing* model parameters. It is not a new kind of parameter
and it runs no DSP: setting it writes `AutomatableModel` values — the same
objects `plugin.param_set` writes, and therefore project state, journalled, and
undoable.

**Why the window is a fraction.** Each target carries a window `low..high` stated
as a **fraction of the parameter's own range**, not in the parameter's units:

```
fraction = clamp(low + value * (high - low), 0, 1)
written  = min + fraction * (max - min)
```

The window is what makes a macro a macro rather than a fader. Stating it as a
fraction is what makes the assignment survive a parameter whose `min..max` the
engine defines — recording the engine's numbers would pin the macro to the range
one build happened to report. `high < low` is legal and inverts the drive, which
is what "less when the macro is up" is.

**How a target is addressed.** By the chain index (0 = the channel's own chain),
the effect index inside that chain (the same `fx-<n>` order `dsp.get_state`
reports) and the parameter's **display name**. The name is resolved through the
effect's own parameter list, so a bind means the same parameter to a human, to an
agent and after a reload — which no pointer would.

**What happens when a target dies.** `rack.macro_set` resolves each target at
call time and **skips** one whose chain, effect or parameter is gone; the result
reports `applied` and `skipped`, so a caller can tell "wrote 3" from "wrote 2 and
one target is dead". `rack.macro_target_add` refuses a target that does not
resolve *at bind time*, so a macro cannot be built out of targets that can only
fail.

**Undo.** `rack.macro_set` writes more than one object (the macro's own scalar,
which lives on the rack and is not a `JournallingObject`, plus N parameter
models), so its inverse is ONE action checkpoint that re-resolves every target by
chain/effect/parameter name when it runs and writes the previous values back. No
raw device pointer is captured — the rule `controlRestoreCapturedState` documents
for a device applies to a macro target for the same reason: an undo step can run
after the project has been replaced under it.

## 3. Key and velocity zones

**What a zone is.** An inclusive **key range** (`0..127`), an inclusive
**velocity range** (`0..200` — the engine's own note-velocity range, the one
`note.velocity_set` uses, not `0..127`), the **chain** they map to, and an
optional free-form **sample reference**. A "key zone" and a "velocity zone" are
the two halves of one object here on purpose: a zone that states only keys is one
whose velocity range is the whole velocity space, and vice versa. One object
means one lookup rule instead of two that can disagree about which wins.

**The lookup.** `RackZones::resolve(key, velocity)` returns the first zone, **in
the order the zones were added**, whose key range *and* velocity range both
contain the note — or `-1`. First match wins is stated in the header rather than
left to whichever loop runs, so a specific-over-general ladder is built by adding
the specific zone first. The lookup reads a vector of scalars: no allocation, no
lock.

**What is refused.** A zone outside the engine's bounds, with `low > high`, or
naming a chain the rack does not have is refused, typed, by `rack.zone_add` —
rather than stored and matched never (or, worse, routing a chain nobody asked
for). The schema bounds the ranges too, so an out-of-range key never reaches the
handler.

**Honest scope — this is the part to read.** The rack renders **one stereo block**
and has no per-note input: the first slice's report said exactly this when it
explained why zones needed "a different data path (per-note routing), not a
control on this one". **Nothing in this build consults a zone while a note
plays.** What shipped is the persisted, validated, queryable zone model and its
resolver, reachable through the socket (`rack.zone_add` / `zone_remove` /
`zone_resolve`), and it survives a save/reload like the rest of the rack. What did
**not** ship is note routing through a zone. Closing that gap needs a per-note
path at the rack (a note event carrying its key and velocity into a chain
decision), which is a change to the mixer's rendering contract, not to this
model. `docs/KNOWN-LIMITATIONS.md` and the release notes carry the same sentence
in one line each.

## 4. What is NOT done

* **Note routing through a zone** — see §3. The model, the surface and the
  resolver are in; nothing calls the resolver on the audio path.
* **No UI, and no Lua binding.** Nothing in `src/gui/` creates a rack, a macro or
  a zone, and there is no macro knob or zone editor. `ScriptBindings` (Lua) is not
  extended either: the reachable path is the control surface.
* **A macro is not an automation source.** It writes values when it is *set*.
  There is no continuous link (no `ControllerConnection`), so a macro cannot be
  automated, and it does not follow anything — a macro is a control-surface
  primitive today, not a modulation source. The #602 modulation layer is a
  separate item.
* **Windows are per-target fractions, not curves.** A macro drives a linear map
  over each target's window; there is no easing, no per-target scale polarity
  beyond `high < low`, and no macro-to-macro chaining.
* **Latency/PDC is still not per-chain** (inherited from the first slice): the
  channel's published latency is `m_fxChain.latencyFrames()`, so a rack whose
  *other* chains report latency is not aligned at the summing point, and changing
  the selection changes the channel's latency without telling the PDC graph.
  Neither macro nor zone work touched this.
* **Zone and macro ids are index-derived** (`macro-<n>`, `zone-<n>`), like
  `clip-<n>` and `note-<n>`: a removal renumbers the ones after it. The *inverse*
  of a removal re-inserts at the original index, so an undo restores the ids; a
  re-created macro elsewhere in a session does not keep an old id.
* **The rack's own reachability is still project-file-or-socket.** A user without
  a socket client still cannot make a rack.
* **`rack.remove_chain` has no automatic inverse** — see §1 and the A16 table.
* **Windows and zone ranges are not validated against each other.** A macro bound
  to a parameter and a zone mapped to the same chain are independent; nothing
  warns that a zone maps to a chain holding a parameter a macro is driving.

## 5. Proofs

### 5.1 The registered ctest — `tests/src/core/RackMacrosTest.cpp`

Registered in `tests/CMakeLists.txt` and declared in `tests/all-sources.txt` and
`tests/fork-sources.txt`. It drives a real `Engine`, a real `Mixer` and real
`AutomatableModel` parameters through a local test effect (no plugin module, so
nothing in this test starts the plugin loader and the teardown race
`docs/RACKS.md` §6 records cannot land on it):

* `macrosDriveParametersThroughTheirWindows` — the window arithmetic against two
  parameters with deliberately different ranges (`0..100` and `-100..100`), the
  inverted window, the `0..1` clamp, and the reported previous/written pair;
* `aMacroSkipsTargetsItCannotResolve` — 3 targets, 1 applied, 2 skipped, named;
* `zonesResolveKeyAndVelocityRanges` — inclusive bounds on both axes, first match
  wins where two zones overlap, `-1` where nothing matches, and the four refusal
  shapes;
* `configuringMacrosAndZonesLeavesTheAudioPathAllocatingNothing` — the rack's own
  `processAudioBuffer()` with macros and zones configured, measured with
  `tests/src/core/AllocationProbe.h`;
* `theGroupIsRegisteredWithSchemasAndA16Classes` — all 12 ids, their group, both
  schemas, the `mutating` flag, and the SPEC A16 class from the one contract
  table;
* `theSurfaceCreatesDrivesAndUndoesAMacro` — the end-to-end registry path
  (`rack.macro_add` → `rack.macro_target_add` → `rack.macro_set` →
  `control.undo`) and the zone path (`rack.zone_add` → `rack.zone_resolve` →
  `rack.zone_remove` → `control.undo`), asserting that the undo restores **both**
  the parameter and the macro's own value;
* `theSurfaceRefusesWhatCannotExist` — the typed refusals (`not_found` for a
  channel that is not there, `invalid_args` for a bad chain/key/selection/target,
  `refused` for chain 0);
* `theConfigurationPersistsUnderTheRackElement` — the whole configuration saved as
  `<macro>`/`<zone>` children of the channel's existing `<rack>` element, reloaded
  and re-saved, with the `version` attribute read as `2`.

### 5.2 Evidence the test prints

`RACK_MACRO_EVIDENCE` lines on stdout, flushed; the run's actual output is in §7.
The numbers, as measured at this commit:

| label | measured |
| --- | --- |
| `window-low` | value `0.0` → gain `25` (0..100 with the 0.25..0.75 window), pan `-100` (-100..100, window 0..1) |
| `window-arithmetic` | value `1.0` → gain `75`, pan `100`; value `0.5` → the window's middle, `50` |
| `window-inverted` | window 0.75..0.25: value `0.0` → 75, value `1.0` → 25 |
| `skipped-targets` | targets 3, applied 1 (a missing effect and an unknown parameter are skipped) |
| `macro-zone-rack-allocations` | **0 allocations**, with macros and zones configured |
| `group-registered` | 12 commands, 10 mutating, 2 read-only, A16 classes from the one table |
| `surface-macro-set` | gain 100 → 75 through the window 0.25..0.75 at value 1.0 |
| `surface-macro-undo` | `control.undo` restored the macro to 0.0 **and** the parameter to 100 |
| `typed-refusals` | `not_found` for a missing channel, `invalid_args` for empty name / bad selection / dead target / bad window, `refused` for chain 0 |
| `zone-resolve` | 2 zones, first-match: (40,150)→0 where both match, (60,150)→1, (49,0)→−1 |
| `zone-refusals` | bad chain/key → `invalid_args`, unknown zone → `not_found`, a note outside every zone → `matched=false` |
| `surface-zone-undo` | `zone_remove` → `control.undo` → the zone is back at its index |
| `persistence-saved` | `<rack version="2" selected="1">` + 1 `<macro>` (1 `<target>`) + 2 `<zone>`, all children of the channel's own `<rack>` |
| `persistence-reloaded` | macro 1 (value 0.25, target 1), zones 2, resolve(40,150)=0, resolve(55,20)=−1, `apply` writes 0 (the fixture's chain has no devices) |
| `persistence-round-trip` | saved = loaded = re-saved |

### 5.3 Gates

`file-length-gate` (7), `complexity-gate` (4), `duplication-gate` (8),
`no-tautology-gate` (3), `fork-sources-gate` (9), `no-upstream-regression-gate` (6)
and `unregistered-tests-gate` all exit 0 at this commit, unpiped (the ratchet
gates in `--check` mode, so no baseline was rewritten). The ratchets were
re-run because nine of the new files are fork-NEW sources, which changes the
measured scopes of gates 4, 7 and 8.

## 6. How to reproduce

```sh
cd ~/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030-w8
JOBS=4 tools/local-ci.sh --build-dir build --jobs 4     # -DWANT_QT6=ON printed as a deviation
cd build/tests && ctest -R RackMacrosTest -V            # the evidence table above
cd build/tests && ctest                                  # the whole suite
bash tests/fork-sources-gate.sh                          # gate 9
bash tests/no-upstream-regression-gate.sh                # gate 6
bash tests/run-all-gates.sh                              # 3 = PASS-WITH-SKIPS
cd build/tests && ctest -R agent_surface -V              # the new commands, swept headless
```

## 7. Run log — unpiped exit codes

Build directory `build/`, configured by `tools/local-ci.sh` (the CI linux job's
own flags, plus the `-DWANT_QT6=ON` deviation that script prints itself, because
this box has no Qt5 development files). Every code below is the shell's `$?` for
that command, unpiped.

```
$ cmake --build build --target RackMacrosTest -j4        BUILD_EXIT=0
$ cmake --build build --target RackZonesTest -j4         ZONES_BUILD_EXIT=0
$ cmake --build build --target zene -j4                  ZENE_BUILD_EXIT=0

$ cd build/tests && ctest -R "RackMacrosTest|RackZonesTest" --output-on-failure
  1/2 Test #44: RackMacrosTest ...... Passed   1.37 sec
  2/2 Test #46: RackZonesTest ....... Passed   1.37 sec
  100% tests passed, 0 tests failed out of 2       CTEST_RACK_EXIT=0

$ cd build/tests && QT_QPA_PLATFORM=offscreen ./RackMacrosTest   MACROS_TEST_EXIT=0
  Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 1334ms
$ cd build/tests && QT_QPA_PLATFORM=offscreen ./RackZonesTest    ZONES_TEST_EXIT=0
  Totals: 5 passed, 0 failed, 0 skipped, 0 blacklisted, 1338ms

$ cd build/tests && ctest -R agent_surface -V             AGENT_SURFACE_CTEST_EXIT=0
  86 commands, 85 swept, 1 allowlisted, 0 compiled out
  PASS: agent surface gate (reflection + ratchet + reverse completeness + headless sweep)
  (all 12 rack.* ids appear in the sweep, each with a typed result)

$ cd build/tests && ctest -R "ReversibilityContractTest|ControlRegistryTest"
  100% tests passed, 0 tests failed out of 2       CTEST_A16_EXIT=0
  (the anti-drift test that walks the WHOLE contract table - now two literal
   blocks - against the registry, in both directions)

$ cd build/tests && ctest -R "ReversibilityUndoTest|RackTest"
  1/2 Test #45: RackTest ................. Passed  1.37 sec
  2/2 Test #52: ReversibilityUndoTest .... Passed  1.63 sec
  100% tests passed, 0 tests failed out of 2       CTEST_EXTRA_EXIT=0
  (RackTest is the #599 engine lane's own test, unchanged here: the rack's
   chains and selector still behave as its report says)

$ bash tests/file-length-gate.sh --check            EXIT=0  (baseline not written)
$ bash tests/complexity-gate.sh --check             EXIT=0  (baseline not written)
$ bash tests/duplication-gate.sh --check            EXIT=0  (baseline not written)
$ bash tests/no-tautology-gate.sh --check           EXIT=0
$ bash tests/fork-sources-gate.sh                   EXIT=0  (257 fork-NEW, 1036 inherited, 34 tooling, 0 stale)
$ bash tests/no-upstream-regression-gate.sh         EXIT=0  (457 changed paths declared; 470 ledger entries)
$ bash tests/unregistered-tests-gate.sh             EXIT=0  (96 test sources: 91 registered, 5 declared-not-built)
```

**NOT RUN here, and why.** The full `ctest` suite (87 tests), gate 1 (ctest),
gate 2 (coverage) and gate 5 (mutation) each need every test executable linked
into `build/tests` at once — about **8.7 GB**, measured, because every test
binary statically links the library with debug info. This box ran out of disk
during this lane: with three sibling lanes building concurrently the volume went
from 39 GB free to **1.1 GB free** and the build was stopped at 50%, after which
`build/tests` was reclaimed to give the sibling lanes their headroom back. So
`tests/run-all-gates.sh` could not be run to completion, and this lane's
evidence is the seven gates above, the four test executables it did link
(`RackMacrosTest`, `RackZonesTest`, `ReversibilityContractTest`,
`ControlRegistryTest`), the `agent_surface` gate against the real `zene` binary,
and a `-fsyntax-only` compile of every source it touched.

**The one number this lane could not execute:** the
`telemetryCommandsAreAbsentWhenTheClientIsCompiledOut` slot of
`ControlRegistryTest.cpp` (the `#else` of the telemetry kill switch, which is
the branch its command-count assertion lives in) does not compile in this
configuration — `ZENE_TELEMETRY` defaults ON. The count was therefore measured
from the running binary instead: the `agent_surface` gate reports **86
commands** with the telemetry pair compiled in, so the telemetry-OFF surface is
**84**, which is what that assertion now states (`84 + 5` synthetic commands). It
needs a `-DZENE_TELEMETRY=OFF` build to execute, and this lane did not have the
disk for a second build.
