FIXED — the NAM loader refuses a `.nam` file over 16 MiB before `json::parse` reads it (cluster new-p14-196).

Finding: "NAM parsing can exhaust memory before the model-size limit is checked" (`plugins/NeuralAmp/nam/NamModelLoader.cpp`, historical line 0, ~80 by the patrol's estimate).

## Evidence before the fix

- `plugins/NeuralAmp/nam/NamModelLoader.cpp:124` (pre-fix tree, `root = json::parse(in);`) read the whole file with nothing before it. The only size check, `detail::validateModelResources` (`:341` pre-fix), runs after the parse, and it counts only declared weights (`plugins/NeuralAmp/NamModelValidation.h`, `validateModelResources`). Unknown fields, metadata and padding are never counted.
- `json::parse` is the only parser of `.nam` text in the plugin (`rg 'json::parse' plugins/NeuralAmp` returns only `NamModelLoader.cpp:140` after the fix). Every `loadFromFile` caller goes through this function: `plugins/NeuralAmp/NeuralAmpEffect.cpp:208` (worker thread, see the note in `plugins/NeuralAmp/nam/NamModel.h:78`), `plugins/NeuralAmp/tests/nam_harness.cpp:410,501`, `plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp:57`, `plugins/NeuralAmp/tests/rt_alloc_probe.cpp:119`, `plugins/NeuralAmp/tests/nam_profile.cpp:220`.

## How much memory the parser really uses

Measured with a probe outside the worktree (`/tmp/nam-probe/probe.cpp`, not committed): a global `operator new` counter around `nlohmann::json::parse(std::ifstream&)` on the bundled `plugins/NeuralAmp/rtneural/modules/json/json.hpp`, file read the same way the loader reads it.

| input shape | file bytes | peak heap | heap per input byte |
| --- | ---: | ---: | ---: |
| `[0,0,…]` numbers | 4,000,003 | 50,331,692 | 12.6 |
| float weights `-0.12345678,` | 8,400,014 | 25,165,880 | 3.0 |
| object members, unique 5-char keys | 10,000,009 | 80,000,160 | 8.0 |
| nested `[0],` | 4,000,005 | 56,777,312 | 14.2 |
| short strings `"a",` | 8,000,005 | 97,554,512 | 12.2 |
| objects in an array `{"a":0},` | 8,000,009 | 144,777,400 | 18.1 |
| empty objects `{},` | 3,000,004 | 68,971,616 | 23.0 |
| nested `[[0]],` | 6,000,007 | 96,777,368 | 16.1 |

The worst measured shape is 23 bytes of heap per input byte. The analytic bound is 32: an empty object costs a heap `std::map` (48 bytes plus allocator overhead) and a parent slot (16 bytes), and vector capacity slack doubles the slots. The cap is therefore `kMaxModelBytes / 32` = 512 MiB / 32 = **16,777,216 bytes (16 MiB)**, so the parser's tree stays inside the same 512 MiB budget the engine already has.

## The change

- `plugins/NeuralAmp/NamModelValidation.h:31-38` — `kParserBytesPerInputByte = 32` and `kMaxModelFileBytes = kMaxModelBytes / kParserBytesPerInputByte`, with the reasoning in the comment.
- `plugins/NeuralAmp/nam/NamModelLoader.cpp:121-135` — after the open, the stream's size is read with `seekg`/`tellg` (`:124-126`). A size of -1 (`:127-130`) or a file over `kMaxModelFileBytes` (`:131-135`) throws `ParseError` before `json::parse` (`:140`). The error reads "model file exceeds the parse size limit (16777216 bytes)".
- `tests/upstream-modifications.txt:2445` — registry line for the inherited loader file extended with this reason (same commit, as the rules require). `NamModelValidation.h` and `NamModelLoaderTest.cpp` are already registered in `tests/fork-sources.txt` (`:1212`, `:2396`).

Nothing else changes: the positivity, receptive-field, layer-count and engine-budget checks are untouched and still run after the parse, and nothing on the audio thread is touched (`loadFromFile` is worker-thread only).

## Regression test

`tests/src/core/NamModelLoaderTest.cpp` (existing ctest `NamModelLoaderTest`), three additions:

- `refusesOversizedFile` — a model that is otherwise valid, padded with an unused string field to exactly `kParseLimitBytes + 1` bytes, must be refused with an error containing "size limit". Before the fix the loader accepts it.
- `loadsFileAtLimit` — the same model padded to exactly `kParseLimitBytes` must still load (the bound is inclusive, and legitimate files at the limit keep working).
- `writeFixture` / `paddedModelJson` — helpers; the file size is checked after writing.

`kParseLimitBytes` is written as the literal `512 MiB / 32` in the test rather than included from the header, so the RED build compiles against the unfixed code and fails on behaviour, not on a missing symbol. The test uses no clock, no sleep and no thread; the only time-derived value is the pre-existing temp-directory name stamp, which only makes the name unique.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-196.r1/logs`.

ccache could not write its temp dir under `/run/user/1000`, so every build from the first attempt needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14`. The first, un-prefixed build (`build-1-red.log`) failed with `ccache: error: failed to create temporary file … Permission denied` and exit 2; the test that ran in that step was a stale binary and is not counted.

| step | command (cwd as noted) | log | EXIT |
| --- | --- | --- | --- |
| RED build | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target NamModelLoaderTest -j4` | build-2-red.log | 0 |
| RED test | `ctest -R '^NamModelLoaderTest$' --output-on-failure` (in `build/tests`) | test-2-red.log | **8** (fails: "loader accepted or misreported a model file one byte over the parse limit") |
| fix + GREEN build | same build command | build-3-green.log | 0 |
| GREEN test | same ctest command | test-3-green.log | **0** (100% passed, 1 of 1) |
| 4b: save product diff | `git diff -- plugins/NeuralAmp/nam/NamModelLoader.cpp plugins/NeuralAmp/NamModelValidation.h > …/logs/fix.patch` | — | 0 |
| 4b: revert | `git apply -R …/logs/fix.patch` | — | 0 |
| 4b: build reverted | build command | build-4-reverted.log | 0 |
| 4b: test reverted | ctest command | test-4-reverted-red.log | **8** |
| 4b: re-apply | `git apply …/logs/fix.patch` | — | 0 |
| 4b: build re-applied | build command | build-5-reapplied.log | 0 |
| 4b: test re-applied | ctest command | test-5-reapplied-green.log | **0** |

Affected family: `ctest -N -R 'Nam|NeuralAmp|Neural'` lists 20 tests (`ctest-list.log`). The first run (`test-6-family.log`, EXIT 8) had 19 failing only because the binaries were not built in this tree (`Could not find executable …`, and `nam_harness` is `EXCLUDE_FROM_ALL`). Built `NamModelMonoOutputTest NamModelPrewarmTest NeuralAmpModelLoadTest NeuralAmpWavInputTest NeuralAmpProfileArgumentsTest NeuralAmpAllocProbeArgumentsTest nam_harness` (`build-6-family.log`, EXIT 0) and then `NamespaceRegistryTest TrackKeyNamesLoadTest` (`build-8-unrelated.log`, EXIT 0; neither source mentions the NAM loader). Final run `test-8-family-final.log`: **EXIT 0, 100% tests passed, 20 of 20**, including all eight `NamLoaderTest_*` malformed-model tests (`valid`, `dilation-zero`, `dilation-negative`, `dilation-overflow`, `receptive-total-overflow`, `channels-zero`, `bottleneck-zero`, `head-size-zero`).

Product binary: `env CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target zene -j4` → `build-7-zene.log`, EXIT 0.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → `gate-1-feature-check.log`, **EXIT 1**. It stops before any gate runs: it tries `mkdir` on `<worktree>/../zene-qwen-new-p14-196-feature-check`, which is outside the write jail (PermissionError). The script was not edited. Its eight commands were instead run from the worktree by `/tmp/nam-p14-gates.sh`, which mirrors `SIMPLE` and `RATCHET` in that script, with output in `gate-2-<name>.log`:

| gate | command | EXIT | note |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | |
| check-namespace | `tests/scripted/check-namespace` | 0 | |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 5 REGRESSION lines, all in other files (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`); none in the three changed files. The script's rule ("fail only on items new in files you changed") is therefore satisfied. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 15 REGRESSION lines, none in the changed files (`grep -c NamModel` on both ratchet logs: 0). `NamModelLoader.cpp` is now 495 lines (was 479), under 500. |

