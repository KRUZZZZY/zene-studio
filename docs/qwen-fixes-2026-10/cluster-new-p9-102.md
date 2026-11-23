ALREADY-FIXED

# cluster new-p9-102: DrumSynth render state is shared across instances

## Verdict

`ALREADY-FIXED` on this branch. The finding is a historical snapshot: it names
`src/core/DrumSynth.cpp:54-64`, where the `.ds` parser/renderer state was once ordinary
process-wide globals. That state is `thread_local` in this branch's product code, and the
change that made it so is an ancestor of both the branch base and `HEAD`. There is no
product change in this job; this is a claim-only closure with code evidence.

## Evidence

Product, current `HEAD` (`21c2f3ee2e`):

* `src/core/DrumSynth.cpp:81-90` — every mutable namespace-scope variable the finding
  names is `thread_local`:
  * `:81-83` `envpts`, `envData`, `chkOn`, `sliLev`
  * `:84` `timestretch`
  * `:85-87` `DD`, `clippoint`, `DF`, `phi`
  * `:89-90` `wavewords`, `wavemode`, `mem_*`
* The only other namespace-scope names in the file are `const int`/`constexpr` constants
  (`:70-74`, `:94`, `:96`, `:166`) and file-local static functions with no mutable state
  (`:170`, `:253`, `:259`). There is no remaining mutable global parse/render state.
* `src/core/DrumSynth.cpp:433-441` resets the `mem_*` adjustments at the start of every
  `GetDSFileSamples()` call, and `:647-652`, `:668` reset the envelope cursors and
  `wavewords`, so a thread-local state block is self-contained per call: it neither
  depends on nor leaks into another render.
* The only production caller is `SampleDecoder::decodeSampleDS`
  (`src/core/SampleDecoder.cpp:191-193`), which parses and renders inside one call. There is
  no DrumSynth instrument plugin in this tree (`rg -n "DrumSynth" src plugins include` finds
  no `GetDSFileSamples` user other than `SampleDecoder.cpp` and the tests), so no audio-thread
  path touches this state at all.

Branch history (proof that this is not a claim about my own edit):

* Branch base from `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/FIX-BASE`
  (`050/m0-train-35`, `fa4a5f2e78`; `git merge-base HEAD <FIX-BASE>` = `fa4a5f2e78`)
  already contains the fixed code:
  `git show fa4a5f2e785c:src/core/DrumSynth.cpp | sed -n '79,90p'` prints
  `thread_local float envpts[8][3][32];` at line 81 and the other `thread_local`
  declarations at 82-90.
* `git log -1 --format='%H %s' 3bb99a2ac8` =
  `3bb99a2ac8... fix: long DrumSynth lines and thread-safe parsing (cluster new-p11-92-port-port-port)`;
  `git merge-base --is-ancestor 3bb99a2ac8 HEAD` exited 0, so the fix is in this branch's
  history.
* The accepted earlier report `docs/qwen-fixes-2026-10/cluster-new-p11-92-port-port-port.md`
  records the same cause and fix (`:35-37` pre-fix process-wide state, `:45` and `:60-61`
  the `thread_local` fix, `:164-172` the same-thread/single-call contract).

Existing regression coverage (deterministic, already in the tree):

* `tests/src/core/DrumSynthTest.cpp:144-148` declares the state
  `extern thread_local float envpts[8][3][32];`, `extern thread_local float timestretch;`,
  `extern thread_local int chkOn[8];`. Because declaration and definition must agree on TLS,
  these declarations make the test binary refuse to link against the unfixed (non-TLS)
  product; that is the deterministic RED shown below.
* `tests/src/core/DrumSynthTestParts1.h:345-407`,
  `DrumSynthTest::concurrentParsesMatchSingleThreadedParses`, renders two different `.ds`
  fixtures in two threads and requires each thread's renders to equal that fixture's
  single-threaded render. It passes with the `thread_local` state.
* Registered in `tests/fork-sources.txt:784` (`DrumSynthTestParts1.h`) and
  `:2814` (`DrumSynthTest.cpp`).

## Commands and exit codes (all logs in the run's log folder)

