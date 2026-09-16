# LANE-STATE — 030/vst3-instrument, wave 9 (feature row 78, board task #668)

**Location:** `projects/lmms-fl-research/lmms/zene-030/wvst3` (the lane's own worktree; the
`…/lmms/zene-030/wvst3` path is the one on disk — an earlier brief named a path that does not exist).
**Branch:** `030/vst3-instrument` · **Base:** `a36e8b0d4` (== `release/0.3.0` at the time of writing) ·
**Date:** 2026-09-16 · Single-agent lane, one build directory (`build/`), `-j2`.

**Commits**

| SHA | what |
| --- | --- |
| `54ac6b710` | `feat(vst3): the VST3 half of the device catalogue - plugin.list and plugin.load reach a third-party VST3 class (row 78, #668)` |
| `0e4abefbf` | `docs(030): row 78 - the VST3 instrument is on the agent surface, and its editor is not` |

---

## 0. The headline: the engine half of row 78 was already at the base commit

Card #668 asks for three engine things, a proof, and an explicit answer on the editor. Reading the tree
before writing anything (rule 3) found the engine and the proof **already merged** at `a36e8b0d4`
(landed by `post-alpha/instrument-hosting-impl`, commits `7566607f2` and `5c69b514f`, documented in
`docs/VST3-INSTRUMENT-HOSTING.md`). Verified in this tree, not taken from that document:

| The card's ask | Where it is, at `a36e8b0d4` |
| --- | --- |
| event-bus enumeration | `plugins/Vst3Effect/Vst3MidiEvent.cpp:37-61` — `getBusCount(kEvent, kInput)` then `getBusInfo()` per index; the first bus with `BusInfo::kDefaultActive` wins (index 0 if the plug-in activates none), and `activateBus(kEvent, kInput, i, i == chosen)` runs for **every** event input so the others are deactivated. Called once, for instruments only, at `Vst3Host.cpp:384` |
| `ProcessData::inputEvents` | wired in `prepare()` at `Vst3Host.cpp:664` (`d.midiEnabled ? &d.inputEvents : nullptr` — an effect keeps `nullptr`), filled in `process()` at `Vst3Host.cpp:763-767` (drain once, in sample order) and sliced per chunk at `Vst3Host.cpp:808-826` |
| lifecycle | `setActive(true)` in `prepare()` at `Vst3Host.cpp:606` (rolled back at `:618` if `setProcessing()` fails), `setProcessing(false)` + `setActive(false)` in `release()` at `Vst3Host.cpp:686-687`, with the buffers freed and the MIDI queue reset in the same call |
| load path onto an instrument track | `plugins/Vst3Instrument/` (module + `IsSingleStreamed | IsMidiBased` + generated parameter view), keyed `(file, class)` by `plugins/Vst3Effect/Vst3SubPluginFeatures.cpp` |
| the proof | `tests/data/vst3-test-instrument/` (MIT fixture from SDK `v3.8.1_build_84`, laid out as a real `.vst3` bundle) and the ctest trio, registered behind `WANT_VST3_TEST_INSTRUMENT` at `tests/CMakeLists.txt:1637-1745` |

So this lane's real gap was the **acceptance contract's second item**, and it was genuinely missing.

## 1. What was missing, and what this lane built

`CHARTER §3.1` requires a feature to be drivable through the socket. The VST3 hosts could be reached from
the interface but **not from the control surface**: `plugin.list` (and therefore every `dev-<n>` id) covered
built-in, LADSPA and LV2 only — `grep -rn "vst3" src/core/ControlDevice*.cpp src/core/ControlCommandsPlugin.cpp`
returned nothing at `a36e8b0d4`, and `plugin.list`'s `format` enum was `builtin|ladspa|lv2` — so no agent
could name a VST3 class and `plugin.load` could not load one onto a track.

Added, mirroring the LADSPA/LV2 halves exactly (copy an existing group, never invent one):