So every gate row is PASS under the script's rules, but the wrapper itself could not run in this sandbox; the table above is the mirror run, not the script's output.

Completion check: `python3 …/submit_check.py <worktree> new-p14-196` → `submit-check-1.log` (before this commit: nothing committed, report missing, gate row failing for the jail reason above). Final run on the committed branch → `submit-check-2.log`, **EXIT 1**: commits, clean working tree, report and tests-changed rows PASS; the only FAIL is the gate row, because `feature_check.py` cannot create its log folder outside the write jail. The gate evidence is the mirror run above.

## Decision for the owner

This is a behaviour change: a `.nam` file over 16 MiB is now refused, where it was accepted up to the 512 MiB engine budget before. `find / -xdev -name '*.nam' -size +1M` found no `.nam` file on the root filesystem, and the shipped `plugins/NeuralAmp/models/` holds only a README. A file over 16 MiB is roughly 800,000 weights in JSON text; I did not measure realtime cost for models that size. If you want a larger cap, the constant is `kParserBytesPerInputByte`; the test follows it through its literal, so change both together.

## Self-check

1. **RED and GREEN with the product change reverted and restored**: RED `ctest -R '^NamModelLoaderTest$'` EXIT 8 (`test-2-red.log`, and again after the revert in `test-4-reverted-red.log`); GREEN EXIT 0 (`test-3-green.log`, `test-5-reapplied-green.log`).
2. **Deterministic**: no `sleep`, no clock, no thread, no network. The fixtures are a valid model padded with `x` characters to exactly `kParseLimitBytes` bytes (must load) and `kParseLimitBytes + 1` (must be refused). The temp-directory stamp in `main` is pre-existing and only makes the name unique.
3. **Fix at the cause, callers checked**: the cause is the unbounded `json::parse` at `NamModelLoader.cpp:140`; the size check sits in `loadFromFile` above it, so every caller listed under "Evidence before the fix" is covered. The validation header's only includer is the loader (`rg -n NamModelValidation.h`). The engine-budget check in `validateModelResources` is unchanged.
4. **No collateral damage**: every `.nam` file at or below 16 MiB loads as before (the test `loadsFileAtLimit` pins the boundary); the 20-test family passes, including the eight `NamLoaderTest_*` malformed-model cases and `NamLoaderTest_valid`. The 16 MiB case is the decision flagged above.
5. **Verdict**: FIXED. Not ALREADY-FIXED or FALSE-POSITIVE: the pre-fix code parsed before any size check (`NamModelLoader.cpp:124` before the fix), and the regression fails on it (test-2-red.log).

Realtime and undo: no audio-thread code, no command, no undo step; nothing new on any path that runs per period.
