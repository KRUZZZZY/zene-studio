FALSE-POSITIVE

new-p10-106: FALSE-POSITIVE

The finding's trigger, "enable a fresh tap before the engine has a nonzero period", cannot occur. The engine's frames-per-period is never 0, and it is never changed after construction. No product file was changed.

## Evidence

- **The only enabling caller passes the engine period.** The `plugin.delta` command calls `effect->deltaTap().setEnabled(on, Engine::audioEngine()->framesPerPeriod())` at `src/core/ControlCommandsDeviceParams.cpp:390`. The device menu's "Hear Only What It Changes" entry drives that command at `src/gui/EffectDeviceMenu.cpp:137-141`. No other production code calls `DeltaTap::setEnabled`.
- **The period is clamped to 32-256 at construction.** `AudioEngine` sets `m_framesPerPeriod(std::min(m_framesPerAudioBuffer, DEFAULT_BUFFER_SIZE))` at `src/core/AudioEngine.cpp:479`. `m_framesPerAudioBuffer` is clamped to `[MINIMUM_BUFFER_SIZE, MAXIMUM_BUFFER_SIZE]` at `src/core/AudioEngine.cpp:475-478`, where `MINIMUM_BUFFER_SIZE = 32` (`include/AudioEngine.h:74`), `DEFAULT_BUFFER_SIZE = 256` (`:75`) and `MAXIMUM_BUFFER_SIZE = 4096` (`:76`). The result is always in [32, 256].
- **Nothing reassigns the period.** `m_framesPerPeriod` (`include/AudioEngine.h:744`) has no assignment outside its constructor initialiser. A search of `include/` and `src/` for `m_framesPerPeriod\s*=` finds none. So the "period grew since" pass-through documented at `include/DeltaTap.h:49` cannot arise either.
- **The test suite observes the nonzero period at runtime.** `tests/src/core/PatcherCommandsTest.cpp:211` asserts `framesPerPeriod() == 256` during setup, and the test passes (see the evidence table).
- **The function does accept 0, which is the latent part.** `DeltaTap::setEnabled` at `src/core/DeltaTap.cpp:40-44`, by reading: with `frames == 0`, `needed` is 0, so `m_dry.size() >= needed` holds and the tap is switched on without allocating. `DeltaTap::process` at `src/core/DeltaTap.cpp:57` then passes the block through unchanged. The header contract, "on with room for @a frames-frame blocks" (`include/DeltaTap.h:53`), does not say what a zero argument does. I read this path; I did not run it, because no shipped caller reaches it.
- **Not changed, deliberately.** A guard such as `if (on && frames == 0)` leaving the tap off would change no behaviour a user can reach. The brief asks for a not-real verdict to be reported rather than an invented change. If the owner wants the guard as hardening, it is a small follow-up; I have not made it.

## Test matrix (written before concluding; no product edit)

| Path | Coverage |
| --- | --- |
| Normal: enable at the real period (256) and process a unit block | `theAudioBufferPathAppliesTheDeltaTap` (existing), PASS |
| Zero-frame enable | Not reachable from any caller; no test added, since a test would pin a call no shipped code makes |
| Exhaustion / reconfiguration (period change) | Not reachable: the period is constant after construction (above) |
| Audio-thread constraints | No product change; nothing new on the audio thread |
| Platform build | The test target builds on this Linux configuration (see below) |

