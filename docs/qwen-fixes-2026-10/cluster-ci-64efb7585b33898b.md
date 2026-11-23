FIXED

# Cluster ci-64efb7585b33898b: windows-arm64 build break at af139394af21

Classification: **test** source portability, not product and not infrastructure. The breakage needs clang plus Qt 5.15, which is what the hosted windows-arm64 MSYS2 job uses. The local build uses GCC and Qt 6 and accepts both constructs. Reproduced on af139394af21 (HEAD of this branch at the start) with clang++ 23.1.5 and the Qt 5.15.19 headers in /usr/include/qt.

## Cause (file:line)
1. `tests/src/core/LinkAudioStreamTestSupport.h:358`: `peer.bind(QHostAddress::LocalHost, 0)`. Before Qt 6.2, QAbstractSocket has only `bind(const QHostAddress&, quint16, BindMode)` and `bind(quint16, BindMode)`. The enum converts to quint16 by a standard conversion and to QHostAddress by a user-defined one. The literal `0` converts to BindMode only through QFlags' user-defined Zero constructor, so neither overload wins: "call to member function 'bind' is ambiguous". The same latent error is at `tests/src/core/ControlOscServerTest.cpp:113`; CI stopped before compiling that file. Lines 145 and 154 there pass a non-literal `m_port`, so they are not ambiguous. They were changed to the same explicit form for consistency, which does not change behaviour.
2. `tests/src/core/LinkAudioCodecTest.cpp:90`: `QVERIFY_EXCEPTION_THROWN(AudioRing(std::numeric_limits<std::size_t>::max()), ...)`. The macro pastes the expression as the statement `expression;`, and `AudioRing(X::max());` is the most vexing parse (a function declaration), so clang reports "definition or redeclaration of 'max' not allowed inside a function". Siblings such as `RmsHelper(0)`, `ValueBuffer(-1)` and `TimeSig(4, 0)` cannot parse as declarations and were left alone.

## Fix
- `bind(QHostAddress(QHostAddress::LocalHost), ...)`, with the address built explicitly (4 sites).
- `QVERIFY_EXCEPTION_THROWN(static_cast<void>(AudioRing(...)), std::length_error)`. It is still the same expression, and it still throws.
No product code changed, and no assertion was weakened: every check is the same.

## Regression
`tests/qt5-clang-test-portability-check.py`, registered as ctest `Qt5ClangTestPortabilityCheck` in tests/CMakeLists.txt. It has two parts:
- a source scan of the tracked C/C++ files, run on every host, for both patterns;
- a clang plus Qt5 compile probe. Known-bad fixtures must be rejected and the fixed forms must compile. The probe skips on GCC or Qt6.
It also runs its own self-test on fixture lines.

## Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs)
- Reproduction before the fix, `bash /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs/repro-commands.sh` (clang++ -fsyntax-only, Qt5): LinkAudioCodecTest EXIT=1, LinkAudioStreamTest EXIT=1, ControlOscServerTest EXIT=1 (01..03-repro-*.log). The errors match CI at LinkAudioCodecTest.cpp:90:72 and LinkAudioStreamTestSupport.h:358:15, plus ControlOscServerTest.cpp:113:20.
- `cmake --build build --target zene LinkAudioCodecTest LinkAudioStreamTest ControlOscServerTest -j4`: EXIT=0 (20-build-green.log).
- `cd build/tests && ctest -R '^(LinkAudioCodecTest|LinkAudioStreamTest|ControlOscServerTest|Qt5ClangTestPortabilityCheck|ClangTemplateKeywordCheck)$' --output-on-failure`: EXIT=0, "100% tests passed out of 5" (21-ctest-green.log).
- `bash /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs/repro-commands.sh` with the fix: all three files EXIT=0 (22-repro-green.log).
- Step 4b: `git diff -- tests/src/core/LinkAudioStreamTestSupport.h tests/src/core/ControlOscServerTest.cpp tests/src/core/LinkAudioCodecTest.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs/fix.patch`: EXIT=0. Then `python3 tests/qt5-clang-test-portability-check.py --root . --compiler /usr/bin/clang++`: **EXIT=1**, listing all five sites (24-check-red.log), and the clang+Qt5 repro gave all three EXIT=1 (25-repro-red.log). Then `git apply /home/kruzzzzy/.config/kads/zene/state/runs/ci-64efb7585b33898b.r1/logs/fix.patch`: EXIT=0, the check gave **EXIT=0** (26-check-green2.log), and the repro gave all three EXIT=0 (27-repro-green2.log).
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT=0, RESULT: PASS, every row PASS (30-feature-check.log).

## Self-check
- Realtime, undo and commands: only test sources and a Python check changed. Nothing touches the audio thread, undo or control commands, and the ControlCommandsSnapshot canary is unaffected.
- Saved data and old projects: unaffected, because no product code changed.
- Siblings: every `bind(QHostAddress::` in the tree was found with `rg -n "bind\(QHostAddress::"` (4 sites, all fixed). Every `QVERIFY_EXCEPTION_THROWN(Type(` use was checked; only LinkAudioCodecTest.cpp:90 parses as a declaration.
- Registration: the new script is in tests/fork-sources.txt. The tests/CMakeLists.txt change (an upstream file) is recorded in tests/upstream-modifications.txt. The three edited test files are already fork files.
- GUI and Song/Record/Undo/Transport/AudioEngine files: none touched, so no screenshot or state-sequence is needed.
- Red/green: with the fix reverted (\`git apply -R fix.patch\`, EXIT=0), Qt5ClangTestPortabilityCheck gave EXIT=1 (24-check-red.log) and the clang+Qt5 compile of the real files gave EXIT=1 x3 (25-repro-red.log). With the fix reapplied (EXIT=0), the check gave EXIT=0 (26-check-green2.log) and the compile gave EXIT=0 x3 (27-repro-green2.log).
- Deterministic: the check is a static text scan plus \`clang++ -fsyntax-only\` on fixed fixture text. It has no timing, sleeps, threads or network.
- Complete at the cause: see "Siblings" above. All four \`bind(QHostAddress::\` sites and the only declaration-shaped exception expression were fixed.
- No collateral damage: the added check reads sources only, and it limits nothing at runtime.
- ALREADY-FIXED / FALSE-POSITIVE: not applicable (verdict FIXED, reproduced in 01..03-repro-*.log).
- Sibling control commands (app.*, arrangement.* ...): this cluster changes no command files. The list comes from the branch-wide diff against base, and none of those commands is affected by a test-only change.
- Not verified: an actual Windows arm64 build. The equivalent toolchain combination, clang plus Qt 5.15 headers, was verified locally with -fsyntax-only. Qt5 builds of the whole tree were not run locally.

Delegation: Haiku reproduced the clang+Qt5 errors and wrote repro-commands.sh. Sonnet wrote qt5-clang-test-portability-check.py and its CMake and fork-sources registration. Haiku ran the build, ctest and the 4b red/green proof. I diagnosed the cause, applied the 5 one-line fixes, reviewed the diffs, and wrote the report and the commit.

## Other findings
- The check is a text scanner: an exception expression that spans several lines is not examined, and a plain function call shaped like `f(x)` inside the macro would be flagged. Neither occurs in the tree today.
