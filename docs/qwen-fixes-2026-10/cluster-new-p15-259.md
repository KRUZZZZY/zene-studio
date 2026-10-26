FALSE-POSITIVE (the stated mechanism does not reproduce on this build; one latent Debug-only assert remains, not fixed - see Residual)

# Cluster new-p15-259 - LAME initialisation failure "used as a valid encoder"

Finding: `src/core/audio/AudioFileMP3.cpp:~95` - "`lame_init()` can return null, but the code immediately
passes `m_lame` to setter and ID3 functions; a setter dereferences the null handle instead of reporting
initialization failure."

## Verdict and why

The finding's mechanism is wrong on this build, and its proposed behaviour (fail initialisation cleanly)
already holds:

1. **"A setter dereferences the null handle" is false for the installed LAME (4.0).** Probed with a NULL
   handle: every setter used on the path returns -1 without touching memory; `id3tag_init(NULL)` and
   `id3tag_set_comment(NULL, ...)` return; `lame_init_params(NULL)` returns -1; `lame_encode_flush(NULL, ...)`
   and `lame_encode_buffer_interleaved_ieee_float(NULL, ...)` return -3; `lame_close(NULL)` returns 0.
   Evidence: `logs/probe-lame-null-1.log` (program source was a scratch file in `$TMPDIR`, not committed).
2. **The failure is already reported.** `initEncoder()` returns `lame_init_params(m_lame) != -1`
   (`src/core/audio/AudioFileMP3.cpp:148`), so a NULL handle gives `successful == false`.
   `ProjectRenderer` then deletes the device and never uses it (`src/core/ProjectRenderer.cpp:180-187`).
   The export path therefore never writes or finishes a device whose encoder did not start.
3. **Behaviour of such a device when it is destroyed (what the test measured).** The destructor calls
   `flushRemainingBuffers()` (`src/core/audio/AudioFileMP3.cpp:52-54`), which calls `lame_encode_flush(NULL)`
   (-3), and `writeData(buf, -3)` (`src/core/audio/AudioFileMP3.cpp:104-111`, `src/core/audio/AudioFileDevice.cpp:84-91`).
   The RED run below checked this path on the unfixed code: the write into the device returned false,
   `finishEncoding()` returned false, `hasWriteError()` was true, and the file stayed at 0 bytes.
   No crash, no bytes written, and the failure is recorded.

## Residual (latent, not fixed here)

`src/core/audio/AudioFileMP3.cpp:105` is `assert (bytesWritten >= 0);`. In a build without `NDEBUG`, the flush
of a NULL handle (-3, probe above) would make that assert abort during the destructor of a device whose
`lame_init()` returned null. This is reachable only if LAME cannot allocate its state (out of memory).
The build tree here is `RelWithDebInfo` (`build/CMakeCache.txt:59`, flags `-DNDEBUG` at line 94), so the
assert is compiled out and this could not be reproduced. A regression test for it cannot fail in this build,
so I did not make a change that cannot be shown to fix anything. Suggested follow-up: a null-handle guard in
`flushRemainingBuffers()`/`writeBuffer()`, verified in a Debug build.

## Evidence (file:line, current HEAD)

