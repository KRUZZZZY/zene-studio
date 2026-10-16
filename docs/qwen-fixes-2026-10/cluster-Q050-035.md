IMPLEMENTED

# Cluster Q050-035 — Correct stem separation availability docs

Docs unit (R-platform-26, area L05) that grew, on review, into the "both surfaces, one command, one
undo step" work the feature bar requires. The docs claimed offline HTDemucs stem separation was
"compiled out of these builds" and reported `WANT_STEM_SPLIT='OFF'`, while owner decision 14 had made the
option default ON and the release-honesty manifest require ON. Corrected the current-facing docs to say
the feature is built in, local and on by default, added a hermetic docs-truth gate, and made the GUI's
"Split to stems" action drive the same registered `stem.*` commands the agent surface uses, with the four
tracks as ONE undo step.

Round-2 rework (`Q050-035.r1.code.md`): rewrote `docs/FEATURE-LIST-0.3.0.md` row 26, extended the gate,
fixed four active opt-in/OFF comments plus the `tests/CMakeLists.txt` comment, declared the inherited
changes. Round-3 rework (`Q050-035.r2.code.md`): Table B row 1 (drivable / superseded) and Reconciliation
5 (only `lufs`/`scan` still absent). Gate 21 → 29 checks.

Round-4 rework (`Q050-035.r3.feature.md`, verdict REWORK): (1) the default-OFF block comment in
`src/core/ControlCommandsStems.cpp` and the header of `include/ControlStemSupport.h` are corrected and the
gate now catches them; (2) `docs/KNOWN-LIMITATIONS.md` describes the **44.1 kHz output with other input
rates resampled** (the "44100 Hz only" claim was wrong — 0.5.0 E27 added resampling); (3) the GUI's split
action routes through the registered `stem.job_start`/`stem.job_status`/`stem.job_cancel` and the new
`stem.tracks_create`; (4) all four tracks are ONE undo step; (5) a QtTest and the socket transcript prove
both the shared command path and the single undo step. Gate 29 → 48 checks; snapshot 828 → 829 ids;
A16 histogram +1 `true_inverse` row.

