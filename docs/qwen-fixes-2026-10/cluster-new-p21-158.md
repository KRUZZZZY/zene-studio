FALSE-POSITIVE: `FilterHolder::reset()` leaves `description` set, but nothing dereferences it, so the reported dangling-pointer harm does not occur. The state the finding describes is reachable, but the affected block is silent.

Cluster: new-p21-158 (Luna patrol: "reset() leaves a dangling description pointer", FilterPool.cpp:18). Branch `bugs/qwen-new-p21-158`, starting HEAD `cf3618e61`. No product file is changed.

## Verdict

The finding's mechanism is "a later process() before a new setup() dereferences the stale `description`". The code does not do that:

- `description` is dereferenced only inside `setup()`, and only after `setup()` assigns it.
- `process()` reads `description` only in a null check.
- The state the finding describes (a reset holder that is processed before setup) is reachable through one early return. In that state the block is all zeros, so no output is affected.

The proposed fix (`description = nullptr;` in `reset()`) would therefore not change any observable behaviour. It is not applied.

## Evidence (file:line on `cf3618e61`)

1. `reset()` clears only the filter and `prepared`: `plugins/SfzPlayer/sfizz/src/sfizz/FilterPool.cpp:18-22`.
2. `setup()` assigns the pointer first: `FilterPool.cpp:29`. Every dereference of `description` in the file is inside `setup()`, after that assignment: `FilterPool.cpp:30, 34, 35, 36, 39, 43, 51, 52`.
3. `process()` uses `description` only as a null test: `FilterPool.cpp:68-72`. Its body (`FilterPool.cpp:74-110`) never reads `*description`. A non-null stale value only selects the filtered branch.
4. Normal path cannot reach `process()` after `reset()`:
   - `Voice::reset()` sets the state to idle, clears `region_`, and resets every holder: `plugins/SfzPlayer/sfizz/src/sfizz/Voice.cpp:1700-1726` (idle at :1703, `region_ = nullptr` at :1705, holder reset at :1719-1720).
   - `renderBlock()` returns when `region_` is null: `Voice.cpp:782-784`.
   - The synth skips idle voices: `Synth.cpp:1170` (`isFree()` is `state_ == idle`, `Voice.cpp:569-573`).
5. The reachable path is `Voice::startVoice()` returning early with the state at `cleanMeUp` after `region_` is set (`Voice.cpp:419`) and before the setup loop (`Voice.cpp:510-512`):
   - Early return at `Voice.cpp:480-484` when the file promise is missing (`cleanMeUp` at :483).
   - `Synth.cpp:1313-1315` calls `reset()` and then `startVoice()`. A voice that returns false is not idle, so `Synth.cpp:1170` renders it.
   - `renderBlock()` then runs `filterStageMono/Stereo` (`Voice.cpp:798-804`), which calls `filters_[i].process()` (`Voice.cpp:1039-1040`, `1058-1059`) on holders that were reset and never set up.
6. The block is silent in that path:
   - `renderBlock()` zero-fills the buffer first (`Voice.cpp:778`).
   - `fillWithData()` returns early when `currentPromise_` is null (`Voice.cpp:1073-1075`), so the input to the filter stage is all zeros.
   - A cleared filter with zero input outputs zero. The `description == nullptr` branch copies zeros as well. This is reasoning from the code, not a measured result.

## Latent issue found while checking (not changed here)

`FilterPool.h:47` declares `const FilterDescription* description;` with no initializer, and the constructor (`FilterPool.cpp:11-16`) does not set it. `process()` reads it at `FilterPool.cpp:68` before any `setup()` on a fresh holder. A fresh holder is created by `Voice::setMaxFiltersPerVoice()` (`Voice.cpp:1802-1811`, called from `Synth.cpp:1995`). If the next `startVoice()` returns early as in step 5, `process()` reads an indeterminate pointer. That is undefined behaviour, though its output is still silent because the input is zero. It is a different defect from the one reported. A deterministic regression test is not possible without a sanitizer, so it is not fixed in this cluster. Suggested follow-up: initialise `description` to `nullptr` in the header, verified under ASan or valgrind.

## Commands run and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-158.r1/logs/`.

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` (PermissionError: the script writes `<worktree>/../<name>-feature-check/`, outside the write-jail) |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log` (7 REGRESSION lines, all in product files) |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log` (19 REGRESSION lines, all in product files) |

The feature_check.py table could not be produced. The eight gate commands above are the same ones it runs, run from the worktree. Complexity and file-length are red on the train by design. Their REGRESSION items cannot be in a file this branch changes, because the branch changes no product file (the only change is this report).

## RED / GREEN and test count

- RED: not applicable. No product code changed, so there is no failing regression to show.
- GREEN: not applicable, for the same reason.
- Affected-family tests executed: 0. No test was run because no code changed.
- No regression test is added. A claim-only closure has no test to add, and an independent reviewer should check the cited lines.

## Self-check

- **Test fails with the change reverted, and passes with it:** not applicable. There is no product change to revert.
- **Acceptance criteria proven by a test:** the criterion is "the reported dangling dereference is real". It is answered by the code citations above, not by a test.
- **Undo, realtime, old-project loading, portability:** no code is changed, so none of these apply. The audio path (`Voice.cpp:776-804`, `FilterPool.cpp:63-111`) is read but not modified.
- **Gates:** feature_check.py cannot run in this write-jail (EXIT 1, PermissionError at its own log directory). The eight gate commands were run individually, with the EXIT codes above. Six pass. Complexity and file-length are red by design and have no new item in a changed file.
- **Submit check:** see the completion check result recorded after this commit.
