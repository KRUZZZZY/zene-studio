FALSE-POSITIVE

# Cluster new-p15-91: a failed waveform load can dereference a null buffer

`SampleBuffer::fromFile()` cannot return a null pointer on this branch. Its failure path returns a
valid, empty `SampleBuffer` (`SampleBuffer::emptyBuffer()`), so `src/gui/LfoControllerDialog.cpp:218`
cannot dereference null after a corrupt or unsupported waveform file. No product code was changed.

## Evidence

| Where | What it shows |
|---|---|
| `src/gui/LfoControllerDialog.cpp:215` | `auto buffer = SampleBuffer::fromFile(fileName);` - the call the finding names |
| `src/gui/LfoControllerDialog.cpp:218` | `m_userWaveBtn->setToolTip(buffer->audioFile());` - safe only if `buffer` is non-null |
| `include/SampleBuffer.h:98` | the declared return type is `std::shared_ptr<const SampleBuffer>` |
| `src/core/SampleBuffer.cpp:111-143` | `fromFile` has exactly three returns, none of them null |
| `src/core/SampleBuffer.cpp:113` | empty path -> `SampleBuffer::emptyBuffer()` |
| `src/core/SampleBuffer.cpp:118-120` | the decoder failure is a `std::optional` (`if (!result)`), not a null buffer |
| `src/core/SampleBuffer.cpp:138` | failed decode -> `return SampleBuffer::emptyBuffer();` |
| `src/core/SampleBuffer.cpp:141-142` | success -> `std::make_shared<SampleBuffer>(...)` |
| `src/core/SampleBuffer.cpp:101-109` | `emptyBuffer()` returns `std::make_shared<const SampleBuffer>()` (:106) or a function-local static `std::make_shared` (:107-108); neither is null |
| `src/core/LfoController.cpp:206-208` | the setter also substitutes `emptyBuffer()` for a null argument, so a hypothetical null could not even be stored |
| `src/core/LfoController.cpp:314` | project load passes `fromFile` straight to the same setter - same non-null contract |
| `src/gui/widgets/Graph.cpp:631,636,639` | the other waveform picker calls `fromFile` and immediately `sampleBuffer.get()` - same contract |
| `plugins/TripleOscillator/TripleOscillator.cpp:166` | a third caller passes `fromFile` on with no null check - same contract |

History: `git log -S 'return SampleBuffer::emptyBuffer();' -- src/core/SampleBuffer.cpp` (EXIT=0) lists
`ed0f288c8a` ("Remove `SampleLoader` (#8186)", 2026-01-04), and `git show ed0f288c8a -- src/core/SampleBuffer.cpp
include/SampleBuffer.h` shows `fromFile` was introduced in that commit already returning
`std::shared_ptr<const SampleBuffer>` and already returning `emptyBuffer()` on failure. The API never had a
null-returning form on this branch, so this is a false premise, not a fix that landed after the patrol.

## Reproduction attempt

I tried the reported input directly, in a temporary case added to the existing
`tests/src/core/SampleBufferNoEngineTest.cpp` (reverted afterwards; `git status` is clean). The case wrote a
file containing `this is not an audio file` into a `QTemporaryDir`, called `SampleBuffer::fromFile()` on it, and
asserted the returned pointer is non-null, `empty()`, and has an empty `audioFile()`. It passed:
`test-probe-2.log`, EXIT=0.

The case is deliberately not kept. It passes with and without any product change (there is no product change),
so it is not a regression test for a fix and would be a "test that proves nothing" in a red/green pair.

## Test matrix

| Path | Behaviour on this branch | Evidence |
|---|---|---|
| normal load | valid file -> non-null buffer with `audioFile` set (`src/core/SampleBuffer.cpp:141-142`) | `SampleBuffer` family, 3/3, EXIT=0 |
| failure | corrupt/unsupported file -> warning (message box with a GUI, `qWarning` otherwise) and `emptyBuffer()` (`:120-138`) | scratch probe, EXIT=0 |
| exhaustion | no separate branch; a failed decode is the only failure path | same probe |
| reconfiguration | the picker replaces the waveform through `setUserDefSampleBuffer`, the render-safe handoff | `LfoControllerUserWaveTest`, 1/1, EXIT=0 |
| audio thread | no product change; nothing added to a render path | n/a |
| platforms | no code change and no new API; Qt 5 / MSVC / macOS requirements unchanged | n/a |

Prerequisites exist: `SampleBufferNoEngineTest`, `SampleBufferBase64Test`, `SampleBufferResampleTest` and
`LfoControllerUserWaveTest` are registered in `tests/CMakeLists.txt` and build in this tree.

## Commands and exit codes

All commands ran in the worktree
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-91`; the test commands ran
from its `build/tests`. Logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p15-91.r1/logs`.

