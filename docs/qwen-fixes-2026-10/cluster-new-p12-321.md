FIXED

# cluster new-p12-321 - capture parameter allocation failures are unchecked

Finding (Luna patrol, reported 2x): `src/core/audio/AudioAlsa.cpp:~420`, "BUG 8: Capture
parameter allocation failures are unchecked ... If allocation fails, subsequent calls can
receive a null parameter pointer. The capture software-parameter allocation has the same
issue. ... fix: Check both allocation results and publish capture as unavailable on
failure."

Verdict: **REAL** - both capture allocations were unchecked on this branch, and the null
parameter pointer was handed to alsa-lib. Reproduced as a SIGSEGV in the new test before the
fix (see RED below) and fixed at both sites.

## Evidence in the code (pre-fix lines, base commit 9913d36d53)

| site | pre-fix code | why it is a defect |
| --- | --- | --- |
| `src/core/audio/AudioAlsa.cpp:396` | `snd_pcm_hw_params_malloc( &m_captureHwParams );` | return ignored; on failure `m_captureHwParams` stays NULL and the next line `snd_pcm_hw_params_any( m_captureHandle, m_captureHwParams )` writes through it (alsa-lib: `*params = pcm->hw_params;`) |
| `src/core/audio/AudioAlsa.cpp:481` | `snd_pcm_sw_params_malloc( &swParams );` | same, followed by `snd_pcm_sw_params_current( m_captureHandle, swParams )`, which also writes through the pointer |

The failure mode is not hypothetical in the test: the pre-fix run dies with
`Received signal 11 (SIGSEGV), code SEGV_MAPERR, ... accessing address 0x0000000000000000`
at the instruction `vmovdqu %ymm0,(%rdi)` - a struct store to a null pointer inside
alsa-lib, i.e. exactly the "subsequent calls can receive a null parameter pointer" the
finding describes (`logs/test-2-red.log`).

## The fix (product)

`src/core/audio/AudioAlsa.cpp`, `AudioAlsa::openCapture()`:

1. the hardware-parameter allocation is checked; on failure the reason is printed on the
   thread that opens the capture (never the capture thread), `closeCapture()` releases the
   PCM, and `AudioInputPath::publishUnavailable()` publishes
   `cannot allocate the capture hardware parameters: <snd_strerror>`, then `return false`;
2. the software-parameter allocation gets the same check and publishes
   `cannot allocate the capture software parameters: <snd_strerror>`.

`startProcessingImpl()` (`src/core/audio/AudioAlsa.cpp:302`) already handles `false` from
`openCapture()`: the playback thread starts, no capture thread is created, and
`AudioInputPath::live()` reports `captureCapable = true, open = false, reason = <the text>`.
That is the "publish capture as unavailable on failure" contract of the finding, and it is
what the test asserts.

Two behaviour-identical rewrites in the same function keep `AudioAlsa::openCapture()` at its
grandfathered CCN (the complexity ratchet fails on any rise of an existing function, and I
must not re-anchor it):

- `if( grantedChannels == 0 ) { grantedChannels = 1; }` -> `grantedChannels = std::max( grantedChannels, 1u );`
- the duplicated `m_captureFloat ? "float" : "s16_le"` in the closing `printf` and
  `publishOpen()` -> one `const QString sampleFormat` used by both.

Neither changes an observable value: the granted channel floor is still 1, and the published
format string is still `float` or `s16_le`.

## Regression test

`tests/src/core/AudioAlsaCaptureErrorTest.cpp` (the existing ALSA capture test file):

- `captureHwParamsAllocationFailureIsPublished` - arms one failing `snd_pcm_hw_params_malloc`
  immediately before `startProcessing()`, then asserts the capture did not open, the backend
  still reports `captureCapable`, the published reason names the hardware parameters, and a
  fault-free restart on the same device opens the capture again (reconfiguration path).
- `captureSwParamsAllocationFailureIsPublished` - the same for the software allocation, with
  everything before it running for real.