Because no product code changed, step 4b (reverse-apply the fix and show the RED) and the RED/GREEN pair do not apply.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-106.r1/logs`

| Evidence | Command | Result |
| --- | --- | --- |
| Build the test target, first try | `cmake --build build --target PatcherCommandsTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | EXIT=2. ccache could not create its temp file under `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` ("Permission denied"). This is the environment, not the code. |
| Build the test target, retry with ccache off | `CCACHE_DISABLE=1 cmake --build build --target PatcherCommandsTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | EXIT=0, `Built target PatcherCommandsTest` (run from the worktree) |
| Family via ctest | `ctest -R '^PatcherCommandsTest$' --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; 100% tests passed, 1 of 1 |
| Per-function run | `./PatcherCommandsTest > .../test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) | EXIT=0; Totals 14 passed, 0 failed; `PASS: PatcherCommandsTest::theAudioBufferPathAppliesTheDeltaTap()` |
| Feature gate script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../feature-check-1.log 2>&1` | EXIT=1, with a traceback at `mkdir` for `<worktree>-feature-check` beside the worktree. My write-jail does not allow that folder (PermissionError), so the script ran no gate. |
| Gate substitute 1: fork-sources | `bash tests/fork-sources-gate.sh` (in the worktree) > `.../gate-fork-sources.log` | EXIT=0 |
| Gate substitute 2: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` > `.../gate-all-sources-reproduce.log` | EXIT=0 |
| Gate substitute 3: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` > `.../gate-no-upstream-regression.log` | EXIT=0 |
| Gate substitute 4: check-namespace | `tests/scripted/check-namespace` > `.../gate-check-namespace.log` | EXIT=0 |
| Gate substitute 5: no-tautology | `bash tests/no-tautology-gate.sh` > `.../gate-no-tautology.log` | EXIT=0 |
| Gate substitute 6: doc-figures | `python3 tests/doc-figures-gate.py` > `.../gate-doc-figures.log` | EXIT=0 |
| Ratchet: complexity | `bash tests/complexity-gate.sh --check` > `.../gate-complexity.log` | EXIT=1; 6 REGRESSION lines, none in a file this change touches |
| Ratchet: file-length | `bash tests/file-length-gate.sh --check` > `.../gate-file-length.log` | EXIT=1; 22 REGRESSION lines, none in a file this change touches |

The two ratchets are red on the train, as the brief says. I applied the script's own rule to their logs: an item fails the gate only when its key is absent from the cached base listing (`gate-baselines/5c1c6715d9e9a1e65209b7cc8dd5d622819c1b8c.json`) and its file is in the changed set. The changed set is this doc only. The result was 0 items in changed files for each ratchet, so both pass by the script's rule.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p10-106.md` (this report). No product file was changed.

## Self-check

Pre-submit run: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p10-106 > .../submit-check-1.log 2>&1` → EXIT=1 before this commit. Items it lists and my answer:

- **Regression test fails with the change reverted and passes with it.** Not applicable: there is no product change to revert and no new test. The verdict is not-real, so there is no RED to show. The existing family passes (`ctest` EXIT=0, 1/1; binary EXIT=0, 14/14).
- **Deterministic (no timing, sleeps, races).** No new test was written. The existing `PatcherCommandsTest.cpp` has no sleep, wait or timer call: `rg -n "sleep|qWait|QElapsedTimer|QTimer|QThread" tests/src/core/PatcherCommandsTest.cpp` gives only a comment match at line 605.
- **Fix at the cause; callers and siblings checked.** No fix was made. The only caller of `setEnabled(true, …)` in shipped code is `src/core/ControlCommandsDeviceParams.cpp:390`, and it passes the engine period. Its sibling `DeltaTap::process` (both overloads, `src/core/DeltaTap.cpp:57` and `:68-69`) reads `enabled()` and the scratch size, so both stay consistent with the current callers.
- **No collateral damage.** No product file changed, so saved projects and existing behaviour are untouched. The existing test passes, including `theAudioBufferPathAppliesTheDeltaTap` (line 18 of the binary's output in `test-2.log`).
- **Verdict evidence (FALSE-POSITIVE).** The period is `std::min(clamp(framesPerAudioBuffer, 32, 4096), 256)` set once at `src/core/AudioEngine.cpp:475-479`, with `include/AudioEngine.h:74-76`. No other assignment to `m_framesPerPeriod` exists. The test asserts 256 at `tests/src/core/PatcherCommandsTest.cpp:211`.
- **Feature gate row: BLOCKED by the environment, not by the code.** `feature_check.py` (and so the `submit_check.py` gate row) needs to create `<worktree>-feature-check` in `zene-studio-program/`, which the sandbox refuses (`touch` in that folder: Permission denied). I ran the same six simple gates and both ratchets directly in the worktree (table above). Six simple gates EXIT=0. The two ratchets EXIT=1, by design, and pass under the script's rule.
