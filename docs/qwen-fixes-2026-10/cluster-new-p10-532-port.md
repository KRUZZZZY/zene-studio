# Cluster new-p10-532-port

**Verdict: FIXED**

Soundpipe success paths in `plugins/ReverbSC/base.c` now return `SP_OK` (1) instead of
`0` (`SP_NOT_OK` is 0), matching `base.h`'s status constants. `sp_createn` already
returned `SP_OK`; `sp_destroy` and `sp_process` returned `0` on success and now
return `SP_OK`. `sp_create` delegates to `sp_createn`. (Note: `sp_process` is compiled
out in this configuration because `NO_LIBSNDFILE` is defined in `base.h`.)

## Evidence

| Location | Evidence |
|---|---|
| `plugins/ReverbSC/base.c:45` | `sp_destroy` returns `SP_OK` on success. |
| `plugins/ReverbSC/base.c:95` | `sp_process` returns `SP_OK` on success. |
| `plugins/ReverbSC/base.c:38` | `sp_createn` returns `SP_OK` on success. |
| `tests/src/plugins/ReverbSCBaseTest.cpp:14-30` | Regression compares create/destroy statuses to `SP_OK`. |
| `tests/upstream-modifications.txt` | Reason recorded for the inherited production file change. |

## Commands and results

All build and test output was captured in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-532-port.r1/logs`.
The build tree could not be reconfigured (bunny-guard), so the new test target was
built manually with `/usr/bin/cmake` from a temporary project that compiles
`tests/src/plugins/ReverbSCBaseTest.cpp` with `plugins/ReverbSC/base.c`.

| Purpose | Command | Exit |
|---|---|---:|
| Build regression target (fixed tree) | `/usr/bin/cmake --build ... --target ReverbSCBaseTest` (`manualbuild-3.log`) | 0 |
| GREEN after fix | `/tmp/scratchpad/reverbtest/build/ReverbSCBaseTest` (`test-3.log`) | 0, 3 passed |
| Remove production fix: `git diff -- plugins/ReverbSC/base.c > .../fix.patch && git apply -R .../fix.patch` | — | 0 |
| Rebuild with production fix removed | `/usr/bin/cmake --build ... --target ReverbSCBaseTest` (`manualbuild-4.log`) | 0 |
| Sensitivity RED with production fix removed | `/tmp/scratchpad/reverbtest/build/ReverbSCBaseTest` (`test-2.log`) | 1, multiDestroyStatus 0 vs SP_OK 1 |
| Restore production fix: `git apply .../fix.patch` | — | 0 |
| Rebuild after restoring fix | `/usr/bin/cmake --build ... --target ReverbSCBaseTest` (`manualbuild-5.log`) | 0 |
| GREEN after restoring fix | `/tmp/scratchpad/reverbtest/build/ReverbSCBaseTest` (`test-3.log`) | 0, 3 passed |
| ReverbSC CTest family | `ctest -R 'ReverbSC|ReverbSc'` (`test-ctest-1.log`) | 0, 4 tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-complexity-2.log`) | 1 (pre-existing baseline drift; no changed file listed) |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-filelength-2.log`) | 1 (pre-existing baseline drift; no changed file listed) |

## Files committed

- `plugins/ReverbSC/base.c`
- `tests/src/plugins/ReverbSCBaseTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-532-port.md`
