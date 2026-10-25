IMPLEMENTED

# Q050-062.1: expose quality settings to stem jobs (stage 1/4 of Q050-062)

Branch `feat/050-Q050-062.1-oc`, cut from `050/m0-train-15` (`aac861e62`). Feature record:
`docs/050-features/Q050-062.1.md`.

`stem.job_start` takes an optional `quality`: `fast`, `standard` or `high`, for 25%, 50% or 75% overlap
between model segments. The in-process ONNX backend, the external-process backend and
`tools/stem_split_cli.py --quality` all use one hop formula (`stemHopFrames`,
`include/StemSeparation/StemTypes.h:126`). Every job object reports the effective `quality`, `overlap`
and `hop_frames`, which come from the job manager, plus `inference_quality`, the setting the worker
handed the backend. If `quality` is omitted the job runs at `standard`, the old hop of `segment / 2`.

## Round 2 (review REWORK: the socket test did not prove the handoff)

The review found that the job object's `quality` came from the control surface's own record of the
request. When the surface submitted `standard` to the manager regardless of the request, the job still
reported the requested value, and `ControlStemCommands` passed. Fixed in `99357b43c`:

- `quality` / `overlap` / `hop_frames` now come from `StemJobManager::quality()`
  (`src/core/StemJobManager.cpp:335`), and the surface keeps no copy of its own.
- The new `inference_quality` is what the worker handed the backend's `separate()`. It is published just
  before the call (`src/core/StemJobManager.cpp:548`) and is empty until then.
- `tests/stem_quality_lib.py` now requires a finished job's `inference_quality` to equal the request
  (`standard` when omitted). `StemJobManagerTest` checks both accessors and the unknown-id case.
- The snapshot was regenerated live (846 ids / 76 groups; only the new result field and descriptions
  changed).

Mutations, each built, run and then restored (the lane forbids `git checkout`, so each was undone by
the reverse edit and checked with grep):

| Mutation | Command | Exit |
| --- | --- | --- |
| The review's case: the surface submits `DefaultStemQuality` whatever was requested (`ControlStemSupport.cpp:253`) | `ctest -R '^ControlStemCommands$' -V`: 6 checks FAILED (fast/high start, status, `inference_quality`); the omitted case still ok | 8 (`r2/logs/test-2-mut-handoff.log`) |
| The manager stores `standard` on every job (`StemJobManager.cpp:174`) | `ctest -R '^(StemJobManagerTest\|ControlStemCommands)$'`: both fail | 8 (`r2/logs/test-3-mut-manager.log`) |
| The worker passes `standard` to `separate()` but still publishes the job's value (`StemJobManager.cpp:548`) | `ctest -R '^StemJobManagerTest$'`: `testSubmitHandsTheQualityToTheBackend` fails | 8 (`r2/logs/test-4-mut-worker.log`) |
| Restored | the 9-test final run below | 0 |

## Acceptance

| Criterion | Test | Command | Exit |
| --- | --- | --- | --- |
| The setting changes inference consistently in both backend paths and the CLI | `OnnxRuntimeStemSeparatorTest::testQualitySetsTheSegmentCount` (in-process: 6/9/17 runs), `StemSplitPipelineTest::testQualitySetsTheSegmentCountThroughTheCli` (external process → CLI: 6/9/17 runs), `StemQualityTest` (shared formula), `ControlStemCommands` CLI checks (`--quality` omitted/fast/standard/high → 9/6/9/17 segments) | `ctest -R '^(StemQualityTest\|OnnxRuntimeStemSeparatorTest\|StemSplitPipelineTest\|ControlStemCommands)$' --output-on-failure` (in the final 9-test run) | 0 |
| `stem.job_start` and job status report the effective setting; callers omitting it keep the legacy behaviour | `ControlStemCommands` (socket, live `zene`): omitted/fast/high are each reported by the engine in the start reply and in `stem.job_status`, and the finished job's `inference_quality` (what the worker ran) equals the request; `stem.get_state` lists the qualities and the default; an unknown name is refused `invalid_args` and creates no job. `StemJobManagerTest::testSubmitHandsTheQualityToTheBackend`: omitted → `standard` reaches the backend | `ctest -R '^(ControlStemCommands\|StemJobManagerTest)$'` (in the final 9-test run) | 0 |

