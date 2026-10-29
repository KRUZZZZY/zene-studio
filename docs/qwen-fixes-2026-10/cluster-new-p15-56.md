Verdict: FIXED — cluster new-p15-56, "Empty envelope fields reuse stale token data" (branch `bugs/qwen-new-p15-56`, base `7641e9493`).

## Evidence (pre-fix code)

The finding's line 0 had moved; the defect is at `src/core/DrumSynth.cpp:176-226` (`GetEnv`) and `:164-174` (`storeEnvelopeValue`) on the base.

- **Stale token.** `GetEnv` clears the token buffer `s` once, before parsing (`src/core/DrumSynth.cpp:185`). At each delimiter the base reset only the length `o` (`:192` for `,`, `:198` for ` `), not `s`. So an empty field is converted from the token read just before it. Example: `0,100 10, 20,100` — the empty level after `10,` is stored as `10`.
- **EOF not treated as failure.** `storeEnvelopeValue` checked `sscanf(token, "%f", ...) == 0` (`src/core/DrumSynth.cpp:170` on the base). For an empty string `sscanf` returns `EOF` (-1), so the check missed it and the destination envelope slot kept whatever an earlier parse left in the thread-local `envpts` (`src/core/DrumSynth.cpp:81`).
- **Sibling convention.** The other `sscanf` numeric parsers in the same file already require exactly one conversion: `src/core/DrumSynth.cpp:357` (`!= 1`, integer) and `:373` (`!= 1` plus `isfinite`, float). The fix uses the same contract.
- **Call path.** `GetEnv` is called from `GetDSFileSamples` at `src/core/DrumSynth.cpp:478, 485, 508, 538, 539, 585, 599` (current numbering). This is the .ds load/render path the scoping brief names (`SampleBuffer::fromFile` → `decodeSampleDS` → `GetDSFileSamples`). I did not trace the thread that runs it; nothing new is added on any path either way.

## Fix

`src/core/DrumSynth.cpp` (5 insertions, 3 deletions: one condition changed, two `s[0] = 0;` lines added, the `storeEnvelopeValue` comment reworded):

- `:170` — `sscanf(...) == 0` → `sscanf(...) != 1`, so EOF (empty token) and any non-conversion store 0.
- `:193` and `:200` — `s[0] = 0;` after each `o = 0;` at the `,` and ` ` delimiters, so every field starts from an empty token.

Points past capacity (`point >= maxEnvelopePoints`), the overlong-token flag (`tokenTooLong`) and the `isfinite` check are unchanged.

## Regression tests

`tests/src/core/DrumSynthTest.cpp:440-488`, in the existing `DrumSynthTest` family (no new file, so `tests/fork-sources.txt` is unchanged):

- `emptyEnvelopeFieldIsZeroNotAStaleToken` (data rows `empty-level`: `0,100 10, 20,100` vs `0,100 10,0 20,100`; `empty-time`: `0,100 10,50 ,100 30,0` vs `0,100 10,50 0,100 30,0`). Renders each malformed file and its literal-zero twin and compares the samples.
- `emptyEnvelopeFieldIgnoresAnEarlierParse` — the EOF path. The envelope parse state is thread-local and survives one file, so an earlier file (`0,100 10,50 20,100`) leaves 50 in the slot that the empty level in the next file (`0,100 10, 20,100`) reaches. The test requires that render to equal the literal-zero render. Only the `!= 1` change makes it pass; clearing `s` alone would not.

Both tests are deterministic (no timing, sleeps or threads).

### Test matrix

| Path | Covered by | Status |
| --- | --- | --- |
| normal (valid envelopes, existing behaviour) | 16 existing `DrumSynthTest` cases, incl. `ignoresEnvelopePointsBeyondSupportedCapacity`, `rejectsNonfiniteEnvelopeLevel`; `DrumSynthLevelTest` 3; `SampleBufferResampleTest` 8 | pass |
| empty level field | `emptyEnvelopeFieldIsZeroNotAStaleToken(empty-level)` | RED → GREEN |
| empty time field | `emptyEnvelopeFieldIsZeroNotAStaleToken(empty-time)` | RED → GREEN |
| empty field after an earlier parse (EOF) | `emptyEnvelopeFieldIgnoresAnEarlierParse` | RED → GREEN |
| capacity / overlong / non-finite | existing tests (`ignoresEnvelopePointsBeyondSupportedCapacity`, `rejectsNonfiniteEnvelopeLevel`, `rejectsOverlongEnvelopeLevel`) | pass |
| audio-thread constraints | parse is load-time; the change adds no allocation, lock, log or Qt call | n/a |
| platform | `sscanf` and `!= 1` were already used at this site; standard C. Built and run on this Linux Qt6 tree only — not built on MSVC or Qt 5.15 here. | Linux only |

## Behaviour change (for the reviewer to weigh)

Only inputs with an empty envelope field change. Such a field now stores 0 (the documented contract of `storeEnvelopeValue`: malformed tokens become 0) instead of the previous token or a value kept from an earlier parse. Well-formed envelopes are byte-for-byte unchanged, and every existing test passes.

One consequence I checked by reading the code, not by rendering: an envelope with a trailing or doubled space (e.g. `0,100 10,50 `) ends with an empty field. On the base, that empty point reused the last level token (`50`); now it stores `0`. Such a line is not a well-formed envelope, and the finding's contract requires it to be read as 0, so I kept the fix as specified. If old saved projects are known to carry trailing spaces, this is the change to check first.

## Commands run and EXIT codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-56.r1/logs`.

