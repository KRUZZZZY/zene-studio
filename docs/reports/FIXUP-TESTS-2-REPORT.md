# FIXUP-TESTS-2 — the last five failing classes, closed on the engine's own truth

**Lane:** fix-up continuation (round 2), branch **`030/fixup-tests-2`** in
`zene-030/wfixt` (worktree of `lmms/.git`). **Base:** `4ba4ddcf2` (the merged tip).
**Tip:** `f7fecddee`. **Date:** 2026-09-16. **One commit per class**, each carrying the
unpiped exit code it was proved with — nothing was held back for a later commit.

| # | class | before | after | commit |
|---|---|---|---|---|
| 1 | `StableTrackIdsTest` | 1 failed / 7 (EXIT=1) | **0 failed / 7 (EXIT=0)** | `1d2640806` |
| 2 | `PatcherCommandsTest` | 8 failed / 11 (EXIT=8) | **0 failed / 11 (EXIT=0)** | `c05ad89f2` |
| 3 | `SafeStartTest` | 3 failed / 9 (EXIT=3) | **0 failed / 9 (EXIT=0)** | `6a1bc0f16` |
| 4 | `SafeStartLoadPathTest` | SIGSEGV (EXIT=139) | **0 failed / 3 (EXIT=0)** | `2403c0a81` |
| 5 | `ControlSocketIntegration` | 1 FAIL (EXIT=1) | **0 FAIL, 2 PASS (EXIT=0)** | `f7fecddee` |

**Full suite, from `build/tests`, the canonical way:**

```
$ LD_LIBRARY_PATH=<wt>/third_party/wasmtime/lib ctest -j4 --output-on-failure
100% tests passed, 0 tests failed out of 213
Total Test time (real) = 143.92 sec
CTEST_EXIT=0
```

Baseline at `4ba4ddcf2` for comparison, same command: **6 failed of 213** —
`PatcherCommandsTest`, `StableTrackIdsTest`, `ControlSocketIntegration`, `SafeStartTest`,
`SafeStartLoadPathTest` (SEGFAULT) and `ControlMidiReconnect`.

---

## 1 · StableTrackIdsTest — the load path never reached the ProjectIds fix

**Not a test defect.** The round-trip drift had a live cause inside the counter.

The failing slot was `legacyProjectGetsDeterministicIdsAndResavesByteIdentically`:
round1 (written after loading the legacy file) says `next-id="3"`, round2 says
`next-id="5"` for a three-track project, so the byte-identity check failed.

Instrumented the engine rather than the test (`gdb -batch`, breakpoints on
`ProjectIds::beginLoad/endLoad/observe/restoreFromDocument/allocate`):

```
>>> beginLoad
observe(1)                     <- Mixer::loadSettings -> MixerChannel::setId(1)
allocate / observe(0)          <- Track ctors (placeholders 2,3,4) + their document ids
allocate / observe(1)
allocate / observe(2)
>>> restoreFromDocument(3)
>>> endLoad
```

`restoreFromDocument(3)` could not restore 3: `ProjectIds::observe(id)` floored the counter
at its **current value** (`if (s_next > s_documentFloor) …`), and during a load pass the
counter sits above the document's ids because every constructor took a placeholder first.
A mixer channel carrying id 1 (the first object the legacy load saw) pushed `s_next` to 2;
the three track placeholders (2,3,4) then pushed it to 5; `observe()` recorded 5 as the
floor and `s_documentFloor` won the max. So the counter came back as 5 and the second save
wrote a different file.

**Fix (`src/core/ProjectIds.cpp`):** an observed id names a live object, so the floor it
raises is *that id's successor*, never the counter's current value —

```cpp
if (id >= s_next) { s_next = id + 1; }
if (id + 1 > s_documentFloor) { s_documentFloor = id + 1; }
```

Allocations outside a load pass still enter the floor through `allocate()`; a placeholder an
object *kept* still enters it through `noteLoadAssignment()`. The sparse-id slot
(`rootCounterIsHonouredAndNeverReused`, ids 5/7/9 with a stale `next-id="1"`) still floors at
10, and the `TempoMapPersistenceTest` / `ControlStableIdsSlice2` classes stayed green.

