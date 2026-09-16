# FIX-UP lane: engine + test defects on the release line (2026-09-16)

**Lane:** `030/fixup-engine`, worktree `zene-030/wfixeng`, based on `49a40b30c`.
**Branch tip:** `b313e3691`. Commits: `825ceadb4`, `0f5ed5543`, `e27aba863`, `6f1bb35dd`,
`16fdbf0d3`, `1a32a247d`, `fbb5a7df5`, `b313e3691`. **No push, no merge, no rebase.**
**Build:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON -DUSE_WERROR=ON
-DUSE_COMPILE_CACHE=ON -DWANT_CLAP=OFF -DWANT_VST3=OFF -DWASMTIME_ROOT=…/zene-030/third_party/wasmtime`
→ `CONFIGURE_EXIT=0`; `cmake --build build -j4 --target …` → `BUILD_EXIT=0` (7 build rounds, all 0).
Every test command below ran from `build/tests` with `QT_QPA_PLATFORM=offscreen`; exit codes unpiped.

## 0 · Result

`ctest -R "^(…16 names…)$"` → **`CTEST_FINAL_EXIT=0`, 16/16 passed** (was: 4 named failures +
7 subprocess aborts + 6 more of the same class, on this tree).

| test | before (measured on this tree, unmodified) | after |
|---|---|---|
| `ReversibilityContractTest` | **EXIT=134 (SIGABRT)** after 5 slots | **EXIT=0**, 6/6 |
| `ControlReversibilityTranscript` (ctest) | not re-measured here; the abort above *is* its `script.run` path | **EXIT=0** |
| `ModulationLayerProjectRoundTripTest` | EXIT=1 (`'Gain' does not resolve … the rack has no chain 1 (it has 1)`) | **EXIT=0** |
| `ControlRegistryTest` | EXIT=1 (`NotFound` 1 vs `Refused` 5) | **EXIT=0** |
| `ControlProjectArchiveTest` | EXIT=1 (`errorKind` 1 vs 3) | **EXIT=0** |
| `ControlNoteScaleVerbsTest` | EXIT=3 (mask ×2 **and** a third slot, `note.select`) | **EXIT=0** |
| `SmfInterchangeTest` | EXIT=1 (`tick` 3840 vs 0) | **EXIT=0** |
| `SmfInterchangeRoundTripTest` | EXIT=1 (`map() == rebuilt` FALSE) | **EXIT=0** |
| `ScriptDawBindingTest` | aborted (never ran a slot) → then EXIT=4 | **EXIT=0**, 8/8 |
| `ControlModulatorCommandsTest` | EXIT=4 | **EXIT=0** |
| `ModulationLayerTest` | EXIT=1 | **EXIT=0** |
| `ControlNoteExpressionCommandsTest` | EXIT=1 | **EXIT=0** |
| `RackMacrosTest` | EXIT=2 | **EXIT=0** |
| `RackZonesTest` | not measured before (same cause) | **EXIT=0** |
| `ControlPdcCommands` (socket) | **ctest FAILED** (`CTEST_PDC_EXIT=8`, master id assertion) | **EXIT=0** |
| `ControlBusCommands` (socket) | not measured before (same cause) | **EXIT=0** |

## 1 · The LuaBridge abort (`src/core/ScriptDawBindings.cpp`) — the release blocker

**Mechanism (found, not guessed).** `beginNamespace("zene")` installs a metatable on the `zene`
table carrying `__newindex` beside `__index` (LuaBridge `Namespace.h`, same block). The code that
installs the raw C closure `zene.apiSurface` used `lua_setfield(L, -2, "apiSurface")`, and
`lua_setfield` *takes* the metamethod: the write ran LuaBridge's `__newindex`, which finds no
setter for a member it does not know and calls `luaL_error("No writable member '%s'")`
(`CFunctions.h:175`). Nothing catches it — registration happens on the script worker before any
chunk is loaded — so the `LuaException` reached `std::terminate`:

```
terminate called after throwing an instance of 'luabridge::LuaException'
  what():  No writable member 'apiSurface'