| Step | Command | Exit | Log / result |
|---|---|---:|---|
| Build probe case | `cmake --build build --target SampleBufferNoEngineTest -j4` | 0 | `build-1.log` |
| Build probe case (byte-count typo fixed) | `cmake --build build --target SampleBufferNoEngineTest -j4` | 0 | `build-2.log` |
| Scratch probe | `./SampleBufferNoEngineTest probeFailedWaveformLoadIsNeverNull` | 0 | `test-probe-2.log`: 3 passed, 0 failed |
| Focused test, whole binary | `ctest -R '^SampleBufferNoEngineTest$' --output-on-failure` | 0 | `test-1.log`: 100% tests passed out of 1 |
| Revert the scratch probe | `git checkout -- tests/src/core/SampleBufferNoEngineTest.cpp` | 0 | `git status --porcelain` printed nothing |
| Rebuild the family | `cmake --build build --target SampleBufferNoEngineTest SampleBufferBase64Test SampleBufferResampleTest -j4` | 0 | `build-3.log` |
| SampleBuffer family | `ctest -R '^SampleBuffer.*Test$' --output-on-failure` | 0 | `test-2.log`: 100% tests passed out of 3 |
| Build the LFO test | `cmake --build build --target LfoControllerUserWaveTest -j4` | 0 | `build-4.log` |
| LFO user-wave test | `ctest -R '^LfoControllerUserWaveTest$' --output-on-failure` | 0 | `test-3.log`: 100% tests passed out of 1 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-91` | 0 | `feature-check-1.log`: `RESULT: PASS` |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p15-91 new-p15-91` | 0 | `submit-check-1.log`: every item PASS, `RESULT: PASS - submit` |

The build exit codes are the background wrapper's `EXIT=` line, read back from the process record; the test
and gate exit codes are the shell's `EXIT=` line appended to each log. The first scratch probe run
(`test-probe-1.log`, EXIT=1) failed on my own wrong byte count in the probe (`write()` returned 25, the probe
expected 24), not on the product; it never touched product code.

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes).** Not applicable:
  this verdict changes no product file, so there is no red/green pair. The only test I added was the scratch
  probe that asserts the finding's premise is false; it passed (EXIT=0, `test-probe-2.log`) and was reverted
  (`git checkout -- ...`, EXIT=0, clean status). I did not keep it because it passes with and without any
  product change, and the check correctly treats such a test as proving nothing.
- **The test is deterministic.** The scratch probe used a fixed byte string written to a file in a
  `QTemporaryDir`, one `fromFile()` call, and assertions on the returned pointer, `empty()` and `audioFile()`.
  No wall-clock timing, sleeps, threads, randomness or injected clock values were involved. The kept tests I
  ran are existing ones with the same determinism.
- **The fix is at the cause and complete.** No fix and no product file changed. I checked every caller and
  sibling of the API the finding names: the dialog (`src/gui/LfoControllerDialog.cpp:215`), project load
  (`src/core/LfoController.cpp:314`), the other waveform picker (`src/gui/widgets/Graph.cpp:636`),
  TripleOscillator (`plugins/TripleOscillator/TripleOscillator.cpp:166`), and the setter itself
  (`src/core/LfoController.cpp:206-208`). All rely on the same non-null contract and stay consistent.
- **No collateral damage.** No limits or checks were added; no saved-data format or project-load path was
  touched. The `SampleBuffer` family (3/3) and the existing LFO user-wave handoff test (1/1) pass on the
  final source state. Saving still serializes `m_userDefSampleBuffer->audioFile()` exactly as before.
- **FALSE-POSITIVE evidence.** `src/core/SampleBuffer.cpp:113` and `:138` return
  `SampleBuffer::emptyBuffer()`, `:142` returns `std::make_shared<SampleBuffer>(...)`, and
  `src/core/SampleBuffer.cpp:101-109` shows `emptyBuffer()` returning `std::make_shared` on both branches.
  The finding's precondition ("`fromFile` returns null") cannot occur, so `src/gui/LfoControllerDialog.cpp:218`
  is safe.

## Other findings

- A failed load still replaces the current user waveform with the empty buffer and clears the picker tooltip
  (`src/gui/LfoControllerDialog.cpp:216-218`); `fromFile` shows the "Failed to load sample" message box first
  (`src/core/SampleBuffer.cpp:126-129`). That is a UX consequence of the non-null contract, not a null
  dereference, and it is outside this finding's claim.
- The fix the finding proposes ("check the returned pointer") cannot detect a load failure here, because the
  failure is signalled by the decoder's `std::optional` (`src/core/SampleBuffer.cpp:118-120`) and not by a
  null pointer. Detecting it would need an API change (for example returning the decode result), which would
  be a different job with its own callers.

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p15-91.md` (this report). No product or test file is committed: the
  scratch probe was reverted and `git status` is clean.
