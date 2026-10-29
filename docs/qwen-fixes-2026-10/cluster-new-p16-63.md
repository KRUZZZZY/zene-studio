FIXED

# Cluster new-p16-63: a DrumSynth envelope can trigger a huge allocation

## Finding

Luna BUG 5 (historical `src/core/DrumSynth.cpp:146`): an envelope time such as `9999999` is not bounded, so at 192 kHz and maximum stretch the render asks for hundreds of millions of frames.

## Evidence before the fix

- The overflow and `bad_alloc` side was already handled on this branch: `checkedWaveSampleCount` (`src/core/DrumSynth.cpp:51-65`) rejects overflowing sizes, `LongestEnv` saturates to `0` past `int` range (`src/core/DrumSynth.cpp:116-121`), and the buffer is `new (std::nothrow)` (`src/core/DrumSynth.cpp:650`). The existing `refusesAnEnvelopeWhoseRoundedLengthOverflows` test (3e9 envelope) covers only the int-overflow case.
- The unbounded case was still open. A duration that fits in `int` (for example `Envelope=0,100 500000,0` at `Stretch=1000`, 44.1 kHz) got past every check and was allocated and rendered in full. The RED run below shows the renderer returning about 5M frames for it.
- The caller `decodeSampleDS` (`src/core/SampleDecoder.cpp:192-193`) then makes `std::vector<SampleFrame>(frames)` and computes `frames * DEFAULT_CHANNELS` as `int`. Both are reached only for in-range lengths, so the bound below also keeps this caller in range.
- The renderer's own longest hold is `442000` samples at 44.1 kHz (the `NEXTT` sentinel in `UpdateEnv`). The longest envelope in the shipped library is `438020`, in `data/samples/drumsynth/misc/verylong.ds` (measured over all 764 files under `data/samples/drumsynth`; the largest stretch is clamped to 10).

## The fix

`src/core/DrumSynth.cpp`:
- `maxEnvelopeHold = 442000.f` and `maxStretch = 10.f` as named constants (`:94`, `:96`). `UpdateEnv` (`:152`) and the stretch clamp (`:465-467`) now use them; the values are unchanged.
- `GetDSFileSamples` refuses a render longer than `maxEnvelopeHold * maxStretch * (Fs / 44100)` plus `LongestEnv`'s 2400-sample base (`:643`), before any buffer is sized. The refusal returns `0`, the same contract the existing overflow refusal uses, so `SampleDecoder` gets no samples and the import fails cleanly.
- Bound at the largest stretch, so it does not depend on a file's own `Stretch`. At 192 kHz the cap is about 19.2M frames, which keeps `frames * DEFAULT_CHANNELS` well inside `int`.

No other behaviour changes: every shipped sample renders (guard test below), and no envelope inside the hold changes.

## Tests

`tests/src/core/DrumSynthTest.cpp`:
- `refusesAnEnvelopeLongerThanTheRendererHold` (`:474`): `Stretch=1000`, `Envelope=0,100 500000,0`, 44.1 kHz, 1 channel; expects `frames == 0` and `samples == nullptr`.
- `rendersTheLongestShippedEnvelopeAtMaximumStretch` (`:497`): the same with `438020`; expects a non-empty render.

`tests/upstream-modifications.txt`: one line for the inherited `src/core/DrumSynth.cpp`, reason `new-p16-63`.

No new files, so `tests/fork-sources.txt` is unchanged. No control command was added.

## Commands and exit codes

