IMPLEMENTED

# Cluster Q050-014 — Document supported plug-in formats and caveats

Docs-only unit (members `R-instruments-11`, `R-instruments-26`, `R-ui-19`, area L10). The current
docs blurred native format support with unverified bridge/Kontakt compatibility, and both `README.md`
and `docs/KNOWN-LIMITATIONS.md` still said the plugin scan cache and its per-file records were
socket-only / "not a GUI" although `CommandLists::pluginModules()` ships the "Plug-in Modules"
interface. Added a user-facing formats-vs-tested section (with the Kontakt case), named the GUI
location, and added a hermetic docs-truth gate.

## Acceptance table

| Criterion | Command actually run | Result / EXIT |
| --- | --- | --- |
| Update the Linux plug-in and Kontakt sections to separate format loading from tested third-party compatibility | `python3 tests/plugin-formats-docs-gate.py` (worktree root) | 23 checks PASS, `RESULT: PASS` — asserts `docs/KNOWN-LIMITATIONS.md` has the "Plug-in formats and libraries on Linux" section, "Third-party compatibility is unverified", "no Kontakt runtime and has verified no Kontakt library", and `README.md` has "formats it is built with" / "no third-party plug-in of any kind has"; **EXIT=0** |
| Name the Plug-in Modules list as the GUI location for per-file scan status and quarantine controls | `ctest -R '^PluginFormatsDocsGate$' --output-on-failure` (from `build/tests`) | `1/1 Test #584: PluginFormatsDocsGate ... Passed`; the gate asserts both docs and the manual name **Plug-in Modules**, that `docs/KNOWN-LIMITATIONS.md` says "per-file scan status and quarantine", and that the list is defined in `src/gui/CommandLists.cpp`, registered in `src/gui/shell/ShellCommandMenus.cpp` and backed by commands in `tools/mcp-zene-control/zene_control/commands_snapshot.json`; **EXIT=0** |
| Same, direct script run | `python3 tests/plugin-formats-docs-gate.py` | all checks PASS; **EXIT=0** |
| Gate is not tautological (red proof, stale claim) | appended "the cache's per-file records are still only on the socket" to `docs/KNOWN-LIMITATIONS.md`, ran the gate | `FAIL docs/KNOWN-LIMITATIONS.md: no stale 'per-file records still only on the socket' claim`, `RESULT: FAIL`; **EXIT=1**; restored file → **EXIT=0** |
| Gate is not tautological (red proof, unnamed location) | replaced `Plug-in Modules` with a placeholder in `README.md`, ran the gate | `FAIL README.md: names the Plug-in Modules list`, `RESULT: FAIL`; **EXIT=1**; restored file → **EXIT=0** |

The red-proof logs refused a claim that would otherwise slip through: the gate fails on a reverted
docs claim, not only on a missing one.

## Files changed

- `README.md` — new formats-vs-tested-compatibility paragraph naming the Kontakt case
  (`README.md:78`); the stale "not a GUI" scan-cache bullet corrected to name **Project tools ▸
  Plug-in Modules...** (`README.md:101`).
- `docs/KNOWN-LIMITATIONS.md` — new "Plug-in formats and libraries on Linux — what loads, and what we
  have tested" section (`:144`); "No plugin-scanning interface worth the name" corrected in place
  (`:442`); "the cache's per-file records are still only on the socket" corrected (`:1146`).
- `data/manual/index.html` — names the Plug-in Modules list (`:117`).
- `tests/CMakeLists.txt` — registers `PluginFormatsDocsGate` (`:5664`).
- `tests/plugin-formats-docs-gate.py` — new hermetic docs-truth gate.
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

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-014.r1/logs/`.
