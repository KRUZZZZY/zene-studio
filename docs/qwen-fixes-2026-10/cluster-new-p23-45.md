Verdict: FIXED

# Cluster new-p23-45: detectStateJson dereferences a possibly null song

**Finding (Luna patrol, BUG 6):** `detectStateJson` dereferences `song` with no null check; `detect.get_state` with no song crashes.

## Decision

The defect is REAL, and it is the same class of defect in the sibling `detect.apply`. It is hard to reach through the public socket path, and I say exactly where below.

**What the code did (before the fix):**

- `ControlDetectSupport.cpp` (old line 212): `const TempoMap& map = song->tempoMap().map();` with no null check. The same unguarded pattern is at old lines 217, 219 and 233.
- `ControlCommandsDetect.cpp` (old line 151): the `detect.get_state` handler called `detectStateJson(Engine::getSong())`, with no check on the returned pointer.
- `ControlCommandsDetectApply.cpp` (old line 88): `Song* song = Engine::getSong();` with no check. It was dereferenced at old line 142 (`song->tempoMap()`), and again at old lines 184, 187, 212 and 218.

**Why the public path is mostly protected, and where it is not:**

- `ControlRegistry::invoke` checks `cmd->requiresEngine && !isEngineReady()` at `ControlRegistry.cpp:283`, on the caller's thread. `isEngineReady()` (`ControlRegistry.cpp:205-207`) requires `Engine::getSong() != nullptr`. A null song therefore returns a typed `Busy` failure before the handler runs.
- The handler then runs on the UI thread through `runOnUiThread` (`ControlRegistry.cpp:296`). The song is read again there. The gate and the read are separate, so the song can be gone in between. `Engine::destroy()` clears it (`Engine.cpp:106` `clearProject`, `Engine.cpp:123` `deleteHelper(&s_song)`).
- So the crash is reachable in the window between the gate and the UI-thread read. It is not reachable through a normal, gated `invoke` at a steady state. I did not reproduce it over the socket. The regression test drives the registered handler directly, past the gate, which is the only way to reach the unguarded read deterministically.

**The fix (at the read, on the thread that uses it):**

- New helper `detectProjectSong(ControlResult* error)` in `ControlDetectSupport.cpp:172-181`, declared in `include/ControlDetectSupport.h:73`. It reads `Engine::getSong()`. A null song becomes `ControlErrorKind::Busy` with the same "not addressable yet" wording the registry gate uses. A null song is never dereferenced.
- `detect.get_state` now calls the helper first (`ControlCommandsDetect.cpp:152`). `detectStateJson` keeps its signature, and its header comment now states the non-null precondition (`include/ControlDetectSupport.h:80-82`).
- `detect.apply` now calls the helper first, before any read or write (`ControlCommandsDetectApply.cpp:99`). A null song refuses with Busy and writes nothing. The refusal order for the other arguments is unchanged, because the song check sits after the gate and before the bound check.
- The `requestedHalves()` helper (`ControlCommandsDetectApply.cpp:54-61`) takes the two `tempo`/`key` booleans out of the handler. The booleans are computed the same way. The change exists only to keep `registerApplyCommand` within its complexity ratchet after adding the null check; see Self-check.

**Behaviour that did not change:** the normal path (non-null song) returns the same payload and writes the same fields. No command was added, and no save format or undo record changed. The refusal paths write nothing, so undo is unchanged.

## Regression test

`tests/src/core/ImportDetectionTest.cpp`, two slots (`:508` and `:523`), with a shared helper `detectRegistry()` (`:541`). The helper uses the process singleton `ControlRegistry::instance()` and registers the detect group once, because a second registration is a `qFatal`. Each slot first asserts `Engine::getSong() == nullptr`, since this test never starts the engine. Then it calls the registered handler directly and asserts `!ok` and `errorKind == Busy`.

- `detectStateRefusesWhenThereIsNoSong`: `detect.get_state` with no args.
- `detectApplyRefusesWhenThereIsNoSong`: `detect.apply` with `path` = a valid click track and `key:false`. This reaches the song read, because the tempo is found and the key half is not requested.

## Evidence (exact commands and EXIT codes)

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-45.r1/logs/`. Build commands were run with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp` set, because the default ccache temp directory is not writable in this sandbox (first attempt: `ccache: error: failed to create temporary file ... Permission denied`, `build-1.log`).