**Evidence:** `./build/tests/StableTrackIdsTest` → `EXIT=0`, `Totals: 7 passed, 0 failed`.

## 2 · PatcherCommandsTest — the fixture drove a different chain than the one it filled

The slots reported `routes_through_graph=false` and “`effect:1` names no node of this chain:
it has **0** effect(s)” while `initTestCase`'s own `QVERIFY2(chain->routesThroughGraph())`
passed. `gdb`, breaking on `patcherStateJson`:

```
target.id = "ch-1" ; target.kind = "channel" ; chain.m_effects.size() = 0
host has 3 mixer channels, ids [1 (master), 3, 2]
&mixerChannel(1)->m_fxChain = 0x55555705fbd0   (the chain initTestCase filled)
patcherStateJson chain      = 0x5555567fdba0   (the MASTER's empty chain)
```

`channelId(kChannel)` in that TU is `lmms::control::channelId(int)` from
`ControlVocabulary.h` — the **formatter**, which writes the literal `ch-1`. The number in a
real `ch-<n>` is the channel object's own `ProjectIds` id from the one project-wide counter,
so `ch-1` resolved to the master, and every wiring slot measured the master's empty chain.

**Fix (`tests/src/core/PatcherCommandsTest.cpp`):** `channelIdAt(index)` asks
`mixer.get_state` for the id of the channel at that index (the same order `mixerChannel()`
uses); `channel()`/`emptyChannel()` use it, and `initTestCase` now asserts that the id it
drives equals the id of the channel it filled, so the two cannot drift apart again.

Two further slots were re-derived on the engine's documented order (which the header
`ControlCommandsPatcherShared.h` states: unknown reference → repeated/self edge → port
arity → cycle → unreachable output):

* the cycle case was `effect:0 → input`, refused earlier by the **port-arity** check
  (“input has 0 input port(s)”) because the graph's input node is the chain's *source*; it is
  now a real loop among the effects (`input→effect:0`, `effect:0→effect:1`,
  `effect:1→effect:0`) and is refused with the cycle message;
* `aChainWithNoGraphRefusesAnEdit` asserted `Refused` for an edge list on a zero-effect
  chain. The engine's own two refusals there are `InvalidArgs` (no effect for a default
  output to name; then the unknown reference with the output named), and `Refused` is
  `setPatchWiring`'s path for a wiring *valid* for the node set — unreachable for a chain
  whose only valid wiring is the empty one. Both refusals are now asserted, plus “nothing was
  written” (`patcher.get_state` is byte-compared before and after).

**Evidence:** `cmake --build build -j4 --target PatcherCommandsTest` → `BUILD_EXIT=0`;
`./build/tests/PatcherCommandsTest` → `EXIT=0`, `Totals: 11 passed, 0 failed`.

## 3 · SafeStartTest — one engine gap, two fixture defects

**(a) `boundsTheMarkerCannotExceedItsCap` — engine.** `SessionRecord` promises strings
“bounded when they are read as well as when they are written” (`include/SafeStart.h`), but
`parseMarker()` took the `project=` line verbatim; `readMarker()`'s 1 KiB byte cap still
admitted a hint above `kMaxProjectPathBytes` (512). The parse now runs the same `bounded()`
the writer uses, so a marker this build did not write (older build, packager wrapper,
hand-edited file) cannot push an oversized hint through the reader.

**(b) `thePredicateOnlySkipsThirdPartyFiles` — fixture.** The case was written as
`own + "/sibling/libtripleoscillator.so"` while its own comment names the component-wise
rule for a **prefix sibling**. A subdirectory *is* under the directory, and the engine's rule
is a directory rule — “A plugin file under one of these is a file this build ships”
(`ownPluginDirectories()`), enforced by `isThirdPartyPluginFile()`'s `own + "/"` prefix test.
The prefix-sibling case is now a real sibling (`own + "-sibling/…"`), and the subtree case is
asserted as own with the reasoning recorded (the factory's own discovery scan is flat —
`QDir::entryInfoList` — so such a file is unreachable through the search paths either way).

