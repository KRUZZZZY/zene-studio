# Cluster new-p10-265-port: Multitap Echo filter stage cascade

Verdict: FIXED.

## Evidence

- In `plugins/MultitapEcho/MultitapEcho.cpp`, each tap now feeds the previous stage's output into the next stage through the existing `m_work` scratch buffer (both swapped and unswapped paths). No allocation on the audio thread.
- `tests/src/plugins/PluginPortsMigrationTest.cpp` adds `multitapEchoStagesCascade()` and a `multitapecho` branch in `migratedPluginsPreserveBehaviour()` asserting the cascaded render differs from the single-stage reference.
- `tests/upstream-modifications.txt` records the inherited production-file change.

## Verification

Affected CTest target: `PluginPortsMigrationTest`; each successful run executed 1/1 registered target. Builds ran from the worktree; ctest ran from `build/tests` with `LADSPA_PATH` pointing at the built LADSPA fixtures (required by an unrelated `nonRealtimeLadspaEffectsAreInvalid` slot).

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code RED build | `cmake --build build --target zene PluginPortsMigrationTest > .../logs/build-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Rebuilt without the DSP fix. |
| Original-code RED test | `LADSPA_PATH=.../build/plugins/ladspa ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-red.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 8 | `multitapEchoStagesCascade` reported `four filter stages match one stage (max\|delta\|=0)`; `migratedPluginsPreserveBehaviour` reported `the fixed two-stage render must differ from the single-stage old reference`. |
| Fixed GREEN build | `cmake --build build --target zene PluginPortsMigrationTest > .../logs/build-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Rebuilt `zene` and the affected test target. |
| Fixed GREEN test | `LADSPA_PATH=.../build/plugins/ladspa ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../logs/test-green.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | 1/1 passed. |
| Reverse-patch application | `git diff -- plugins/MultitapEcho/MultitapEcho.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 | Temporarily removed only the production fix. |
| No-fix proof build | `cmake --build build --target zene PluginPortsMigrationTest > .../logs/build-red.log 2>&1; ...` | 0 | Rebuilt without the fix. |
| No-fix proof test | `LADSPA_PATH=... ctest -R '^PluginPortsMigrationTest$' ...` | 8 | Stage-count assertion reported `max\|delta\|=0`. |
| Restore-patch application | `git apply .../logs/fix.patch; status=$?; ...` | 0 | Restored the production fix. |
| Final build | `cmake --build build --target zene PluginPortsMigrationTest > .../logs/build-green.log 2>&1; ...` | 0 | Rebuilt after restoring the fix. |
| Final test | `LADSPA_PATH=... ctest -R '^PluginPortsMigrationTest$' ...` | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity.log 2>&1; ...` | 1 | Pre-existing regressions unrelated to this change (6 new over-target functions already on the branch). |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-file.log 2>&1; ...` | 1 | Pre-existing regressions unrelated to this change (14 new over-500-line files already on the branch). |
| Diff whitespace check | `git diff --check` | 0 | Passed. |

## Files in the commit

- `plugins/MultitapEcho/MultitapEcho.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-265-port.md`
