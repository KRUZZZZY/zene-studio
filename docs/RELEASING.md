# Releasing Zene Studio

The runbook for publishing a release of Zene Studio. It documents the machinery behind a release and the
worked example of the one that actually shipped — **`v0.1.0-alpha`** — so the next one is mechanical. The
0.2.0-alpha deltas are the section right after this one.

## The tag

The release tag is an annotated tag on `KRUZZZZY/zene-studio`:

```
$ git ls-remote --tags product | grep v0.1.0
b7c9361c935d9acd15a072b3d9dc5e3ec54cecbc	refs/tags/v0.1.0-alpha
0c23587d254ff981d3881c42ef98ac4b0997a7e6	refs/tags/v0.1.0-alpha^{}
```

Tag object `b7c9361c9` points at commit **`0c23587d2`**. The version string the tag's build
reports comes from `CMakeLists.txt`: `VERSION_MAJOR "0"`, `VERSION_MINOR "1"`,
`VERSION_RELEASE "0"`, `VERSION_STAGE "alpha"` → `0.1.0-alpha`. A build from an earlier CI run
reports `1.3.0-alpha...` instead — that string is build provenance, not a second product name
(see [KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md)).

Pushing the tag triggers `.github/workflows/build.yml` — it runs on bare `push:` with no branch
or tag filter. Its six job keys expand to **seven jobs**: `linux-x86_64`, `linux-arm64`,
`macos-x86_64`, `macos-arm64`, `mingw64`, `msvc-x64`, `windows-arm64`.

## 0.2.0-alpha — what changes in this runbook

Everything above is the v0.1.0-alpha release *as it happened*, and it stays as the record. Four things
differ for 0.2.0-alpha, and two of them are corrections to what the v0.1.0 text implies.

**1. The version the build reports comes from the tag, not from `CMakeLists.txt`.**
`cmake/modules/VersionInfo.cmake` runs `git describe --tags --match 'v[0-9]*.[0-9]*.[0-9]*'` at configure
time and lets the result override the `VERSION_*` values whenever a tag is reachable. On the release commit,
with the tag present, describe returns exactly `v0.2.0-alpha`, so the build reports **`0.2.0-alpha`**; on an
untagged commit it reports `0.2.0-alpha.<commits-since-tag>+<hash>` (that is where the published alpha's
`0.1.0-alpha.28+ccd07f4` strings came from — the string is build provenance, not a second product name).
`CMakeLists.txt` now carries `VERSION_MINOR "2"`, which is the **fallback** used when git is unavailable or
`-DFORCE_VERSION=internal` is passed, so a source tarball also reports 0.2.0-alpha. **Bump the version in
`CMakeLists.txt` only as that fallback; the tag is what the release reports.**

**2. The packages are `zene-0.2.0-alpha-*`.** The rename moved the package name from `lmms-*` to `zene-*`
(`CPACK_PACKAGE_FILE_NAME = ${CMAKE_PROJECT_NAME}-${VERSION}-<platform>-<arch>`, generated as
`zene-0.1.0-alpha.123+34c1f4f-linux-x86_64` for this base in `build/CPackConfig.cmake`), and the workflow's
upload globs follow it. The `lmms-0.1.0-alpha-*` names in the commands below are the previous release's: read
them as `zene-0.2.0-alpha-*`.

**3. Every package job must pass the release-honesty guard before its package is worth publishing.**
`bash tests/release-honesty-gate.sh --header build/lmmsversion.h --artifacts build` reads
`tests/advertised-features.tsv` and fails when a feature the release documents as present is not explicitly
ON in the build, when one it documents as absent is ON, or when the plug-in module a claim implies is missing
from the build tree. It is a step in all six package jobs. A red guard means the documents and the artefacts
disagree: stop, do not publish.

**4. `if-no-files-found: error` is on every package-upload step.** A glob that matches nothing fails its job
instead of uploading zero assets for that platform — the failure mode the v0.1.0-alpha release nearly shipped.

## Packages are artifacts first — a release only if you make one

Each job uploads its package only when the ref is a tag or the run was dispatched by hand:

```yaml
if: startsWith(github.ref, 'refs/tags/') || github.event_name == 'workflow_dispatch'
# Only a tag (the release path) or a deliberate dispatch publishes a package:
# per-push uploads of ~700 MB exhausted the artifact storage quota.
```

Artifact paths are `build/lmms-*.AppImage`, `build/lmms-*.dmg` and `build/lmms-*.exe`, uploaded
with `archive: false`, `retention-days: 14`.

**No workflow creates the GitHub release.** Verified:

```
$ grep -rn -E "softprops/action-gh-release|gh release|releases/create" .github/
NO MATCHES
```

So a tag build produces artifacts, and publishing them is a **manual step**.

**Correction (2026-09-11, after this document was written and merged):** the release is no longer
absent — `v0.1.0-alpha` went out and this section's original text ("the release page is empty") was
true only at the moment it was drafted. The live state, which is the state a publisher should start
from:

```
$ gh release list --repo KRUZZZZY/zene-studio
Zene Studio v0.1.0-alpha   Pre-release   v0.1.0-alpha   2026-09-11T20:27:22Z

$ gh release view v0.1.0-alpha --repo KRUZZZZY/zene-studio --json assets --jq '.assets | length'
7
```

