//! A small fake engine for the deterministic tests: a JSON-lines server on a unix socket that
//! answers the handful of commands the link and the Phase 0 model loader use, keeps a tiny project
//! (tracks and a tempo) that it saves to and opens from a JSON file, and pushes `project_changed`
//! events to subscribers. Run in-process ([`FakeEngine::start`]) or as the `zene-fake-engine`
//! binary, which the supervisor can spawn, kill and watch like the real engine.
//!
//! It copies the real engine's observed behaviour where the link depends on it: `control.ping`'s
//! fields, `control.quit {save:true}` refused without a project file, the 30 s idle close (here
//! configurable) skipped for subscribed connections, `id:-1` for an unparsable request, and the
//! engine's `modified` flag NOT being set by `transport.set_tempo` (measured on the real engine,
//! see the README), so the link's own unsaved-change tracking is exercised.
//!
//! It is a fixture, not a model of the engine; the real-engine tests (`tests/real_engine.rs`) are
//! the evidence that the link works with the real thing.

#[derive(Clone, Debug)]
pub struct FakeConfig {
    /// `interface: "none"` (serve mode) or `"qt"`.
    pub serve: bool,
    pub proto: i64,
    /// `engine_ready` turns true this long after start.
    pub ready_after: std::time::Duration,
    pub never_ready: bool,
    /// Close a connection with no subscription after this much silence.
    pub idle_close: Option<std::time::Duration>,
}

impl Default for FakeConfig {
    fn default() -> Self {
        FakeConfig { serve: true, proto: 1, ready_after: std::time::Duration::ZERO, never_ready: false, idle_close: None }
    }
}

impl FakeConfig {
    /// The configuration the `zene-fake-engine` binary reads from its environment.
    pub fn from_env() -> FakeConfig {
        let var = |k: &str| std::env::var(k).ok();
        FakeConfig {
            serve: var("FAKE_ZENE_NOT_SERVE").as_deref() != Some("1"),
            proto: var("FAKE_ZENE_PROTO").and_then(|v| v.parse().ok()).unwrap_or(1),
            ready_after: std::time::Duration::from_millis(var("FAKE_ZENE_READY_MS").and_then(|v| v.parse().ok()).unwrap_or(0)),
            never_ready: var("FAKE_ZENE_NEVER_READY").as_deref() == Some("1"),
            idle_close: var("FAKE_ZENE_IDLE_MS").and_then(|v| v.parse().ok()).map(std::time::Duration::from_millis),
        }
    }
}

#[cfg(unix)]
pub use imp::*;

#[cfg(unix)]
mod imp {
    use std::collections::HashMap;
    use std::io::{BufRead, BufReader, Write};
    use std::os::unix::net::{UnixListener, UnixStream};
    use std::path::{Path, PathBuf};
    use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
    use std::sync::{Arc, Condvar, Mutex};
    use std::time::{Duration, Instant};

    use serde_json::{Value, json};

    use super::FakeConfig;

    #[derive(Default)]
    struct Project {
        file: Option<PathBuf>,
        modified: bool,
        tracks: Vec<String>,
        tempo: i64,
    }

    struct Shared {
        cfg: FakeConfig,
        started: Instant,
        project: Mutex<Project>,
        subscribers: Mutex<HashMap<u64, Arc<Mutex<UnixStream>>>>,
        streams: Mutex<HashMap<u64, UnixStream>>,
        seq: AtomicU64,
        next_conn: AtomicU64,
        stop: AtomicBool,
        quit: Mutex<Option<bool>>,
        quit_cv: Condvar,
        /// Every request line received, for assertions.
        requests: Mutex<Vec<Value>>,
    }

    pub struct FakeEngine {
        shared: Arc<Shared>,
        pub socket: PathBuf,
    }

