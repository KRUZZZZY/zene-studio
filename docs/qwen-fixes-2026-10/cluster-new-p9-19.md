FIXED

# Cluster new-p9-19 — Key filter publishing can spin without a bound on the audio thread

## Verdict

REAL. `KeyFilterMailbox::publish()` (`plugins/Compressor/CompressorKeyFilter.h:94`) retried in
`while (m_dirty.load())` with no bound. `CompressorEffect::calcKeyFilter()`
(`plugins/Compressor/Compressor.cpp:251-258`) publishes from whichever thread moved a key-filter
knob, and the knob models are connected with `Qt::DirectConnection`
(`plugins/Compressor/Compressor.cpp:98-99`), so automation moves them on the audio thread. A
publisher that owns the version therefore loops once per dirty mark from any other thread that
moves a knob, and the audio callback can spend an unbounded time in `publish()`.

The fix bounds each `publish()` call to `MaxDesignsPerPush` (2: one design plus one catch-up). If
a design is still owed when the budget is spent, `m_dirty` stays true and
`CompressorEffect::processImpl()` (`plugins/Compressor/Compressor.cpp:270-273`) finishes it at the
top of a later block by calling `calcKeyFilter()` and adopting the result. Because `make()` reads
the current knob values, that later design preserves the latest control values. Nothing new on the
audio thread allocates, frees, locks, waits, logs or emits.

## Evidence (current branch)

| What | Where |
| --- | --- |
| Unbounded retry (pre-fix) | `plugins/Compressor/CompressorKeyFilter.h:92` `while (m_dirty.load())` |
| Audio-thread publisher | `plugins/Compressor/Compressor.cpp:98-99` `Qt::DirectConnection` to `calcKeyFilter()` |
| `publish()` designs from the live knob values | `plugins/Compressor/Compressor.cpp:255-257` |
| Bounded loop (fix) | `plugins/Compressor/CompressorKeyFilter.h:96-106` `for (unsigned attempt = 0; attempt < MaxDesignsPerPush; ++attempt)` |
| Pending flag (fix) | `plugins/Compressor/CompressorKeyFilter.h:109-112` `pending()` |
| Later-block completion (fix) | `plugins/Compressor/Compressor.cpp:270-273` `if (m_keyMailbox.pending()) { calcKeyFilter(); ... }` |
| Reader contract unchanged | `plugins/Compressor/CompressorKeyFilter.h:114-126` `take()` — one attempt, no wait |

The header is fork-NEW (`tests/fork-sources.txt:1528`). `Compressor.cpp` is inherited and its new
divergence is declared in `tests/upstream-modifications.txt` (last line).

## Regression test

Added to the existing compressor header test, `tests/src/plugins/CompressorVisualizerTest.cpp`
(the file already includes `CompressorKeyFilter.h`; it is a fork-NEW test in
`tests/fork-sources.txt`):

`CompressorVisualizerTest::theKeyFilterPublishIsBounded` (`tests/src/plugins/CompressorVisualizerTest.cpp:132-175`)

It is deterministic and thread-free. The `make()` hook that runs inside `publish()` (while the
version is odd) performs a nested `publish()`, which is exactly what another thread does when it
finds the version odd: it marks the design dirty and returns. Eight such writers arrive. The test
counts the designs the outer `publish()` runs and requires at most 2; the old `while (m_dirty)`
loop ran 9. It then checks that the published design is complete and that a later publish of the
latest knob values is adopted, so the bound did not lose the newest controls.

Test matrix:

- normal path — one publication is taken exactly once, coefficients and `hpOn`/`lpOn` intact
  (`CompressorVisualizerTest::theVisualizerHoldsAreAtomic` and the first section of
  `theKeyFilterPublishIsBounded`).
- failure/interleaving — a `take()` from inside `make()` (odd version) must not adopt a torn
  design; this is the seam `theKeyFilterPublishIsBounded` uses.
- exhaustion — eight dirty marks cannot make `publish()` run more than 2 designs.
- reconfiguration/preservation — after the bound, publishing the current knob values is adopted.
- audio-thread constraints — the added audio path is one atomic load plus, only while a design is
  pending, one bounded `calcKeyFilter()`; `designKeyFilter()` is pure stack arithmetic and the
  model read is `publishedValue()`, already the automation path.
- supported platforms — test uses QtTest, `QStringLiteral`/`qPrintable` and `std::atomic` only
  (Qt 5.15 / MSVC / macOS compatible); `std::atomic<float>::is_always_lock_free` is already
  assumed by the unchanged `take()` path.