Received signal 6 (SIGABRT)
```

**Fix.** A RAW write (`lua_pushstring` + `lua_pushcfunction` + `lua_rawset`), which is what
LuaBridge itself does for its own members (`rawsetfield` throughout `Namespace.h`/`CFunctions.h`).
Comment says why, so nobody "tidies" it back.

**Proof:** `./ReversibilityContractTest` **EXIT=134 → EXIT=0**;
`ctest -R '^ControlReversibilityTranscript$'` → **EXIT=0** (was: `script.run` closed the socket).

### 1a · Four more Lua-binding defects the abort was hiding (same file, all measured)

With registration fixed, `ScriptDawBindingTest` ran for the first time and failed 4 of 6 slots.
Each cause was isolated on the built binary with a `pcall`-per-call probe
(`./build/zene --run-script`):

```
channelCount => true | 1        ids    => false | The class is not registered in LuaBridge
channel(0)   => true | userdata  id     => false | The class is not registered in LuaBridge
addChannel   => true | userdata  name   => false | The class is not registered in LuaBridge
index        => true | 0         gainModel => false | The class is not registered in LuaBridge
isValid      => true | true      chain  => false | The class is not registered in LuaBridge
gain         => true | 1.0       master => true | userdata
```

* **Missing `Stack<QString>`/`Stack<QStringList>`** (`include/ScriptLuaQtTypes.h` was included by
  `ScriptBindings.cpp` only): every QString-returning function instantiated LuaBridge's generic
  Stack → "The class is not registered in LuaBridge". One include; `id()`/`name()`/`ids()` work.
* **`id()` and `sendTarget()` built `"ch-<n>"` from an INDEX** (`control::channelId(index)` /
  `channelId(receiverIndex())`) — not the object's id the surface resolves. They use
  `channelIdOf()` now. (Header comment already promised "the same stable id mixer.* uses".)
* **`channelById()` passed the parsed number to `channel()`, which reads it as a position** — so
  `mixer:channelById(channel:id())` answered an invalid view whenever the two numbers differed
  (measured: master `ch-1`, first `mixer.add_channel` `ch-7`). It looks the id up now.
* **`chain()` returned a raw `lmms::EffectChain*`**, a class the bridge does not have (the
  registered name `"EffectChain"` is `LuaEffectChain`): it returns the view (`newEffectChain`).
* **`apiSurface` put `function_count` inside `functions`**, so the documented read
  `zene.apiSurface().function_count` raised `attempt to concatenate a nil value`. It is a field of
  the surface now (shape matches the function's own doc comment).

**Proof:** `ScriptDawBindingTest` **EXIT=4 → EXIT=0** (and it asserts the script's own console
lines: `created id=<real id> …`, `readback name=lua-driven index=1`, `mixer 1 functions 23`).

## 2 · `modulator.target_set` "binding Gain" — the fixture addressed the wrong channel

**Mechanism (measured).** The test asks for `channel: channelIdOf()` = `"ch-1"`. In that session
`ch-1` is the **master**; the fixture's channel (index 1) carries id **3**
(`mixer.get_state`/`MixerChannel::id()`), so the resolver found the master's rack, which has no
chain 1: `'Gain' does not resolve to a parameter: the rack has no chain 1 (it has 1)`. A `ch-<n>`
id is allocated at construction and is not the channel's index — SPEC-stable-ids.md slice 2, and
§4.2: a client *"never parses, derives, or predicts an id"*.

**Fix.** `RackTestSupport.h` gains `underTestChannelId()/underTestChannel()`, which ask the engine
for the object `initRackFixture` built (`Engine::mixer()->mixerChannel(kChannel)->id()`) — the rule
`SampleAccurateAutomationTest::channelIdOf()` already states. `ModulationTestSupport.h`'s
`routeOf()`/`channelIdOf()` and the two rack command tests use it. **This was 9 slots across four
binaries**, not one: `ModulationLayerProjectRoundTripTest` (1), `ControlModulatorCommandsTest` (4),
`ModulationLayerTest` (1), `RackMacrosTest` (2), `RackZonesTest` (1) — all EXIT=0 now.

**Proof:** `ModulationLayerProjectRoundTripTest` **EXIT=1 → EXIT=0** (and the round trip's own
assertions on `routes[0].channel` now compare against the engine's id).

## 3 · `mixer.set_pan` on `ch-0` — the expectation was wrong, and the engine fact is measurable

**Ruling: the engine's resolution is right; the test's id was a prediction.** The engine fact, taken
from the running binary (not from code reading):

```
$ ./build/zene --control-socket … (throwaway HOME/config, dummy audio)
mixer.get_state      -> [{"id":"ch-1","index":0,"is_master":true,...}]            # the master
mixer.add_channel    -> {"channel":"ch-7","index":1}
mixer.set_pan ch-0   -> not_found: "no mixer channel ch-0 (the mixer has 2)"
```

The master's id is allocated from the project's counter at construction (`MixerChannel::m_id`),
like every other object's — SPEC-stable-ids.md's one counter per document. `ch-0` names no channel,
and `not_found` is the correct answer for it; the *typed refusal* (`refused`) is for a channel that
exists. **Fix:** the slot addresses the channel `mixer.add_channel` just returned and asserts the
PAN refusal for it (net 0 lines — the file sits at its 505-line baseline). Two engine strings that
told an agent the master *is* `ch-0` (`mixer.remove_channel`'s description; the VCA group refusal,
which now names the channel it resolved) were corrected in the same commit.

Supporting evidence in the tree: `tests/control-pdc-commands.py` and `tests/control-bus-commands.py`
also predicted `ch-0` for the master and both **failed** (`CTEST_PDC_EXIT=8`); both read the id off
`mixer.get_state` now → **EXIT=0**. (SPEC §4.2's own acceptance procedure does exactly this:
`master = fetch(client, 911, "mixer.get_state")…[0].get("id")`.)

## 4 · The four named ctest decisions

### (a) `ControlProjectArchiveTest::junkArgumentsAreTypedRefusals` — actual 1, expected 3

**Decided: the argument-validation layer must reject the junk arg (InvalidArgs), before the project
file is read.** Deciding evidence: `controlRelinkProjectAsset()` itself validates `from` at its top,
*before* it reads the document; this file's own convention is "refuse a missing/empty one before
anything touches the filesystem" (`projectArg`); and — decisive — with `dry_run: true` the same
arguments already answered `invalid_args` (nothing is read), so the error *kind* depended on a
preview flag. **Fix:** one shared `controlRelinkAddressOk(from, to, error)` (one message), called by
the command before it captures the recorded inverse and by the relink routine. The test now pins
both answers (with and without `dry_run`). **Proof: EXIT=1 → EXIT=0.**

### (b) `ControlNoteScaleVerbsTest` — mask `101011010101` (engine) vs `101010110101` (test)

**Decided from the scale data itself: the ENGINE is right; the expectation was transposed.**
LMMS's `ChordTable` scale rows (`src/core/InstrumentFunctions.cpp`, the `isScale()` = size>6
entries): `Major = {0,2,4,5,7,9,11}` → at index 0 = C the mask is `101011010101` (C D E F G A B);
`Lydian = {0,2,4,6,7,9,11}` → `101010110101`, which is what the test asserted (and what the
engine's own `scale.list` description quoted as an example — corrected too). "Major" also exists as
a 3-note chord row, but `getScaleByName` requires `isScale()`, and the slot already asserts 7
degrees — so the scale row is the only reading. **Fix:** both expectations + the engine's
description example. **Proof: EXIT=3 → EXIT=0.**

*Same class, third failure in that file (not in the fix-up list):* `theThreeTransformsMoveNotes…`
called `note.select` with a predicted `"note-0"`; the fixture's first note carries the id
`note.add` returned. Now read from the command's own result.

### (c) `SmfInterchangeTest::theFileIsAWellFormedStandardMidiFile` — last event tick 3840 vs 0

**Decided: the file is well formed; the expectation confused the DELTA with the position.** The
writer emits `0x2F` with a delta of 0 after the last real event
(`src/core/SmfInterchange.cpp`, `trackBody`) — so its *absolute* tick is that event's, 3840 here
(the second tempo/metre pair), which is the track's end as any reader sees it. The test asserted
`tick == 0`, i.e. the delta. **Fix:** the parser records `delta`; the assertion states the format's
rule (`delta == 0`, `tick == ` the previous event's). **Proof: EXIT=1 → EXIT=0.**

### (d) `SmfInterchangeRoundTripTest::theRoundTripComparesTheMapAndNotTheHash` — map equality FALSE

**Decided: the comparator was at fault, not the round trip.** Measured diff (the test now prints it
field by field): `event 1: live {tick 384, has_tempo 0, bpm 140, has_signature 1, 3/4} vs rebuilt
{tick 384, has_tempo 0, bpm 0, has_signature 1, 3/4}` — the only difference is `tempo` on an event
whose `hasTempo` is **false**. `TempoMapEvent::operator==` compared fields of halves the event does
not carry; `validEvent()` does not constrain them (`if (!event.hasTimeSignature) { return true; }`),
the halves are documented independent, and the engine's own published JSON reports an absent half as
0 (`eventState`). So a map rebuilt from the engine's published state could never compare equal.
**Fix:** the comparison reads a half's value only when the half is present. The round trip's
meaningful content already matched (JSON digest and both step functions passed). **Proof:
EXIT=1 → EXIT=0.** (`SmfInterchangeEvent::operator==` has the same shape and no caller compares
it — reported, not changed.)

## 5 · What I could NOT verify, and what remains

* **`gainModel()`/`muteModel()`/`soloModel()`** on `LuaMixerChannel` still return
  `AutomatableModel*`, a class the bridge does not have — measured: *"The class is not registered in
  LuaBridge"*. No test or shipped script calls them; the fix is a view class or removing them from
  the surface + ratchet, which is a design call. **Open.**
* **The same id-prediction class in python tests not fixed here:** `tests/control-socket-integration.py`
  (`{"target": "ch-1"}` in ~10 places; `mixer.set_pan {"channel": "ch-0"}` expecting `refused` at
  ~1870), `tests/control-negative-control.py:280`, `tests/control-shutdown.py:176`,
  `tests/control-mcp-group-coverage.py:190` (excludes `ch-0`). Each needs the id read from the
  surface. **Open, same rule.**
* **`tools/mcp-zene-control/zene_control/commands_snapshot.json`** is DERIVED and still carries the
  old `scale.list` description text; it refreshes at a merge (`snapshot_commands.py --socket …`).
  It must not be hand-edited. **Expected-red in this lane.**
* **CI is not run** (`release/0.3.0` is local-only). The MSVC/macOS-only classes are invisible here.
* **The A16 histogram** was not re-taken: this lane does not move
  `ControlReversibilityTable*` or the release-notes block, and `ReversibilityContractTest`'s
  histogram slot passes against the published figure (its doc path is absolute in this build).
* **The engine's numbering itself is unchanged, on purpose.** A fresh `Engine::init` numbers the
  hidden global automation track 0 and therefore the master 1; `Song::createNewProject()` resets the
  counter after that, so the app's master also stays 1 (`mixer.add_channel` then hands out 7). The
  alternative — re-number so the master is always `ch-0` — is a behavioural change to the id design
  (SPEC-stable-ids: one monotonic counter per document) and would shift ids in every project file;
  it is the parent's call, not a fix-up's. The tests/spec forbid *predicting* ids, and that is what
  was fixed.

## 6 · Gates (this tree, final state)

* `bash tests/all-sources-reproduce.sh` → **`REPRO_EXIT=0`, "REPRODUCES"** — both manifests'
  recipes still reproduce. No new file was added and every edited path is fork-authored, so
  `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are UNCHANGED.
