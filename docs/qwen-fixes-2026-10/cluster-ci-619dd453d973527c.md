FIXED

# Cluster ci-619dd453d973527c: Windows `file(WRITE)` turned pinned PCRE2 patch bytes into CRLF

Hosted msvc-x64 Configure failed while installing the overlay `pcre2` port:

```
CMake Error at .../.github/vcpkg/ports/pcre2/portfile.cmake:11 (message):
  Upstream patch pcre2-10.35_fix-uwp.patch SHA512 mismatch
Call Stack:
  .../portfile.cmake:18 (zene_pcre2_patch)
```

That is a REAL product/infrastructure bug in the repository, not a flaky runner
or a bad upstream pin.

## Evidence: the decoded bytes were right, the materialized bytes were not

`portfile.cmake:4-12` decoded each `*.patch.json` string correctly, then wrote it
with `file(WRITE)`:

```cmake
file(WRITE "${path}" "${patch_content}")
file(SHA512 "${path}" actual_sha512)
if(NOT actual_sha512 STREQUAL expected_sha512)
    message(FATAL_ERROR "Upstream patch ${name} SHA512 mismatch")
endif()
```

CMake's `HandleWriteImpl` opens `file(WRITE)` in text mode
(`Source/cmFileCommand.cxx`: `cmsys::ofstream file(..., std::ios::out)`), so on
Windows the C runtime translates every `\n` to `\r\n`. The committed JSON content
is LF (`git check-attr` says `eol: lf`; Python SHA512 of each decoded string equals
the pinned value), so Windows hashed different bytes and failed.

Proof with the same Windows CMake 4.4.4 used by the hosted job, run under wine:

```
file(WRITE)          SHA512 = d28d2687f604eee33cfc79076a0214acfc2a195b6ea786ac85918904fdbb3bec815e2cf05fbae0110a0afce25d73522ea6d9fe1eabb4b54b8c9e45130509069d
pinned/expected      SHA512 = 92f231765fea9868c39552009fbe1096279e0dd9b1f121f5184ff945e0b0c7a0d6e2167976db2e2b5bbdfaf231175894fcf9221c4d94fc7d436e400847fda209
file(CONFIGURE UNIX) SHA512 = 92f231765fea9868c39552009fbe1096279e0dd9b1f121f5184ff945e0b0c7a0d6e2167976db2e2b5bbdfaf231175894fcf9221c4d94fc7d436e400847fda209
EXIT=0
```

Command: `wine .../cmake-4.4.4-windows-x86_64/bin/cmake.exe -DSRC=... -DOUT=... -P probe.cmake`

## Fix at the cause

`.github/vcpkg/ports/pcre2/portfile.cmake:12` now materializes the patch with:

```cmake
file(CONFIGURE OUTPUT "${path}" CONTENT "${patch_content}" @ONLY NEWLINE_STYLE UNIX)
```

`NEWLINE_STYLE UNIX` makes CMake open the generated file in binary mode and emit
exactly the requested LF bytes. `@ONLY` is required because
`fix-msvc-pdb-install.patch` contains literal `${PROJECT_BINARY_DIR}` text; the
three decoded patch strings hash to the pinned SHA512 before and after this call.
The three callers (`portfile.cmake:22,24,26`) all go through the one function; no
other patch materialization exists in the tree (searched `zene_pcre2_patch`,
`file(CONFIGURE`, `*.patch.json`). No product source, build flag, dependency pin,
test assertion or gate was weakened.

The README beside the port now records why the lossless JSON inputs are written
with `file(CONFIGURE ... NEWLINE_STYLE UNIX)` rather than `file(WRITE)`.

## Regression test

`tests/vcpkg-pcre2-gitlink-test.py` (already registered as ctest
`VcpkgPcre2Gitlink`) gained a behavioural Windows seam:
`windows_text_mode_port()` copies the real overlay port and rewrites only
`file(WRITE)`/`file(APPEND)` inside `zene_pcre2_patch` to a helper that performs the
same LF -> CRLF translation Windows text mode performs. It then runs the real
portfile and requires all three `PATCH-BYTES` transcript entries to equal the pinned
SHA512. It is deterministic (no network, clock, sleep or race); `cmake -P` only.
Before the fix this reproduces the hosted error exactly:

```
FAIL portfile completes with Windows LF->CRLF text-mode writes:
  CMake Error at .../windows-port/portfile.cmake:11 (message):
  Upstream patch pcre2-10.35_fix-uwp.patch SHA512 mismatch
FAIL all three patches stay byte-identical through Windows text mode: None
```

The same test was also run end-to-end with the real Windows CMake 4.4.4 binary
under wine (wrapper named `cmake` first in PATH); all checks passed, including
the tagged-release path and the new Windows check (log below).

## RED / GREEN evidence