The seam is a counter the test arms (`hwParamsMallocFailures` / `swParamsMallocFailures`) and
the two `snd_pcm_*_params_malloc` fakes reproduce alsa-lib's own implementation
(`calloc(1, snd_pcm_hw_params_sizeof())`) for the success path, so the whole file's existing
tests still exercise the real allocator. No clock, no sleep, no memory pressure, no thread
race: the injection is a single decrement in the same thread that calls the allocation. The
two fakes and the null-safety guards added to the existing `snd_pcm_readi`/`snd_pcm_drop`
fakes leave the existing `captureThreadDoesNotLogTheFailedRecovery` test's behaviour
unchanged (it still arms both hand-over semaphores before it opens a capture).

## Commands and EXIT codes (all logs in `~/.config/kads/zene/state/runs/new-p12-321.r1/logs`)

| step | command | EXIT | log |
| --- | --- | --- | --- |
| baseline build | `cmake --build build --target AudioAlsaCaptureErrorTest -j4` | 0 | `build-1.log` |
| baseline test | `ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure` (from `build/tests`) | 0 | `test-1.log` |
| RED build (test only, product unmodified) | `cmake --build build --target AudioAlsaCaptureErrorTest -j4` | 0 | `build-2-red.log` |
| RED run | `ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure` | **8** (SIGSEGV in `captureHwParamsAllocationFailureIsPublished`) | `test-2-red.log` |
| GREEN build | `cmake --build build --target AudioAlsaCaptureErrorTest -j4` | 0 | `build-3-green.log` |
| GREEN run | `ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure` | **0** | `test-3-green.log` |
| GREEN, per slot | `./AudioAlsaCaptureErrorTest` (from `build/tests`) | 0, `Totals: 5 passed, 0 failed` | `test-3b-green-slots.log` |
| 4b: product patch | `git diff -- src/core/audio/AudioAlsa.cpp > logs/fix.patch` | 0 | `proof-4b.log` |
| 4b: revert | `git apply -R logs/fix.patch` | 0 | `proof-4b.log` |
| 4b: rebuild | `cmake --build build --target AudioAlsaCaptureErrorTest -j4` | 0 | `build-7-red-reverted.log` |
| 4b: run reverted | `ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure` | **8** (SIGSEGV) | `test-7-red-reverted.log` |
| 4b: restore | `git apply logs/fix.patch` | 0 | `proof-4b.log` |
| 4b: rebuild | `cmake --build build --target AudioAlsaCaptureErrorTest -j4` | 0 | `build-8-green-restored.log` |
| 4b: run restored | `ctest -R '^AudioAlsaCaptureErrorTest$' --output-on-failure` | **0** | `test-8-green-restored.log` |
| sw site alone reverted (temporary edit, not committed) | build 0, `./AudioAlsaCaptureErrorTest captureSwParamsAllocationFailureIsPublished` | **139** (SIGSEGV at `0x48`) | `build-9-sw-only-reverted.log`, `test-9-red-sw-site.log` |
| sw site restored (diff byte-identical to `fix.patch`, exit 0) | build 0, same slot | **0** | `build-10-sw-restored.log`, `test-10-sw-green-site.log` |
| product binary | `cmake --build build --target zene -j4` | 0 | `build-11-zene.log` |
| family | `cmake --build build --target AudioAlsaPollDescriptorTest AudioAlsaSuspendShutdownTest AudioAlsaResumeRecoveryTest AudioAlsaFatalWriteTest AudioAlsaWriteTest RecordingInputPathTest AudioDeviceDiagnosticsTest -j4` | 0 | `build-4-family.log`, `build-5-diag.log`, `build-6-write.log` |
| family run | `ctest -R '^(AudioAlsa.*|AudioDeviceDiagnosticsTest|RecordingInputPathTest)$' --output-on-failure` | **0**, `100% tests passed out of 8` | `test-6-family.log` |