## Commands and EXIT codes (logs in `.../new-p9-19.r1/logs/`)

RED (product fix reversed with `git apply -R`; final tree's test):

| Command | EXIT | Log |
| --- | --- | --- |
| `git diff -- plugins/Compressor/CompressorKeyFilter.h plugins/Compressor/Compressor.cpp > logs/fix.patch` | 0 | — |
| `git apply -R logs/fix.patch` | 0 | — |
| `cmake --build build --target CompressorVisualizerTest -j4` | 0 | `build-7-red.log` |
| `ctest -R CompressorVisualizerTest --output-on-failure` (from `build/tests`) | **8** | `test-4-red.log` — `FAIL!: theKeyFilterPublishIsBounded ... publish() ran 9 designs ... ; the audio thread is unbounded` |

GREEN (fix restored):

| Command | EXIT | Log |
| --- | --- | --- |
| `git apply logs/fix.patch` | 0 | — |
| `cmake --build build --target CompressorVisualizerTest -j4` | 0 | `build-8-green.log` |
| `cmake --build build --target compressor -j4` | 0 | `build-9-compressor-green.log` |
| `ctest -R CompressorVisualizerTest --output-on-failure` | **0** | `test-5-green.log` |

Family (`ctest -R Compressor`): `SfzCompressorStereoLinkTest` was not built yet, so it was built
with `cmake --build build --target SfzCompressorStereoLinkTest -j4` (EXIT 0,
`build-10-sfztest.log`), then `ctest -R Compressor --output-on-failure` (EXIT 0,
`test-7-family.log`): 2/2 passed.

The same RED result was also obtained before the first GREEN build (`test-1-red.log`, EXIT 8,
9 designs) with the unmodified header.

Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
(EXIT 0, `feature-check-1.log`):

```
PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
     no-tautology / doc-figures / complexity / file-length
RESULT: PASS
```

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** Reverted:
  `git apply -R logs/fix.patch` EXIT 0, build EXIT 0, ctest EXIT 8 (9 designs). Restored:
  `git apply logs/fix.patch` EXIT 0, build EXIT 0, ctest EXIT 0. Logs `test-4-red.log` /
  `test-5-green.log`.
- **Deterministic.** No clock, sleep or thread: the interleaving is forced by calling
  `mailbox.publish()` from inside the outer `make()` while the version is odd, and the assertion
  is a count of designs (<= 2) with an exact value comparison of the adopted design.
- **Fix at the cause and complete.** `calcKeyFilter()` is the only caller of
  `KeyFilterMailbox::publish()`; `processImpl()` is the only caller of `follow()`/`take()`. Both
  were checked: the publisher is bounded where the loop was, and the only audio-thread consumer
  drains a pending design before the block. The wider class is unchanged for normal use (one
  design, nothing pending). The other copy of this seqlock, `plugins/Lb302/Lb302FilterMailbox.h`,
  is a separate plugin and its three connections are ordinary `AutoConnection`s (not
  `DirectConnection`), so it is not this audio-thread path; it is listed under Other findings.
- **No collateral damage.** No save/load or model format changed; the public contract of
  `publish()`/`take()` is unchanged, and the only new limit (2 designs per call) is internal and
  cannot reject another caller — the remaining work is completed by the next call/block. A single
  uncontended knob move still designs once.
- **Undo/commands/realtime.** No undo step and no control command added. The audio-thread additions
  are one atomic load per block and, only when a design is pending, one bounded `calcKeyFilter()`
  (pure arithmetic + atomic stores, no allocation/lock/wait/I/O/log/signal).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable; verdict is FIXED.
- **Sibling commands in command files (`app.*`, `arrangement.*`, ...):** this cluster changed no
  command file. The command sources the pre-submit check lists are inherited from the branch's
  base commits, not from this fix, so none is affected by the bounded key-filter hand-off.
- **Windows/macOS portability:** no Qt-6-only API, no `windows.h` names, no `std::filesystem`; the
  test uses `QStringLiteral`, `qPrintable`, `QCOMPARE`/`QVERIFY` and `std::atomic` only.

## Other findings

- `plugins/Lb302/Lb302FilterMailbox.h:63` carries the same `while (m_dirty)` retry. It is not in
  this finding and its publishers are control-thread (`AutoConnection`, no `DirectConnection`), so
  it is not changed here; it could still spin a control thread under a matching writer pattern and
  may warrant its own cluster.