Two facts that follow from it, both verified after the publish: every asset was downloaded back from
the release and its SHA-256 compared with an independent pre-publish baseline — **all seven
byte-identical** — and GitHub exposes `digest: sha256:…` per asset, so the release notes' promise
(verify your download against the digest shown on the release page) holds without pasting a digest
block into the body. A second release should therefore start from
`gh release view v0.1.0-alpha --json isDraft,isPrerelease,assets` rather than from an assumed-empty
release page, and must not re-publish the same tag.

## The publish sequence

Only platforms whose build job is green have a package. Release only the artifacts that exist.

```sh
# 1. Watch the tag run to completion.
gh run list --repo KRUZZZZY/zene-studio --workflow build.yml --limit 5
gh run view <run-id> --repo KRUZZZZY/zene-studio
gh run watch <run-id> --repo KRUZZZZY/zene-studio   # exits non-zero if the run fails

# 2. Download that run's artifacts.
gh run download <run-id> --repo KRUZZZZY/zene-studio --dir dist
find dist -type f -name 'zene-*'                    # archive:false → one file per artifact

# 3. Smoke-test the Linux AppImage: run it headless and render a project (from the repo root).
APP=dist/zene-0.2.0-alpha-linux-x86_64.AppImage    # adjust to where step 2 put it
chmod +x "$APP"
QT_QPA_PLATFORM=offscreen "$APP" --version          # expect "Zene Studio 0.2.0-alpha"
QT_QPA_PLATFORM=offscreen "$APP" render tests/emptyproject.mmp -o build/zene-smoke.wav -f wav
echo "EXIT=$?"; ls -l build/zene-smoke.wav           # expect EXIT=0 and a non-empty WAV

# 4. SHA-256 every asset. The release notes promise a digest per file, so this is required.
cd dist && sha256sum * > ../build/digests.txt && cd .. && cat build/digests.txt

# 5. Build the release body: the notes, then the digests. Do not edit the notes file itself.
cat docs/RELEASE-NOTES-v0.2.0-alpha.md > build/zene-body.md
{ echo; echo "## SHA-256 digests"; echo; echo '```'; cat build/digests.txt; echo '```'; } >> build/zene-body.md

# 6. Publish, listing only the assets that were actually downloaded.
gh release create v0.2.0-alpha --repo KRUZZZZY/zene-studio \
  --title "Zene Studio v0.2.0-alpha" \
  --notes-file build/zene-body.md \
  dist/zene-0.2.0-alpha-linux-x86_64.AppImage \
  dist/zene-0.2.0-alpha-linux-aarch64.AppImage \
  dist/zene-0.2.0-alpha-mac*arm64*.dmg \
  dist/zene-0.2.0-alpha-mac*x86_64*.dmg \
  dist/zene-0.2.0-alpha-msvc2022-win64.exe \
  dist/zene-0.2.0-alpha-mingw-win64.exe \
  dist/zene-0.2.0-alpha-clangarm64-arm64.exe
```

The `v0.2.0-alpha` tag must **already exist** (the owner creates it): `gh release create` attaches to an
existing tag rather than making one. If the tag is missing, stop — publishing would produce a release whose
version and assets come from different commits. The asset names above are the previous release's platform
suffixes with the new prefix and version (`zene-0.2.0-alpha-*`); confirm them against the actual downloaded
artefact list, and if a job was skipped or failed, drop its line.

## Hazards

**1. The concurrency group discards runs.** `build.yml` sets

```yaml
concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true
```

so a push to the same ref cancels the run already in flight for it. This is not theoretical: on
2026-09-11 the runs for `d8d70e9d3` (23m12s) and `3c6ecedf7` (31m19s) were both cancelled
mid-matrix when further pushes landed, and a third at `2315fc246` was cancelled after 7m25s
(`gh run view <id> --json createdAt,updatedAt`). A cancelled run uploads nothing, so a cancelled
tag build means re-running it. **Batch your commits, push once, then wait for the tag run to
finish before touching the ref again.**

**2. Every build is unsigned.** Windows SmartScreen and macOS Gatekeeper will warn on first
launch. That is expected for this alpha and it is documented in the release notes and in
[KNOWN-LIMITATIONS.md](KNOWN-LIMITATIONS.md). Never answer a warning by telling users to disable
protection: point them at **More info → Run anyway** on Windows and **System Settings → Privacy &
Security → Open Anyway** on macOS, and at the SHA-256 digest for verifying the download.

## Rolling back a bad release

Delete the release — that is reversible and safe:

```sh
gh release delete v0.1.0-alpha --repo KRUZZZZY/zene-studio --yes
```

**You cannot move or delete the tag.** The `release-tag-protection` ruleset (id 22926011) applies
the `deletion` and `non_fast_forward` rules to `refs/tags/v*`, with the owner as the only bypass:

```
$ gh api repos/KRUZZZZY/zene-studio/rulesets/22926011
{"name":"release-tag-protection","target":"tag", ... "conditions":{"ref_name":{ ... "include":["refs/tags/v*"]}},
 "rules":[{"type":"deletion"},{"type":"non_fast_forward"}], ... "current_user_can_bypass":"always"}
```

So `git push --force product v0.1.0-alpha` and `git push product --delete v0.1.0-alpha` are both
rejected for anyone but the owner. Treat a published tag as immutable: if the build it points at
is bad, cut a **new** tag (e.g. `v0.1.1-alpha`) and publish that, and mark the old release as
superseded or delete it.

## After publishing

There is no package repository, no release channel and no auto-update, so the release page is the
only distribution point. Check that the page lists every asset, that the digest block is present,
and that the notes render — then link it wherever the alpha is announced.
