# Cluster new-p12-61 — Failed bounded inflation leaves partial output behind

**Verdict: FALSE-POSITIVE**

## Evidence

The described bounded inflater and its caller-visible byte buffer are absent on
this branch. `readProjectDocument` at `src/core/ControlProjectAssets.cpp:183-195`
assigns `qUncompress(stored)` to a local `QByteArray xml`, checks for an empty
result, and returns `false` on that path. The function signature in
`src/core/ControlProjectAssetsShared.h:101-104` exposes a `QDomDocument*`, format
and error output; it has no `QByteArray* out`. The header's “nothing partial”
statement at `include/ControlProjectAssets.h:138-145` describes the reference
count and per-file hash bounds, not inflation output.

Code inventory command:

```text
rg -n "qUncompress|uncompress|inflateInit|inflateEnd|inflate\(|bounded.*infl|infl.*bounded|inflate.*out" src include tests/src tests
EXIT=0
```

Since the reported behavior is not present in the current caller-visible path,
no regression test, build or `ControlProjectArchiveTest` run was appropriate.

## Evidence table

| Check | Command / result |
| --- | --- |
| RED | N/A — false-positive; no regression test run |
| GREEN | N/A — false-positive; no regression test run |
| Affected-family test count | N/A — `ControlProjectArchiveTest` was not run |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-61.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-61.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` — EXIT=0 |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p12-61.md` |

The session did not expose the requested `fff grep` tool. The three single-term
log searches used as a fallback returned no matches:

```text
rg -n 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-61.r1/logs — EXIT=1 (no matches)
rg -n 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-61.r1/logs — EXIT=1 (no matches)
rg -n 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-61.r1/logs — EXIT=1 (no matches)
```
