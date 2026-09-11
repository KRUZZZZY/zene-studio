# Releasing Zene Studio

The runbook for publishing a release of Zene Studio. It documents the release that is happening
now — **`v0.1.0-alpha`** — and the machinery behind it, so the next one is mechanical.

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
find dist -type f -name 'lmms-*'                    # archive:false → one file per artifact

# 3. Smoke-test the Linux AppImage: run it headless and render a project (from the repo root).
APP=dist/lmms-0.1.0-alpha-linux-x86_64.AppImage     # adjust to where step 2 put it
chmod +x "$APP"
QT_QPA_PLATFORM=offscreen "$APP" --version          # expect 0.1.0-alpha, not 1.3.0-alpha.*
QT_QPA_PLATFORM=offscreen "$APP" render tests/emptyproject.mmp -o /tmp/zene-smoke.wav -f wav
echo "EXIT=$?"; ls -l /tmp/zene-smoke.wav           # expect EXIT=0 and a non-empty WAV

# 4. SHA-256 every asset. The release notes promise a digest per file, so this is required.
cd dist && sha256sum * > ../digests.txt && cd .. && cat digests.txt

# 5. Build the release body: the notes, then the digests. Do not edit the notes file itself.
cat docs/RELEASE-NOTES-v0.1.0-alpha.md > /tmp/zene-body.md
{ echo; echo "## SHA-256 digests"; echo; echo '```'; cat digests.txt; echo '```'; } >> /tmp/zene-body.md

# 6. Publish, listing only the assets that were actually downloaded.
gh release create v0.1.0-alpha --repo KRUZZZZY/zene-studio \
  --title "Zene Studio v0.1.0-alpha" \
  --notes-file /tmp/zene-body.md \
  dist/lmms-0.1.0-alpha-linux-x86_64.AppImage \
  dist/lmms-0.1.0-alpha-linux-aarch64.AppImage \
  dist/lmms-0.1.0-alpha-mac*arm64*.dmg \
  dist/lmms-0.1.0-alpha-mac*x86_64*.dmg \
  dist/lmms-0.1.0-alpha-msvc2022-win64.exe \
  dist/lmms-0.1.0-alpha-mingw-win64.exe \
  dist/lmms-0.1.0-alpha-clangarm64-arm64.exe
```

The `v0.1.0-alpha` tag already exists, so `gh release create` attaches to it rather than making a
new one. The asset names above are the ones [RELEASE-NOTES-v0.1.0-alpha.md](RELEASE-NOTES-v0.1.0-alpha.md)
advertises — if a job was skipped or failed, drop its line.

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
