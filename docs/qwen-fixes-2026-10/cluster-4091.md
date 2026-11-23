ALREADY-FIXED

4091: ALREADY-FIXED — starting a stem job no longer decodes or resamples the source on the application thread

Finding: `ControlStemSupport.cpp:225` (historical line) said `start` loaded the whole source with
`SampleBuffer::fromFile` and resampled it before submitting the job.

## Decision and code evidence

The current branch already implements the requested fix. `start` reads only the source's header length and
hands decoding/resampling to the job's worker:

- `src/core/ControlStemSupport.cpp:223-236`: the comment states "only the source's length is read here ... the
  worker decodes and resamples"; `stemSourceLength` is called at `:226`, and the job is submitted at `:234-236`
  through `m_manager.submitFrom(...)` with a loader that calls `stemDecodeAtModelRate`.
- `src/core/ControlStemSource.cpp:87-108`: `stemSourceLength` uses `probeHeader` (`sf_open`/`sf_close`, header
  only). A file libsndfile cannot open is refused unless its suffix is a worker-decoded format (`.ds`, `.ogg`,
  `.oga`); no `SampleBuffer::fromFile` call is on this path.
- `src/core/ControlStemSource.cpp:115-131`: `stemDecodeAtModelRate` is the only function that calls
  `SampleBuffer::fromFile` and `resampleBuffer`; it is the loader handed to the worker.
- `src/core/StemJobManager.cpp:86-92`: the manager starts its worker as a detached `std::thread`;
  `:518` calls `runJob` on that worker, `:544-545` runs `prepareJob` -> `loadMix`, and `:562` invokes the stored
  loader there. The caller only builds the lambda and enqueues it (`:185-206`).

History (both commits are ancestors of this branch; `git merge-base --is-ancestor` EXIT=0 for each):

- `52d7334966` ("feat(tasks): wire the background jobs ... Q050-010") is the change that removed the caller-side
  `SampleBuffer::fromFile` + `resampleBuffer` block from `start` and replaced it with `stemSourceLength` plus
  `submitFrom(stemDecodeAtModelRate)`. The diff hunk begins at the historical line named by the finding.
- `ef85c1ac92` ("fix(stems): decode a .ds source ... on workers ... Q050-010") removed the remaining caller-side
  fallback decode in `stemSourceLength` for `.ds`/`.ogg` and added the `loadedFrames` path used by
  `stem.job_status`.

The GUI's "Split to Stems" path is covered too: a clip that came from a file is handed to the registered
`stem.job_start` (`src/gui/StemSplitController.cpp:292-315`), and an in-memory clip's WAV is written on the global
thread pool before the same command starts the job (`src/gui/StemSplitController.cpp:328-356`). The command path is
`src/core/ControlCommandsStems.cpp:143` -> `stemStartJob` -> `surface()->start`.

## Existing regression tests

- `tests/src/core/StemJobLoaderTest.cpp:75-108`, `testTheLoaderRunsOnTheWorkerAfterSubmitReturned`: holds a
  loader on a gate and asserts it runs on a thread other than the caller while the caller's event loop turns.
- `tests/src/gui/StemSplitBackgroundTest.cpp:196-216`, `theLengthOfASourceNeverDecodesOnTheCaller`: asserts
  `stemSourceLength` runs no decoder for a `.ds` source and refuses garbage.
- `tests/src/gui/StemSplitBackgroundTest.cpp:218-242`, `aDrumSynthSourceIsDecodedByTheJobWhileTheLoopTurns`:
  the start reply has `frames` 0 (no caller decode) and the length appears only after the worker decoded it.

## Commands and results (this session)