Both regression sites are proven separately: with the whole product patch reverted the run
dies at the hardware site (the first one), and with only the software check reverted the
software slot alone dies (`test-9-red-sw-site.log`), so neither slot passes by accident.

## Test matrix

| path | how it is covered | result |
| --- | --- | --- |
| normal path | `captureThreadDoesNotLogTheFailedRecovery` still opens the capture on the ALSA `null` device, and the hw test's fault-free restart opens it again (`ALSA capture: null, 2 channels at 44100 Hz (float), period 256` in `test-3b-green-slots.log`) | PASS |
| hardware allocation failure | `captureHwParamsAllocationFailureIsPublished` | PASS (FAIL/SIGSEGV before the fix) |
| software allocation failure | `captureSwParamsAllocationFailureIsPublished` | PASS (SIGSEGV with only that check reverted) |
| exhaustion / reconfiguration | the second `startProcessing()` in the hw test: after a failed open the same device opens normally | PASS |
| audio-thread constraint | the new code runs in `openCapture()` (called from `startProcessingImpl()` on the opening thread); `captureLoop()` is untouched; nothing added allocates, locks, waits or logs on the capture thread | reviewed in the diff |
| platform | the test is registered inside `if(LMMS_HAVE_ALSA)` in `tests/CMakeLists.txt:1954`, so non-ALSA hosts do not build it; the product change is inside the existing `LMMS_HAVE_ALSA` guard | n/a here (Linux) |
| reuse / reconfiguration of the device | no header changed, so no dependent target needed a rebuild beyond `zene` and the family above | `build-11-zene.log` |

## Gates

`python3 .../feature_check.py <worktree>` cannot complete in this session: its base-commit
listing step checks out the base commit into its own cache worktree and the lane's git shim
refuses that (`logs/gates-1.log`):

```
bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12).
...
subprocess.CalledProcessError: Command '['git', '-C', '.../gate-baselines/tree', 'checkout', '-q', '--detach', '9913d36d53...']' returned non-zero exit status 97.
FEATURE_CHECK_EXIT=1
```

I did not bypass the shim, and I did not write the missing
`gate-baselines/9913d36d53....json`: with lizard absent from this box that cache entry would
record an empty complexity baseline and poison the shared cache for every later run of this
base. Instead every gate was run individually in the worktree (`logs/gates-2-individual.log`):

| gate | command | EXIT | log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-reproduce.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-regression.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| file-length | `bash tests/file-length-gate.sh --check` | 0 (`PASS (check mode: no regressions)`) | `gate-file-length.log` |
| complexity | `bash tests/complexity-gate.sh --check` | 2 (`lizard is not installed`) | `gate-complexity.log` |

`lizard` cannot be installed here (read-only `~/.local`), so the complexity gate cannot run
at all in this environment - the same state the train is in on this box. To keep the ratchet
satisfied in an environment where it does run, `openCapture()`'s CCN was kept at its
grandfathered value by the two behaviour-identical rewrites listed above (`openCapture` is
grandfathered at CCN 16 in `tests/complexity-baseline-all.tsv:167`).

Registration: no new product file (the test lives in a file already listed in
`tests/fork-sources.txt:2713`), and `src/core/audio/AudioAlsa.cpp` is an inherited file that
is already listed; its entry for this change is appended to
`tests/upstream-modifications.txt`. The list files end with a newline.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Proven
   in the 4b cycle above: `git apply -R logs/fix.patch` (exit 0) -> rebuild (0) -> ctest
   **exit 8, SIGSEGV**; `git apply logs/fix.patch` (exit 0) -> rebuild (0) -> ctest **exit 0**.
   The software slot is proven on its own: with only that check reverted, that slot alone
   exits **139** (`test-9-red-sw-site.log`), and 0 when restored (`test-10-green-sw-site.log`).
