# rune/ - the Rune front end (prototype)

A desktop UI for Zene Studio written in Rust with [Rune](https://github.com/anomalyco/rune), brought
into the product tree from `side-projects/zene-rune-prototype` (commit `37026f8`) by Q050-525 so its
platform CI runs with the product's hosted CI. It is the Phase 0 prototype: by default it shows demo
data with simulated playback and **produces no sound**. With an engine it binds the same UI to a real
Zene Studio engine through the engine link crate `link/` (Q050-506): `zene-rune --engine-bin PATH`
starts and supervises `zene --engine` (serve mode: no Qt window), `zene-rune --engine SOCKET` attaches
to a running engine. Unix sockets only for now; Windows needs the named-pipe transport (Q050-524).

**It is not part of the product build.** The default CMake build, the Qt application, `ctest` and
`tests/run-all-gates.sh` do not build or run anything here. Its checks are `cargo test` and the hosted
workflow `.github/workflows/rune.yml`.

## Layout

| Path | What |
|---|---|
| `app/` | the UI crate and the workspace root (`Cargo.toml`, `Cargo.lock`, `rust-toolchain.toml` pinning rustc 1.99.1, `src/`) |
| `link/` | `zene-link`, the engine link: connection, engine process supervision, link status. Std plus `serde_json`, no GUI; a workspace member, so `cargo build`/`cargo test` in `app/` cover it |
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
cargo test --locked                     # both crates: 71 tests on Linux (2 Phase 0 engine tests are ignored)
```

On Linux and macOS the count is 71: 20 link unit tests, 12 deterministic lifecycle tests against the
fake engine (`link/tests/lifecycle.rs`), 7 real-engine link tests (`link/tests/real_engine.rs`) and 32
UI tests (6 of them on the link, `app/src/link_tests.rs`, one of which is real-engine). The 8
real-engine tests only print `DID NOT RUN` unless `ZENE_ENGINE_BIN` is set (below). On
Windows the unix-socket tests compile to one test each that prints why it was skipped: 43 tests.
The two ignored tests in `app/src/engine_tests.rs` (Phase 0 spikes S1 and S2) need a running engine;
their drivers (`spikes/s1_run.py`, `spikes/s2_run.py`) stay in the prototype and were not imported.

## The engine link (`link/`, Q050-506)

```sh
# start and supervise an engine; silent throwaway settings (own HOME, Dummy audio device: no sound)
zene-rune --engine-bin build/zene --engine-sandbox --project song.mmpz --engine-log-dir /tmp/zr-logs
# attach to an engine someone else started (closing the window never stops it)
zene-rune --engine /tmp/zene.sock
```

`--engine-bin` (or `ZENE_ENGINE_BIN`) without `--engine-sandbox` runs the engine with the user's own
settings and audio device; `--engine-config XML` passes a config. The window always opens; the chip at
the right of the status bar shows the link status (Starting, Engine starting up, Engine ready,
Reconnecting, Engine disconnected, Engine stopped, Engine failed to start) and its tooltip and the
status line say what happened and what to do. The Engine menu has Reconnect and Restart Engine.

What the link does (`link/src/session.rs` has the rules):

- Requests are sent without waiting; replies are matched by id on a reader thread; completions and
  events wait in bounded queues. A keep-alive ping beats the engine's 30 s idle close. Every line is
  kept in a protocol log (ring buffer, mirrored to `protocol.log` in the log directory).
- A supervised engine gets a private runtime directory with a short socket path. A stale socket there
  (nothing accepting) is removed; a live engine's socket or anything that is not a socket is never
  touched, and the start fails with a sentence saying so.
- Readiness is `control.ping` until `engine_ready`; a different `proto`, or a program that is not in
  serve mode (`interface` not `"none"`), is refused with the reason.
- A dropped connection to a running engine reconnects without touching the process; the UI rereads
  the project (its unsent edits go first). An engine that ends on its own is restarted (at most 3 times
  a minute) with the project's last SAVED file, and the status line says what was lost: serve mode
  keeps no autosave, so edits after the last save are gone.
- Data safety: `control.quit {save:false}` (or SIGTERM) is only sent when the user chose Discard or a
  fresh read shows nothing unsaved. "Unsaved" is the engine's `modified` flag OR any `project_changed`
  event since the last save/open, because the engine's flag misses edits (below). Closing the window
  (File > Quit, Ctrl+Q, the close button) with unsaved changes asks: Save and close, Discard changes
  and close, or Cancel. Rune cannot veto a window close, so the app runs with `keep_running` and shows
  the window again with the question. An attached engine is never stopped. A session dropped without
  a decision leaves an engine with unsaved changes running and prints how to reach it.

Still blocking (Q050-507 replaces them): the Phase 0 model code in `app/src/zene.rs` (`sync`, `load`,
`reconcile`, `peaks`) waits for each reply on the UI thread, as the spike did; the close question's
fresh read waits up to 2 s.

Engine behaviour the link works around (measured on the Q050-517 build, `16111db59b`):

- `transport.set_tempo`, `track.set_mute` and `clip.add` change the project but leave
  `project.get_state`'s `modified` false (`track.rename` sets it). Their `project_changed` events do
  arrive, so the link counts those.
- `project_changed` events can arrive after the reply to a later command; before an exit decision the
  link asks `events.get_state` for the newest `seq` and waits for the events up to it.
- The engine's default project has tracks on the master channel; the Phase 0 loader gives each its own
  mixer channel (a strip needs one), which makes a freshly opened project count as modified.

### Real-engine tests

```sh
W=<workspace>; B=$W/zene-fable-rune/build/zene   # a build with serve mode (Q050-517)
export LD_LIBRARY_PATH=$HOME/.local/zene-deps/root/usr/lib:$HOME/.local/zene-deps/onnxruntime-linux-x64-1.28.0/lib:$W/zene-fable-rune/third_party/wasmtime/lib
ZENE_ENGINE_BIN=$B ZENE_ENGINE_EVIDENCE=/tmp/zl-evidence cargo test --locked -- --test-threads=1 real_engine
```

They start the engine only in a sandbox (own HOME and XDG directories, a `--config` naming the Dummy
audio device), one at a time, and write status histories, pids, exit statuses, the protocol log and
the engine's output under `$ZENE_ENGINE_EVIDENCE/<test>/`. The hosted workflow has no engine binary, so
there they print `DID NOT RUN`.

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
(both crates; at least 43 must pass, the Windows count), a headless `--snap` checked by `ci/check_png.py`, a real window that must
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
