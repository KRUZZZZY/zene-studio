# Cluster new-p12-255 — Concurrent downloads overwrite the same partial file

**Verdict: FIXED.** The finding is real on this branch: `StemModelStore::download()` staged every
download of a model in a directory at one fixed `<name>.onnx.part` path, opened with truncation, so
concurrent downloads could not coexist. The fix gives every attempt a staging file of its own (the
convention `RenderQueue::stagedPath` already uses for the same bug class) and coordinates the final
install so a losing installer reports the installed pinned bytes as success.

## Evidence of the bug (before the change)

`src/core/StemModelStore.cpp:413-417` (pre-fix tree; the finding's line 414):

```cpp
const auto finalPath = dir.filePath(spec.name + QStringLiteral(".onnx"));
const auto partPath = finalPath + QStringLiteral(".part");   // ONE path per model+dir
QFile partFile(partPath);
if (!partFile.open(QIODevice::WriteOnly | QIODevice::Truncate))   // wipes a co-owned file
```

Concurrent callers of that path exist in this product:

- `src/core/StemJobManager.cpp:386` runs the provisioner (`StemModelStore::fetchDefaultModelIfMissing`,
  which calls `download()`) on the job's worker thread, and there are TWO independent
  `StemJobManager` instances with their own workers: `src/core/ControlStemSupport.cpp:146` and
  `src/gui/StemSplitController.cpp:56`. First-use downloads from both can overlap.
- `src/core/ControlStemModel.cpp:148` (`stem.model_download`) calls `StemModelStore::download()`
  directly, and can overlap either worker.
- Two app instances share `defaultModelDir()` (`.../models/stems`), so the overlap is also
  cross-process.

With one shared `.part`, the later `open(Truncate)` wipes the bytes the earlier attempt has already
streamed and both attempts interleave writes into one file: verification then fails (a spurious
"Download failed") or, if a truncated file is renamed into place after the winner's verified install
was replaced, a corrupt model is left installed. The install step (`remove(finalPath)` then
`rename`) could also make the losing installer report "Cannot move ... into place" while the
identical pinned bytes were in fact installed.

## The change

- `src/core/StemModelStore.cpp:415-422` — each attempt stages to
  `<destDir>/<name>.onnx.part.<12-hex>`, a name of its own (`QUuid`, the same staged-path convention
  as `src/core/RenderQueue.cpp:75` and its comment "a fixed 'out.part' would be truncated by every
  conversion"). No live attempt can share a staging file: the clobbering open is gone, and the
  existing `verify()` gate still keeps any unverified bytes from being installed. Failure and cancel
  paths remove the attempt's own staging file exactly as before.
- `src/core/StemModelStore.cpp:446-453` — if the final `rename()` loses the race to a concurrent
  installer, the pinned file that landed IS this download: `verifiedInPlace()` (the same contract the
  function opens with) turns that into success instead of a spurious failure.
- `include/StemSeparation/StemModelStore.h:102-105` — the documented `.part` contract now documents
  the per-attempt staging file.

Nothing else changes: the policy refusals, the transfer, verification, and the single-installer
behaviour are untouched. A process killed mid-transfer leaves its uniquely named staging file behind
(there is no cross-process way to tell a dead attempt's file from a live one); every failure and
cancel path cleans up its own, as before.

## The regression

`tests/src/core/StemModelStoreTest.cpp` — `testConcurrentDownloadsDoNotClobberAPartialFileInFlight`:
a local `QTcpServer` accepts and never answers, so a download hangs in its transfer with its staging
file open (no TLS server is needed: the staging file is created before any network I/O). The test
writes stand-in streamed bytes into that file, starts a second download of the same model into the
same directory, and asserts those bytes are never truncated or overwritten. On the unfixed code the
second `open(Truncate)` wipes them and the test fails on the finding's exact defect; with the fix the
second download stages to a file of its own and the in-flight bytes are untouched. The fake server
is closed before joining, so the transfers end immediately instead of on the 30s stall timeout.

## Evidence table

| Check | Exact command (cwd) | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target zene StemModelStoreTest > .../logs/build-8.log 2>&1` (worktree root; fix reversed first by `git apply -R .../logs/fix.patch`, exit 0) | 0 | Built with the product fix removed and the final test in place. |
| RED regression | `ctest -R '^StemModelStoreTest$' --output-on-failure > .../logs/test-9.log 2>&1` (`build/tests`) | 8 | FAIL: `'!clobbered' returned FALSE. (a concurrent download truncated the partial file of a download still in flight)` at `tests/src/core/StemModelStoreTest.cpp` — the finding's defect, on the unfixed code. |
| GREEN build | `cmake --build build --target zene StemModelStoreTest StemJobManagerTest StemSplitPipelineTest StemExportTest > .../logs/build-9.log 2>&1` (worktree root; fix restored first by `git apply .../logs/fix.patch`, exit 0) | 0 | Built with the fix. |
| GREEN regression | `ctest -R '^Stem' --output-on-failure > .../logs/test-10.log 2>&1` (`build/tests`) | 0 | 4/4 ctest tests passed: StemExportTest, StemJobManagerTest, StemModelStoreTest, StemSplitPipelineTest. |
| QtTest count (StemModelStoreTest) | `QT_QPA_PLATFORM=offscreen ./build/tests/StemModelStoreTest > .../logs/test-11.log 2>&1` (worktree root) | 0 | `Totals: 20 passed, 0 failed, 0 skipped, 0 blacklisted` — 20 QtTest functions executed, 0 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-complexity-2.log 2>&1` (worktree root) | 1 | **Pre-existing red, not from this change** — see below. The 6 over-target functions named are `dotCentreAlong` (XyPadPairTest.cpp), `parseNotes` (DawProjectReadTracks.cpp), `adjust` (ControlCommandsMixerTools.cpp), `setStrip` (ControlCommandsMixerStrip.cpp), `rotateProjectRevision` (ProjectRevisions.cpp), `importedOverrides` (ControlCommandsShortcuts.cpp); none is touched here. New/changed functions in this change: `StemModelStore::download` CCN 9 (was 8, target 10), test helpers CCN 5/5/3/1, test slot CCN 5. |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-file-length-2.log 2>&1` (worktree root) | 1 | **Pre-existing red, not from this change** — the 14 files named (SmfInterchangeReader.cpp, TakeLane.cpp, CollabTransportTest.cpp, …) are none of this change's files. |
| Gates at HEAD (proof of pre-existence) | `git diff > .../logs/working-tree.patch && git apply -R .../logs/working-tree.patch` (exit 0), then the two gate commands above into `.../logs/gate-complexity-head.log` / `.../logs/gate-file-length-head.log`, then `git apply .../logs/working-tree.patch` (exit 0) | 1 / 1 | Same red at the unmodified HEAD tree. `diff` of the REGRESSION lists HEAD vs final: identical for both gates (`CMP_C=0`, `CMP_L=0`). No REGRESSION line names any file this change touches. |

The gate debt predates this change (it is in files from earlier work on this branch). I did not
re-anchor either gate and did not "fix" unrelated files, since this lane changes only the buggy
behaviour; the identical-at-HEAD comparison above is the proof the change adds zero gate regressions.

## Files committed

- `src/core/StemModelStore.cpp` — unique per-attempt staging file; rename-race install coordination.
- `include/StemSeparation/StemModelStore.h` — updated documented staging-file contract.
- `tests/src/core/StemModelStoreTest.cpp` — the regression above.
- `docs/qwen-fixes-2026-10/cluster-new-p12-255.md` — this report.

No new files were created, so no `tests/fork-sources.txt` / `tests/upstream-modifications.txt`
changes are required (both stem sources are already registered in `tests/fork-sources.txt`; the test
source is already in `tests/all-sources.txt` and `tests/CMakeLists.txt`).

## Observations left out of scope (not this finding)

- `QObject::startTimer: current thread's event dispatcher has already been destroyed` QWARNs come
  from the download's cancel-poll `QTimer` being started before the calling thread has an event
  dispatcher (first `transfer()` on a fresh thread); the cancel poll then only takes effect after an
  event loop runs. Pre-existing in `StemModelStore::transfer`/`streamInto`, unrelated to the shared
  partial path; the regression works around it by closing its fake server to end the transfers.