* `bash tests/no-upstream-regression-gate.sh` → **`GATE6_EXIT=0`, PASS** (422 declared paths, 460
  ledger entries; every change to upstream-inherited code since `0114894` declared).
* `bash tests/no-tautology-gate.sh` → **`GATE3_EXIT=0`, PASS**.
* `bash tests/file-length-gate.sh --scope all --check` → `EXIT=1` with **only pre-existing
  regressions** (`include/ControlRegistryGroups.h` 671→684, `include/ControlReversibility.h`
  518→528, `plugins/ClapEffect/ClapHost.cpp` 953→1158, `tests/data/clap-test-plugin/clap-test-instrument.c`,
  `tests/src/plugins/ClapHostTest.cpp`). My first cut of `ScriptDawBindings.cpp` did add one
  (453→522) and it was **compressed back to 500** before commit.
* `bash tests/file-length-gate.sh --check` (fork scope) → `EXIT=1`, two pre-existing regressions,
  both over 500 at `HEAD` too: `tests/control-stable-ids-slice2.py` (549, untouched) and
  `tests/src/core/ControlRegistryTest.cpp` (505 → 505, net 0 in this lane).
* Not run here: coverage (gate 2), complexity (4), mutation (5), duplication (8), unregistered-tests
  (10), evidence (11).

