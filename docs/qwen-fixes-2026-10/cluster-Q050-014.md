IMPLEMENTED

# Cluster Q050-014 — Document supported plug-in formats and caveats

Docs-only unit (members `R-instruments-11`, `R-instruments-26`, `R-ui-19`, area L10). The current
docs blurred native format support with unverified bridge/Kontakt compatibility, and both `README.md`
and `docs/KNOWN-LIMITATIONS.md` still said the plugin scan cache and its per-file records were
socket-only / "not a GUI" although `CommandLists::pluginModules()` ships the "Plug-in Modules"
interface. Added a user-facing formats-vs-tested section (with the Kontakt case), named the GUI
location, and added a hermetic docs-truth gate.

Round 2 (Luna REWORK r1): the README overstated VST2 availability as "on Linux and the other
platforms alike", although `CMakeLists.txt` disables VST hosting on Apple builds. The README format
list is qualified as the **Linux** one and `docs/KNOWN-LIMITATIONS.md` says availability varies by
platform.

Round 3 (Luna REWORK r2): the gate checked format names **document-wide** and only that mapped loader
paths **exist**, so an omission from the Linux list could escape on any other mention. Now every
format-name claim is scoped to the README plug-in-formats paragraph and the KNOWN-LIMITATIONS Linux
section (`README.md` line 80, `docs/KNOWN-LIMITATIONS.md` line 144), each format is verified against
the **registration symbol** in its mapped loader source, the README's inherited-core inventory is
qualified as the Linux build's (line 41), and "the platform these release builds target" is now "one
of the platforms these release builds target". A `--self-test` red proof (ctest
`PluginFormatsDocsGateSelfTest`) drops `LADSPA` from a scoped list while its other mentions stay
intact and asserts the gate exits 1 naming the omission.

## Acceptance table

| Criterion | Command actually run | Result / EXIT |
| --- | --- | --- |
| Update the Linux plug-in and Kontakt sections to separate format loading from tested third-party compatibility | `python3 tests/plugin-formats-docs-gate.py` (worktree root) | 53 checks PASS, `RESULT: PASS` — asserts `docs/KNOWN-LIMITATIONS.md` has the "Plug-in formats and libraries on Linux" section with "Third-party compatibility is unverified" and "no Kontakt runtime and has verified no Kontakt library" **inside it**, and `README.md` has "formats it is built with" / "no third-party plug-in of any kind has" **inside the plug-in-formats paragraph**; both scoped regions advertise VST3 / CLAP / VST2 / LV2 / LADSPA / SoundFont2 / GUS, scope the list to Linux and say availability varies by platform; **EXIT=0** |
| Format claims are scoped, not document-wide | `python3 tests/plugin-formats-docs-gate.py --self-test` and `python3 tests/plugin-formats-docs-gate.py` | the green run requires each format in the bounded README paragraph **and** the bounded KNOWN-LIMITATIONS section; the self-test removes `LADSPA` from each bounded region (its other mentions intact) and the gate exits **1** naming `advertises LADSPA`; self-test `RESULT: PASS`, **EXIT=0** |
| Mapped loader artifacts register the format, not merely exist | `python3 tests/plugin-formats-docs-gate.py` | for each format the gate reads the loader source and requires its `Plugin::Descriptor` symbol (`vst3effect_plugin_descriptor`, `clapeffect_plugin_descriptor`, `vsteffect_plugin_descriptor`, `lv2effect_plugin_descriptor`, `ladspaeffect_plugin_descriptor`, `sf2player_plugin_descriptor`, `patman_plugin_descriptor`); 7 such checks PASS; **EXIT=0** |
| The README does not imply VST2 hosting on macOS, and Linux is one target among several | same `ctest` | gate asserts README says "a macOS build has no VST2/Vestige path" in the scoped paragraph, rejects "on Linux and the other platforms alike", and finds `SET(WANT_VST OFF)` inside `IF(LMMS_BUILD_APPLE)` in `CMakeLists.txt`; README now reads "one of the platforms these release builds target"; **EXIT=0** |
| Name the Plug-in Modules list as the GUI location for per-file scan status and quarantine controls | `ctest -R '^PluginFormatsDocsGate$' --output-on-failure` (from `build/tests`) | gate asserts both docs and the manual name **Plug-in Modules**, that `docs/KNOWN-LIMITATIONS.md` says "per-file scan status and quarantine", and that the list is defined in `src/gui/CommandLists.cpp`, registered in `src/gui/shell/ShellCommandMenus.cpp` and backed by commands in `tools/mcp-zene-control/zene_control/commands_snapshot.json`; **EXIT=0** |
| Red proof, scoped-list removal, direct run | `python3 tests/plugin-formats-docs-gate.py --self-test` | 2 cases PASS (`README.md` and `docs/KNOWN-LIMITATIONS.md`); each runs the gate on a mirrored throwaway root and asserts exit 1 + `advertises LADSPA`; `RESULT: PASS`, **EXIT=0** |
| Red proof, scoped-list removal, through ctest | `ctest -R '^PluginFormatsDocsGateSelfTest$' --output-on-failure` (from `build/tests`) | `1/1 Test #...: PluginFormatsDocsGateSelfTest ... Passed`; ctest **EXIT=0** |
| Green gate through ctest | `ctest -R '^PluginFormatsDocsGate$' --output-on-failure` (from `build/tests`) | `1/1 Test #...: PluginFormatsDocsGate ... Passed`; ctest **EXIT=0** |