| # | Command | Exit | Result |
|---|---|---|---|
| build-1 | `cmake --build build --target DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest -j4` | **0** | built |
| test-1 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **0** | `100% tests passed out of 1` |
| test-2 | `cd build/tests && ctest -R 'DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest\|ImportDetectionTest' --output-on-failure` | **8** | first attempt: `ImportDetectionTest (Not Run)` because its binary was not built yet; see build-2 |
| build-2 | `cmake --build build --target ImportDetectionTest -j4` | **0** | built |
| build-3-unfixed | product state temporarily reverted from `thread_local` to plain globals; `cmake --build build --target DrumSynthTest -j4` | **2** | `ld: _ZN4lmms6envptsE: TLS reference in CMakeFiles/DrumSynthTest.dir/src/core/DrumSynthTest.cpp.o mismatches non-TLS definition in ../src/CMakeFiles/lmmsobjs.dir/core/DrumSynth.cpp.o` |
| build-4-restored | fix restored (sha256 `1a2a41ef...b40ba`, same as backup); `cmake --build build --target DrumSynthTest -j4` | **0** | built |
| test-3-concurrent | `cd build/tests && ./DrumSynthTest concurrentParsesMatchSingleThreadedParses` | **0** | `PASS : DrumSynthTest::concurrentParsesMatchSingleThreadedParses()` |
| test-4-family | `cd build/tests && ctest -R 'DrumSynthTest\|DrumSynthLevelTest\|SampleBufferResampleTest\|ImportDetectionTest' --output-on-failure` | **0** | `100% tests passed out of 4` |
| build-5-unfixed | revert repeated, binary removed; `cmake --build build --target DrumSynthTest -j4` | **2** | TLS mismatch again |
| test-5-unfixed | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | `Unable to find executable: .../build/tests/DrumSynthTest`, `0% tests passed` |
| build-6-restored | fix restored from the backup; `cmake --build build --target DrumSynthTest -j4` | **0** | built |
| test-6-restored | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **0** | `100% tests passed out of 1` |
| gates-1 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `PASS` on `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`; `RESULT: PASS` |
| submit-pre-1 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-102` | **1** | only FAILs at that point were the still-missing report and its `Self-check` section |
| submit-final-1 | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-102` (after the report commit, clean tree) | **0** | `RESULT: PASS - submit`; every mechanical item PASS, the six ASK items are answered above |

The command-and-output record of the inspection itself is in `evidence-1.log` in the same
log folder (`git blame` of `src/core/DrumSynth.cpp:76-90`, the test declarations, the branch
base's version of `:79-90`, and the fixing commit).

## Test matrix

* **Normal path.** `DrumSynthTest` renders ordinary `.ds` fixtures; the two-thread case
  renders two fixtures concurrently (`test-3-concurrent`, exit 0).
* **Failure / reverted fix.** Removing `thread_local` makes the existing test's
  `extern thread_local` declarations disagree with the product definition, so the test
  target does not build (`build-3` / `build-5`, exit 2) and `ctest` cannot run it
  (`test-5`, exit 8). Restoring the fix builds and passes (`build-6` / `test-6`, exit 0).
* **Exhaustion / malformed input.** Unchanged and covered by the same binary: over-long
  envelopes, over-long lines, non-finite values, the maximum stretch and the render-length
  refusal cases (`DrumSynthTest`, all pass in `test-1`/`test-4`).
* **Reconfiguration.** The render state is reset per `GetDSFileSamples()` call
  (`src/core/DrumSynth.cpp:433-441`, `:647-652`, `:668`); the family test renders different
  fixtures back to back in one process (`test-4-family`, exit 0).
* **Audio-thread constraints.** No audio-thread path calls this code (only
  `SampleDecoder::decodeSampleDS` on the import path). `thread_local` state is
  constant-initialised; touching it does no allocation, locking, waiting or logging.
* **Supported platforms.** No new code; the existing guarded test file builds on
  `_WIN32` with the strtok stand-in left out (`tests/src/core/DrumSynthTest.cpp:295-307`).
  I built and ran on this Linux host only.

## Self-check

**1. The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
No product change was made in this job (verdict ALREADY-FIXED), so the revert applies to the
already-accepted fix. Reverting only the nine `thread_local` declarations to plain globals and
rebuilding `DrumSynthTest` fails deterministically: `cmake --build build --target DrumSynthTest`
exited **2** with the TLS-mismatch link error (`build-3-unfixed.log`, repeated in
`build-5-unfixed.log`), and `ctest -R '^DrumSynthTest$'` exited **8** with
`Unable to find executable` (`test-5-unfixed.log`). With the declarations as committed, the same
build exits **0** and `ctest` exits **0** (`build-6-restored.log`, `test-6-restored.log`), and
the targeted concurrency case exits **0** (`test-3-concurrent.log`). The tree was left byte-identical
to the backup: both `sha256sum src/core/DrumSynth.cpp` runs printed
`1a2a41efbefaf227b4962c18184d9358e354de90e41878e1ecb16b71295b40ba`, and `git status --porcelain`
was empty after the restore.

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values).**
The RED proof above is a link-time mismatch, not a race: it does not depend on the scheduler,
on timing, or on a sleep. For the green path, the existing `concurrentParsesMatchSingleThreadedParses`
case compares two threads' renders against single-threaded reference renders computed first; its
rendezvous is released by the second arrival (no timeout, no sleep, no wall-clock read) at
`tests/src/core/DrumSynthTest.cpp:221-264`. No new test was added, because the fix was already in
the tree.