Round-5 rework (`Q050-035.r4.code.md`, verdict REWORK): the shared GUI path handed
`PathUtil::toAbsolute()`'s result to `stem.job_start`, so on Windows the source was a drive path (`C:/…`)
or a UNC share (`//server/share/…`) — forms `stemRequireAbsolutePath` rejected because they do not
begin with `/`, preventing the GUI split from ever starting on Windows. `stemRequireAbsolutePath` now
accepts `QDir::isAbsolutePath()` (the platform's own rule) or an explicit Windows drive/UNC form, and
still refuses relative paths. Added coverage for Windows drive and UNC paths, both directly and through
the registered command the GUI calls (`StemSplitPipelineTest` slot + four new `ControlStemCommands`
transcript checks, 58 → 62).

## Acceptance table

| Criterion | Command actually run | Result / EXIT |
| --- | --- | --- |
| Verify the default `WANT_STEM_SPLIT` configuration | `grep -n WANT_STEM_SPLIT CMakeLists.txt` | `130:OPTION(WANT_STEM_SPLIT "..." ON)` — default ON; **EXIT=0** |
| Verify the supported build reports it ON | `./build/zene --version` | `WANT_STEM_SPLIT='ON'`, `WANT_WASM='OFF'`, `LMMS_HAVE_ONNXRUNTIME='1'`; **EXIT=0** |
| Release-honesty text matches the build | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h` | `[PASS] stem-separation ON matches ON`; `RESULT: PASS — all 5 documented feature(s) match this build on linux`; **EXIT=0** |
| Docs describe local/offline processing; no stale "compiled out"/"OFF by default"/"44100 Hz only" claim | `python3 tests/stem-availability-docs-gate.py` | all **48 checks PASS**, `RESULT: PASS`; **EXIT=0** |
| Same, through ctest | `ctest -R '^(StemAvailabilityDocsGate\|StemSplitPipelineTest\|ControlStemCommands\|ReversibilityContractTest\|ControlCommandsSnapshot\|DocFiguresGate)$' --output-on-failure` (from `build/tests`) | `100% tests passed out of 6`; **EXIT=0** |
| Build the changed C++ | `cmake --build build --target zene StemSplitPipelineTest -- -j4` | **EXIT=0** |
| GUI and agent share the registered command; four tracks are ONE undo step | `StemSplitPipelineTest::testGuiSplitSharesTheRegisteredCommandAndIsOneUndoStep` + socket transcript `ControlStemCommands` | QtTest `Totals: 0 failed`; transcript `stem.tracks_create materialises the four stems as four tracks`, `stem.tracks_create records exactly one project transaction`, `ONE control.undo removes all four stem tracks together` — **58 checks OK**; **EXIT=0** |
| Agent surface does not regress | `ctest -R '^agent_surface$' --output-on-failure` | `agent_surface ... Passed`; **EXIT=0** |
| Validator accepts platform-native absolute paths; Windows drive/UNC reach the engine's refusal | `StemSplitPipelineTest::testValidatorAcceptsWindowsAbsolutePathsThroughJobStart`; `ControlStemCommands` transcript | Unix accepted; `mix.wav`/`C:foo` refused; `C:\…`/`C:/…`/`\\server\share\…`/`//server/share/…` accepted; `stem.job_start` on a Windows path is `refused` (`no source file`), not `invalid_args`; transcript `stem.job_start accepts a Windows drive path (the GUI route)` / `… UNC path …` / `… backslashes` / `a drive-relative path (C:foo) is still refused typed`; **62 checks OK**; **EXIT=0** |
| Gate is not tautological (new round-4 check) | injected `THE FEATURE IS OFF IN THE DEFAULT RELEASE CONFIGURATION` into the `ControlCommandsStems.cpp` block comment, ran the gate | **EXIT=1** (1 FAIL); restored file → **EXIT=0** (`29-redproof-comment.log`, `30-restored-comment.log`) |
| Ratio gates are pre-existing red only | `bash tests/complexity-gate.sh --check` / `bash tests/file-length-gate.sh --check` and the same in `../zene-cachyos` | both **EXIT=1**; the `REGRESSION` line sets diff **empty** against `zene-cachyos`; no file of mine newly listed |

The gate asserts: `CMakeLists.txt` declares `WANT_STEM_SPLIT` default ON; `tests/advertised-features.tsv`'s
`stem-separation` row requires ON and claims presence; `README.md` says "in the build by default";
`docs/KNOWN-LIMITATIONS.md` says "built in and on by default", "local and offline", "resampled to it
offline", names `stem.tracks_create` and "ONE undo step", and carries none of `WANT_STEM_SPLIT='OFF'`,
"Two features are compiled out of these builds", "44100 Hz only", "there is no resampler";
`docs/RELEASE-NOTES-v0.3.0-alpha.md` no longer says "The feature is OFF in the default release
configuration"; `tests/CMakeLists.txt`'s stem registration comment no longer says OFF; the active inline
comments in `ControlRegistryGroups.h`, `ControlRegistryRegistrations.cpp`, `src/core/CMakeLists.txt`,
`src/gui/CMakeLists.txt`, `tests/CMakeLists.txt`, `src/core/ControlCommandsStems.cpp`,
`include/ControlStemSupport.h` and `include/StemSeparation/StemTypes.h` no longer call the feature
opt-in/OFF or claim resampling is unimplemented; `docs/FEATURE-LIST-0.3.0.md` row 26 is **in the tree**,
names the eight `stem.*` ids (including `stem.tracks_create`) and "local and offline"; Table B row 1 is
**drivable**/**superseded** and names `stem.tracks_create`; Reconciliation 5 names `lufs`/`scan` and
accounts for `stem`/`mastering`; and `src/gui/StemSplitController.cpp` invokes
`stem.job_start`/`stem.job_status`/`stem.job_cancel`/`stem.tracks_create`.

## Files changed

- `docs/KNOWN-LIMITATIONS.md` — the "compiled out" bullet (now one feature, WASM), the `stem.*` section
  lead and the corrected 44.1 kHz-output / resampling bullet; names `stem.tracks_create` and the one undo
  step.
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` — the "OFF in the default release configuration" bullet; the
  A16-HISTOGRAM block (`838 rows … 418 true_inverse`; `stem.get_state rows=8 true_inverse=1`).
- `docs/STATUS.md`, `docs/FEATURE-LIST-0.3.0.md` — the availability lines; row 26 (eight ids), Table B
  row 1 and Reconciliation 5.
- `README.md` — canary `828 → 829 ids / 76 groups`.
- `include/ControlRegistryGroups.h`, `src/core/ControlRegistryRegistrations.cpp`, `src/core/CMakeLists.txt`,
  `src/gui/CMakeLists.txt`, `tests/CMakeLists.txt` — the stem comments (were opt-in/OFF).
- `src/core/ControlCommandsStems.cpp` — the default-OFF block comment corrected; the new
  `stem.tracks_create` command (one undo step, `mutating`).
- `include/ControlStemSupport.h`, `src/core/ControlStemSupport.cpp` — the header's default-OFF /
  "resampling is not implemented" claims corrected; `stemTakeResult` exposes a completed job's buffers;
  `stemRequireAbsolutePath` now accepts `QDir::isAbsolutePath()` or a Windows drive/UNC form (round 5).
- `include/StemSeparation/StemTypes.h` — the model-rate comment now says inputs are resampled.
- `include/ControlReversibility.h`, `src/core/ControlReversibilityTableStems.cpp` — the `stem.*` A16 rows
  (seven `not_mutating` + one `true_inverse`).
- `src/gui/StemSplitController.cpp`, `include/StemSplitController.h` — the controller is now a client of
  `ControlRegistry` (no private `StemJobManager`); a temp WAV carries in-memory clips through
  `stem.job_start`.
- `tests/stem-availability-docs-gate.py` — the docs-truth gate, now 48 checks.
- `tests/src/core/StemSplitPipelineTest.cpp` — `ControlRegistry::setReady(true)` and the shared-command /
  single-undo-step case.
- `tests/control-stem-commands.py`, `tests/stem_commands_lib.py` — the transcript's `stem.tracks_create`
  checks.
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` — regenerated from a live no-`wasm`
  instance (829 ids / 76 groups).
- `tests/upstream-modifications.txt` — the `StemSplitPipelineTest.cpp` reason extended for this change.

New command: `stem.tracks_create` (`stem.*`), reversibility **`true_inverse`** (recorded action; ONE undo
removes the four tracks). Project format: unchanged.

## Gates

| Gate | EXIT |
| --- | ---: |
| `bash tests/complexity-gate.sh --check` | 1 (pre-existing train-5 red; `REGRESSION` diff vs `zene-cachyos` empty) |
| `bash tests/file-length-gate.sh --check` | 1 (pre-existing train-5 red; `REGRESSION` diff vs `zene-cachyos` empty) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/release-honesty-gate.sh --header build/lmmsversion.h` | 0 |
| `ctest -R '^(StemSplitPipelineTest\|ControlStemCommands\|StemAvailabilityDocsGate\|ReversibilityContractTest\|ControlRegistryTest\|ScriptCommandBridgeTest\|ControlCommandsSnapshot\|DocFiguresGate\|agent_surface)$'` | 0 (9/9) |
| `ctest -R '^agent_surface$'` | 0 |

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-035.r5/logs/`.