| File | Change |
| --- | --- |
| `src/core/ControlDeviceVst3.cpp` **(new, 212 lines)** | `controlVst3DeviceEntries()` — one catalogue entry per class the VST3 hosts' **own** discovery enumerates (`Plugin::Descriptor::SubPluginFeatures::listSubPluginKeys()`, the call the instrument browser makes; not a second scanner) — and `controlVst3DeviceModule()` — the `("vst3instrument"|"vst3effect", (file, class))` key, with typed refusals for a missing host, an entry with no class, and a bundle that is gone |
| `include/ControlDeviceSupport.h` | `ControlDeviceEntry::className`; the two declarations; the format comment |
| `src/core/ControlDeviceCatalogue.cpp` | one join call after the LV2 block, and `file`/`class` in the JSON for `format == "vst3"` |
| `src/core/ControlDeviceHosted.cpp` | one dispatch branch, `format == "vst3"` |
| `src/core/ControlCommandsPlugin.cpp` | `plugin.list`'s `format` filter gains `vst3`; the description names the order (built-in, LADSPA, LV2, VST3) |
| `src/core/CMakeLists.txt`, `tests/CMakeLists.txt` | the new source; the fixture-backed compile definition for the catalogue test (both declared in `tests/upstream-modifications.txt`) |
| `tests/src/core/ControlDeviceCatalogueTest.cpp` | the closed-set rule gains `vst3`; **a new case** that points the product's VST3 search directory at the fixture and drives `plugin.list format=vst3` → `plugin.load` onto an instrument track through the registry |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | `src/core/ControlDeviceVst3.cpp` |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `docs/KNOWN-LIMITATIONS.md` | row 78's section, and the socket surface named in the hosting bullet |

**No new command ids and no new A16 row**: the two verbs are `plugin.list` and `plugin.load`, which already
have their rows (`src/core/ControlReversibilityTableSnapshot.cpp:84`) and their schemas. The `dev-<n>` block
for VST3 is **appended after** LADSPA and LV2, so no id a client already holds is renumbered (the property
`ControlDeviceCatalogueTest::builtinBlockIsNotRenumberedByHostedFormats` guards).

## 2. The IPlugView / editor question — answered, not dodged

**Not built. Recorded as out of scope, in the tree, with the reason.** The card allows exactly this branch of
the question and the reason is written down in three places:

1. `plugins/Vst3Instrument/Vst3InstrumentView.h` — the class comment states that this is *not* the plug-in's
   own editor and that `IPlugView` is not implemented anywhere in the tree.
2. `docs/KNOWN-LIMITATIONS.md:129-149` — "**No instrument editor.** You can load a VST3 instrument and play
   it, but the plugin's own GUI **does not open**", with the measurement behind the claim
   (`grep -rn IPlugView src/ include/ plugins/Vst3Effect/ plugins/ClapEffect/` → 0 hits).
