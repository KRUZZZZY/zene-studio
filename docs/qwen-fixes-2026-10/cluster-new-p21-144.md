Verdict: FALSE-POSITIVE

new-p21-144: "EqEffect processImpl may discard all filter writes by copying SampleFrame" is not real on this branch. `inOut.frame(f)` returns a `std::span` view over the interleaved buffer, not a copy of the samples, so `auto frame = ...` followed by `frame[0] = ...` writes into the buffer. No code change is made.

## Evidence

- The loop in question is `plugins/Eq/EqEffect.cpp:198-200`: `for (f_cnt_t f = 0; f < inOut.frames(); ++f)` then `auto frame = inOut.frame(f);`. The band writes assign through `frame`, from `EqEffect.cpp:207` (`frame[0] = m_hp12.update(frame[0], 0, periodProgress);`) through `:258` (`m_highShelf`).
- The type of `inOut` is `InterleavedBufferView<float, 2>` (`plugins/Eq/EqEffect.cpp:96`).
- `include/AudioBufferView.h:359-369`: `frame(index)` returns `std::span<T, channelCount>{framePtr(index), Base::channels()}` for static channel counts (`std::span<T>` for dynamic). `framePtr` (`AudioBufferView.h:375`) points into the interleaved storage. A span is a pointer-and-size view, so copying it aliases the same samples. `operator[]` on a span returns `T&`, so `frame[0] = x` is a store into the buffer.
- Runtime probe (`logs/span_probe.cpp`, built against the real `include/AudioBufferView.h`, compile/link exit 0, run exit 0, output `writes_propagate=yes`). It writes through `auto frame = view.frame(f)` for every frame and reads the original `std::vector<float>` back. A `static_assert` in the probe confirms the type is `std::span<float, 2>`.
- The suggested fix `auto& frame = inOut.frame(f);` does not compile: `frame()` returns a prvalue, and a non-const lvalue reference cannot bind to it. Log: `logs/ref-probe-1.log` (`ref_probe.cpp`, exit 1, "cannot bind non-const lvalue reference of type 'std::span<float, 2>&' to an rvalue").
- Existing coverage in the live test family (`tests/src/plugins/AudioPluginTest.cpp:637-654`, `eqActiveFilterKeepsOneFrameOutputFinite`) processes an EQ block with an active filter and asserts the output is not the input values (`QVERIFY(data[0].left() != 0.8f || data[0].right() != -0.4f)` at line 654). This test was cited from source and not run here.

Limitation: the probe proves the aliasing mechanism. It does not prove band-level audible behaviour of the EQ. The existing AudioPluginTest assertion is the only evidence for that, and I did not run it.

## Exact commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-144.r1/logs/`.

| Step | Command (run from the worktree) | Log | EXIT |
| --- | --- | --- | --- |
| Probe compile, first attempt (missing `lmmsconfig.h`) | `g++ -std=c++20 -I include -fsyntax-only span_probe.cpp` | `probe-compile-1.log` | 1 (include path, not a finding) |
| Probe compile, namespace error | `g++ ... -I <build> -fsyntax-only span_probe.cpp` | `probe-compile-2.log` | 1 (namespace `lmms::`, not a finding) |
| Probe build | `g++ -std=c++20 -I include -I build -I logs -o span_probe span_probe.cpp` | `probe-build-4.log` | 0 |
| Probe run | `./span_probe` | `probe-run-3.log` | 0 (`writes_propagate=yes`) |
| Suggested `auto&` form | `g++ -std=c++20 -I include -I build -I logs -fsyntax-only ref_probe.cpp` | `ref-probe-1.log` | 1 |
| Gate wrapper | `python3 .../feature_check.py <worktree>` | `gate-feature-check-1.log` | 1 (see below) |
| fork-sources | `bash tests/fork-sources-gate.sh` | `gate-fork-sources-3.log` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | `gate-all-sources-reproduce-3.log` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | `gate-no-upstream-regression-3.log` | 0 |
| check-namespace | `tests/scripted/check-namespace` | `gate-check-namespace-5.log` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | `gate-no-tautology-3.log` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | `gate-doc-figures-3.log` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | `gate-complexity-3.log` | 1 (7 `REGRESSION` lines) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | `gate-file-length-3.log` | 1 (22 `REGRESSION` lines) |

Gate notes:
- The `feature_check.py` wrapper exited 1 with `PermissionError` at `feature_check.py:71`. It creates `<worktree>/../<name>-feature-check` (`zene-studio-program/zene-qwen-new-p21-144-feature-check`), which is outside this worktree and outside my write-jail. I did not edit the script. I ran each gate command the wrapper runs (listed above) directly, from the worktree.
- The two ratchet gates exit 1 on items in other files, such as `plugins/Vst3Effect/Vst3Host.cpp`, `src/wasm/WasmSandbox.cpp`, and `src/core/TakeLane.cpp`. This branch changes no product file, so none of those items can be "new in your files". This is the expected train-level red. I did not compare against the base listing cache.

## Regression test

Not added. The finding is not real, so there is no fix to prove. The regression procedure (RED / GREEN / reverse-apply) does not apply to a false positive. The probe above is the evidence that the current code already does what the finding says it does not.

## Self-check

- *The regression test FAILS with the product change reverted and PASSES with it.* Not applicable: no product change, verdict FALSE-POSITIVE. The probe shows the current code writes through to the buffer (exit 0).
- *The test is deterministic.* Not applicable (no test added). The probe is deterministic: fixed input, no timing.
- *The fix is at the cause and complete.* Not applicable: no fix. The suggested fix would not compile.
- *No collateral damage.* Nothing changed in product code, so no behaviour changed.
- *FALSE-POSITIVE: the code (file:line) or intended contract.* `plugins/Eq/EqEffect.cpp:198-200` and `207-258`, `include/AudioBufferView.h:359-369` and `:375`, and `tests/src/plugins/AudioPluginTest.cpp:654`.
- *Gates.* The wrapper is red for an environmental reason (write-jail on its log directory); the individual gate commands are green except the two ratchets, whose red items are in files this branch does not touch.
