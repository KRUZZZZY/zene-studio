# Cluster new-p10-531: `Waveform::Count` indexes past the wavetable array

## Verdict
**FALSE-POSITIVE** (for the current tree). The out-of-bounds read is real *if* `BandLimitedWave::oscillate`
is ever called with `Waveform::Count` or an out-of-range value, but no caller in the tree can do that:
every call site passes a literal `Waveform` enumerator, and both plugin entry points that take a
project-stored value filter it through a switch before reaching `oscillate`. No product code was changed,
so there is no regression test and no RED/GREEN pair for this cluster.

## Evidence

### The mechanism (real, but only if reached)
* `include/BandLimitedWave.h:93-100` - `enum class Waveform { BLSaw, BLSquare, BLTriangle, BLMoog, Count }`.
  `Count` is a sentinel, not a waveform.
* `include/BandLimitedWave.h:101` - `NumWaveforms = static_cast<std::size_t>(Waveform::Count)` (= 4).
* `include/BandLimitedWave.h:173` - `s_waveforms` is `std::array<WaveMipMap, NumWaveforms>`, so index 4 is past the end.
* `include/BandLimitedWave.h:144-149` - `oscillate` indexes `s_waveforms[ static_cast<std::size_t>(_wave) ]` with no check.
  (The finding's line 143 is now 144; the body moved by one line.)
* `include/BandLimitedWave.h:127-130` - the documented contract lists only "saw, triangle, square and moog saw" as
  waveforms, i.e. the four enumerators. Passing `Count` violates the contract.

### Every call site passes a literal enumerator
| Caller | Lines | What it passes |
|---|---|---|
| `plugins/Lb302/Lb302.cpp` | 467-481 | `BandLimitedWave::Waveform::BLSaw/BLSquare/BLTriangle/BLMoog` (literals in four `case` labels) |
| `tests/reference/Lb302/Lb302.cpp` | 458-470 | same four literals |
| `plugins/Monstro/Monstro.h` | 183-244 | `oscillate(int _wave, ...)` switches on `_wave`; each BL call is a literal enumerator; a value with no case falls through to the trailing `return 0.0` (`:244`) and never reaches `BandLimitedWave::oscillate` |
| `tests/reference/Monstro/Monstro.h` | 178-215 | same shape as the plugin copy |

A search of the whole worktree for `oscillate(`, `BandLimitedWave::Waveform`, and `Waveform>(` /
`static_cast<BandLimitedWave::Waveform` found no other caller and no integer-to-`Waveform` cast.

### The "unchecked project value" path does not reach `oscillate`
* Lb302 (`plugins/Lb302/Lb302.cpp:418`): `m_vcoShape = static_cast<VcoShape>(m_waveShape.value());`.
  `m_waveShape` is an `IntModel` constructed with range `[0, 11]` (`plugins/Lb302/Lb302.cpp:224`,
  `plugins/Lb302/Lb302.h:197`). The value is clamped by `AutomatableModel::fittedValue`
  (`src/core/AutomatableModel.cpp:604-616`), which setting paths use: `setInitValue` at
  `src/core/AutomatableModel.cpp:1061` (project load) and `setValueInternal` at `:500`.
  So `m_vcoShape` is always one of the 12 `VcoShape` enumerators `0..11`.
* The `switch (m_vcoShape)` at `plugins/Lb302/Lb302.cpp:423` has **no `default:`**. The four `BL*` cases
  that call `oscillate` (`:467-481`) are matched only by name, so `oscillate` runs only for
  `BLSawtooth/BLSquare/BLTriangle/BLMoog`, which map to literal `Waveform` values.
* Monstro (`plugins/Monstro/Monstro.h:183`): the `int` switch routes only to literal enumerators; any
  value with no case reaches the trailing `return 0.0` (`:244`).

Conclusion: no project value, command, or test path reaches `oscillate` with `Waveform::Count` or an
invalid `Waveform`. The contract "callers pass a valid `Waveform`" holds for the whole tree.

### Hardening (not done, needs a decision)
`oscillate` is per-sample on the audio path and has no guard. A defensive check (return 0, or fall back to
`BLSaw`) would change the function's behaviour for an input no caller sends, and the choice between
"silence" and "fallback" is a design decision that the finding leaves open. This verdict does not add one.
If the owner wants defence in depth, it is a separate small change: the check is one compare on
`_wave` against `NumWaveforms`, placed before line 144. A test for it would need a seam, because the
current out-of-bounds read is undefined behaviour and cannot give a deterministic RED.

## Commands run (LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-531.r1/logs`)

| # | Command (cwd = worktree unless noted) | Exit | Log |
|---|---|---:|---|
| 1 | `cd build/tests && ctest -N -R '^Lb302'` | 0 | `ctest-list-1.log` - lists only `Lb302NotePathTest` (#615), `Total Tests: 1`. No test is named `Lb302`, so `ctest -R '^Lb302$'` (the finding's pattern) selects 0 tests. |
| 2 | `cmake --build build --target Lb302NotePathTest -j4` | **2** | `build-1.log` - ccache could not write `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6/...` (permission denied). Not a compile error. |
| 3 | `CCACHE_DISABLE=1 cmake --build build --target Lb302NotePathTest -j4` | 0 | `build-2.log` - linked and built. |
| 4 | `cd build/tests && ctest -R '^Lb302NotePathTest$' --output-on-failure` (before build) | 8 | `test-1.log` - `Not Run`, binary absent. |
| 5 | `cd build/tests && ctest -R '^Lb302NotePathTest$' --output-on-failure` (after build) | **0** | `test-2.log` - `100% tests passed, 0 tests failed out of 1` (1 test executed). |
| 6 | `python3 .../qwen/feature_check.py <worktree>` | **1** | `feature-check-1.log` - `PermissionError`: the script creates `<worktree>-feature-check/` beside the worktree, which is outside the write-jail. The script was not changed. |
| 7 | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| 8 | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-1.log` |
| 9 | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-1.log` |
| 10 | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| 11 | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-1.log` |
| 12 | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-1.log` |
| 13 | `bash tests/complexity-gate.sh --check` | **1** | `gate-complexity-1.log` - 6 `REGRESSION` lines, all for pre-existing items (for example `src/wasm/WasmSandbox.cpp` CCN 15 -> 16, `src/core/ControlCommandsMixerStrip.cpp`). The branch changes no file (`git diff --name-only <merge-base>..HEAD` is empty; see below), so under the wrapper's rule these rows should PASS. **Not observed through the wrapper.** |
| 14 | `bash tests/file-length-gate.sh --check` | **1** | `gate-file-length-1.log` - 22 `REGRESSION` lines, all pre-existing; same reasoning as row 13. |

Changed-file set used by the gate wrapper: `FIX-BASE` merge-base is the current `HEAD`
(`5c1c6715d9`), so `git diff --name-only <base>..HEAD` is empty. No product file is changed.

## Files committed
* `docs/qwen-fixes-2026-10/cluster-new-p10-531.md` (this report).

No product file, test, or registration file is changed.

## Self-check

### ? The regression test FAILS with your product change reverted and PASSES with it (exit codes).
N/A - there is no product change. The existing family test `Lb302NotePathTest` passes on the unchanged tree
(row 5, EXIT=0, 1/1). A RED test here would have to call `oscillate` with `Waveform::Count`, which no
production path does, so writing one would test an unreachable state rather than the finding.

### ? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).
N/A - no test added. The finding's mechanism is undefined behaviour and cannot give a deterministic RED without a seam;
see "Hardening" above.

### ? The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.
N/A - no fix. Callers checked: Lb302 (`plugins/Lb302/Lb302.cpp:418-481`, `tests/reference/Lb302/Lb302.cpp:458-470`),
Monstro (`plugins/Monstro/Monstro.h:183-244`, `tests/reference/Monstro/Monstro.h:178-215`). Each passes a literal enumerator or is switched first.

### ? No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.
No change made. The project-value path (`static_cast<VcoShape>(m_waveShape.value())`, `plugins/Lb302/Lb302.cpp:418`) is clamped to `[0,11]` by `AutomatableModel::fittedValue` (`src/core/AutomatableModel.cpp:604-616`, used at `:1061` and `:500`).

### ? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.
Yes: `include/BandLimitedWave.h:127-130` (contract: four waveforms), `:93-101` (enum and `NumWaveforms`), `plugins/Lb302/Lb302.cpp:423-481`
(no-default switch, literal BL cases), `plugins/Monstro/Monstro.h:183-244` (int switch, literal cases, `return 0.0` default),
`src/core/AutomatableModel.cpp:604-616` (clamp). Gate-wrapper gaps (row 6) and the ratchet rows (13-14) are reported above, not hidden.