All commands were run from the worktree. `LOGD` is the session's writable log
directory (`$TMPDIR/zene-qwen-ci-619dd453d973527c/qwen-logs`); the requested
`~/.local/share/zene-bunny-fix/.../logs` path was not writable in this container
(`touch` and the editor both returned `EACCES`; even the run directory is denied,
while the worktree and `$TMPDIR` are writable). Logs were therefore saved in
`LOGD`, which is listed below with every command.

| Step | Exact command | Exit |
|---|---|---|
| RED (fix reverted) | `git diff -- .github/vcpkg/ports/pcre2/portfile.cmake > $LOGD/fix.patch && git apply -R $LOGD/fix.patch` then `cd build/tests && ctest -R VcpkgPcre2Gitlink --output-on-failure` | `0` for diff/apply, `8` for ctest (1/1 failed with the SHA512 mismatch above) |
| GREEN (fix restored) | `git apply $LOGD/fix.patch` then `cd build/tests && ctest -R VcpkgPcre2Gitlink --output-on-failure` | `0` for apply, `0` for ctest (1/1 passed) |
| Direct RED (pre-fix) | `python3 tests/vcpkg-pcre2-gitlink-test.py` before the edit | `1` (2 Windows checks failed) |
| Direct GREEN (post-fix) | `python3 tests/vcpkg-pcre2-gitlink-test.py` after the edit | `0` (RESULT: PASS) |
| Real Windows CMake | `PATH="$TMPD/bin:$PATH" TMPDIR="$TMPD" python3 tests/vcpkg-pcre2-gitlink-test.py` (wrapper runs the Windows `cmake.exe` under wine) | `0` (all checks PASS) |
| Rebuild | `cmake --build build --target zene` | `0` |
| Affected ctest family | `ctest -R VcpkgPcre2Gitlink` from `build/tests` | `0`, 1/1 test executed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `0` (PASS table below) |
| Whitespace | `git diff --check` | `0` |

Gate table (`==> RESULT: PASS`, saved by `feature_check.py`):

```
PASS  fork-sources
PASS  all-sources-reproduce
PASS  no-upstream-regression
PASS  check-namespace
PASS  no-tautology
PASS  doc-figures
PASS  complexity              no new item in your files
PASS  file-length             no new item in your files
RESULT: PASS
```

The change set contains exactly:

```
.github/vcpkg/ports/pcre2/README.md
.github/vcpkg/ports/pcre2/portfile.cmake
tests/vcpkg-pcre2-gitlink-test.py
docs/qwen-fixes-2026-10/cluster-ci-619dd453d973527c.md
```

## Self-check

- **Regression FAILS with the product change reverted and PASSES with it (exit
  codes).** `git apply -R` of the two-hunk portfile diff, `ctest -R
  VcpkgPcre2Gitlink`: exit `8` (fails with `Upstream patch
  pcre2-10.35_fix-uwp.patch SHA512 mismatch`). Re-applied: exit `0` (1/1 passed).
  Direct Python run: `1` -> `0`. The reverse/forward patch is saved as
  `$LOGD/fix.patch` and was produced only from the product file, not the test.
- **Deterministic:** no wall-clock timing, sleeps, network or races. The Windows
  seam injects the documented `file(WRITE)` LF->CRLF translation into the real
  portfile text; the transcript is the only sink. Every path uses `cmake -P` on
  committed inputs. A separate wine run used the actual Windows CMake binary.
- **Cause and completeness:** the cause is the text-mode `file(WRITE)` at
  `portfile.cmake:8` (pre-fix). All three callers of `zene_pcre2_patch` still
  receive byte-identical patches; the `@ONLY` flag preserves the literal
  `${PROJECT_BINARY_DIR}` text in `fix-msvc-pdb-install.patch`. Searched the whole
  tree for any other patch materialization: none. Dependency pins, parse and
  validation code are unchanged.
- **No collateral damage:** patch inputs, pinned SHA512 values, vcpkg manifest,
  sljit gitlink handling, `--head` API query, and the builtin-port red control are
  unchanged. The generated files are byte-identical to the previous Linux output;
  only the Windows failure path changed. No project file, save format, limit or
  contract is touched.
- **Realtime/undo/commands:** this is a CI dependency-fetch portfile; it contains
  no audio-thread, undo or command code, and adds no control command or snapshot
  change.
- **Old projects / saved data:** no product source or serialization changed.
- **Registration:** the test was already registered as `VcpkgPcre2Gitlink`
  (`tests/CMakeLists.txt:3163`); no new test file or registration was needed.
  The existing `tests/fork-sources.txt` entries already list the changed test and
  portfile; no `tests/upstream-modifications.txt` entry is required for a
  fork-source file.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE:** the pre-fix code at
  `portfile.cmake:8` was `file(WRITE "${path}" "${patch_content}")`, and the
  Windows CMake probe above hashed those bytes to `d28d26...` instead of the
  pinned `92f231...`. The failing hosted job ran the same text-mode write. That
  is positive evidence of a real cause, so the conditional ALREADY-FIXED /
  FALSE-POSITIVE item does not apply.
