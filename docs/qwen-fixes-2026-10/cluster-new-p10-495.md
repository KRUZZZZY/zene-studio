# Cluster new-p10-495

Verdict: **FIXED** (narrowed: the finding's default-view trigger is not undefined behaviour; the null-data view with non-zero counts is)

## Finding and decision

Finding: `framesView()` computes `m_data + channels * frames` even when the view is empty, and null-pointer arithmetic is undefined.

- **Zero counts on a null view: not UB.** `[expr.add]` (C++ standard carve-out: "If P evaluates to a null pointer value and J evaluates to 0, the result is a null pointer value") makes `nullptr + 0` defined. The secondary quotations I found are from an ISO Liaison thread (https://lists.isocpp.org/liaison/2023/10/1324.php); the exact draft wording was not fetched. A local probe (`clang++ -fsanitize=undefined,pointer-overflow`) reported only the non-zero control offset (`applying non-zero offset 40 to null pointer`); `null + 0` was silent under both g++ and clang++.
- **Null data with non-zero counts: REAL.** `empty()` returns true for it (`include/AudioBufferView.h:339` for the InterleavedBufferView overload, `:512` for PlanarBufferView), yet `framesView()` produced a non-empty range from null to `null + channels*frames`. The new test shows it: before the fix, `framesView()` of a null view built with 4 frames iterates 4 frames (expected 0).
- Reachability: no production caller found that builds a null view with non-zero counts. The view constructors accept any `(data, channels, frames)` combination, so the state is constructible. The fix is for the contract (`empty()` and iteration must agree) rather than for an observed crash.

## Change

- [include/AudioBufferView.h:408-409](../../include/AudioBufferView.h#L408-L409): const `framesView()` ends at `m_data` when `empty()`, otherwise at `m_data + channels*frames` as before.
- [include/AudioBufferView.h:423-424](../../include/AudioBufferView.h#L423-L424): the same for the non-const overload.
- [tests/src/core/AudioResamplerRatioTest.cpp:174-196](../../tests/src/core/AudioResamplerRatioTest.cpp#L174-L196): regression slot `framesViewOfNullDataIsEmpty()` covers static and dynamic null views (distance must be 0), a default-constructed view (empty, distance 0), and a valid view (distance 4, unchanged).
- [tests/upstream-modifications.txt](../../tests/upstream-modifications.txt): one line recording the inherited header change and its reason.

Sibling functions in the same header (`framePtr` at `:378`, `subspan` at `:391-403`, `sampleFrameAt`) also form `m_data + n` and would be UB for null data with n > 0. They are different functions, not part of the finding, and are left unchanged.

## Regression evidence

RED assertion (unfixed header): `Actual (std::ranges::distance(staticNull.framesView())): 4`, `Expected 0`.

| Step | Command (run from the worktree unless noted) | Log | Exit |
| --- | --- | --- | ---: |
| First RED build, failed on ccache temp dir | `cmake --build build --target zene AudioResamplerRatioTest -j4` | `build-1.log` | 2 |
| RED build (unfixed header) | `CCACHE_TEMPDIR=<on-disk tmp> cmake --build build --target zene AudioResamplerRatioTest -j4` | `build-2.log` | 0 |
| RED test | from `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | `test-1-red.log` | 8 |
| Full header-dependent rebuild (refused: bunny-guard needs a named target) | `cmake --build build -j4` | `build-3-full.log` | 97 |
| Named rebuild, first try (zsh did not split the target list) | `cmake --build build -j4 --target $TARGETS` | `build-3-named.log` | 2 |
| Named rebuild of all 1,051 product targets (every target that includes the header) | `bash -c "mapfile -t T < final.txt; cmake --build build -j4 --target \"\${T[@]}\""` | `build-4-named.log` | 0 |
| GREEN test | from `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | `test-2-green.log` | 0 (100% of 1) |
| Family test | from `build/tests`: `ctest -R '^(DispersionNonFiniteTest\|ZynAddSubFxInstrumentTest)$' --output-on-failure` | `test-3-family.log` | 0 (2/2) |

Reverse-patch proof (step 4b):

| Step | Command | Log | Exit |
| --- | --- | --- | ---: |
| Save the product diff | `git diff -- include/AudioBufferView.h > <logs>/fix.patch` | - | 0 |
| Reverse it | `git apply -R <logs>/fix.patch` | - | 0 |
| Rebuild with reversed header | `cmake --build build --target zene AudioResamplerRatioTest -j4` | `build-5-reversed.log` | 0 |
| Reversed RED | from `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | `test-4-reversed-red.log` | 8 (`Actual 4`, `Expected 0`) |
| Re-apply it | `git apply <logs>/fix.patch` | - | 0 |
| Rebuild with fix | `cmake --build build --target zene AudioResamplerRatioTest -j4` | `build-6-restored.log` | 0 |
| Restored GREEN | from `build/tests`: `ctest -R '^AudioResamplerRatioTest$' --output-on-failure` | `test-5-restored-green.log` | 0 (100% of 1) |

The AudioResamplerRatioTest family test count is 1 test (one ctest entry, 18 QTest functions: 17 existing plus the new one). The family run executed 2 tests. Zero were skipped.

## Gates

`feature_check.py` was run first (`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`, log `gates-1.log`, exit 1) but failed before any check: it creates `<worktree>-feature-check` next to the worktree, which is outside the write-jail (`PermissionError`). I did not change the gate. I ran the same checks directly in the worktree, saving each output:

| Gate | Command | Log | Exit |
| --- | --- | --- | ---: |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity.log` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length.log` | 1 |

The two ratchets are red on the train by design. Their regression lines name files such as `src/core/TakeLane.cpp`, `src/wasm/WasmSandbox.cpp` and various `tests/src/core/*` files; none names `include/AudioBufferView.h`, `tests/src/core/AudioResamplerRatioTest.cpp` or `tests/upstream-modifications.txt`. Under the feature-check rule (only items in files the branch changed count) both would PASS. This is my reading of the output, not a `feature_check.py` run.

## Notes

- The named rebuild covers every product target (zene, plugins, test executables) that could include the header. The reverse-patch rebuild covered only `zene` and this test. Other objects are from the fixed-header build.
- The ccache temp directory default is not writable from this session; builds used `CCACHE_TEMPDIR` pointed at an on-disk directory under `~/.cache/zene-tmp`.
