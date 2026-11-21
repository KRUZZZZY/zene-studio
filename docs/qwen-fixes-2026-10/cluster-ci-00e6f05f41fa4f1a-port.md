ALREADY-FIXED

# Cluster ci-00e6f05f41fa4f1a-port

The original failure was in run 43764792400:

```
tests/src/plugins/SfzDistoBuffersTest.cpp:60: use of undeclared identifier 'posix_memalign'
```

The Windows path is guarded by `_WIN32` rather than `_MSC_VER`, so MinGW/clang
Windows builds take the `_aligned_malloc` branch and the POSIX branch stays
behind `#else`.

## File:line guards

- `tests/src/plugins/SfzDistoBuffersTest.cpp:10` — `#if defined(_WIN32)` around `<malloc.h>`.
- `tests/src/plugins/SfzDistoBuffersTest.cpp:56-60` — `_aligned_malloc` under `_WIN32`, `posix_memalign` under `#else`.
- `tests/src/plugins/SfzDistoBuffersTest.cpp:82-85` — `_aligned_free` under `_WIN32`, `std::free` under `#else`.
- `tests/train-51-portability-check.py:123-146` — compiles the fixed allocation/deallocation pair with `_WIN32` and the original with `_MSC_VER` as the negative control.

## Proof commands and EXITs

```
git show f3b5426267 -- tests/src/plugins/SfzDistoBuffersTest.cpp > .../logs/proof-1.log 2>&1
EXIT=0
```

The diff replaces `#if defined(_MSC_VER)` with `#if defined(_WIN32)` on the
three affected lines. The file at HEAD has no `_MSC_VER` left.

```
git merge-base --is-ancestor f3b5426267 HEAD
EXIT=0
git merge-base --is-ancestor 1f1c5192fe HEAD
EXIT=1
```

```
python3 tests/train-51-portability-check.py --source-root . > .../logs/guard-1.log 2>&1
EXIT=0
```

The guard log contains `PASS sfz-windows-original: failed as expected`.

## Ancestry

The original fix commits are not ancestors of HEAD (`1f1c5192fe` exits 1).
The equivalent product change `f3b5426267` is an ancestor (exit 0), so the
same `_MSC_VER` to `_WIN32` product change is present at HEAD.

## Coverage note

`tests/windows-arm64-aligned-new-check.py` is absent, but
`tests/train-51-portability-check.py:123-146` guards the same class by
compiling the fixed pair for Windows and asserting the `_MSC_VER` variant
fails with the `posix_memalign` diagnostic.

A real windows-arm64 hosted run is unverified locally.

## Other findings

None.

## Self-check

<!-- stub -->