2. **The test is deterministic.** No wall clock, no sleep, no allocation pressure, no racy
   thread: the fault is a counter (`hwParamsMallocFailures`/`swParamsMallocFailures`) armed in
   the test's own thread immediately before the `startProcessing()` call that allocates, and
   consumed by the fake `snd_pcm_hw_params_malloc`/`snd_pcm_sw_params_malloc` in that same
   thread. The test also asserts the counter reached 0, so a fault that is never consumed is a
   failure, not a silent pass. The success path of the fakes is alsa-lib's own body
   (`calloc(1, snd_pcm_*_params_sizeof())`), verified by the unchanged tests in the same file
   and by the fault-free reopen.
3. **The fix is at the cause and complete.** Both capture allocation sites named by the
   finding are checked; every failure path in `openCapture()` now publishes through
   `AudioInputPath::publishUnavailable()` and is handled by the existing
   `startProcessingImpl()` contract. The `closeCapture()`/`publishUnavailable()` order matches
   the other five failure branches in the same function (and `closeCapture()` cannot overwrite
   the reason: `m_captureOpen` is false, so it does not publish a closure). The playback
   constructor's own two allocations (`src/core/audio/AudioAlsa.cpp:95-96`, inherited upstream
   lines, not the capture path the finding describes) are left exactly as they are - see Other
   findings.
4. **No collateral damage.** No header changed; the public API, the config keys, the save
   format and `AudioInputPath`'s published fields are untouched, so old projects and saved data
   load as before. The only added limit is "a failed allocation ends the open", which rejects
   nothing a successful allocation allowed. The two rewrites preserve the granted-channel
   floor (1) and the published format string, and the family run
   (`AudioAlsaWriteTest, AudioAlsaPollDescriptorTest, AudioAlsaSuspendShutdownTest,
   AudioAlsaResumeRecoveryTest, AudioAlsaFatalWriteTest, AudioDeviceDiagnosticsTest,
   RecordingInputPathTest` and this test) is green: `test-6-family.log`, EXIT 0.
5. **Verdict REAL, not ALREADY-FIXED / FALSE-POSITIVE**, so item 5 of the ask list is not
   applicable: the pre-fix code is quoted at `AudioAlsa.cpp:396` and `:481` above and the
   pre-fix run crashes (exit 8).
6. **Gate aggregate.** `feature_check.py` itself is refused by the lane's git shim before it
   can compute its base listing (exact output quoted under Gates); every gate it runs was run
   individually and passed, except `complexity`, which cannot run in this environment because
   `lizard` is not installed (also true of the base). `submit_check.py` (`logs/submit-1.log`,
   EXIT 1) reports exactly one FAIL - `gates (feature_check.py)` - which is that same refusal;
   its other four mechanical rows PASS (`commits on the branch`, `working tree clean`,
   `report docs/qwen-fixes-2026-10/cluster-new-p12-321.md`, `tests added or changed`), and the
   five ASK items are answered in items 1-5 above. I did not bypass the shim and did not
   fabricate the cache entry, because with `lizard` absent it would record an empty complexity
   baseline and mis-flag every over-target function of this base for every later run.

## Other findings

- `src/core/audio/AudioAlsa.cpp:95-96` (playback constructor, inherited upstream code): both
  `snd_pcm_hw_params_malloc(&m_hwParams)` and `snd_pcm_sw_params_malloc(&m_swParams)` ignore
  their result in exactly the same way, and `setHWParams()`/`setSWParams()` then write through
  a NULL pointer. Same defect shape, outside this finding's capture scope; not touched here
  because the rules say to change only the buggy behaviour of the finding. It needs its own
  fix (a failing constructor must leave `_success_ful == false`).
- `AudioEngineTeardownTest` and `AudioAlsaWriteTest` were not built in this tree when the
  family was first run; `AudioEngineTeardownTest` still has no binary here
  (`test-4-family.log`, `test-5-family.log`), and the first `AudioAlsaWriteTest` link failed
  with `undefined reference to 'main'` after an interrupted background build; a clean rebuild
  of that target links and passes (`build-6-write.log`, `test-6-family.log`). Unrelated to this
  change, reported for completeness.
