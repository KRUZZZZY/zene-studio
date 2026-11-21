# Cluster new-p9-363-port

**Verdict: FIXED** for the redirect path. The control-URL half of the finding is **ALREADY-FIXED** on `050/m0-train-5`.

Port of `bugs/qwen-new-p9-363` (`c57eabdc3`, `fb9095c7d`) onto the newest line, written by hand; nothing was merged or cherry-picked.

## What the finding says versus what the line already does

- **Control URL not restricted to the discovered router (ALREADY-FIXED).** `fb9095c7d` added a router check that the newest line already carries in a different form. `isRouterHttpUrl` (`src/core/PortMappingCodec.cpp:145-151`) requires the resolved control URL to be `http`, to match the location's scheme, and to have the same host as the location. `parseIgdDescription` applies it before storing the control URL (`src/core/PortMappingCodec.cpp:313-316`). The finding's scenario, a `URLBase` of `http://127.0.0.1:...`, is rejected by that check. The existing regression in `tests/src/core/PortMappingCodecTest.cpp:331-332` (`hostileBase`, `hostileControl`) covers it and passed (see the GREEN run below).
- **Redirect not refused (real, FIXED here).** `UpnpMapper::fetchDescription` followed HTTP redirects, and `onDescription` then parsed the description against `reply->url()`, which is the redirect target. A router at `192.168.1.1` that 302s to another host would therefore have its description, and its control URL, judged against the wrong host. This is the gap `c57eabdc3` closed.

## The fix (`c57eabdc3`, production part)

- `src/core/PortMappingUpnp.cpp:138` sets `QNetworkRequest::ManualRedirectPolicy`, so the mapper does not follow redirects.
- `src/core/PortMappingUpnp.cpp:152-155` fails with `the UPnP router redirected its description to another URL` when `RedirectionTargetAttribute` is valid.
- `src/core/PortMappingUpnp.cpp:140,143,157` pass the discovered `location` into `onDescription` and parse against it, not `reply->url()`.
- `include/PortMapping.h:135` declares the new `onDescription(QNetworkReply*, const QUrl&)` signature to match the definition.

## Regression test

`c57eabdc3`'s test lived in `PortMappingCodecTest` and used `#define private public`. This line replaced that with `UpnpMapperTestAccess`, so the regression goes into the closest existing harness instead: `tests/src/core/UpnpMapperFakeRouterTest.cpp`, which already runs a fake IGD on 127.0.0.1.

- New `Mode::Redirect` (`:79`). `FakeRouter::respond` answers `GET /rootDesc.xml` with a 302 to `/moved/rootDesc.xml` on the same fake router (`:203-208`).
- New case `aRedirectedDescriptionIsRefusedNotFollowed` (`:424-435`). It asserts the mapping fails, the message names the redirect, and neither the redirect target nor any `AddPortMapping` is requested.
- Before the fix the mapper followed the redirect and mapped successfully. The test's first assertion failed with `a redirected description was followed and a mapping was made`.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-363-port.r1/logs`