    impl FakeEngine {
        /// Listen at `socket` and serve until stopped, crashed or quit. `project` is opened first.
        pub fn start(socket: &Path, cfg: FakeConfig, project: Option<&Path>) -> std::io::Result<FakeEngine> {
            let listener = UnixListener::bind(socket)?;
            let mut p = Project { tempo: 120, tracks: vec!["Bass".into(), "Keys".into()], ..Default::default() };
            if let Some(path) = project {
                load(&mut p, path)?;
            }
            let shared = Arc::new(Shared {
                cfg,
                started: Instant::now(),
                project: Mutex::new(p),
                subscribers: Mutex::new(HashMap::new()),
                streams: Mutex::new(HashMap::new()),
                seq: AtomicU64::new(0),
                next_conn: AtomicU64::new(0),
                stop: AtomicBool::new(false),
                quit: Mutex::new(None),
                quit_cv: Condvar::new(),
                requests: Mutex::new(Vec::new()),
            });
            let sh = shared.clone();
            std::thread::spawn(move || {
                for s in listener.incoming() {
                    if sh.stop.load(Ordering::SeqCst) {
                        break;
                    }
                    let Ok(s) = s else { continue };
                    let id = sh.next_conn.fetch_add(1, Ordering::SeqCst);
                    if let Ok(c) = s.try_clone() {
                        sh.streams.lock().unwrap().insert(id, c);
                    }
                    let sh2 = sh.clone();
                    std::thread::spawn(move || serve(sh2, id, s));
                }
            });
            Ok(FakeEngine { shared, socket: socket.to_path_buf() })
        }

        /// Block until a client sent `control.quit`; returns its `save` flag.
        pub fn wait_quit(&self) -> bool {
            let mut q = self.shared.quit.lock().unwrap();
            while q.is_none() {
                q = self.shared.quit_cv.wait(q).unwrap();
            }
            q.unwrap()
        }

        /// The `save` flag of a `control.quit` received so far, if any.
        pub fn quit_requested(&self) -> Option<bool> {
            *self.shared.quit.lock().unwrap()
        }

        /// Close every connection (the engine dropping its clients) but keep serving.
        pub fn drop_connections(&self) {
            for (_, s) in self.shared.streams.lock().unwrap().drain() {
                let _ = s.shutdown(std::net::Shutdown::Both);
            }
            self.shared.subscribers.lock().unwrap().clear();
        }

        /// Stop like a crash: connections close, the socket file stays behind (stale).
        pub fn crash(&self) {
            self.shared.stop.store(true, Ordering::SeqCst);
            self.drop_connections();
            let _ = UnixStream::connect(&self.socket); // wake the accept loop so it ends
        }

        /// Stop like a clean quit: as `crash`, and the socket file is removed.
        pub fn stop(&self) {
            self.crash();
            let _ = std::fs::remove_file(&self.socket);
        }

        pub fn requests(&self) -> Vec<Value> {
            self.shared.requests.lock().unwrap().clone()
        }
        /// Change the project as another control client would (it emits an event).
        pub fn edit_from_outside(&self, track: &str) {
            let mut p = self.shared.project.lock().unwrap();
            p.tracks.push(track.into());
            p.modified = true;
            drop(p);
            emit(&self.shared, "track.add", json!({"name": track}));
        }
        pub fn project_file(&self) -> Option<PathBuf> {
            self.shared.project.lock().unwrap().file.clone()
        }
        pub fn modified(&self) -> bool {
            self.shared.project.lock().unwrap().modified
        }
    }

    /// Read a fake project file.
    pub fn load_project(path: &Path) -> std::io::Result<Value> {
        let text = std::fs::read_to_string(path)?;
        serde_json::from_str(&text).map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
    }

    fn load(p: &mut Project, path: &Path) -> std::io::Result<()> {
        let v = load_project(path)?;
        p.tracks = v["tracks"].as_array().into_iter().flatten().filter_map(|t| t.as_str().map(String::from)).collect();
        p.tempo = v["tempo"].as_i64().unwrap_or(120);
        p.file = Some(path.to_path_buf());
        p.modified = false;
        Ok(())
    }

    fn save(p: &mut Project, path: &Path) -> std::io::Result<()> {
        std::fs::write(path, json!({"tracks": p.tracks, "tempo": p.tempo}).to_string())?;
        p.file = Some(path.to_path_buf());
        p.modified = false;
        Ok(())
    }

