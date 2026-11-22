# Cluster new-p10-263: zero-capacity MonoDelay divides by zero in `tick`

Verdict: **ALREADY-FIXED**

The reported crash is real on the code before commit `40da59feea`, and it does not happen on this branch.
`MonoDelay::tick()` now returns before the modulo whenever `m_length == 0`, and every write path keeps
`m_length` at zero whenever `m_maxLength` is zero. The finding's line 59 is that guard. This cluster adds
a regression slot that fails on the unguarded code and passes on the guarded code. No product file changes.

## Evidence (file:line on `bugs/qwen-new-p10-263`)

- `plugins/Flanger/MonoDelay.cpp:59` is `if (m_length == 0.0f) { return; }`. The modulo the finding names is
  at `:61`, after the guard. The guard came from `40da59feea` ("Fix zero-length MonoDelay", cluster
  new-p12-157-port), which is an ancestor of `HEAD`.
- Zero capacity implies zero length. The constructor sets `m_length = m_maxLength` (`:36-37`).
  `setSampleRate` sets `m_length` to `0` when the new length is `0`: scaled by `newMaxLength / m_maxLength`
  at `:82` (which is `0` when `newMaxLength` is `0`) or assigned `newMaxLength` at `:86`. `setLength`
  accepts only `0 <= length <= m_maxLength` (`plugins/Flanger/MonoDelay.h:41-43`), so with zero capacity
  only `0` is accepted. A grep of `m_length` in `plugins/Flanger/` shows no other writes.
- Before `40da59feea` the same `tick()` had no guard, so `MonoDelay(0, 48000).tick()` computed
  `(m_writeIndex + 1) % 0`. The RED run below reproduces that exact state and crashes with SIGFPE.
- Callers: `plugins/Flanger/FlangerEffect.cpp:65-66` constructs `MonoDelay( 1, outputSampleRate )` and
  `:139-140` calls `setSampleRate( outputSampleRate )`, so the app itself never builds zero capacity. The
  zero-capacity case is reachable only through direct construction, as in the finding.

## Regression

- New slot `PluginPortsMigrationTest::zeroCapacityDelayPassesInputThrough` in
  `tests/src/plugins/PluginPortsMigrationTest.cpp` (declaration and body beside the existing
  `zeroLengthDelayPassesInputThrough`). Matrix:
  - `MonoDelay{0, 48000}`: `m_maxLength == 0`; `setLength(5)` is rejected and `m_length` stays `0`.
  - `MonoDelay{1, 0}`: zero sample rate gives zero capacity.
  - `MonoDelay{1, 4}`, `setLength(2)`, then `setSampleRate(0)`: reconfiguration to zero capacity.
  - Each of the three ticks eight frames; each output must equal its input (pass-through, no ring read).
- The slot runs inside the existing ctest entry `PluginPortsMigrationTest` (`tests/CMakeLists.txt`, no
  CMake change). It is an audio-path check: no allocation, lock, log or timing.
- `MonoDelay` is included by `#include` in the test, so the test rebuilds with the plugin module.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-263.r1/logs`. Every command ran from
the worktree, or from `build/tests` for the test binary.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Check product clean | `git diff --quiet -- plugins/Flanger/MonoDelay.cpp` | 0 | - |
| Save guard hunk | `git show 40da59feea -- plugins/Flanger/MonoDelay.cpp > logs/fix.patch` | 0 | `fix.patch` |
| RED: remove guard | `git apply -R logs/fix.patch` | 0 | - |
| RED build (first try) | `cmake --build build --target flanger PluginPortsMigrationTest -j2` | 2 (ccache could not write its temp file under the shared cache; no code error) | `build-red-1.log` |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j2` | 0 | `build-red-2.log` |
| RED run | `cd build/tests && ./PluginPortsMigrationTest zeroCapacityDelayPassesInputThrough` | **136** (SIGFPE, `FPE_INTDIV`) | `test-red-1.log` |
| GREEN: restore guard | `git apply logs/fix.patch`, then `git diff --quiet -- plugins/Flanger/MonoDelay.cpp` | 0, 0 | - |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j2` | 0 | `build-green-1.log` |
| GREEN run | `cd build/tests && ./PluginPortsMigrationTest zeroCapacityDelayPassesInputThrough` | **0** (`PASS zeroCapacityDelayPassesInputThrough()`, `Totals: 3 passed`) | `test-green-1.log` |
| Build the other family test | `CCACHE_DISABLE=1 cmake --build build --target MonoDelayTest -j2` | 0 | `build-mono-1.log` |
| Family | `cd build/tests && ctest -R '^(PluginPortsMigrationTest\|MonoDelayTest)$' --output-on-failure` | 8 (one entry failed, see below) | `test-family-1.log` |
| Family slots | `PluginPortsMigrationTest` executed 89 slots: 86 passed, 1 failed, 2 skipped | - | `test-family-1.log` |
| `MonoDelayTest` | ctest entry #13 | Passed | `test-family-1.log` |

Tests executed: the ctest family ran 2 entries (`PluginPortsMigrationTest` with its 89 slots, and
`MonoDelayTest`), and `zeroCapacityDelayPassesInputThrough` was among the 86 passes.

The one failing slot is `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid`, which fails with
`SWH Decimator test fixture was not loaded` (`tests/src/plugins/PluginPortsMigrationTest.cpp:1597`). It
fails the same way when run alone (`test-ladspa-iso-1.log`, `ISO_EXIT=1`). The fixture
`decimator_1202.so` (the SWH LADSPA set) is not installed on this machine: it is not in `/usr/lib/ladspa`
or `/usr/lib64/ladspa`, and `swh-plugins` is not in pacman. The slot does not touch MonoDelay, so the
failure is environmental and not caused by this change.

Gates were run one command at a time. `feature_check.py` could not run here: it creates
`<worktree>-feature-check/` in the parent `projects/zene-studio-program/` folder, which is outside the
write-jail, and the mkdir was denied. Each gate command the script runs was executed from the worktree with
its log in the LOG folder:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-1.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-1.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-1.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace-1.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-1.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-1.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-1.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-1.log` |