| # | Command (run in the worktree or `build/tests` as noted) | Log | EXIT |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target DrumSynthTest -j4` | `build-1-red.log` | 2 (ccache could not write its temp file under `~/.local/share/zene-bunny-fix/dsh`, outside the jail; no compile output) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthTest -j4` | `build-2-red.log` | 0 |
| 3 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` (RED, unfixed code) | `test-1-red.log` | 8 — `Totals: 16 passed, 3 failed` (the three new cases fail: `emptyEnvelopeFieldIsZeroNotAStaleToken(empty-level)`, `(empty-time)`, `emptyEnvelopeFieldIgnoresAnEarlierParse`) |
| 4 | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest -j4` (after the fix) | `build-3-green.log` | 0 |
| 5 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` (GREEN) | `test-2-green.log` | 0 — 100% passed |
| 6 | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthLevelTest SampleBufferResampleTest -j4` | `build-4-family.log` | 0 |
| 7 | `cd build/tests && ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure` | `test-3-family-green.log` | 0 — 3/3 passed |
| 8 | Proof, step 4b: `git diff -- src/core/DrumSynth.cpp > .../logs/fix.patch` | — | 0 |
| 9 | `git apply -R .../logs/fix.patch` (product fix removed) | — | 0 |
| 10 | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest -j4` | `build-5-proof-unfixed.log` | 0 |
| 11 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` (fix removed) | `test-5-proof-unfixed.log` | 8 — `Totals: 16 passed, 3 failed` (same three cases) |
| 12 | `git apply .../logs/fix.patch` (fix restored) | — | 0 |
| 13 | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest -j4` | `build-6-proof-fixed.log` | 0 |
| 14 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` (fix restored) | `test-6-proof-fixed.log` | 0 — 100% passed |
| 15 | `cd build/tests && ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' -V` (final tree) | `test-7-final-family-verbose.log` | 0 — QTest totals: DrumSynthTest 19 passed / 0 failed; DrumSynthLevelTest 3 / 0; SampleBufferResampleTest 8 / 0 |

Affected-family test count: 30 QTest cases executed (19 + 3 + 8), zero failures.

Proof of the regression: step 11 (fix removed) fails exactly the three new cases; step 14 (fix restored) passes all 19 `DrumSynthTest` cases.

### Gates

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` — EXIT 1, but only because the script could not create its log directory `<worktree>/../zene-qwen-new-p15-56-feature-check` (`PermissionError`, `logs/gate-1-feature-check.log`). No gate ran.
- The same gate was then run unmodified except for two path lines: `python3 -I logs/feature_check_logdir.py <worktree>`, where the copy differs from the original only in `ROOT` (kept pointing at the qwen directory, so the same `gate-baselines` cache is used) and in the log directory (moved into `logs/feature-check`). The gate logic, criteria and baselines are untouched. Result: EXIT 0, `RESULT: PASS` — fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS (`logs/gate-2-feature-check.log`).

## Files committed

- `src/core/DrumSynth.cpp` — the fix (see above).
- `tests/src/core/DrumSynthTest.cpp` — the two regression tests.
- `tests/upstream-modifications.txt` — one line registering the `src/core/DrumSynth.cpp` change for cluster new-p15-56 (the file is inherited and already listed there for earlier clusters).
- `docs/qwen-fixes-2026-10/cluster-new-p15-56.md` — this report.

## Self-check

Evidence for each item of `submit_check.py`'s list (`logs/submit-check-1.log`).

1. **The regression fails with the product change reverted and passes with it.** RED: `test-1-red.log`, `ctest -R '^DrumSynthTest$'` EXIT 8, 3 failed. Revert proof: `test-5-proof-unfixed.log` EXIT 8 (same three cases), and after `git apply` restore `test-6-proof-fixed.log` EXIT 0 (19/19). The family run `test-7-final-family-verbose.log` is EXIT 0 with 30/30.
2. **Deterministic.** The new tests use fixed fixture text in a `QTemporaryDir`, with no timers, sleeps, threads or races. The EOF case (`emptyEnvelopeFieldIgnoresAnEarlierParse`) depends on the thread-local `envpts` left by an earlier render in the same thread. QTest runs its cases sequentially on the main thread, so that ordering is fixed.
3. **At the cause, every caller and sibling checked.** `GetEnv` has seven callers (`src/core/DrumSynth.cpp:478, 485, 508, 538, 539, 585, 599`), all through the one function, so one fix covers them. `storeEnvelopeValue` has two delimiter callers (`:191`, `:198`) and the final store (`:222`), which reads `s` after the last delimiter; with `s` cleared at each delimiter that final read is also correct. The other numeric `sscanf` sites (`:357`, `:373`) already use `!= 1`. No header is changed.
4. **No collateral damage.** Well-formed envelopes are unchanged: all 16 pre-existing `DrumSynthTest` cases, the 3 `DrumSynthLevelTest` cases and the 8 `SampleBufferResampleTest` cases pass. The only behaviour change is for empty fields, as described above. The trailing-space case is reasoned from the code, not rendered. No new limit or check is added, so nothing else can hit one.
5. **Verdict FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE evidence clause does not apply.

### Gate status — BLOCKED by the environment, not by the change

- `submit_check.py` runs `feature_check.py` directly. That script writes its logs to `<worktree>/../zene-qwen-new-p15-56-feature-check`, which is outside the write-jail. It fails with `PermissionError` before any gate runs (`logs/gate-1-feature-check.log`). `submit_check` therefore reports `FAIL gates` with no gate-level FAIL lines, which is the same environmental failure, not a gate result.
- Running the same gate with only its log directory redirected (`logs/feature_check_logdir.py`, the copy described above) gives `RESULT: PASS`, EXIT 0, and every row PASS (`logs/gate-2-feature-check.log`). The gate logic is unchanged. Whoever owns the loop should run `feature_check.py` from outside the jail to confirm the gate result independently.
