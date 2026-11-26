FIXED

# cluster ci-fd7a243c2685c9ce: the macOS lane cannot compile LinkAudioCodecTest

## Verdict

FIXED. Classification: **test source** — the product code is correct and was not touched.

At the pinned commit `3436a808b6` the hosted `macos-x86_64` lane dies in the build:

```
tests/src/core/LinkAudioCodecTest.cpp:90:72: error: definition or redeclaration of 'max' not allowed inside a function
   90 |   QVERIFY_EXCEPTION_THROWN(AudioRing(std::numeric_limits<std::size_t>::max()), std::length_error);
      |                                                    ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^
```

The finding's own log path (`…/state/ci/3436a808b65a…/run-45603756511-failed.log`) does not exist on this box, so the
CI excerpt in the finding is the CI-side evidence; it is reproduced here with the same file, line, column and wording
(rows 4 and 9 below).

Cause, in the language, not in the product: `AudioRing(std::numeric_limits<std::size_t>::max())` is a statement that
can be read two ways, and [dcl.ambig.res] resolves it as a *declaration* — return type `AudioRing`, function
`std::numeric_limits<std::size_t>::max()` with an empty parameter list. A block-scope declaration of that qualified
name is ill-formed, so clang rejects the translation unit. gcc parses the same statement as an expression (a
deviation), which is exactly why the Linux lane stayed green and only the clang lanes went red (rows 2 and 5). The
statement came in with `4f738a8756` (cluster new-p14-148; `git log -S` finds it as the introducing commit); gcc
accepted it, which is why the failure surfaced only on the clang lanes.

Fix: wrap the whole expression — `tests/src/core/LinkAudioCodecTest.cpp:92`:

```cpp
QVERIFY_EXCEPTION_THROWN((AudioRing(std::numeric_limits<std::size_t>::max())), std::length_error);
```

A declaration cannot begin with `(`, so the statement is unambiguously an expression; it is the same product call,
the same temporary and the same `std::length_error` requirement. `tests/src/core/ArrayVectorTest.cpp:137-160` already
uses this idiom for its own throwing temporaries. Wrapping only the argument does **not** fix it (row 3 measures
that), so the parentheses have to enclose the whole expression.

## Evidence (file:line)

- Failing statement: `3436a808b6:tests/src/core/LinkAudioCodecTest.cpp:90`; fixed form at
  `HEAD:tests/src/core/LinkAudioCodecTest.cpp:92`, comment at `:88-90`.
- The product contract the test guards, unchanged: `include/LinkAudioStream.h:117-119` (`class AudioRing`,
  `explicit AudioRing(std::size_t capacityFrames)`), `src/core/LinkAudioCodec.cpp:189-193` (constructor),
  `:85` (`roundUpPowerOfTwo`) and `:97` (`ringStorageSize`) — the two `throw std::length_error` sites the test
  exercises. Nothing here was edited.
- Siblings checked, all of the `QVERIFY_EXCEPTION_THROWN` sites in `tests/src/`: `ArrayVectorTest.cpp:137-160`
  already wraps (`(lmms::LocklessAllocator(1, 0))`), `InputChannelRingTest.cpp:55` uses the braced form
  (`{ InputChannelRing ring(...); }`), `AudioBufferTest.cpp`, `DeviceDspTestParts1.h:89`, `ImportFilterTest.cpp:70`
  and the rest pass method calls or literals, which cannot be declarations. `LinkAudioCodecTest.cpp:90` was the only
  bare `Type(qualified::name())` statement in the suite.
- No product caller is affected: `AudioRing` is constructed at `src/core/LinkAudioStream.cpp:105-106` only, with
  `AudioRingFrames`, not with a qualified-name argument.

## Commands and results

`W=/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-fd7a243c2685c9ce`,
`L=/home/kruzzzzy/.config/kads/zene/state/runs/ci-fd7a243c2685c9ce.r1/logs`. Every EXIT below is the code of the
command as run (builds and ctest were started as `… > $L/<log> 2>&1; echo EXIT=$?`).

