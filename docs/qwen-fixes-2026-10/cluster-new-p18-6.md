Verdict: FALSE-POSITIVE

new-p18-6: FALSE-POSITIVE. The finding assumes `mix_ch_t` is signed. It is `std::uint16_t`, so a `-1` assignment is stored as `65535`, and the existing guard already rejects it. No product code changed.

## Evidence

- **`mix_ch_t` is unsigned.** `include/LmmsTypes.h:47`: `using mix_ch_t = std::uint16_t;`. `include/TrackFolder.h:163-165` states the same thing about the type and why a signed sentinel is kept as `int` elsewhere.
- **The "-1" sentinel is stored as 65535.** `include/AudioBusHandle.h:72-76` (`nextMixerChannel()` / `setNextMixerChannel(const mix_ch_t)`). The comment there says "-1 = none"; the value that reaches the field is `static_cast<mix_ch_t>(-1) == 65535`.
- **The guard rejects it.** `src/core/ControlCommandsPdc.cpp:118-121`: `assigned < static_cast<mix_ch_t>(mixer->numChannels())`. `Mixer::numChannels()` returns `mix_ch_t` (`include/Mixer.h:802-805`), so the right-hand side is at most 65535. `65535 < n` is never true for `n <= 65535`. The handle therefore takes the `nullptr` branch and reports `channelIdOf(nullptr)`, the empty id, and `mixerChannel(...)` is never called.
- **Type probe.** `probe-1.log` (standalone `g++ -std=c++20 -Wall -Wextra`, COMPILE_EXIT=0, RUN_EXIT=0) prints `is_unsigned=1 assigned=65535 guard=0`. This reproduces the type semantics only, not the Zene code path.
- **Sibling readers use the same unsigned guard.** `src/core/Mixer.cpp:2322-2323` and `src/core/Mixer.cpp:2388-2389` (`channel < count`) compare the same `nextMixerChannel()` value and index only after the check.
- **The suggested fix does nothing.** `assigned >= 0` on an unsigned type is always true. It adds no protection and may trigger `-Wtype-limits`. It was not applied.

## Adjacent observation (not this finding, not reproduced)

`Mixer::numChannels()` returns `m_mixerChannels.size()` truncated to `mix_ch_t` (`include/Mixer.h:802-805`). Channels are unbounded: `src/core/Mixer.cpp:1098` is a plain `push_back` with no cap. With 65536 or more channels, `numChannels()` wraps (65536 becomes 0), and the guards above would report valid channels as empty. I did not reproduce this, and the practical reach is low. A fix would change a type used by many callers, so it is left for a separate decision.

## Commands and results

No product file changed, so there is no RED/GREEN regression test. Step 2 of the procedure sends a not-real finding straight to the report.

| Evidence | Command | EXIT |
| --- | --- | --- |
| Type probe | `g++ -std=c++20 -Wall -Wextra -o $D/p18 $D/p18.cpp > logs/probe-1.log 2>&1; $D/p18 >> logs/probe-1.log` | 0 / 0 (`is_unsigned=1 assigned=65535 guard=0`) |
| Gate runner, as provided | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/001-feature-check.log 2>&1` | 1: `PermissionError` at `feature_check.py:71`. The script writes its logs to `<worktree>/../zene-qwen-new-p18-6-feature-check/`, which is outside the write-jail. |
| fork-sources | `bash tests/fork-sources-gate.sh` (logs/002) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (logs/003) | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (logs/004) | 0 |
| check-namespace | `tests/scripted/check-namespace` (logs/005) | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` (logs/006) | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` (logs/007) | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` (logs/008) | 1: 6 REGRESSION lines, none in a file this branch changes (0 matches for `ControlCommandsPdc` or `docs/`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` (logs/009) | 1: 22 REGRESSION lines, none in a file this branch changes |

The ratchet rows pass under `feature_check.py`'s own rule ("no item in a changed file"). This branch changes only `docs/qwen-fixes-2026-10/cluster-new-p18-6.md`, and no reported item names that file.

My first attempt at the gate loop ran under zsh, which does not word-split `$cmd`. Every gate returned 127 (command not found). Those logs were overwritten by the bash rerun, which produced the table above.

Affected-family ctest: not run. No test was added or changed, and no product file changed.

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p18-6.md` only.

## Self-check

- **RED/GREEN with the product change reverted.** Not applicable. There is no product change, because the verdict is FALSE-POSITIVE. The probe (logs/probe-1.log, EXIT 0) shows the type behaviour the finding depends on.
- **Deterministic test.** Not applicable. No test was added. The probe is a single-process program with fixed inputs.
- **Fix at the cause, callers and siblings checked.** Not applicable as a fix. The sibling guards are checked at `src/core/Mixer.cpp:2322-2323` and `2388-2389`, and they stay consistent with the PDC guard. The adjacent `numChannels()` truncation is recorded above and deliberately not changed.
- **No collateral damage.** No behaviour changed, so nothing could regress. Saved projects load as before.
- **Verdict evidence.** `include/LmmsTypes.h:47` (unsigned type); `src/core/ControlCommandsPdc.cpp:118-121` (guard); `include/Mixer.h:802-805` (`numChannels()` bound); `include/AudioBusHandle.h:72-76` (sentinel storage).
- **Gate runner (`submit_check.py` FAIL "gates (feature_check.py)").** `feature_check.py` exits 1 at `feature_check.py:71` with `PermissionError`. It writes its logs beside the worktree, outside the write-jail, so this row cannot pass from inside the worktree. The individual gates were run directly and pass, as listed in the table above (logs/002-009). The ratchet rows are red on the train by design and have no items in a file this branch changed.