All ctest runs from `build/tests`. `CCACHE_DISABLE=1` was needed because the sandbox ccache temp directory is not writable (first build attempt: `build-1-red.log`, EXIT=2, `ccache: error: failed to create temporary file ... Permission denied`); it changes no product code.

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| RED build | `cmake --build build --target DrumSynthTest -j4` | 2 | ccache permission error only (`build-1-red.log`) |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target DrumSynthTest -j4` | 0 | `build-2-red.log` |
| RED | `ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | `FAIL! : DrumSynthTest::refusesAnEnvelopeLongerThanTheRendererHold() Compared values are not the same`; 17 passed, 1 failed (`test-1-red.log`) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4` | 0 | `build-3-green.log` |
| GREEN | `ctest -R '^(DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest)$' --output-on-failure` | **0** | 3/3 passed (`test-2-green.log`) |
| GREEN count | `ctest -V -R '^DrumSynthTest$'` | 0 | `Totals: 18 passed, 0 failed` (`test-5-count-drumsynth.log`) |
| 4b: patch | `git diff -- src/core/DrumSynth.cpp > fix.patch` | 0 | saved in the lane log folder |
| 4b: unfix | `git apply -R fix.patch` | 0 | |
| 4b: unfixed build | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest -j4` | 0 | `build-4-unfixed.log` |
| 4b: unfixed run | `ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | same `FAIL!` line, 17 passed 1 failed (`test-3-4b-unfixed.log`) |
| 4b: refix | `git apply fix.patch` | 0 | |
| 4b: fixed build | `CCACHE_DISABLE=1 cmake --build build --target zene DrumSynthTest -j4` | 0 | `build-5-fixed-again.log` |
| 4b: fixed run | `ctest -R '^DrumSynthTest$' --output-on-failure` | **0** | 100% passed (`test-4-4b-fixed.log`) |
| Collateral build | `CCACHE_DISABLE=1 cmake --build build --target ImportDetectionTest -j4` | 0 | `build-6-importdetection.log` |
| Collateral | `ctest -V -R '^ImportDetectionTest$'` | 0 | `Totals: 15 passed, 0 failed` (`test-6-importdetection.log`) |

Affected family, all passing: `DrumSynthTest` (18 functions), `DrumSynthLevelTest`, `SampleBufferResampleTest`, plus `ImportDetectionTest` (15), which calls `GetDSFileSamples` on a shipped `.ds`.

RED is the test as written before the fix, and the test's own assertion fails. The 4b step reverses only `src/core/DrumSynth.cpp` and gives the same failure.

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: **EXIT=1, did not run.** It fails with `PermissionError` when it creates `<worktree>-feature-check/` (`gate-1-feature-check.log`). That directory sits in `zene-studio-program/`, outside the write-jail, so I did not redirect it. Instead I ran each command the script runs, from the worktree, with logs in the lane folder:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (raw) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (raw) |

The two ratchets are red on the train on purpose. The wrapper fails only on a `REGRESSION:` line in a file this branch changed. The changed files are `src/core/DrumSynth.cpp`, `tests/src/core/DrumSynthTest.cpp` and `tests/upstream-modifications.txt`. The 7 complexity and many file-length `REGRESSION:` lines name other files (`Vst3Host.cpp`, `TakeLane.cpp`, `WasmSandbox.cpp`, `CollabTransportTest.cpp`, ...). None names `DrumSynth` (`gate-8-complexity.log`, `gate-9-file-length.log`). So the wrapper's own rule yields no new item in my files. I did not reproduce the baseline comparison itself, so the claim rests on that reading of the raw output.

## Self-check

`submit_check.py` (`submit-check-1.log`, EXIT=1): PASS commits on the branch, PASS working tree clean, PASS report present, PASS tests changed (`DrumSynthTest.cpp`). **FAIL gates**: it runs `feature_check.py`, which exits with the `PermissionError` above. This is the environment limit described under Gates, not a failing check: every gate command passes except the two ratchets, and those are red on the train by design with no item in a changed file. Blocked on the wrapper's log location (`<worktree>-feature-check/`, outside the write-jail). Nothing else was changed to get past it.

1. **RED/GREEN, 4b.** RED with the product fix absent: `test-1-red.log`, `ctest` EXIT=8, `refusesAnEnvelopeLongerThanTheRendererHold` fails with "Compared values are not the same". GREEN with the fix: `test-2-green.log`, EXIT=0, 3/3 family tests pass. 4b: the diff reverted gives EXIT=8 (`test-3-4b-unfixed.log`); reapplied gives EXIT=0 (`test-4-4b-fixed.log`).
2. **Deterministic.** No wall-clock, sleep or thread. The inputs are `.ds` files written into a `QTemporaryDir` with fixed `Stretch`/`Envelope` values; the assertions are on the returned frame count and the null pointer, and the guard test asserts only that the render is non-empty. Nothing depends on timing.
3. **Fix at the cause; callers and siblings.** The cause is the unbounded render duration in `GetDSFileSamples` (`src/core/DrumSynth.cpp:643`). Callers of `GetDSFileSamples`: `SampleDecoder.cpp:185-193` (`decodeSampleDS`, engine rate, 2 channels), `ImportDetectionTest.cpp:104,265,292`, and the DrumSynth family tests. Siblings checked: the `UpdateEnv` hold literal and the stretch clamp now use the same named constants (`:152`, `:465-467`), with unchanged values. `LongestEnv` is unchanged; its boundary test still passes. The downstream sibling, `decodeSampleDS`'s `std::vector<SampleFrame>(frames)` and `frames * DEFAULT_CHANNELS` (`SampleDecoder.cpp:192-193`), is bounded by the new cap. The cap is about 19.2M frames at 192 kHz (about 38.5M at 384 kHz), so `frames * 2` stays inside `int`. `SampleDecoder.cpp` is not changed.
4. **No collateral damage.** Shipped samples: the largest envelope in all 764 `.ds` files is 438020 (`misc/verylong.ds`), under the 442000 hold, and the guard test renders it at maximum stretch. `ImportDetectionTest` (which renders a shipped `.ds` and the negative-bit corrupt case) passes 15/15. **One behaviour change, for the owner to know:** a `.ds` whose envelope time, multiplied by its stretch, exceeds 442000 samples at 44.1 kHz (for example `9999999`) now renders nothing, where the old code rendered it in full. That is the bug. Saved projects that reference such a file would load without that sample. No shipped or known sample is affected. The refusal reuses the existing overflow contract (return 0, no samples).
5. **Verdict.** FIXED, so ALREADY-FIXED and FALSE-POSITIVE do not apply. The pre-existing checks that the finding did not mention (`checkedWaveSampleCount`, `nothrow` allocation, and the int-overflow test) were checked and are kept as they were.