## 7 · Hotspots

* `hotspot: src/core/ScriptDawBindings.cpp` — now exactly 500 lines; the next edit overflows.
* `hotspot: tests/src/core/ReversibilityContractTest.cpp` — 499/500, untouched here.
* `hotspot: src/core/ControlCommandsScale.cpp` — its `scale.list` description is mirrored into the
  derived `commands_snapshot.json`; a merge must regenerate the snapshot.
* `hotspot: the id-prediction class` — any test or doc that spells `ch-0`/`ch-1`/`note-0` as a
  *prediction* will fail on a fresh session; `docs/reports/CMDN-TRANSCRIPT.md` and
  `tools/mcp-zene-control/README.md` still show the pre-slice-1 `ch-0` master in their transcripts
  (historical records, left alone).
* `hotspot: include/TempoMap.h` — its comparator is public API; a lane that relied on comparing
  unused halves will see the change.

## 8 · The single next action

**Merge `030/fixup-engine` into `release/0.3.0` and run `bash tests/run-all-gates.sh` on the merged
tip** (`--scope all` for gate 7, and re-take the A16 histogram/snapshot there as the train does),
because this branch changes engine behaviour in three places another lane's tests could assert:
the Lua binding's reported channel ids, the tempo-map comparator, and `project.relink`'s error order
for blank addresses.