- `src/core/audio/AudioFileMP3.cpp:131` `m_lame = lame_init();` - no null check (the finding's point, true).
- `src/core/audio/AudioFileMP3.cpp:135-146` setters and ID3 calls on `m_lame` - null-safe in LAME 4.0 (probe).
- `src/core/audio/AudioFileMP3.cpp:148` `return lame_init_params(m_lame) != -1;` - the failure is reported.
- `src/core/ProjectRenderer.cpp:130-134` the encoder is `&AudioFileMP3::getInst`; `:180-187` a device with
  `successful == false` is deleted and discarded.
- `include/AudioFileMP3.h`, `src/core/audio/AudioFileMP3.cpp:52-56,96-112` destructor and flush path (above).

## Regression test (RED), and why it does not prove a defect

A test was written first to drive the NULL-encoder path through a test seam (`LMMS_TESTING` static flag making
`initEncoder()` skip `lame_init()`): `tests/src/core/AudioFileMP3InitTest.cpp` (a copy is at
`logs/red-test-AudioFileMP3InitTest.cpp`). Its checks: `successful` false; `writeBuffer` false; `finishEncoding()`
false; `hasWriteError()` true; file size 0 after destruction.

It PASSED on the unfixed code, so it is not a regression proof for any defect, and the seam, the test and the
CMake registration were reverted. Nothing from the test is committed. Per the procedure, a not-real finding
does not get a fix.

## Commands run (EXIT codes are the ones shown)

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-259.r1/logs`

| # | Command (from worktree) | EXIT | Log |
|---|---|---|---|
| 1 | `cmake --build build --target AudioFileMP3InitTest -j4` | 2 | `build-1-red.log` ("No rule to make target": tree not yet regenerated for the new test) |
| 2 | `cmake --build build --target cmake_check_build_system` | 0 | (regeneration; output not kept) |
| 3 | `cmake --build build --target AudioFileMP3InitTest -j4` | 2 | `build-2-red.log` (ccache could not create its temp file under the sandbox path; environment, not code) |
| 4 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-new-p15-259 cmake --build build --target AudioFileMP3InitTest -j4` | 0 | `build-3-red.log` (built; ccache temp dir changed by environment only) |
| 5 | `cd build/tests && ctest -R AudioFileMP3InitTest --output-on-failure` | 0 | `test-1-red.log` (1/1 executed, passed on UNFIXED code) |
| 6 | probe of LAME with a NULL handle (`$TMPDIR/lamecheck/probe`, linked `-lmp3lame`) | 0 | `probe-lame-null-1.log` |

No product file is committed; the only commit is this report.

## Gates (step 5) - what ran and what could not

`feature_check.py` could not run as shipped in this lane: it creates its log folder at
`<worktree>/../<name>-feature-check` (`qwen/feature_check.py:70-71`), outside the write-jail, and its ratchet
baseline for the merge-base needs a git worktree inside `zene-cachyos`, also outside it. I did not work around
that. Instead I ran each of its checks with the same commands, from the worktree, logging to
`logs/feature-check/`:

| Check | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (7 train-wide items, none in a changed file) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (22 train-wide items, none in a changed file) |

The ratchets are red on the train on purpose. The gate only fails on items in files this branch changed, and
this commit changes only `docs/qwen-fixes-2026-10/cluster-new-p15-259.md`, so no ratchet item can be "new in
your files". The red items listed are in `src/wasm/WasmSandbox.cpp`, `src/core/DawProjectReadTracks.cpp`,
`plugins/Vst3Effect/Vst3Host.cpp`, `src/core/TakeLane.cpp` and test files, none of them touched here
(`logs/feature-check/complexity.log`, `logs/feature-check/file-length.log`).

Affected-family test count: `AudioFileMP3InitTest` 1 executed (`test-1-red.log`), passed on the unfixed code.
There is no GREEN run because there is no fix.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** Not met, and that is the
  finding. There is no product change, because the claim does not reproduce. The one test written
  (`logs/red-test-AudioFileMP3InitTest.cpp`) passed on the unfixed code (`test-1-red.log`, EXIT 0, 1/1), so it
  was reverted with its seam and CMake registration. Nothing from it is committed.
- **Deterministic: no timing, sleeps or races.** The reverted test used a static `LMMS_TESTING` flag to make
  `lame_init()` return null for the next device, with fixed frame counts and no timing. It is not committed.
- **Fix at the cause; every caller and sibling checked.** No fix. Callers: the only construction path is
  `&AudioFileMP3::getInst` in `src/core/ProjectRenderer.cpp:130-134`, and the only consumer of a failed device is
  `src/core/ProjectRenderer.cpp:180-187` (deleted, never used). The destructor path is
  `src/core/audio/AudioFileMP3.cpp:52-56,96-112`.
- **No collateral damage.** No product code changed. Existing MP3 export behaviour is untouched, and the normal
  path (stereo encoder starts, encodes, finishes, non-empty file) was exercised by the reverted test's first
  case before revert.
- **FALSE-POSITIVE evidence (file:line / contract).** Set out above: the setter claim is false for LAME 4.0
  (`logs/probe-lame-null-1.log`), the failure is reported at `src/core/audio/AudioFileMP3.cpp:148`, and the
  device is discarded at `src/core/ProjectRenderer.cpp:180-187`. The residual Debug-only assert
  (`src/core/audio/AudioFileMP3.cpp:105`) is recorded above as latent and not reproduced here.

## Environment notes (affect reproduction, not the verdict)

- The first build attempts failed inside ccache (`Permission denied` for its temp file under the sandbox
  path). The retry used `CCACHE_TEMPDIR` inside the ccache directory (an environment setting for that one
  command, no config change).
- The build tree was regenerated with `cmake --build build --target cmake_check_build_system`, because the
  wrapper refuses an explicit `cmake -S/-B` configure.