The red proof refuses a claim that would otherwise slip through: when a format is deleted from the
scoped list but its other document mentions remain, the gate fails (a document-wide substring check
would have passed).

## Files changed

- `README.md` — formats-vs-tested-compatibility paragraph, its format list qualified as the **Linux**
  list and stated to vary by platform (a macOS build has no VST2/Vestige path), Linux named as one of
  the platforms these release builds target (`README.md:80`); the earlier inherited-core inventory
  qualified as the Linux build's inherited list (`README.md:41`); the stale "not a GUI" scan-cache
  bullet corrected to name **Project tools ▸ Plug-in Modules...** (`README.md:105`).
- `docs/KNOWN-LIMITATIONS.md` — "Plug-in formats and libraries on Linux — what loads, and what we have
  tested" section (`:144`), its format list scoped to the Linux build with availability varying by
  platform; "No plugin-scanning interface worth the name" corrected in place (`:444`); "the cache's
  per-file records are still only on the socket" corrected (`:1147`).
- `data/manual/index.html` — names the Plug-in Modules list (`:117`).
- `tests/CMakeLists.txt` — registers `PluginFormatsDocsGate` and `PluginFormatsDocsGateSelfTest`
  (`:5667`, `:5669`).
- `tests/plugin-formats-docs-gate.py` — hermetic docs-truth gate; round 3 scopes every format claim to
  the bounded README/KNOWN-LIMITATIONS regions, verifies each loader source's registration symbol, and
  adds the `--self-test` red proof (53 checks).
- `tests/fork-sources.txt` — registers the new gate script (fork-NEW, `:2153`).
- `docs/050-features/Q050-014.md`, `docs/qwen-fixes-2026-10/cluster-Q050-014.md` — these reports.

New commands: none. Reversibility: unchanged. Project format: unchanged. Canary figure, snapshot and
A16 histogram: unchanged (`python3 tests/doc-figures-gate.py` exits 0).

## Gates

| Gate | EXIT |
| --- | ---: |
| `bash tests/complexity-gate.sh --check` | 1 (pre-existing train-5 red; identical `REGRESSION` set to `../zene-cachyos`, no file of mine listed) |
| `bash tests/file-length-gate.sh --check` | 1 (same: identical set to `../zene-cachyos`, no file of mine listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `ctest -R '^PluginFormatsDocsGate$'` (from `build/tests`) | 0 |
| `ctest -R '^PluginFormatsDocsGateSelfTest$'` (from `build/tests`) | 0 |

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-014.r3/logs/`.
