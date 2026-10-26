FALSE-POSITIVE

## Evidence

| Location | Evidence |
|---|---|
| `/usr/include/lilv-0/lilv/lilv.h:1266-1274` | The `lilv_port_get_symbol()` declaration returns `const LilvNode*`; its contract says the node is owned by the port and must not be freed. |
| `/usr/include/lilv-0/lilv/lilv.h:1276-1282` | `lilv_port_get_name()` has the opposite contract: it returns a node the caller must free. `PortBase::name()` correctly wraps that allocated result in `AutoLilvNode`. |
| `src/core/lv2/Lv2Ports.cpp:299-302` | `PortBase::uri()` converts the borrowed symbol to a string while its owning port remains alive; it does not own a Lilv node. |
| `src/core/lv2/Lv2Proc.cpp:849-851`, `src/gui/Lv2ViewBase.cpp:120-122` | The other two `lilv_port_get_symbol()` call sites also convert the port-owned node without freeing it. |

Wrapping the symbol in `AutoLilvNode` would be invalid: the API returns a `const` borrowed pointer, while `AutoLilvNode` invokes `lilv_node_free`. A compile attempt confirmed the type mismatch. No production or test changes are retained.

## Review matrix

| Case | Result |
|---|---|
| Normal/repeated lookup | The symbol remains owned by the port. A temporary 32-call fixture check returned `latency` on every call. |
| Failure, exhaustion, reconfiguration | Not applicable: this accessor returns an existing port-owned node; it does not allocate from a bounded pool or have mutable configuration. |
| Realtime | No changes. These symbol reads occur during port registration or GUI setup; they are not plugin render callbacks. |
| Supported platforms | No source change. The declaration and ownership contract come from the configured Lilv API. |

No leak regression test is retained because it would assert the opposite of Lilv's ownership contract. An exploratory source assertion requiring `AutoLilvNode` failed as expected for the correct borrowed-node implementation and was removed; it was not a valid regression test. The final existing LV2 family test passed.

## Manifest gate repair

The initial gate failure was independent of this false-positive: the whole-tree manifest had 1,148 extra `plugins/SfzPlayer/sfizz/` entries, all excluded by the manifest's own recipe. `fork-sources-gate.sh --verbose` reported no unregistered source files; its `fork-sources` failure came from its nested whole-tree reproduction check. I reran the exact `Regenerate with` recipe from `tests/all-sources.txt`, preserving the comments and replacing only the entry list. The result was 0 additions and 1,148 removals, all under that vendored tree. `tests/all-sources-reproduce.sh` and `tests/fork-sources-gate.sh` then both passed.

The manifest recipe command was run with its output captured as `all-sources-generated-1.txt` and exit status recorded:

```bash
{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/all-sources-generated-1.txt; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

## Commands and results

| Command | Exit | Result |
|---|---:|---|
| `cmake --build build --target Lv2PortsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 2 | No generated rule for the temporary target. |
| `cmake -S . -B build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/configure-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 97 | `bunny-guard` rejected explicit configure because the parent owns configuration. |
| `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | 8 | Exploratory test failed only its invalid `AutoLilvNode` source assertion; its real fixture slot returned the symbol on all 32 calls. The assertion was removed. |
| `cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 2 | The attempted wrapper did not compile because Lilv returns `const LilvNode*`; this matches the API contract above. The attempt was reverted. |
| `cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Final unchanged product and existing test target built. |
| `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 1 | `fork-sources` and `all-sources-reproduce` fail; all other rows pass, including complexity and file length. The existing baseline note is `tests/upstream-modifications.txt:5077`. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-50 new-p18-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/submit-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 1 | Initial pre-repair check: report, commit, clean tree, and false-positive claim checks passed; only the two then-failing manifest gates failed. |
| `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/gate-repro-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | The manifest reproduces from its published recipe. |
| `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/gate-fork-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | All tracked source files are registered and the nested reproduction check passes. |
| `cmake --build build --target zene Lv2StateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Rebuilt the focused application and LV2 test target after the manifest repair. |
| `ctest -R '^Lv2StateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` (run from `build/tests`) | 0 | 1/1 CTest test passed. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/gate-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Final feature table passes all gates after the manifest re-derivation. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-50 new-p18-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-50.r1/logs/submit-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Final completion check passed after the manifest repair. |

The regression RED/GREEN proof is **not applicable** to this false-positive finding. The affected-family count is 1 existing CTest test, passed; no regression test was added.

## Feature gate table

| Gate | Result |
|---|---|
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |
| feature_check.py overall | PASS (exit 0; final rerun recorded after this report update) |

## Self-check

- ASK 1 — Regression RED/GREEN: not applicable to a false-positive. No product patch exists to reverse; the exploratory test that demanded an invalid free exited 8 and was removed. The real LV2 test target passes (CTest exit 0).
- ASK 2 — Determinism: no retained leak regression test because Lilv returns borrowed storage. The exploratory fixture lookup used 32 direct calls without time, sleeps, or races; it checked the symbol value, not release behavior.
- ASK 3 — Cause and siblings: checked all three `lilv_port_get_symbol()` call sites in `PortBase::uri()`, `Lv2Proc.cpp`, and `Lv2ViewBase.cpp`. The borrowed-node contract applies at each call. The adjacent allocated `lilv_port_get_name()` result remains correctly wrapped.
- ASK 4 — Collateral/data: no source, test, project-format, or saved-data behavior changed. The only scope edit removes vendor paths the published manifest recipe excludes; it adds 0 paths and leaves all first-party entries intact.
- ASK 5 — False-positive evidence: `/usr/include/lilv-0/lilv/lilv.h:1266-1274` returns `const LilvNode*` owned by the port and explicitly forbids freeing it; `lilv_port_get_name()` at lines 1276-1282 has the opposite caller-free contract.

## Committed files

- `docs/qwen-fixes-2026-10/cluster-new-p18-50.md` (this report)
- `tests/all-sources.txt` (entry list re-derived from its own recipe)
- `tests/upstream-modifications.txt` (records the manifest repair)
