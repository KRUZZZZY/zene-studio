# Cluster new-p9-358

**Verdict: FALSE-POSITIVE**

The reported calls are at [PinConnector.cpp:454](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-358/src/gui/PinConnector.cpp:454) and [PinConnector.cpp:465](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-358/src/gui/PinConnector.cpp:465). In the configured Qt 6 headers, `QMouseEvent::pos()` is declared as an inline compatibility accessor without a deprecation annotation (`/usr/include/x86_64-linux-gnu/qt6/QtGui/qevent.h:210-211`). The same header marks the related `QMouseEvent::x()`, `y()`, and `globalPos()` methods deprecated. The configured build has `USE_WERROR=ON` (`build/CMakeCache.txt:573`), and a syntax-only compile of the actual translation unit with `-Werror=deprecated-declarations` succeeded. The finding does not reproduce; no product or test changes were made.

## Evidence

| Check | Exact command | Result |
| --- | --- | --- |
| RED regression | Not applicable: the finding is false-positive, so no regression was added. | N/A |
| Qt 6 strict compile | `python3 -c 'import json,shlex,subprocess; entries=json.load(open("build/compile_commands.json")); entry=next(item for item in entries if item["file"].endswith("/src/gui/PinConnector.cpp")); args=shlex.split(entry["command"]); out=args.index("-o"); del args[out:out+2]; args[args.index("-c")]= "-fsyntax-only"; args.append("-Werror=deprecated-declarations"); raise SystemExit(subprocess.run(args, cwd=entry["directory"]).returncode)' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-358.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | EXIT=0 |
| GREEN regression | Not applicable: no fix was needed. | N/A |
| Affected-family tests | Not run; no code changed. | 0 ctest tests run |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-358.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | EXIT=0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-358.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | EXIT=0 |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-358.md` | Report only |

Gate logs: `gate-3.log` and `gate-4.log` in the requested log folder. The `fff` grep tool was unavailable in this session, so the logs were searched with `rg -n -e error -e FAILED -e Assertion`; no matches were found.
