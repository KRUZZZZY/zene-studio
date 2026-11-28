# rune/ - the Rune front end (prototype)

A desktop UI for Zene Studio written in Rust with [Rune](https://github.com/anomalyco/rune), brought
into the product tree from `side-projects/zene-rune-prototype` (commit `37026f8`) by Q050-525 so its
platform CI runs with the product's hosted CI. It is the Phase 0 prototype: by default it shows demo
data with simulated playback and **produces no sound**; `zene-rune --engine SOCKET` binds the same UI
to a running Zene Studio engine over its control socket (`app/src/zene.rs`, unix only, a spike).

**It is not part of the product build.** The default CMake build, the Qt application, `ctest` and
`tests/run-all-gates.sh` do not build or run anything here. Its checks are `cargo test` and the hosted
workflow `.github/workflows/rune.yml`.

## Layout

| Path | What |
|---|---|
| `app/` | the Rust crate (`Cargo.toml`, `Cargo.lock`, `rust-toolchain.toml` pinning rustc 1.99.1, `src/`) |
| `assets/` | the eight Kenney patterns `app/src/art.rs` embeds, the Lucide licence for the icons compiled into `app/src/icons.rs`, the songbird mark; see `NOTICE.md` |
| `scripts/` | two input scripts for `--script` frame-time runs |
| `ci/` | the hosted-CI helpers: `check_png.py`, `window_check.py`, `summarize.py` (stdlib Python) |
| `fetch-rune.sh` | clones Rune at the pinned revision into `vendor/rune` |
| `vendor/rune` | the Rune checkout, **git-ignored**; never committed, never patched |

## Rune is fetched, not vendored

`app/Cargo.toml` reaches Rune through path dependencies into `vendor/rune`. That directory is created
by `fetch-rune.sh`, which clones `https://github.com/anomalyco/rune.git` at the pinned SHA
`b53fe7236ab11225108f6aee837c2d9942f63991` (a shallow fetch of that one commit, about 180 MB) and
refuses a checkout at any other revision or with local changes. The pin lives in that script only;
CI and developers both run it. If Rune itself fails on a platform, record the failure and a minimal
reproduction; do not patch the checkout.

## Build and test

Needs a Rust toolchain (`rustup` picks up `app/rust-toolchain.toml`). On Linux the build also needs
`pkg-config`, `libxkbcommon-dev` and `libwayland-dev`.

```sh
sh rune/fetch-rune.sh
cd rune/app
cargo build --locked --profile perf     # release codegen, no incremental; binary target/perf/zene-rune
cargo test --locked                     # 26 headless interaction and model tests (2 engine tests are ignored)
```

The two ignored tests in `app/src/engine_tests.rs` (Phase 0 spikes S1 and S2) need a running engine;
their drivers (`spikes/s1_run.py`, `spikes/s2_run.py`) stay in the prototype and were not imported.

A from-scratch build writes several GB under `rune/app/target/` (git-ignored); point `CARGO_TARGET_DIR`
elsewhere to keep it out of the worktree.

Headless render and a real window (what CI checks):

```sh
B=rune/app/target/perf/zene-rune
$B --list-scenes
$B --snap out.png --size 1366x768 --scale 1 --scene piano --at 0.4
python3 rune/ci/check_png.py out.png 1366 768
python3 rune/ci/window_check.py --exe $B --timeout 90 --out window.json
$B --script rune/scripts/drag-clip.json --size 1366x768 --scale 1 --frames 110 --out frames/
```

## Hosted CI

`.github/workflows/rune.yml` runs on every push to a `ci/**` branch (and by hand), on ubuntu-24.04
(lavapipe software Vulkan under Xvfb), windows-latest (MSVC, D3D12/WARP) and macos-latest (Apple
silicon, Metal). Each job: fetch Rune, `cargo build --locked --profile perf`, `cargo test --locked`
(at least 26 must pass), a headless `--snap` checked by `ci/check_png.py`, a real window that must
report its first frame (`ci/window_check.py`), then `ci/summarize.py` writes `summary.json`, prints one
`error:` line per failed check and fails the job if any check failed. Logs, the PNG and the summary
are uploaded as the artifact `rune-<os>`.

A red `rune` run is filed by the KADS CI watch like any other hosted failure: the watch reads every
workflow run of a pushed `ci/*` sha and queues each failed one (`zene-queue`
`crates/kads-train/src/ci_watch.rs`). Hosted results: `PHASE0-S3-HOSTED.md`.

## Gates

`rune/` does not exist upstream, so Gate 6 (`tests/no-upstream-regression-gate.sh`) allows every path
under it by construction, as it does `tools/`. The Rust sources are deliberately outside the C/C++
ratchet scope (`tests/fork-sources.txt` header, 2026-11-28 entry): lizard would measure Rust against
the C++ complexity baseline. The Python and shell helpers here are registered in
`tests/fork-sources.txt` and measured by the complexity and file-length ratchets like any fork script.

## More

The design rationale, research, the full measurement notes and the Phase 0 report stay in the
prototype (`side-projects/zene-rune-prototype`: `README.md`, `DESIGN-RATIONALE.md`, `PHASE0-REPORT.md`).