The two ratchet gates exit 1 on the train by design. Their 6 complexity and 22 file-length `REGRESSION:`
lines all name other files. None names `PluginPortsMigrationTest.cpp` or `zeroCapacityDelayPassesInputThrough`,
so under the script's rule (a regression counts only when it is in a changed file) neither row fails for
this cluster.

## Files committed

- `tests/src/plugins/PluginPortsMigrationTest.cpp` (new slot: declaration and body)
- `docs/qwen-fixes-2026-10/cluster-new-p10-263.md` (this report)

No product file changed. `plugins/Flanger/MonoDelay.cpp` matches `HEAD` after the RED/GREEN cycle.

## Notes not in scope

- Not investigated: `setSampleRate` with a negative rate. It deletes `m_buffer` and then `new sample_t[...]`
  with a negative count, which can throw and leave `m_buffer` dangling for the destructor. I read this in
  `plugins/Flanger/MonoDelay.cpp:74-92` but did not run it. The app never passes a negative rate
  (`FlangerEffect.cpp:139-140`), so it is outside this finding.

## Self-check

`submit_check.py <worktree> new-p10-263` (log `submit-check-1.log`, EXIT 1 before this section existed):
PASS commits on the branch, PASS working tree clean, PASS report present, PASS tests changed.
**FAIL `gates (feature_check.py)`**: this is a lane-environment block, not a code failure. `feature_check.py`
creates `<worktree>-feature-check/` next to the worktree, in `projects/zene-studio-program/`, which the lane
cannot write (`PermissionError: [Errno 13]`, traceback in the same log). A scratch detached worktree
under `$TMPDIR` to host the logs was refused by the lane guard (`bunny-guard`: `git worktree add` not allowed
in a Bunny lane); no worktree was kept (the empty scratch folder was removed). Substitute evidence: every gate
command that `feature_check.py` runs was executed from the worktree with its log in the LOG folder (table
above). The six pass-gates exit 0; the two ratchets exit 1 with no regression line naming a changed file.
The row needs a run of `feature_check.py` from an allowed location to turn green.

Answers to the "?" items:

1. **RED/GREEN.** The product hunk of `40da59feea` was reversed with `git apply -R`, the test built and run:
   EXIT 136 (SIGFPE, `FPE_INTDIV`, `test-red-1.log`). Restored with `git apply` (EXIT 0), `MonoDelay.cpp`
   matches `HEAD`, and the test passed (EXIT 0, `test-green-1.log`). The product patch is the `40da59feea` hunk,
   not a diff of my own change, because I changed no product file.
2. **Deterministic.** The test constructs `MonoDelay` with fixed integers, ticks fixed sample values and
   compares them exactly. It has no clock, sleep, thread or random input, so no seam is needed.
3. **At the cause, complete.** The cause is the unguarded `tick()` modulo. The guard at
   `plugins/Flanger/MonoDelay.cpp:59` covers it, and the invariant it relies on holds on every write path:
   constructor `:36-37`, `setSampleRate` `:79-88`, `setLength` `plugins/Flanger/MonoDelay.h:41-43`. The callers
   `plugins/Flanger/FlangerEffect.cpp:65-66` and `:139-140` construct and reconfigure with `maxTime = 1`. No
   product code was changed, so no sibling needed a matching change.
4. **No collateral.** No product code changed; the test adds one slot that runs in the existing
   `PluginPortsMigrationTest` entry. The family ran 89 slots: 86 passed, 2 skipped, and 1 failed
   (`nonRealtimeLadspaEffectsAreInvalid`). That failure is environmental: the `decimator_1202.so` LADSPA fixture
   is not installed, and it fails when run alone (`test-ladspa-iso-1.log`). `MonoDelayTest` passed.
5. **ALREADY-FIXED evidence.** The guard `plugins/Flanger/MonoDelay.cpp:59` is in `HEAD` from commit
   `40da59feea`, and the invariant that zero capacity implies zero length is at `:36-37`, `:79-88` and
   `plugins/Flanger/MonoDelay.h:41-43`. The RED run shows the same `tick()` without the guard does divide by zero.
