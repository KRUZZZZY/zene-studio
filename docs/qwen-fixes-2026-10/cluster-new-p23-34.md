FIXED

## Decision and evidence

Verdict: REAL. `src/core/ControlCommandsTrackVersions.cpp:86` trims `name` in `saveVersion`; the sibling handlers read it raw at lines 103 and 126 before looking up the saved-name map. A saved `foo` therefore could not be switched to or removed with `foo `.

Both handlers now trim the same argument before lookup. This is control-command argument handling; it changes no saved data, project loading, audio-render code, or command registration.

## Regression matrix

| Area | Case | Expected result |
|---|---|---|
| Normal switch | Switch with `A ` after saving `A` | Success; `A` is active and its saved clips are restored. |
| Normal remove | Remove active `B ` after saving `B` | Success; `B` is removed, active is cleared, and current clips stay unchanged. |
| Failure | Switch a missing padded name or remove a whitespace-only name | `NotFound`; versions and current clips stay unchanged. |
| Exhaustion/reconfiguration | These handlers have no bounded resource, configurable state, or reconfiguration path. Not applicable. |
| Audio thread | Only control handlers are changed; no render/audio-thread path is touched. |
| Platform build | `ArrangeToolsTest` is built on the configured Linux/Qt build. The change uses portable `QString::trimmed()` and adds no platform branch. |

The regression lives in the existing fork-registered `tests/src/core/ArrangeToolsTest.cpp`. It adds separate slots for switch and remove and checks their failure paths without sleeps, time, or races.

## Execution evidence

Commands below were run from the stated directories; log files are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-34.r1/logs`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial pre-fix build (worktree root) | `cmake --build build --target zene ArrangeToolsTest --parallel 2` | 0 | `01-red-build.log` |
| Initial RED (from `build/tests`) | `ctest -R ArrangeToolsTest --output-on-failure` | 8 | `02-red-test.log`: padded `A ` switch and `B ` remove each failed with `NotFound`. One CTest executable ran. |
| Initial fixed-code build (worktree root) | `cmake --build build --target zene ArrangeToolsTest --parallel 2` | 0 | `03-green-build.log` |
| Initial GREEN attempt (from `build/tests`) | `ctest -R ArrangeToolsTest --output-on-failure` | 8 | `04-green-test.log`: the fixture expected stale `A` contents; saving `B` intentionally snapshots the current clips back into active `A`. The fixture was corrected to edit after both saves. |
| Save product patch | `git diff -- src/core/ControlCommandsTrackVersions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-34.r1/logs/fix.patch` | 0 | Patch contains only the two production `.trimmed()` changes. |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-34.r1/logs/fix.patch` | 0 | Negative control setup. |
| Final negative-control build (worktree root) | `cmake --build build --target zene ArrangeToolsTest --parallel 2` | 0 | `05-negative-control-build.log` |
| Final RED (from `build/tests`) | `ctest -R ArrangeToolsTest --output-on-failure` | 8 | `06-negative-control-test.log`: both final regression slots fail on the unpatched handlers with `NotFound` for `A ` and `B `. One CTest executable ran. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-34.r1/logs/fix.patch` | 0 | Restored the two trim calls. |
| Final build (worktree root) | `cmake --build build --target zene ArrangeToolsTest --parallel 2` | 0 | `07-restored-build.log` |
| Final GREEN (from `build/tests`) | `ctest -R ArrangeToolsTest --output-on-failure` | 0 | `08-restored-test.log`: 100% of 1 CTest test passed, including both new slots. |

The first feature-check run exited 1 because `tests/all-sources.txt` included 1,148 vendored `sfizz` paths excluded by its own recipe. The manifest was regenerated from that embedded recipe (exit 0); this makes no product or test behavior change. The exact recipe run was:

```text
{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u
```

The Python wrapper preserved the manifest header and replaced its entries with that recipe's output; exit 0.

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-34
```

Exit 0, recorded in `11-feature-check.log`. Results: `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, and `doc-figures` PASS; complexity and file-length PASS with no new-item failures.

| Completion check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Post-commit check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p23-34 new-p23-34` | 0 | `13-submit-check.log`: committed branch, clean tree, report, tests, and gates PASS; RESULT: PASS. |

## Self-check

- **Reverted test fails, restored test passes:** with the final test source, the patch was reversed, `zene` and `ArrangeToolsTest` rebuilt (exit 0), and `ctest -R ArrangeToolsTest --output-on-failure` returned 8 with both padded-name slots failing. After `git apply` restored the patch and the same targets were rebuilt (exit 0), the same CTest command returned 0 (1/1). See logs `05`–`08` above.
- **Deterministic:** tests invoke the control registry synchronously with literal names (`A `, `B `, `missing `, and `  `); there are no sleeps, clock inputs, races, or timing assumptions.
- **Cause, completeness, and undo:** `saveVersion` already trims, `listVersions` takes no name, and the `track.version_*` siblings were checked. `track.version_switch` and `track.version_remove` now normalize before lookup. The underlying undo operations are untouched; the existing `ArrangeToolsTest` switch and remove cases verify undo behavior, and the family passed.
- **No collateral or saved-data change:** only command argument lookup is normalized. Whitespace-padded lookup failures do not mutate versions or clips, checked by the regression. Project version serialization/loading and clip replacement code are unchanged.
- **False-positive evidence:** not applicable; the verdict is REAL, supported by the raw reads at `src/core/ControlCommandsTrackVersions.cpp:103` and `:126` and by both RED assertions.
- **Sibling commands:** `track.version_list` has no `name` argument; `track.version_save` already trims it; switch and remove now match that normalization. No command IDs or reversibility declarations changed.

## Files committed

- `src/core/ControlCommandsTrackVersions.cpp`
- `tests/src/core/ArrangeToolsTest.cpp`
- `tests/all-sources.txt` (regenerated to satisfy its embedded source-list recipe)
- `docs/qwen-fixes-2026-10/cluster-new-p23-34.md`
