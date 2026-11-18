# Cluster new-p8-93

Verdict: **FIXED**

`ControlServer::sendBytes()` now applies `MaxQueuedReplyBytes` to the unsent tail after `writeWhatFits()` reports a full socket. It drops the client when that tail is over the cap. A complete write is not capped by total reply size, and an over-8-MiB reply is retained when its remaining tail fits.

## Evidence

- `src/core/ControlServer.cpp:427-449`: the existing queue path still checks the combined bytes retained in `pending` (line 432). On the first partial write, the new check uses `bytes.size() - written` before creating `pending` (line 445). Complete writes bypass this tail check.
- `tests/src/core/ControlShutdownHookTest.cpp:203-264`: the over-cap-tail case constrains the receive buffer and asserts that the server sends a prefix, then closes before completing the reply.
- `tests/src/core/ControlShutdownHookTest.cpp:267-334`: the over-8-MiB case checks `FIONREAD` to prove the accepted prefix is larger than the reply's overage, while still partial, then verifies that the entire reply arrives.
- Both product-new C++ files are already registered in `tests/fork-sources.txt` (lines 1404 and 1936); no manifest change was needed.

## Build and test evidence

Commands ran from the worktree root unless stated otherwise. CTest ran from `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs`.

| Evidence | Exact command | Exit |
|---|---|---:|
| Initial regression build, before the production edit | `cmake --build build --target ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/build-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| Initial RED | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/test-1.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 8 |
| Final regression RED with only the production change reverted | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/test-8.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 8 |
| Final fixed build (`zene` and affected test target) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/build-10.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| Final GREEN | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/test-9.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/gate-5.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-93.r2/logs/gate-6.log 2>&1; status=$?; printf 'EXIT=%s\n' "$status"; exit "$status"` | 0 |

The affected CTest family ran one registered test, `ControlShutdownHookTest` (1/1). The final RED assertions fail without the production change; the same test passes after restoring it. The final GREEN log reports 100% tests passed.

The requested `fff` grep tool was not exposed in this session (`command -v fff` exited 1). I searched the numbered logs with `rg` for `error`, `FAILED`, and `Assertion`; matches were only from the intentional RED runs.

## Files in this commit

- `src/core/ControlServer.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-93.md`