| Step | Command (run from the worktree unless stated) | Result |
| --- | --- | --- |
| RED, state (unfixed code) | `cd build/tests && ctest -R '^ImportDetectionTest$' --output-on-failure > logs/test-red-5-final-unfixed.log 2>&1; echo EXIT=$?` | **EXIT=8**, SIGSEGV (`Received signal 11`), in `detectStateJson(nullptr)` |
| RED, apply (unfixed code) | `cd build/tests && ./ImportDetectionTest detectApplyRefusesWhenThereIsNoSong > logs/test-red-6-final-apply.log 2>&1; echo EXIT=$?` | **EXIT=139**, SIGSEGV |
| RED build (unfixed) | `cmake --build build --target ImportDetectionTest -j4 > logs/build-8-unfixed-final.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Proof reversal | `git diff -- <the four product files> > logs/fix-final.patch; git apply -R logs/fix-final.patch` | EXIT=0 (tree = test change only) |
| Fix re-applied | `git apply logs/fix-final.patch` then rebuild `build-9-fixed-final.log` | EXIT=0, EXIT=0 |
| GREEN, ImportDetectionTest | `cd build/tests && ctest -R '^ImportDetectionTest$' --output-on-failure > logs/test-green-9-final.log 2>&1; echo EXIT=$?` | **EXIT=0**, 1/1 passed |
| GREEN, direct binary | `cd build/tests && ./ImportDetectionTest > logs/test-green-10-final-direct.log 2>&1; echo EXIT=$?` | **EXIT=0**, `Totals: 17 passed, 0 failed` (includes both new slots, both `PASS`) |
| GREEN, detect family | `cd build/tests && ctest -R 'ControlDetect\|ImportDetection' --output-on-failure > logs/test-green-11-family.log 2>&1; echo EXIT=$?` | **EXIT=0**, 2/2 passed (`ImportDetectionTest`, `ControlDetectCommands`: the real-binary socket test) |
| Build of product and test (fixed, final) | `cmake --build build --target zene ImportDetectionTest -j4` (build-4 and build-7) | EXIT=0 |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh > logs/g1b-fork-sources.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | EXIT=0 |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | EXIT=0 |
| Gate: check-namespace | `tests/scripted/check-namespace` | EXIT=0 |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | EXIT=0 |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | EXIT=0 |
| Gate: complexity (ratchet, `--check`) | `bash tests/complexity-gate.sh --check > logs/g7b-complexity.log 2>&1; echo EXIT=$?` | **EXIT=1**, 7 REGRESSION lines, none in a file this fix changed (see Self-check) |
| Gate: file-length (`--check`) | `bash tests/file-length-gate.sh --check > logs/g8b-file-length.log 2>&1; echo EXIT=$?` | **EXIT=1**, 19 REGRESSION lines, none in a file this fix changed (see Self-check) |
| Full feature_check.py | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **Not run.** It exits at `feature_check.py:71` with `PermissionError` creating `zene-qwen-new-p23-45-feature-check` beside the worktree, which is outside the write-jail (`logs/gate-feature-check-1.log`). I ran each of its gate commands above directly, with the same arguments, instead. |

Number of tests executed for the affected family: **2** (`ImportDetectionTest` 17 cases, `ControlDetectCommands` 1 script). Zero is a failure, and this is not zero.

## Files committed

- `include/ControlDetectSupport.h`
- `src/core/ControlDetectSupport.cpp`
- `src/core/ControlCommandsDetect.cpp`
- `src/core/ControlCommandsDetectApply.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p23-45.md`

No new files were created, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no new entries. The gates `fork-sources`, `all-sources-reproduce` and `no-upstream-regression` all pass.

## Self-check

- **Regression fails before, passes after (step 4b):** yes. Shown in the RED/GREEN rows above, using the final product diff.
- **Complexity:** my first version raised the existing `registerApplyCommand` from CCN 17 to 18 (`complexity-gate.sh` REGRESSION `CCN rose 17 -> 18`). I took the `tempo`/`key` booleans into `requestedHalves()`, which brings it to CCN 16. The gate now lists 7 REGRESSION items, all in files this fix does not touch (`XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsSessionRecordLand.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `WasmSandbox.cpp`). The gate exits 1 for them on the train, as the brief says it is red on purpose. The new helpers are small (`detectProjectSong`, `requestedHalves`) and are not listed.
- **File length:** 19 REGRESSION items, none in a file this fix changed. The two product files I grew, `ControlCommandsDetectApply.cpp` and `ControlDetectSupport.cpp`, are each 254 lines. The test file already existed and is not listed.
- **Real-time:** the change runs on the UI thread in the command handlers. It adds no audio-thread code, and nothing new on any audio path allocates, locks or logs.
- **Undo:** the refusal paths return before any write, so no undo step is recorded. The write path and its recorded undo step are unchanged.
- **Commands:** no new command was added, so the `ControlCommandsSnapshot` canary is not affected.
- **Not verified:** the full ctest suite, the GUI, the socket-level crash (the regression drives the handler directly), and the `feature_check.py` wrapper (see the evidence row above).