**3. The fix is at the cause and complete: every caller and sibling checked.**
The cause is the mutable namespace-scope render state; all of it is `thread_local`
(`src/core/DrumSynth.cpp:81-90`) and nothing else mutable is namespace-scope (see Evidence). The
only production caller is `SampleDecoder::decodeSampleDS` (`src/core/SampleDecoder.cpp:191-193`),
which parses and renders inside one call, so a per-thread block cannot split a render. The
`GetPrivateProfileString` / `GetEnv` / `GetPrivateProfileInt` / `GetPrivateProfileFloat` family
still writes the same thread-local arrays and is unaffected by the distinction. The test's three
declarations are `extern thread_local` (`tests/src/core/DrumSynthTest.cpp:146-148`), matching the
definition, so they stay consistent.

**4. No collateral damage: old projects and saved data still load as before; no new limit hits other callers.**
No new limit or check was added in this job, and no product byte changed. The accepted
`thread_local` change is behaviour-preserving in one thread: the state is initialised to the same
values and reset the same way at the start of every `GetDSFileSamples()` call
(`src/core/DrumSynth.cpp:433-441`, `:647-652`, `:668`). The `.ds` parse/render family
(`DrumSynthTest`, `DrumSynthLevelTest`, `SampleBufferResampleTest`, `ImportDetectionTest`) passes
4/4 (`test-4-family.log`, exit 0).

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract.**
`src/core/DrumSynth.cpp:81-90` — all mutable parse/render state is `thread_local`. `:70-74`,
`:94`, `:96`, `:166` are the only other namespace-scope names and are `const`/`constexpr`; the
file-local helpers at `:170`, `:253`, `:259` hold no state. The branch base `fa4a5f2e78` already
contains the same declarations (`git show fa4a5f2e785c:src/core/DrumSynth.cpp`, lines 81-90), and
the fixing commit `3bb99a2ac8` is an ancestor of `HEAD` (`git merge-base --is-ancestor` exit 0).
The existing regression test is `tests/src/core/DrumSynthTestParts1.h:345-407`.

**6. Sibling commands in the command files the check lists.**
No command file was changed by this job, and none of the listed `app.*` / `arrangement.*` commands
relates to the `.ds` parser or its render state. The list appears because the mechanical check
diffs the whole branch against the older `FIX-BASE` snapshot, which includes train commits from
other clusters; my commit changes only this report. Nothing to reconcile.

## Realtime, undo and commands

* No audio-thread code was added or changed. The `.ds` state is reached only from the import/load
  decoder (`SampleDecoder::decodeSampleDS`) and from tests.
* No undo step and no control command was added; the `ControlCommandsSnapshot` canary is untouched.
* `thread_local` state here has constant initialisers (zero-initialised arrays and literal
  `mem_*` values), so no per-thread allocation, lock or dynamic initialisation is involved.

## Other findings

* The `strtok` stand-in in `tests/src/core/DrumSynthTest.cpp:151-264` is now inert for the
  product: the fixed parser tokenises with `std::string` and never calls `strtok`
  (`src/core/DrumSynth.cpp:305`, `:322` mention it only in comments). The concurrency case
  therefore no longer forces an interleaving through that seam; it still verifies the
  public two-thread contract but its RED proof for the *state* fix is the TLS link mismatch,
  not the rendezvous. Not changed here; it is outside this finding and the existing test was
  not weakened.
* `ctest -R '...ImportDetectionTest'` exits 8 if that target has not been built (test-2 above);
  it passes once built (test-4). This is a build-order artefact, not a product failure.