| Step | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| RED build, attempt 1 | `cmake --build build --target UpnpMapperFakeRouterTest -j2 > .../build-1-red.log 2>&1` (worktree) | 2 | Failed in ccache (`failed to create temporary file ... /run/user/1000/ccache-tmp: Permission denied`). No code error. |
| RED build, attempt 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-363 cmake --build build --target UpnpMapperFakeRouterTest -j2 > .../build-2-red.log 2>&1` (worktree) | 0 | Built on the unfixed product code, test edited. |
| **RED test** | `ctest -R UpnpMapperFakeRouterTest --output-on-failure > .../test-2-red.log 2>&1` (`build/tests`) | **8** | `FAIL! : aRedirectedDescriptionIsRefusedNotFollowed() '!result.ok' returned FALSE`. Totals: 11 passed, 1 failed. |
| GREEN build (zene) | `CCACHE_TEMPDIR=... cmake --build build --target zene -j2 > .../build-3-green-zene.log 2>&1` (worktree) | 0 | Header and source changed; `zene` rebuilt. |
| GREEN build (dependent tests) | `CCACHE_TEMPDIR=... cmake --build build --target UpnpMapperFakeRouterTest PortMappingCodecTest PortMappingOrchestratorTest CollabPolicyTest -j2 > .../build-4-green-tests.log 2>&1` (worktree) | 0 | Every target that includes `PortMapping.h` rebuilt. |
| **GREEN test (family)** | `ctest -R "UpnpMapperFakeRouterTest\|PortMappingCodecTest\|PortMappingOrchestratorTest\|CollabPolicyTest" --output-on-failure > .../test-3-green.log 2>&1` (`build/tests`) | **0** | 100% passed, **4 of 4** CTest tests executed. |
| GREEN per-function | `ctest -R UpnpMapperFakeRouterTest -V > .../test-4-green-verbose.log 2>&1` (`build/tests`) | 0 | `PASS aRedirectedDescriptionIsRefusedNotFollowed()`; Totals: **12 passed, 0 failed**. |
| Proof: save fix | `git diff -- include/PortMapping.h src/core/PortMappingUpnp.cpp > .../fix.patch` (worktree) | 0 | Product files only: `include/PortMapping.h` and `src/core/PortMappingUpnp.cpp`; the test file is excluded. |
| Proof: remove fix | `git apply -R .../fix.patch` (worktree) | 0 | Product code back to original. |
| Proof RED build | `cmake --build build --target zene UpnpMapperFakeRouterTest -j2 > .../build-5-reverse.log 2>&1` (worktree) | 0 | Rebuilt without the fix. |
| **Proof RED test** | `ctest -R UpnpMapperFakeRouterTest --output-on-failure > .../test-5-reverse-red.log 2>&1` (`build/tests`) | **8** | Same assertion fails: `a redirected description was followed and a mapping was made`. 11 passed, 1 failed. |
| Proof: restore fix | `git apply .../fix.patch` (worktree) | 0 | Fix restored. |
| Proof GREEN build | `cmake --build build --target zene UpnpMapperFakeRouterTest -j2 > .../build-6-reapply.log 2>&1` (worktree) | 0 | Rebuilt with the fix. |
| **Proof GREEN test** | `ctest -R UpnpMapperFakeRouterTest --output-on-failure > .../test-6-reapply-green.log 2>&1` (`build/tests`) | **0** | 100% passed. |

Note: the RED build's first attempt (exit 2) was an ccache temp-directory permission failure in the environment, not a compile error. Every later build used `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p9-363`.

## Gates: RED, and not caused by this change

| Gate | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| Complexity, this tree | `bash tests/complexity-gate.sh --check > .../gate-1-complexity.log 2>&1` (worktree) | **1** | 6 REGRESSION lines, in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`. None of these is a file I changed. |
| File length, this tree | `bash tests/file-length-gate.sh --check > .../gate-2-file-length.log 2>&1` (worktree) | **1** | 14 REGRESSION lines for other files (`SmfInterchangeReader.cpp`, `TakeLane.cpp`, several `tests/src/core/*Test.cpp`, and others). None of these is a file I changed. |
| Complexity, unmodified HEAD | `git archive HEAD \| tar -x -C /tmp/new-p9-363-head-proof`, then `bash tests/complexity-gate.sh --check > .../gate-3-head-complexity.log 2>&1` in that export | **1** | Same 6 REGRESSION lines. The `diff` of the two lists is empty. |
| File length, unmodified HEAD | same export, `bash tests/file-length-gate.sh --check > .../gate-4-head-file-length.log 2>&1` | **1** | Same 14 REGRESSION lines. The `diff` of the two lists is empty. |

Both gates fail identically on the unmodified commit (`b02a0ff2b`), and the flagged files are not any of the three files this change touches. The baseline files (`tests/complexity-baseline.tsv`, `tests/file-length-baseline.tsv`) are unchanged. I did not fix these failures: they are in about 20 unrelated files, and clearing them is a separate job from this cluster. I did not edit any gate limit or baseline. The export at `/tmp/new-p9-363-head-proof` was removed after the run.

## Scope notes

- Not ported: `c57eabdc3`'s codec-test copy of the redirect regression (`descriptionRedirectCannotChangeRouter`, with `#define private public`) and its `LocalHttpEndpoint` helper. The fake-router test covers the same behaviour without that hack.
- Not ported: `fb9095c7d`'s codec change, which is superseded by `isRouterHttpUrl` (above).
- No new control commands. No new product files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged; `PortMappingUpnp.cpp`, `PortMapping.h` and `UpnpMapperFakeRouterTest.cpp` were already registered.
- The `ctest` counts above are CTest-level for the family (4 of 4 executables) and function-level only for `UpnpMapperFakeRouterTest` (12 of 12). The codec and orchestrator test functions were not counted individually.

## Files committed

- `include/PortMapping.h`
- `src/core/PortMappingUpnp.cpp`
- `tests/src/core/UpnpMapperFakeRouterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-363-port.md`