| Step | Command | Exit |
| --- | --- | --- |
| Build both test targets | `cmake --build build --target StemJobLoaderTest StemSplitBackgroundTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/build-1.log 2>&1` | EXIT=0 |
| Run both tests (from `build/tests`) | `ctest -R '^(StemJobLoaderTest|StemSplitBackgroundTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/test-1.log 2>&1` | EXIT=0, 2/2 passed |
| Reintroduce the pre-Q050-010 caller-side decode in `ControlStemSupport.cpp` (temporary mutant), build | `cmake --build build --target StemSplitBackgroundTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/build-2-mutant.log 2>&1` | EXIT=0 |
| Run the regression test against the mutant | `ctest -R '^StemSplitBackgroundTest$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/test-2-mutant.log 2>&1` | EXIT=8; 6 passed, 1 failed: `aDrumSynthSourceIsDecodedByTheJobWhileTheLoopTurns` ("Compared values are not the same") |
| Restore the product file | `git checkout -- src/core/ControlStemSupport.cpp` | file restored; `git status --short` empty |
| Rebuild restored tree | `cmake --build build --target StemSplitBackgroundTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/build-3-restored.log 2>&1` | EXIT=0 |
| Rerun both tests restored | `ctest -R '^(StemJobLoaderTest|StemSplitBackgroundTest)$' --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/test-3-restored.log 2>&1` | EXIT=0, 2/2 passed |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-4091 > /home/kruzzzzy/.config/kads/zene/state/runs/4091.r1/logs/feature-1.log 2>&1` | EXIT=0, all 8 rows PASS |

The tests passed before any edit, so RED cannot be produced from the current product code; under procedure step 2
this is ALREADY-FIXED. The mutant above is proof that the existing test catches the reported caller-side decode,
and it was reverted before committing.

## Self-check

- **The regression test FAILS with the fix reverted and PASSES with it.** No product fix was made this session.
  The temporary mutant that restored the reported behaviour (inline `SampleBuffer::fromFile` + `resampleBuffer`
  before `submit`) made `StemSplitBackgroundTest` fail with EXIT=8 (`test-2-mutant.log`); restored, it passes
  with EXIT=0 (`test-3-restored.log`). The product file was restored with `git checkout --` and is unchanged.
- **Deterministic.** `aDrumSynthSourceIsDecodedByTheJobWhileTheLoopTurns` asserts an exact value in the start
  reply (`frames == 0` for a `.ds` whose header has no length): the caller-side decode makes it non-zero
  immediately, with no timing involved. `testTheLoaderRunsOnTheWorkerAfterSubmitReturned` uses an atomic gate and
  records the executing thread id; it does not measure wall-clock time.
- **Cause and completeness.** Every caller was checked: the socket command goes
  `ControlCommandsStems.cpp:143` -> `stemStartJob` -> `StemSurface::start`; the GUI goes through the same
  registered command for a stored file and prepares an in-memory clip's WAV on a pool worker. Both the ordinary
  file path and the `.ds`/`.ogg` header-less path hand their decode to the worker. No sibling path in `start`
  still calls `SampleBuffer::fromFile` or `resampleBuffer`; the only remaining synchronous read is the header
  probe (`probeHeader`, `src/core/ControlStemSource.cpp:64-74`).
- **No collateral damage.** No product, save-format, undo, command or limit change this session; old projects
  load and render as before. The branch diff carries other lanes' merged train work, not changes from this run.
- **ALREADY-FIXED evidence.** `src/core/ControlStemSupport.cpp:223-236`; `src/core/ControlStemSource.cpp:87-108`
  and `:115-131`; `src/core/StemJobManager.cpp:86-92`, `:518`, `:544-545`, `:562`; commits `52d7334966` and
  `ef85c1ac92` are ancestors of the branch.
- **Sibling commands.** No command file was changed by this run. The submit check lists `app.*`,
  `arrangement.*` and others because the branch carries merged train history. Within the stem family,
  `stem.job_start` still returns the job object and `stem.job_status` reads the worker-loaded length through
  `StemJobManager::loadedFrames`; `stem.job_cancel`, `stem.job_result` and `stem.get_state` are untouched.
- **Undo / realtime.** Nothing was added to any code path this session; the worker is off the audio thread and
  the existing design uses a detached job worker, not the audio callback. No undo step was created.
- **Portability.** No API change. The existing code uses `std::thread`, `QFileInfo` and libsndfile; the suite was
  built and run on Linux/Qt 6 only here. No platform-specific product code was touched.
- **Submit-check "tests added or changed".** The row lists `tests/control-*.py` from the merged train history,
  not from this run. This run adds only the report.

## Other findings

- The start path still performs a synchronous header probe (`sf_open`/`sf_close`) to obtain the length and rate.
  That is O(header), not the full-source decode/resample the finding describes, and it was not measured for
  latency here. It is not part of this finding's defect.
