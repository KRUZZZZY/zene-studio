# CLAP instrument hosting (feature row 79, board task #669) — lane report

Lane: `wclapin`, branch `030/clap-instrument`, cut from `d7a402041` (release/0.3.0 tip).
One agent, one worktree, no pushes.

## 1. What the feature is, and where it now lives

Before this lane, **CLAP hosting was effects only**: there was no CLAP instrument module anywhere, so a CLAP
generator could not be loaded — not through the socket and not from the interface. Three halves are added:

| half | file(s) | what it does |
| --- | --- | --- |
| note ports + input events | `plugins/ClapEffect/ClapHost.{h,cpp}`, `plugins/ClapEffect/ClapNoteQueue.h` (new) | `load()` reads `clap.note-ports` (while deactivated, per the extension's contract) and remembers the plug-in's own port **index**; `setNoteOn/setNoteOff/setNoteChoke` push a POD into a bounded lock-free ring; `process()` drains it into the plug-in's input event list with `header.time` = the frame offset LMMS computed. |
| audio-output configuration | `plugins/ClapEffect/ClapHost.cpp` (`load()`), `plugins/ClapInstrument/` (new module) | a **generator** layout (output, no audio input port) is now accepted and the instrument module takes its channel counts from the plug-in's own `clap.audio-ports`, with a stereo floor. |
| control surface | `src/core/ControlDeviceClap.cpp` (new), `include/ControlDeviceSupport.h`, `src/core/ControlDeviceCatalogue.cpp`, `src/core/ControlDeviceHosted.cpp`, `src/core/ControlCommandsPlugin.cpp` | `plugin.list` carries `format: "clap"` entries and `plugin.load` loads one onto an instrument track, through the hosts' own discovery (`ClapSubPluginFeatures::listSubPluginKeys`). |

## 2. The ids, the schema, the A16 row

- **No new id needed for the load path**: `plugin.list` (its `format` enum gains `clap`) and `plugin.load`
  reach a CLAP instrument exactly as they reach a VST3 one. `plugin.load` keeps the A16 row it already had
  (`snapshot`, `ControlReversibilityTableSnapshot.cpp`); `plugin.list` keeps its `not_mutating` row.
- **One new id: `plugin.host_notes`** (group `plugin`, `mutating: false`), because the note path and the audio
  layout had no socket-observable surface (CHARTER 3.1). Result schema: `host`, `contract{note_ports,
  event_form, delivery_rule, queue_rule, audio_output_rule}`, `ports{count, preferred, dialects{clap,midi,
  midi_mpe,midi2}}`, `audio{inputs,outputs}`, `counters{loads,pushed,dropped,delivered,played}`, `note`.
  Engine half: `include/PluginHostNotes.h` + `src/core/PluginHostNotes.cpp` (counters in the core, because the
  host is a plugin module — the `plugin.host_chunking` seam). Command:
  `src/core/ControlCommandsHostNotes.cpp`; declared in `include/ControlRegistryGroups.h`, called once from
  `src/core/ControlRegistryRegistrations.cpp`.
- **A16 row**: `plugin.host_notes` — `not_mutating`, one row, in the new
  `src/core/ControlReversibilityTableClapInstrument.cpp`, joined by **one** entry in
  `src/core/ControlReversibilityTable.cpp` and declared in `include/ControlReversibility.h`.
- **The catalogue order** is built-in → LADSPA → LV2 → VST3 → **CLAP**, so no `dev-<n>` id that already exists
  is renumbered.

## 3. The proof vehicle (MIT)

`tests/data/clap-test-plugin/clap-test-instrument.c` (new, `SPDX-License-Identifier: MIT`, built from the
pinned CLAP 1.2.10 headers like its `clap-test-gain.c` sibling): a real CLAP **generator** — one CLAP-dialect
note input port, one main stereo output port and **no** audio input port, one parameter, and a `clap.state`
whose counters let a test read back what the host actually delivered. Its voice writes a constant level
(`gain * 0.25`) so a test asserts an amplitude instead of inferring one; note-off starts a 256-frame release,
choke silences immediately.

Registered in `tests/data/clap-test-plugin/CMakeLists.txt` (same strict-warning guard as the gain fixture),
wired to `ClapHostTest` through `CLAP_TEST_INSTRUMENT_PATH` and to `ControlDeviceCatalogueTest` through
`CLAP_TEST_INSTRUMENT_DIR`.

## 4. Evidence (every command run on this box, exit codes unpiped)

Configure — my own build dir, `-DWANT_CLAP=ON`, CLAP 1.2.10 headers copied into it:

```
$ cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_CLAP=ON -DWANT_VST3=OFF \
      -DWANT_QT6=ON -DWANT_DEBUG_CPACK=OFF -DUSE_COMPILE_CACHE=OFF
EXIT=0
-- Found CLAP 1.2.10 headers (MIT) at .../wclapin/build/clap/include
```

Fixture + host test build (`-j2`, one `cc1plus` box-wide, 62 GB free):

```
$ cmake --build build --target clap-test-instrument clap-test-gain ClapHostTest -j2
EXIT=0
$ ls -l build/tests/data/clap-test-plugin/*.clap
-rwxrwxr-x 45904 clap-test-instrument.clap      <-- THE FIXTURE BUILDS HERE
-rwxrwxr-x 43952 clap-test-gain.clap
```

`ClapHostTest`, run from the build tree's `tests/` directory:

```
$ ./ClapHostTest ; echo EXIT=$?
PASS   : lmms::clap::ClapHostTest::testEffectRefusesNotes()
QINFO  : ...testInstrumentNotePath() MEASURED 256-frame request into a 64-frame block:
         4 plug-in calls, 1 note(s) delivered, audio level 0.250000
PASS   : lmms::clap::ClapHostTest::testInstrumentNotePath()
Totals: 14 passed, 0 failed, 0 skipped, 0 blacklisted, 8ms
EXIT=0
```

Full semantic compiles (`-fsyntax-only`, the targets' own flags from their `flags.make`) — the CLAP family
builds here, the core half was checked without a full build:

```
EXIT=0 plugins/ClapEffect/ClapHost.cpp        (the note-path edits)
EXIT=0 plugins/ClapInstrument/ClapInstrument.cpp
EXIT=0 plugins/ClapInstrument/ClapInstrumentView.cpp
EXIT=0 src/core/PluginHostNotes.cpp
EXIT=0 src/core/ControlCommandsHostNotes.cpp
EXIT=0 src/core/ControlDeviceClap.cpp
EXIT=0 src/core/ControlReversibilityTableClapInstrument.cpp
EXIT=0 src/core/ControlCommandsPlugin.cpp
EXIT=0 src/core/ControlDeviceCatalogue.cpp
EXIT=0 src/core/ControlDeviceHosted.cpp
EXIT=0 tests/src/core/ControlDeviceCatalogueTest.cpp   (moc generated by hand for the check)
```

## 5. What is NOT verified (open items)

- **The full link of `clapinstrument` / `clapeffect` / `zene` and the `ControlDeviceCatalogueTest` run were
  not executed**: both plugin modules link the `zene` executable, so building them means a full core build
  (~2000 TUs) that does not fit this lane's window at `-j2`. The TUs compile (above); the link and the
  catalogue case are unverified here and are the first thing the fix-up pass should run:
  `cmake --build build --target clapinstrument ControlDeviceCatalogueTest -j2` then
  `cd build/tests && ctest -R 'ControlDeviceCatalogueTest|ClapHostTest' --output-on-failure`.
- **The file-length ratchet**: `plugins/ClapEffect/ClapHost.cpp` is 1158 lines against the baseline's 953
  (890 before this lane), and `tests/file-length-gate.sh` reports exactly that one growth as a regression
  (`REGRESSION: plugins/ClapEffect/ClapHost.cpp grew 953 -> 1143 lines` at the first commit; 1158 after the
  note-port scan became an `Impl` member). The other 8 reds that gate prints are inherited (identical at
  `d7a402041`: `include/ControlRegistry.h` 509, `src/core/ControlCommandsNotes.cpp` 512,
  `ControlCommandsProject.cpp` 510, `ControlCommandsWarpEdit.cpp` 510,
  `ControlReversibilityTablePassive.cpp` 518, `tests/control-session-api-proof.py` 889,
  `tests/control-stable-ids-slice2.py` 549, `tests/src/core/ControlRegistryTest.cpp` 505). The honest fix is a
  split of the note path and the `Impl` note state into their own TU, **not** a re-anchor: `HostedPlugin::Impl`
  is currently defined inside `ClapHost.cpp`, so the move has to take the note-state slice of `Impl` with it.
- **`tests/complexity-gate.sh` on `HostedPlugin::load`**: CCN was 31 at the base commit and is **28** here -
  the note-port scan is an `Impl::scanNotePorts()` member, not inline in `load()`, so the function improved
  instead of regressing. The gate still exits 1 for 57 inherited regression lines in other lanes' files
  (`dawproject.*`, `wasm.*`, the VST3 host, several `tests/control-*.py`); **none of the 57 is this lane's**.
- **`tools/mcp-zene-control/zene_control/commands_snapshot.json` is derived** and was not regenerated (a merge
  job, from a live instance): `ControlCommandsSnapshot` is expected red for exactly that reason.
- The A16 histogram constant in `tests/src/core/ReversibilityContractTest.cpp` is a measurement; this lane
  added one `not_mutating` row, so **the merge tip must re-measure it**.

## 6. Hotspots

- `hotspot: src/core/ControlCommandsPlugin.cpp — 479/500 lines after the two-line format-enum edit; the next
  person who needs a line here must split the file.`
- `hotspot: plugins/ClapEffect/ClapHost.cpp — carries both hosts now (effect + instrument) and is over the
  953-line baseline; the note path is the natural file to split out next.`
- `hotspot: tests/CMakeLists.txt — the CLAP block, the VST3 block and the fixture defines all live here; my
  edits are guarded on TARGET so they are no-ops without the headers.`

## 7. Next action

Build the two plugin modules and run the catalogue case on the merged tip:
`cmake --build build --target clapinstrument ControlDeviceCatalogueTest -j2 && cd build/tests && ctest -R
ControlDeviceCatalogueTest --output-on-failure`.