    fn emit(sh: &Shared, command: &str, args: Value) {
        let seq = sh.seq.fetch_add(1, Ordering::SeqCst) + 1;
        let line = format!("{}\n", json!({"event": "project_changed", "command": command, "args": args, "kind": "edit", "seq": seq, "source": "command"}));
        let subs: Vec<Arc<Mutex<UnixStream>>> = sh.subscribers.lock().unwrap().values().cloned().collect();
        for s in subs {
            let _ = s.lock().unwrap().write_all(line.as_bytes());
        }
    }

    fn serve(sh: Arc<Shared>, conn: u64, s: UnixStream) {
        let w = Arc::new(Mutex::new(match s.try_clone() {
            Ok(w) => w,
            Err(_) => return,
        }));
        let mut r = BufReader::new(s);
        let mut line = String::new();
        loop {
            let subscribed = sh.subscribers.lock().unwrap().contains_key(&conn);
            let _ = r.get_ref().set_read_timeout(if subscribed { None } else { sh.cfg.idle_close });
            line.clear();
            match r.read_line(&mut line) {
                Ok(0) | Err(_) => break,
                Ok(_) => {}
            }
            let req: Value = match serde_json::from_str(line.trim()) {
                Ok(v) => v,
                Err(e) => {
                    let _ = writeln!(w.lock().unwrap(), "{}", json!({"id": -1, "ok": false, "error": {"kind": "invalid_args", "message": format!("malformed JSON request: {e}")}}));
                    continue;
                }
            };
            sh.requests.lock().unwrap().push(req.clone());
            let cmd = req["cmd"].as_str().unwrap_or("").to_string();
            let reply = match handle(&sh, conn, &w, &cmd, &req["args"]) {
                Ok(result) => json!({"id": req["id"], "ok": true, "result": result}),
                Err((kind, message)) => json!({"id": req["id"], "ok": false, "error": {"kind": kind, "message": message}}),
            };
            let _ = writeln!(w.lock().unwrap(), "{reply}");
            if cmd == "control.quit" && reply["ok"] == true {
                let save = req["args"]["save"].as_bool().unwrap_or(false);
                *sh.quit.lock().unwrap() = Some(save);
                sh.quit_cv.notify_all();
            }
        }
        sh.subscribers.lock().unwrap().remove(&conn);
        sh.streams.lock().unwrap().remove(&conn);
    }