| # | command | EXIT | log | what it shows |
|---|---|---|---|---|
| 1 | `clang++ -std=c++20 -fsyntax-only $L/scratch-mvp.cpp` | 1 | `scratch-mvp.log` | the ambiguity alone, `struct AudioRing` + the same statement: identical diagnostic, no Qt, no project |
| 2 | `g++ -std=c++20 -fsyntax-only $L/scratch-mvp.cpp` | 0 | `scratch-mvp-gcc.log` | gcc accepts the same snippet |
| 3 | `clang++ -std=c++20 -fsyntax-only $L/scratch-variants.cpp` | 1 | `scratch-variants.log` | four rewrites measured: `AudioRing((…max()))` still fails; `(AudioRing(…max()))`, `{ AudioRing ring(…max()); }` and `AudioRing{…max()}` compile |
| 4 | `python3 $L/clang-syntax-check.py $W tests/src/core/LinkAudioCodecTest.cpp` (source at `:90`, pre-fix) | 1 | `test-1-red-clang-prefix.log` | the CI failure reproduced here: `LinkAudioCodecTest.cpp:90:72: error: definition or redeclaration of 'max' not allowed inside a function` |
| 5 | `python3 $L/tu-syntax-check.py $W tests/src/core/LinkAudioCodecTest.cpp g++` (pre-fix) | 0 | `test-2-baseline-gcc-prefix.log` | the lane compiler accepts the pre-fix source — the defect is clang-only |
| 6 | `git diff -- tests/src/core/LinkAudioCodecTest.cpp > $L/fix.patch` | 0 | `step4b-1-patch-cycle.log` | the patch holds the whole change (no product file is touched, so there is no product hunk) |
| 7 | `git apply -R $L/fix.patch` | 0 | `step4b-1-patch-cycle.log` | fix reverted |
| 8 | `git diff --quiet -- tests/src/core/LinkAudioCodecTest.cpp` | 0 | `step4b-1-patch-cycle.log` | the reverted tree is byte-identical to `3436a808b6` |
| 9 | `python3 $L/tu-syntax-check.py $W tests/src/core/LinkAudioCodecTest.cpp clang++` (reverted) | 1 | `test-4-red-clang-reverted.log` | **RED**: without the fix the TU does not compile under clang |
| 10 | `git apply $L/fix.patch` | 0 | `step4b-1-patch-cycle.log` | fix restored |
| 11 | `python3 $L/tu-syntax-check.py $W tests/src/core/LinkAudioCodecTest.cpp clang++` (fixed) | 0 | `test-5-green-clang-reapplied.log`, `test-3-green-clang-postfix.log` | **GREEN**: with the fix clang compiles the TU, 0 errors |
| 12 | `cmake --build build --target LinkAudioCodecTest -j4` | 0 | `build-2-gcc-green.log` | `[100%] Built target LinkAudioCodecTest` |
| 13 | `ctest -R '^LinkAudioCodecTest$' --output-on-failure` (cwd `build/tests`) | 0 | `test-7-ctest-gcc-green.log` | `1/1 Test #685: LinkAudioCodecTest … Passed` |
| 14 | `$W/build/tests/LinkAudioCodecTest` | 0 | `test-8-gcc-direct.log` | `Totals: 7 passed, 0 failed` — includes `rejectsCapacityThatCannotBeRounded` |
| 15 | clang++ standalone compile+link of the same two TUs (same flags) | 0 | `build-3-clang-standalone.log` | clang can build the suite, not just parse it |
| 16 | `$L/scratch-obj/LinkAudioCodecTest-clang` | 0 | `test-6-clang-standalone-run.log` | under clang: `PASS … rejectsCapacityThatCannotBeRounded()`, `Totals: 7 passed, 0 failed` |
| 17 | `cmake --build build --target LinkAudioStreamTest -j4` | 0 | `build-4-gcc-streamtest.log` | the rest of the family binary |
| 18 | `ctest -R '^LinkAudio' --output-on-failure` (cwd `build/tests`) | 0 | `test-9-ctest-family-gcc.log` | `LinkAudioStreamTest` 6.16 s Passed, `LinkAudioCodecTest` 0.04 s Passed — 2/2 |
| 19 | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py $W` | 0 | `gates-3-feature-check-final.log` | all eight gate rows `PASS`, `RESULT: PASS` at the committed state |

`clang-syntax-check.py` in row 4 and `tu-syntax-check.py` in rows 5, 9 and 11 are the same replay: take the TU's own
command from `build/compile_commands.json`, swap the compiler, drop the two gcc-only flags (`-mno-direct-extern-access`,
`-Wno-error=sfinae-incomplete`) and `-Werror`, add `-fsyntax-only`. The second script only adds the compiler argument.

`build-1-baseline-gcc.log` is empty on purpose: that first build was still queued behind the shared build guard when
I cancelled it in favour of one build of the fixed tree; it produced no evidence and no claim rests on it.

## Test matrix

| path | how it is covered | result |
|---|---|---|
| normal capacity rounding | `normalCapacityRoundsUp` (`LinkAudioCodecTest.cpp:72-80`): 2, 3, `AudioRingFrames`, `AudioRingFrames + 1` | pass (row 14) |
| failure / refusal | `rejectsCapacityThatCannotBeRounded` (`:90-94`, comment `:88-89`), the statement clang rejected; `rejectsCapacityWhoseStorageWouldWrap` (`:82-86`) | pass under gcc and under clang (rows 14, 16) |
| exhaustion / boundary | `size_t{1} << 63` (rounding wrap) and `SIZE_MAX` (no power of two exists) both require `std::length_error` | pass (rows 14, 16) |
| reconfiguration | none: `AudioRing` has no runtime reconfiguration; the stream that owns rings is covered by `LinkAudioStreamTest` | pass (row 18) |
| audio thread | the change is test-only; no product file, no render/process path touched — nothing new allocates, locks, waits or logs on the audio thread | n/a by construction |
| supported platforms | gcc (lane, rows 12-14, 17-18) and clang (rows 11, 15-16). The macro used is `QVERIFY_EXCEPTION_THROWN`, which exists in Qt 5 and Qt 6, so the Qt 5.15/macOS/MSVC lanes see the same code path as before | pass |

## Self-check

1. **Regression fails without the fix, passes with it.** There is no product hunk to revert: the fix *is* the test
   source, and the failing step in CI is the compilation of that translation unit. So the regression is the compile
   of the TU with clang on the build's own flags: reverted, `EXIT=1` with the CI's diagnostic (row 9); restored,
   `EXIT=0` (row 11), and the rebuilt target's suite passes under gcc (rows 13-14, 18) and under clang (row 16).
   Row 5 (`EXIT=0` under gcc on the pre-fix source) is the reason a run-only test cannot be the regression here: gcc
   never rejects the ill-formed statement.
2. **Deterministic.** The regression executes no code: it is a front-end compile of one TU, no clock, no sleep, no
   thread. The runtime test it protects is a single constructor call that must throw, with no timing in it. The clang
   binary in row 16 takes 2 ms.
3. **At the cause and complete.** The cause is the parse of one statement, fixed in that statement. Every
   `QVERIFY_EXCEPTION_THROWN` in `tests/src/` was read (listed under Evidence): one already used this idiom, one used
   the braced form, the others cannot be declarations. The product side needs nothing: `AudioRing`'s constructor
   contract (`src/core/LinkAudioCodec.cpp:189-193`) is unchanged and its only caller
   (`src/core/LinkAudioStream.cpp:105-106`) passes a constant.
4. **No collateral damage.** The expression `AudioRing(std::numeric_limits<std::size_t>::max())` is unchanged inside
   the new parentheses, the required exception type is still exactly `std::length_error`, and no limit or check
   changed — so no other caller can be hit. No save format, project file or persisted data is involved (the test
   builds an in-memory ring). The neighbouring ring tests still pass (row 18).
5. **ALREADY-FIXED / FALSE-POSITIVE.** Not claimed; the defect was present at `HEAD` (`tests/src/core/LinkAudioCodecTest.cpp:90`)
   and reproduced this session (row 4).

## Other findings

- The macOS lane stopped at this TU, so test files scheduled after it were never compiled by clang in that run. This
  report claims clang-cleanliness for `LinkAudioCodecTest.cpp` only, and that the rest of the suite contains no other
  bare `Type(qualified::name())` statement (Evidence), not that the whole suite is clang-clean.
- `QVERIFY_EXCEPTION_THROWN` is deprecated in Qt 6.3+ (the Qt 6.10 header here routes it through
  `QVERIFY_THROWS_EXCEPTION`). Switching macros is a separate change: the Qt 5.15 lane still needs
  `QVERIFY_EXCEPTION_THROWN`, so this report leaves the macro alone.