**(c) `realSignalLeavesTheMarkerAndTheNextLaunchStartsSafe` — fixture.** `safeStartRunCount()`
is 2, not 1. The engine counts “How many consecutive sessions have started safe; 1 for the
first” as `previous.safeStartRuns + 1`. The sequence this test builds is: the parent's
session (idle directory, runs 0, normal) → the **child's** session, which must run
`beginSession()` to put its own pid in the marker and therefore *is* safe start 1 (it then
dies by SIGSEGV) → this session, safe start 2. 1 would describe a crash whose session started
with no marker on disk. The slot now asserts both the absolute 2 and the derivation
`previous.safeStartRuns + 1`.

**Evidence:** `cmake --build build -j4 --target SafeStartTest` → `BUILD_EXIT=0`;
`./build/tests/SafeStartTest` → `EXIT=0`, `Totals: 9 passed, 0 failed`.

## 4 · SafeStartLoadPathTest — a null-deref in `JournallingObject` (gdb first)

Backtrace taken before anything was touched (`gdb`, `handle SIGSEGV stop`):

```
#0 lmms::fastRand ()                                  include/lmms_math.h:92
#1 lmms::ProjectJournal::allocID (this=0x0)           src/core/ProjectJournal.cpp:257
#2 lmms::JournallingObject::JournallingObject ()       src/core/JournallingObject.cpp:38
#3 lmms::Plugin::Plugin (descriptor=0x0, parent=0x0)  src/core/Plugin.cpp:71
#4 lmms::DummyPlugin::DummyPlugin ()                  include/DummyPlugin.h:39
#5 lmms::Plugin::instantiate ()                       src/core/Plugin.cpp:241
#6 SafeStartLoadPathTest::theLoadPathReallySkipsAThirdPartyInstance()  :151
-> SIGSEGV, code 1, address 0x8
```

The crash is not in the predicate: the safe-start skip correctly handed back the engine's
`DummyPlugin`, whose construction dereferenced `Engine::projectJournal()` — null before
`Engine::init()` and after `Engine::destroy()`, a state the class's own **destructor** has
always guarded for (`JournallingObject.cpp:49`) and the constructor never did. The test
binary never runs `Engine::init()` because it is about the load-time predicate alone.

**Fix (`src/core/JournallingObject.cpp`, upstream-inherited):** the constructor takes the
same guard and leaves `m_id` at 0, the “no journal saw this object” value — every id
`allocID()` hands out carries `EO_ID_MSB`, so 0 cannot be mistaken for a registered id.
Nothing changes while a journal exists, which is every path in the product.

`tests/upstream-modifications.txt` carries the declaration for that file **in the same
commit**: `bash tests/no-upstream-regression-gate.sh` → `EXIT=0`.

**Evidence:** `cmake --build build -j4 --target SafeStartLoadPathTest` → `BUILD_EXIT=0`;
`./build/tests/SafeStartLoadPathTest` → `EXIT=0`, `Totals: 3 passed, 0 failed` (was 139).

## 5 · ControlSocketIntegration — three expectations that describe an older tree

Re-run against the merged tip: `EXIT=1`, exactly one FAIL — and the whole 340-command leg
otherwise ran clean. Three checks were fixed on the engine's own answers:

1. **`"clip-0"` literal.** The editing flow asserted `fixture_clip["id"] == "clip-0"`; the
   clip's id comes from the project-wide counter and this fixture's PatternClip is
   `clip-26`. The check now asserts the shape (`trk-<n>` / `clip-<n>`) and addresses
   `roll.get_state` with the id the engine reported.
2. **The effect id across `plugin.state_load`.** `plugin.bypass`/`unload` used the `fx-<n>`
   captured before the instrument section, whose last call is `plugin.state_load`. For an
   instrument that is the product's own track-preset path
   (`controlRestoreDeviceState` → `Track::loadPreset` → `loadTrack` in preset mode), which
   **re-creates the track's device chain** exactly as it already re-creates its clips; the
   re-created effect is a new object with a new id (rule R4: the preset document is one of
   the copy containers `ProjectIds::isCopyContainer` names — `instrumenttracksettings`).
   Measured in isolation on a live instance:

   ```
   plugin.load effect  -> fx-29 amplifier
   plugin.load instrument -> inst audiofileprocessor
   after instrument load        devices=[('fx-29','Amplifier')] instrument=inst
   state_load : True
   after state_load             devices=[('fx-30','Amplifier')] instrument=inst   <-- new id
   bypass fx-29 : not_found: no device fx-29 on trk-27 (it carries 1)
   ```

   The driver now re-reads the id from `dsp.get_state` at the point of use, and prints the
   re-creation when it happens.