    type Reply = Result<Value, (&'static str, String)>;

    fn handle(sh: &Shared, conn: u64, w: &Arc<Mutex<UnixStream>>, cmd: &str, args: &Value) -> Reply {
        let ready = !sh.cfg.never_ready && sh.started.elapsed() >= sh.cfg.ready_after;
        if !ready && cmd != "control.ping" {
            return Err(("not_ready", "the engine is still starting".into()));
        }
        let mut p = sh.project.lock().unwrap();
        let tracks = |p: &Project| -> Vec<Value> {
            p.tracks.iter().enumerate().map(|(i, n)| json!({"id": format!("trk-{}", i + 1), "name": n, "type": "instrument", "channel": format!("ch-{}", i + 1), "muted": false, "soloed": false, "instrument": "tripleoscillator"})).collect()
        };
        Ok(match cmd {
            "control.ping" => json!({"pong": true, "engine_ready": ready, "interface": if sh.cfg.serve { "none" } else { "qt" }, "proto": sh.cfg.proto, "version": "fake-1", "reason": if ready { "" } else { "loading the project" }}),
            "project.get_state" => json!({"file": p.file.as_ref().map(|f| f.display().to_string()).unwrap_or_default(), "modified": p.modified, "tempo": p.tempo, "track_count": p.tracks.len()}),
            "project.save" => {
                let path = args["path"].as_str().map(PathBuf::from).or_else(|| p.file.clone()).ok_or(("refused", "the project has no file; pass path".to_string()))?;
                save(&mut p, &path).map_err(|e| ("io", e.to_string()))?;
                drop(p);
                emit(sh, "project.save", args.clone());
                json!({"saved": true, "file": path.display().to_string()})
            }
            "project.open" => {
                let path = PathBuf::from(args["path"].as_str().unwrap_or(""));
                load(&mut p, &path).map_err(|e| ("io", e.to_string()))?;
                drop(p);
                emit(sh, "project.open", args.clone());
                json!({"file": path.display().to_string()})
            }
            "control.quit" => {
                let save_it = args["save"].as_bool().unwrap_or(false);
                if save_it {
                    let file = p.file.clone().ok_or(("refused", "control.quit with save:true needs a project file; call project.save first".to_string()))?;
                    save(&mut p, &file).map_err(|e| ("io", e.to_string()))?;
                }
                json!({"quitting": true, "save_requested": save_it, "project_modified": p.modified})
            }
            "events.subscribe" => {
                sh.subscribers.lock().unwrap().insert(conn, w.clone());
                json!({"subscription": format!("sub-{conn}"), "seq": sh.seq.load(Ordering::SeqCst)})
            }
            "track.add" => {
                p.tracks.push(args["name"].as_str().unwrap_or("Track").to_string());
                p.modified = true;
                let id = format!("trk-{}", p.tracks.len());
                drop(p);
                emit(sh, "track.add", args.clone());
                json!({"id": id, "track": id})
            }
            "transport.set_tempo" => {
                // As the real engine: the tempo changes, an event goes out, `modified` stays as it was.
                p.tempo = args["bpm"].as_i64().unwrap_or(p.tempo);
                let t = p.tempo;
                drop(p);
                emit(sh, "transport.set_tempo", args.clone());
                json!({"tempo": t})
            }
            "transport.get_state" => json!({"playing": false, "position_ticks": 0, "tempo": p.tempo}),
            "track.overview" => json!({"tracks": tracks(&p)}),
            "mixer.get_state" => {
                let mut ch = vec![json!({"id": "ch-0", "name": "Master", "is_master": true, "volume": 1.0, "pan": 0.0, "muted": false, "soloed": false, "sends": []})];
                ch.extend((1..=p.tracks.len()).map(|i| json!({"id": format!("ch-{i}"), "name": p.tracks[i - 1], "is_master": false, "volume": 1.0, "pan": 0.0, "muted": false, "soloed": false, "sends": [{"to": "ch-0"}]})));
                json!({"channels": ch})
            }
            "mixer.strip_get" => json!({"meters": {"rms_left": 0.0, "rms_right": 0.0}}),
            "clip.list" => json!({"clips": [], "count": 0}),
            "arrangement.modes" => json!({"overlap": "trim"}),
            "browser.query" => json!({"items": []}),
            _ => return Err(("not_found", format!("no command '{cmd}'"))),
        })
    }

    /// The `zene-fake-engine` binary's main: `--control-socket PATH [--engine] [--config X] [project]`.
    /// Exits 1 without `--control-socket`, `FAKE_ZENE_EXIT_AT_START=N` exits N before listening,
    /// and a `control.quit` ends it with 0 after removing the socket.
    pub fn main_with(args: Vec<String>) -> i32 {
        if let Some(code) = std::env::var("FAKE_ZENE_EXIT_AT_START").ok().and_then(|v| v.parse().ok()) {
            eprintln!("fake engine: exiting at start with {code} as asked");
            return code;
        }
        let mut socket = None;
        let mut project = None;
        let mut it = args.into_iter().skip(1);
        while let Some(a) = it.next() {
            match a.as_str() {
                "--control-socket" => socket = it.next(),
                "--config" => {
                    it.next();
                }
                "--engine" => {}
                other => project = Some(PathBuf::from(other)),
            }
        }
        let Some(socket) = socket else {
            eprintln!("fake engine: --engine needs --control-socket");
            return 1;
        };
        let fe = match FakeEngine::start(Path::new(&socket), FakeConfig::from_env(), project.as_deref()) {
            Ok(f) => f,
            Err(e) => {
                eprintln!("fake engine: {e}");
                return 2;
            }
        };
        eprintln!("fake engine: listening at {socket}");
        fe.wait_quit();
        // Let the reply reach the client before the socket goes.
        std::thread::sleep(Duration::from_millis(20));
        fe.stop();
        0
    }
}

#[cfg(not(unix))]
pub fn main_with(_args: Vec<String>) -> i32 {
    eprintln!("zene-fake-engine: unix sockets only; the Windows transport is Q050-524");
    2
}
