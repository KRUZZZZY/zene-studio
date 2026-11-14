# Overlay port: pcre2 (cluster ci-b38398fa7833e0a4)

`portfile.cmake` is vcpkg's `ports/pcre2` portfile at scripts commit
`177e817512` (the vcpkg the msvc-x64 and mingw64 jobs pin), with **one** change: the
`deps/sljit` gitlink for a tagged release is read from the committed
`sljit-gitlink-pcre2-10.49.json` instead of being downloaded from the
unauthenticated GitHub REST API during `vcpkg install`.

Why: the hosted request returned `403`; the response's underlying cause is not
established by the log. vcpkg treats 403 as non-transient (it retries only 408,
429 and 5xx). Job 132369662829 of run 43569206372 died there:

```
Downloading https://api.github.com/repos/PCRE2Project/pcre2/contents/deps/sljit?ref=pcre2-10.49
error: curl operation failed with response code 403.
error: Not a transient network error, won't retry download from https://api.github.com/...
error: building pcre2:x64-windows failed with: BUILD_FAILED
```

The response is a build input, not a live query, so it is committed. Its SHA512 is
the value vcpkg pins for it
(`719fa1d8…2411c`) and the portfile still verifies it and the parsed gitlink
(`sha` is 40 hex characters, `submodule_git_url` is
`https://github.com/zherczeg/sljit.git`) exactly as upstream does. A `--head`
build still queries the API, because its gitlink is whatever the branch points at
that day.

The pcre2 source tarball and the sljit source tarball are still fetched from
`github.com` (not the API); only the unauthenticated API call is gone.

Regression: `tests/vcpkg-pcre2-gitlink-test.py` (`ctest -R VcpkgPcre2Gitlink`).
It runs the real portfile text with a stub network that answers 403 to
`api.github.com` — the CI's own failure — and requires the committed file to be
used, hash-checked, and the head path to still query.

The three upstream patches are stored as lossless `*.patch.json` strings because
unified-diff blank context lines contain a trailing space. Before any dependency
fetch, the portfile reconstructs them under `CURRENT_BUILDTREES_DIR/zene-patches`,
checks each original SHA512, and passes those exact files to vcpkg. The regression
checks both decoded inputs and materialized files, plus missing/tampered inputs.
The full repository `git diff --check` remains enabled without exemptions.