3. **`automation.mode_set` refusing.** The leg expected `refused` with a message citing
   KNOWN-LIMITATIONS; the merged tree has the mode vocabulary (off/read/touch/latch/write).
   As the mode is runtime state (not persisted, not journalled) the leg now sets `write`,
   reads it back through `automation.get_state`, sets it back to `read` (so the render leg
   below still measures the default engine) and keeps the invalid-mode refusal.

**Evidence (unpiped):**

```
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=<wt>/third_party/wasmtime/lib \
  python3 tests/control-socket-integration.py build/zene tests/data/agent-control-fixture.mmp
-> EXIT=0   ("PASS: 8 pipelined 340-command replies …", "PASS: control socket integration …", 0 FAIL)
ctest -R "ControlSocketIntegration|ControlMidiReconnect"  ->  CTEST_EXIT=0, 2/2 passed
```

---

## Already green classes that were re-checked and must keep passing

`TempoMapPersistenceTest`, `ControlStableIdsSlice2`, `ClipLinkTest`,
`ClipSerialisationTest`, `ControlNoteScaleVerbsTest`, `ControlProjectArchiveTest`,
`SmfInterchangeTest`, `ReversibilityContractTest` — all green in the 213/213 run above.

## Residual reds, honestly

* **`ControlMidiReconnect`** was red in the *baseline* `-j4` run and green everywhere after.
  Its own guard says why it can fail on a busy box:

  ```
  the device returns at a NEW address and the engine re-attaches FAILED (1)
      the recreated client came back at the SAME address 130:0: this run cannot show a
      re-attachment to a new one
  ```

  The ALSA sequencer recycled the client address under four concurrent test binaries, so the
  scenario could not be staged. It passed in the `-R` re-run and in the final full run. Not
  attributable to any change in this lane.

## Open items for the merge (measured, not fixed)

1. **A device-state restore gives up the whole track's ids.** `plugin.state_load` for an
   instrument re-creates the track's clips and devices through `Track::loadPreset` →
   `loadTrack(presetMode)`, so every `clip-<n>` and `fx-<n>` on that track changes. Upstream
   behaviour, untouched here; the driver now re-reads the id, and the product question
   (“should an instrument preset reload re-create the chain?”) is recorded rather than
   answered in a fix-up pass.
2. **`changeID()` still dereferences `Engine::projectJournal()` unguarded**
   (`JournallingObject.cpp:134`). It is reached through `restoreState()` of a document that
   carries a `<journallingObject id=N>` node, which cannot happen without a journal today;
   the constructor was the measured crash and is fixed, this one is named for the next pass.
3. **`SafeStart.cpp`'s subtree rule is a directory rule.** A module under an own plugin
   directory (including a subdirectory) is treated as this build's own, while the factory's
   discovery scan is flat. Harmless today (unreachable through the search paths), recorded
   because a per-file list is what would tighten it.
4. The **A16 histogram / `ControlCommandsSnapshot`** constants were not touched by this lane
   and are green in the 213/213 run; nothing here moved them (`ProjectIds`,
   `SafeStart`, `JournallingObject` and the fixture files are the whole diff).

## Gates (all run after the last commit, unpiped)

```
bash tests/fork-sources-gate.sh            -> EXIT=0
   660 fork-sources entry(ies), 1104 all-sources (whole-tree), 40 tools-sources, 0 stale
bash tests/no-upstream-regression-gate.sh  -> EXIT=0
   422 changed path(s) declared; the ledger holds 460 entries
bash tests/all-sources-reproduce.sh        -> EXIT=0
   REPRODUCES: the entry list in all-sources.txt is the recipe's own output
```

No new source file was added by this lane (the diff is two engine files, one inherited file
plus its ledger line, and three test files), so no manifest needed a new entry.

## The next single action

Merge `030/fixup-tests-2` (`f7fecddee`) and re-run `ctest -j4` from `build/tests` on the
merge tip — the five classes closed here are the whole of the `-j4` red set at `4ba4ddcf2`,
so any red that survives belongs to a lane merged after this one.