3. `docs/VST3-INSTRUMENT-HOSTING.md` §7 — the design that *would* be needed (`createView(kEditorView)`,
   `isPlatformTypeSupported("X11EmbedWindowID")`, `attached()`/`setFrame()` with an `IPlugFrame`,
   an `IRunLoop` on Qt's event loop, `detached()` on close) and the reason it is deferred: a 300–600 line
   foreign-window implementation with **no in-tree analogue** — the only prior art in the tree is the VST2
   embed path (`ConfigManager::vstEmbedMethod()`, `src/gui/SubWindow.cpp`), and the `I…` plumbing and the run
   loop still have to be written for X11 *and* Wayland. Nothing on the audio, MIDI, load or save path calls
   `createView`/`IPlugView`, so a GUI-less instrument is fully usable.

The release notes now carry the same sentence in the new row-78 section, so the release does not read as if an
editor were there.

## 3. Proof — commands and exit codes (every code unpiped)

Build directory: `build/` in this worktree, configured with the **exact `linux-x86_64` CI flags**
(`tools/local-ci.sh`'s headless-equivalent route, plus `-DWANT_QT6=ON`, this box's documented deviation).

```
bash tools/local-ci.sh --configure-only --build-dir build      ->   configure EXIT=0
  provision: VST3 SDK v3.8.1_build_84 (3cdf9ca5d, MIT), CLAP 1.2.10 (195b42a00, MIT) - nothing to fetch
```

```
cmake --build build -j 2 --target Vst3InstrumentFixtureProbe Vst3InstrumentTest \
      Vst3InstrumentIntegrationTest ControlDeviceCatalogueTest     ->   BUILD3_EXIT=0
```

```
cd build/tests && ctest -R '^Vst3Instrument' --output-on-failure   ->   CTEST_TRIO_EXIT=0
  1/3 Test #152: Vst3InstrumentFixtureProbe .......   Passed    0.00 sec
  2/3 Test #153: Vst3InstrumentTest ...............   Passed    0.01 sec
  3/3 Test #154: Vst3InstrumentIntegrationTest ....   Passed    2.38 sec
  100% tests passed, 0 tests failed out of 3
```

Not a skip in disguise — run directly, with their own totals:

```
build/tests/Vst3InstrumentFixtureProbe   -> PROBE_EXIT=0   RESULT: PASS (0 failed check(s))
    [PASS] the block is NON-SILENT before the note-off - MIDI really drives audio
    [PASS] the block is EXACTLY silent from the note-off sampleOffset onwards
    level=0.500000 sample[0]=0.500000 sample[255]=0.500000 sample[256]=0.000000 sample[511]=0.000000
build/tests/Vst3InstrumentTest           -> VST3INTEG_EXIT=0   Totals: 11 passed, 0 failed, 0 skipped
build/tests/Vst3InstrumentIntegrationTest-> VST3INTEG_EXIT=0   Totals: 9 passed, 0 failed, 0 skipped
```

The **new socket half**, proved on this box where no third-party VST3 instrument can be installed:

```
build/tests/ControlDeviceCatalogueTest   -> CATALOGUE_EXIT=0   Totals: 8 passed, 0 failed, 1 skipped
  QINFO : vst3ClassesListAndLoadThroughTheSurface() plugin.list format=vst3: dev-1 ->
          plugin.load on trk-3 -> .../build/tests/data/vst3-test-instrument/vst3-test-instrument.vst3,
          1 parameter(s)
  PASS   : ControlDeviceCatalogueTest::vst3ClassesListAndLoadThroughTheSurface()
  SKIP   : lv2EntriesCarryTheirUri()  (this build has no lv2effect module - pre-existing, unrelated)
```

The neighbours, re-run because the shared catalogue/command files changed:

```
ctest -R '^(Vst3HostTest|Vst3BusMapTest|Vst3ChunkProbeTest|ControlRegistryTest|ControlDeviceCatalogueTest)$'
   -> CTEST_NEIGHBOURS_EXIT=8 : 4 of 5 passed
      Vst3BusMapTest Passed 0.01s | Vst3HostTest Passed 0.01s | Vst3ChunkProbeTest Passed 0.01s
      ControlRegistryTest ***Failed - see §4 item 1
```

Gates that are cheap and touch this change (all from the tree's own scripts, exit codes unpiped):

```
bash tests/fork-sources-gate.sh          -> GATE_FORK_EXIT=0
   PASS: every tracked source in scope is registered (625 fork-NEW, 1103 inherited, 40 tooling)
bash tests/no-upstream-regression-gate.sh-> GATE6_EXIT=0
   PASS: every change to upstream-inherited code since 01148947ea4d is declared
         (421 changed path(s) declared; the ledger holds 457 entries)
bash tests/no-tautology-gate.sh          -> GATE_TAUT_EXIT=0
bash tests/duplication-gate.sh           -> GATE_DUP_EXIT=0   (0.51% of a 5% budget)
bash tests/file-length-gate.sh           -> GATE_FILELEN_EXIT=1   FAIL, none of it mine - §4 item 2
ctest -R '^ControlCommandsSnapshot$'     -> CTEST_SNAPSHOT_EXIT=8 - expected, §4 item 3
```

## 4. Red, unexplained or open

1. **`ControlRegistryTest::mixerSetPanRefusesTyped` fails** (`errorKind 1 = NotFound`, expected `Refused`)
   in this tree at this tip: `mixer.set_pan` on `ch-0` does not resolve. **Not attributable to this change** —
   nothing here touches mixer channel resolution (`resolveControlTarget` is in `ControlDeviceSupport.cpp`,
   untouched), and in that test's process the plug-in factory has no VST3 host descriptor at all, so both new
   functions are no-ops there. **Not measured at the base commit**: the lane did not re-build the base to
   settle it. The merge tip must re-check it; the failure is in the "pre-existing red, name it and move on"
   class until then.
2. **Gate 7 (file length) is red and none of the reds are this lane's**: `include/ControlRegistryGroups.h`
   (671), `include/ControlReversibility.h` (518), `include/ControlRegistry.h` (509),
   `src/core/ControlCommandsNotes.cpp` (512), `ControlCommandsProject.cpp` (510),
   `ControlCommandsWarpEdit.cpp` (510), `ControlReversibilityTablePassive.cpp` (518),
   `tests/control-stable-ids-slice2.py` (549), `tests/src/core/ControlRegistryTest.cpp` (505),
   and `plugins/Vst3Effect/Vst3Host.cpp` grew 800 → 891 (a merge after that file's own record). This lane's
   files are not in the list; `ControlCommandsPlugin.cpp` is still 478 of 500 and `ControlDeviceVst3.cpp`
   is new at 212. (**`GATE_FILELEN_EXIT=1`**.)
   **Gate 4 (complexity) is red for the same reason and also none of it is this lane's**
   (`GATE_COMPLEXITY_EXIT=1`): the "new function over target" list is the merged 0.3.0 features
   (`DawProjectWrite.cpp`, `ImportDetectionDsp.cpp`, `ControlCommandsDetectApply.cpp`,
   `ControlCommandsNoteRandom.cpp`, the golden-audio and stem Python harnesses, …) plus
   `Vst3Effect/Vst3Host.cpp`'s `HostedPlugin::Impl::runChunk` (CCN 14) and `prepare` (CCN 14) from the
   CODE-4/instrument work that was already in the tree. Neither `ControlDeviceVst3.cpp` nor
   `controlVst3DeviceEntries()`/`controlVst3DeviceModule()` appears anywhere in either gate's report.
3. **`ControlCommandsSnapshot` is red, for the derived-file reason the brief predicts** — and now in two
   parts: the 8 `wasm.*` ids this build does not register (pre-existing; `WANT_WASM` is off in this
   configuration), and `plugin.list`'s `format` enum, which the committed snapshot still shows as
   `["builtin","ladspa","lv2"]` while the live registry offers `vst3` as well. The file is
   **derived — never hand-edited**: regenerate it at the merge with
   `python3 tools/mcp-zene-control/snapshot_commands.py --socket <sock>` against a live instance.
4. **The VST3 catalogue block is empty in an ordinary build.** It appears only when the build has the host
   *and* the machine has VST3 bundles in the product's VST3 directory (`ConfigManager::vstDir()`), which is
   also why no existing catalogue count moves on this box. Nothing was measured with a real third-party
   instrument, because none can be installed here (the card's own premise).
5. **Not run in this lane:** the full ctest suite (the trio + the four neighbours + the catalogue test are
   what this change touches; a full suite needs the whole tree built), `run-all-gates.sh` (gates 2/5 need
   `--with-coverage`/mutation), the render-proof script, and the socket-transcript harnesses. The merge tip
   re-runs them, per the brief.
6. **`Vst3Effect/Vst3Host.cpp` is at 891 lines and no baseline entry** — the file-length gate reports it as
   growth against a 800-line record. Anything that adds lines to it (row 669's CLAP instrument work, row 670)
   will add to that red; that is a merge-time re-anchor decision, not a lane's.

## 5. The next single action

Rebuild `ControlDeviceCatalogueTest` on the merge tip after `git merge 030/vst3-instrument` and re-run
`cd build/tests && ctest -R '^(Vst3Instrument|ControlDeviceCatalogueTest)' --output-on-failure`, then
regenerate `tools/mcp-zene-control/zene_control/commands_snapshot.json` from a live instance — the only file
this lane knowingly left stale, and the one the merge train owns.