Final run: `ctest -R '^(StemQualityTest|StemJobManagerTest|OnnxRuntimeStemSeparatorTest|StemSplitPipelineTest|ControlStemCommands|ReversibilityContractTest|ControlCommandsSnapshot|DocFiguresGate|StemAvailabilityDocsGate)$' --output-on-failure`
→ `100% tests passed out of 9`, exit 0. Round 2: `r2/logs/test-5-final.log`; the build of `zene` plus the
five test targets exited 0 (`r2/logs/build-5-final.log`). Round 1: `r1/logs/test-9-final.log`.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** Round 2: see the
  mutation table above (exit 8 under each mutation, 0 restored). Round 1:
  - Every non-test hunk reverted (`git checkout aac861e62 -- include src tools/stem_split_cli.py`), then
    rebuilt: `zene` exit 0. `StemQualityTest`, `StemJobManagerTest`, `OnnxRuntimeStemSeparatorTest` and
    `StemSplitPipelineTest` each fail to build, exit 2 (`'StemQuality' has not been declared`).
    `ctest -R '^ControlStemCommands$'` exits 8, with all 10 quality checks FAILED
    (`logs/build-7-revert-*.log`, `logs/test-7-revert-ControlStemCommands.log`).
  - Behaviour reverted with the API kept (ONNX hop `segment / 2`, no `--quality` passed, the manager
    no longer stores the quality, `stemHopFrames` always `segment / 2`): all four targets build (exit 0),
    and ctest exits 8 with 4/4 failing on their new assertions (`fast: 10 progress calls, expected 7`,
    `fast: 9 runs, expected 6`, `Compared values are not the same`, `logs/test-8-semrevert.log`).
  - Restored (`git checkout HEAD -- include src tools/stem_split_cli.py`), rebuilt and re-run: 9/9
    passed, exit 0.
- **Every acceptance criterion is proven by a test I ran.** See the table above: `OnnxRuntimeStemSeparatorTest`,
  `StemSplitPipelineTest`, `StemQualityTest`, `StemJobManagerTest` and `ControlStemCommands`, each exit 0
  in the final run.
- **One undo step per edit; honest reversibility classes.** No command was added, and no command's
  mutation changed. `stem.job_start` gained an argument and result fields but still changes no project
  state, so it stays `not_mutating` (`src/core/ControlReversibilityTableStems.cpp:86`). `stem.job_status`
  and `stem.get_state` stay read-only `not_mutating`. `ReversibilityContractTest` exit 0, and it asked for
  no A16 histogram change.
- **Realtime.** Nothing here runs on the audio thread. `inference_quality` is a `std::atomic<int>` that
  the worker stores before `separate()`; the owner reads it under the manager's mutex. The quality is read on the control/GUI thread in
  the command handler and stored on the job inside `submit()` under the manager's mutex, before the job
  is queued. The worker reads it after taking the job off the queue under the same mutex, so there is a
  happens-before edge and no data race. Local TSan does not run on this box; this is reasoned from the
  design, not measured. The inference loops only change their hop.
- **Projects.** No project data or format changed, and quality is not stored in projects. Old projects
  load and render as before. Omitting `quality` runs the old hop (`StemQualityTest`: standard =
  `segment / 2` for every segment), so old callers get the same output.
- **Portability.** No Qt-6-only API: `QStringList`, `QLatin1String`, `QJsonObject` and `QJsonArray` are all
  Qt 5.15. The `StemTypes.h` helpers are C++17 `constexpr` with `long long` arithmetic. No
  `min`/`max`/`small`/`near`/`far`/`interface` identifiers, no `std::filesystem`, no POSIX calls. The test
  CLI helper uses only the Python standard library plus the existing harness.
- **Snapshot and figures.** `snapshot_live.py` exit 0: 846 ids / 76 groups, unchanged count; the diff is
  only the new argument and result fields. `python3 tests/doc-figures-gate.py` exit 0.
- **Gates.** `~/.local/share/zene-bunny-fix/qwen/feature_check.py` itself cannot run in this write-jail.
  It creates `<worktree>/../zene-feat-q050-062.1-feature-check/`, and that `mkdir` fails with
  PermissionError. So `submit_check.py` reports its `gates` row as FAIL. I ran a copy
  (`/tmp/q0621-scripts/feature_check.py`) that is identical except that its log directory is in my run
  folder. It exits 0, with every row PASS: fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item).
  Round 2: `r2/logs/feature-check-1.log`, exit 0.
  - **Two commits are not about the feature:**
    - **`57e670bdd`.** It moves the hop-formula test out of `StemJobManagerTest` into the new
      `StemQualityTest`, because otherwise `StemJobManagerTest` would grow past the 500-line ratchet.
    - **`78619ef4d`.** It re-derives `tests/all-sources.txt` from its header recipe. The base
      `aac861e62` already failed `all-sources-reproduce` (and gate 9) on its own: the file had 1148
      `plugins/SfzPlayer/sfizz/` entries that the recipe excludes (`logs/base-all-sources-diff.log`).
      The re-derive removes only those vendored paths. It is a separate commit, so the train can drop
      it if it re-derives the file itself.
- **GUI path (submit_check WARN).** The clip menu's quality choice is stage 2 (Q050-062.2) in the scoping
  split, so it is not built here. The existing **Split to stems** action still goes through the same
  registered `stem.job_start` with no `quality`, so it splits at `standard`, unchanged.
- **Not done here (later stages):** real-model SDR and time per setting (stage 3), and the persisted
  link between each source and its results (stage 4).
