IMPLEMENTED

# Cluster Q050-035 — Correct stem separation availability docs

Docs-only unit (R-platform-26, area L05). The docs claimed offline HTDemucs stem separation was
"compiled out of these builds" and reported `WANT_STEM_SPLIT='OFF'`, while owner decision 14 had made
the option default ON and the release-honesty manifest require ON. Corrected the current-facing docs to
say the feature is built in, local and on by default, and added a hermetic docs-truth gate.

Round-2 rework (review `Q050-035.r1.code.md`): rewrote `docs/FEATURE-LIST-0.3.0.md` row 26 so its status
is **in the tree** (the stale audit status no longer sits in the row with a contradicting correction),
extended the gate to verify that row and the corrected inline comments, fixed the four active
opt-in/OFF comments (`include/ControlRegistryGroups.h`, `src/core/ControlRegistryRegistrations.cpp`,
`src/core/CMakeLists.txt`, `src/gui/CMakeLists.txt`) plus the `tests/CMakeLists.txt` test comment, and
declared the inherited-file changes in `tests/upstream-modifications.txt`. The gate went from 10 to
**21 checks**; the report below is updated to match.

## Acceptance table

| Criterion | Command actually run | Result / EXIT |
| --- | --- | --- |
| Verify the default `WANT_STEM_SPLIT` configuration | `grep -n WANT_STEM_SPLIT CMakeLists.txt` | `130:OPTION(WANT_STEM_SPLIT "..." ON)` — default ON; **EXIT=0** |
| Verify the supported build reports it ON | `./build/zene --version` | `WANT_STEM_SPLIT='ON'`, `WANT_WASM='OFF'`, `LMMS_HAVE_ONNXRUNTIME='1'`; **EXIT=0** |
| Release-honesty text matches the build | `bash tests/release-honesty-gate.sh --header build/lmmsversion.h` | `[PASS] stem-separation ON matches ON`; `RESULT: PASS — all 5 documented feature(s) match this build on linux`; **EXIT=0** |
| Docs describe local/offline processing; no stale "compiled out"/"OFF by default" claim | `ctest -R '^(StemAvailabilityDocsGate\|StemSplitPipelineTest)$' --output-on-failure` (from `build/tests`) | `100% tests passed out of 2`; `StemAvailabilityDocsGate ... Passed`, `StemSplitPipelineTest ... Passed`; **EXIT=0** |
| Same, direct script run | `python3 tests/stem-availability-docs-gate.py` | all **21 checks PASS**, `RESULT: PASS`; **EXIT=0** |
| Gate is not tautological (red proof, Known Limitations hunk) | appended `WANT_STEM_SPLIT='OFF'` to `docs/KNOWN-LIMITATIONS.md`, ran the gate | **EXIT=1**; restored file → **EXIT=0** |
| Gate is not tautological (red proof, feature-list row + inline comment) | reverted row 26 and the `ControlRegistryGroups.h` comment in a throwaway root, ran the gate with `--root` | `RESULT: FAIL - 4 ... stale`; **EXIT=1**; throwaway root removed |

The gate asserts: `CMakeLists.txt` declares `WANT_STEM_SPLIT` default ON; the `stem-separation` row of
`tests/advertised-features.tsv` requires the same value and claims presence; `README.md` says "in the
build by default"; `docs/KNOWN-LIMITATIONS.md` says "built in and on by default" and "local and offline"
and contains neither `WANT_STEM_SPLIT='OFF'` nor "Two features are compiled out of these builds";
`docs/RELEASE-NOTES-v0.3.0-alpha.md` no longer says "The feature is OFF in the default release
configuration"; the `tests/CMakeLists.txt` stem registration comment no longer says OFF by default;
`docs/FEATURE-LIST-0.3.0.md` row 26 is **in the tree**, names the seven `stem.*` ids, says "local and
offline", and carries neither the stale `WANT_STEM_SPLIT OFF` nor "no `stem.*` group" text; and the
active inline comments in `include/ControlRegistryGroups.h`, `src/core/ControlRegistryRegistrations.cpp`,
`src/core/CMakeLists.txt`, `src/gui/CMakeLists.txt` and `tests/CMakeLists.txt` no longer call the
feature opt-in/OFF.

## Files changed

- `docs/KNOWN-LIMITATIONS.md` — the stale "Two features are compiled out" bullet (now one feature, WASM)
  and the `stem.*` section heading/lead.
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` — the "OFF in the default release configuration" bullet.
- `docs/STATUS.md` — the "defaults OFF, absent from every release build" line.
- `docs/FEATURE-LIST-0.3.0.md` — row 26 is rewritten: status **in the tree**, the seven `stem.*` ids,
  and the local/offline statement, replacing the stale "no `stem.*` group … `WANT_STEM_SPLIT OFF`".
- `include/ControlRegistryGroups.h` — the `registerStemCommands` comment (was "opt-in … defaults to OFF").
- `src/core/ControlRegistryRegistrations.cpp` — the call-site comment (was "WANT_STEM_SPLIT=OFF").
- `src/core/CMakeLists.txt`, `src/gui/CMakeLists.txt` — the stem source-list comments (were "opt-in").
- `tests/CMakeLists.txt` — stale registration/test comment + `StemAvailabilityDocsGate` ctest.
- `tests/stem-availability-docs-gate.py` — the hermetic docs-truth gate, now 21 checks.
- `tests/fork-sources.txt` — registers the new gate script (fork-NEW).
- `tests/upstream-modifications.txt` — declares the four inherited files changed above.
- `docs/050-features/Q050-035.md`, `docs/qwen-fixes-2026-10/cluster-Q050-035.md` — these reports.

New commands: none. Reversibility: unchanged. Project format: unchanged.

## Gates

| Gate | EXIT |
| --- | ---: |
| `bash tests/complexity-gate.sh --check` | 1 (pre-existing train-5 red; no file of mine newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (pre-existing train-5 red; no file of mine newly listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 |
| `bash tests/release-honesty-gate.sh --header build/lmmsversion.h` | 0 |
| `ctest -R '^StemAvailabilityDocsGate$'` (from `build/tests`) | 0 |

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-035.r2/logs/`.
