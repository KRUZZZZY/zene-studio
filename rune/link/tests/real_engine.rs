//! Real-engine lifecycle tests: the session against the actual `zene --engine` binary.
//!
//! They run only when `ZENE_ENGINE_BIN` names the engine binary; otherwise each prints
//! `DID NOT RUN` and passes (they are not `#[ignore]`d: the output says they were skipped). The
//! engine runs in a silent throwaway world (`zene_link::runtime::Sandbox`: its own HOME and XDG
//! directories and a `--config` with the Dummy audio device), so no sound plays and no user settings
//! are touched. Libraries the binary needs come from the caller's environment (`LD_LIBRARY_PATH`).
//! One engine at a time (a lock serialises the tests).
//!
//! Evidence (status history, pids, exit status, the protocol log and the engine's own output) goes
//! to `$ZENE_ENGINE_EVIDENCE/<test>/` (default: `<temp>/zene-link-real-<pid>/<test>/`).
//!
//!   ZENE_ENGINE_BIN=/path/to/build/zene LD_LIBRARY_PATH=... cargo test --locked -p zene-link --test real_engine

#[cfg(not(unix))]
#[test]
fn real_engine_tests_skipped_on_this_platform() {
    eprintln!("SKIPPED: the real-engine tests need the unix-socket transport; Windows is Q050-524");
}

#[cfg(unix)]
mod unix {
    use std::os::unix::net::UnixListener;
    use std::path::{Path, PathBuf};
    use std::process::Command;
    use std::sync::Mutex;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::time::{Duration, Instant};

    use serde_json::{Value, json};
    use zene_link::runtime::Sandbox;
    use zene_link::session::ReadyKind;
    use zene_link::{ConnOptions, EngineSpec, ExitCheck, ExitChoice, ExitOutcome, Session, SessionOptions, Status};

    const T: Duration = Duration::from_secs(60);
    static ONE_AT_A_TIME: Mutex<()> = Mutex::new(());
    static N: AtomicU32 = AtomicU32::new(0);

    struct Real {
        bin: PathBuf,
        evidence: PathBuf,
        sandbox: Sandbox,
        runtime: PathBuf,
    }

    /// The engine, a sandbox and an evidence directory for one test; `None` (and a line saying so)
    /// when `ZENE_ENGINE_BIN` is not set.
    fn real(test: &str) -> Option<Real> {
        let Some(bin) = std::env::var_os("ZENE_ENGINE_BIN").map(PathBuf::from) else {
            eprintln!("DID NOT RUN {test}: set ZENE_ENGINE_BIN to the engine binary (see rune/README.md)");
            return None;
        };
        let base = std::env::var_os("ZENE_ENGINE_EVIDENCE").map(PathBuf::from).unwrap_or_else(|| std::env::temp_dir().join(format!("zene-link-real-{}", std::process::id())));
        let evidence = base.join(test);
        let _ = std::fs::remove_dir_all(&evidence);
        std::fs::create_dir_all(&evidence).unwrap();
        let sandbox = Sandbox::create(&evidence.join("sandbox")).unwrap();
        let runtime = PathBuf::from(format!("/tmp/zlr-{}-{}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
        let _ = std::fs::remove_dir_all(&runtime);
        eprintln!("{test}: evidence in {}", evidence.display());
        Some(Real { bin, evidence, sandbox, runtime })
    }

    impl Real {
        fn spec(&self, project: Option<&Path>) -> EngineSpec {
            EngineSpec {
                config: Some(self.sandbox.config.clone()),
                env: self.sandbox.env.clone(),
                cwd: Some(self.sandbox.root.clone()),
                project: project.map(Path::to_path_buf),
                ..EngineSpec::new(&self.bin)
            }
        }
        fn opts(&self, sub: &str) -> SessionOptions {
            SessionOptions {
                runtime_dir: Some(self.runtime.join(sub)),
                log_dir: Some(self.evidence.join(sub)),
                state_poll: Duration::from_millis(100),
                ready_timeout: T,
                spawn_timeout: T,
                ..Default::default()
            }
        }
        /// Status history, pids and the outcome, as JSON beside the logs.
        fn record(&self, sub: &str, s: &Session, extra: Value) {
            let hist: Vec<Value> = s.history().iter().map(|(t, st)| json!({"t": t, "label": st.label(), "sentence": st.sentence(), "debug": format!("{st:?}")})).collect();
            let v = json!({"pids": s.pids(), "socket": s.socket_path(), "history": hist, "project": format!("{:?}", s.project()), "extra": extra});
            let d = self.evidence.join(sub);
            let _ = std::fs::create_dir_all(&d);
            std::fs::write(d.join("session.json"), serde_json::to_string_pretty(&v).unwrap()).unwrap();
        }
    }

    fn alive(pid: u32) -> bool {
        Command::new("kill").args(["-0", &pid.to_string()]).stderr(std::process::Stdio::null()).status().is_ok_and(|s| s.success())
    }

    fn call(s: &Session, cmd: &str, args: Value) -> Value {
        s.connection().unwrap_or_else(|| panic!("{cmd}: not ready ({:?})", s.status())).call(cmd, args.clone(), T).unwrap_or_else(|e| panic!("{cmd} {args}: {e}"))
    }

    fn until(s: &mut Session, what: &str, f: impl Fn(&Session) -> bool) {
        let t0 = Instant::now();
        while !f(s) {
            assert!(t0.elapsed() < T, "timed out waiting for {what}; status {:?}; history {:#?}", s.status(), s.history());
            s.tick();
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    fn track_names(s: &Session) -> Vec<String> {
        call(s, "track.overview", json!({}))["tracks"].as_array().unwrap().iter().map(|t| t["name"].as_str().unwrap_or("").to_string()).collect()
    }

    /// A saved project with one extra, recognisable track, made through the engine itself.
    fn fixture(r: &Real, name: &str) -> PathBuf {
        let mut s = Session::supervise(r.spec(None), r.opts("fixture"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("fixture engine: {st:?}"));
        call(&s, "track.add", json!({"type": "instrument", "name": "Fixture Lead", "instrument": "tripleoscillator"}));
        let path = r.evidence.join(name);
        call(&s, "project.save", json!({"path": path}));
        assert!(matches!(s.stop_if_clean().unwrap(), ExitOutcome::Stopped { exit: Some(e) } if e.is_clean()));
        path
    }

    #[test]
    fn real_engine_supervised_start_reaches_ready_with_no_interface() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("start") else { return };
        let t0 = Instant::now();
        let mut s = Session::supervise(r.spec(None), r.opts("run"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        let ready_s = t0.elapsed().as_secs_f64();
        let ping = s.ping().unwrap().clone();
        assert_eq!(ping.interface, "none", "serve mode: no Qt interface");
        assert_eq!(ping.proto, Some(1));
        let pid = s.pid().unwrap();
        until(&mut s, "project state", |s| s.project().known);
        let state = call(&s, "project.get_state", json!({}));
        assert!(state["track_count"].as_i64().unwrap_or(0) > 0, "a usable project: {state}");
        let out = s.stop_if_clean().unwrap();
        let ExitOutcome::Stopped { exit: Some(exit) } = out else { panic!("{out:?}") };
        assert!(exit.is_clean(), "{exit:?}");
        assert!(!alive(pid));
        r.record("run", &s, json!({"ready_seconds": ready_s, "ping": format!("{ping:?}"), "exit": exit.describe(), "project_state": state}));
    }

    #[test]
    fn real_engine_killed_is_restarted_with_the_last_saved_project() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("kill9") else { return };
        let song = fixture(&r, "kill9.mmpz");
        let mut s = Session::supervise(r.spec(Some(&song)), r.opts("run"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        assert!(track_names(&s).iter().any(|n| n == "Fixture Lead"));
        call(&s, "track.add", json!({"type": "instrument", "name": "Unsaved Pad", "instrument": "tripleoscillator"}));
        until(&mut s, "unsaved noticed", |s| s.project().modified());
        let first = s.pid().unwrap();
        Command::new("kill").args(["-9", &first.to_string()]).status().unwrap();
        until(&mut s, "ready on a new engine", |s| s.status().is_ready() && s.pid().is_some_and(|p| p != first));
        assert_eq!(s.ready_kind(), ReadyKind::Restarted);
        let exited = s.history().iter().find_map(|(_, st)| match st { Status::EngineExited { exit, .. } => Some(*exit), _ => None }).expect("EngineExited in the history");
        assert_eq!(exited.signal, Some(9));
        let notices = s.take_notices();
        assert!(notices.iter().any(|n| n.contains("kill9.mmpz") && n.contains("were lost")), "{notices:?}");
        let names = track_names(&s);
        assert!(names.iter().any(|n| n == "Fixture Lead") && !names.iter().any(|n| n == "Unsaved Pad"), "{names:?}");
        assert_eq!(call(&s, "project.get_state", json!({}))["file"], song.display().to_string());
        let second = s.pid().unwrap();
        s.stop_if_clean().unwrap();
        r.record("run", &s, json!({"first_pid": first, "second_pid": second, "first_exit": exited.describe(), "notices": notices, "tracks_after_restart": names}));
    }

    #[test]
    fn real_engine_connection_drop_and_idle_close_reconnect_to_the_same_process() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("reconnect") else { return };
        let mut s = Session::supervise(r.spec(None), SessionOptions { subscribe_events: false, state_poll: Duration::from_secs(3600), ..r.opts("run") });
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        let pid = s.pid().unwrap();
        // 1. The client closes its connection.
        s.connection().unwrap().close();
        until(&mut s, "ready again", |s| s.status().is_ready() && s.generation() == 2);
        assert_eq!((s.pid(), s.ready_kind()), (Some(pid), ReadyKind::Reconnected));
        // 2. Idle: a second client with no keep-alive and no subscription is closed by the engine
        //    after 30 s; the first (keep-alive every 10 s) is not.
        let mut idle = Session::attach(
            s.socket_path(),
            SessionOptions { subscribe_events: false, state_poll: Duration::from_secs(3600), conn: ConnOptions { keepalive: None, ..Default::default() }, ..r.opts("idle") },
        );
        idle.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        let t0 = Instant::now();
        while t0.elapsed() < Duration::from_secs(34) {
            s.tick();
            idle.tick();
            std::thread::sleep(Duration::from_millis(50));
        }
        assert_eq!(s.generation(), 2, "with keep-alive the connection survived 34 s idle: {:#?}", s.history());
        assert!(idle.generation() >= 2, "without keep-alive the engine closed it and the session reconnected: {:#?}", idle.history());
        assert!(idle.status().is_ready() && s.pid() == Some(pid) && alive(pid), "same engine process throughout");
        r.record("idle", &idle, json!({"engine_pid": pid}));
        drop(idle);
        s.stop_if_clean().unwrap();
        r.record("run", &s, json!({"engine_pid": pid}));
    }

    #[test]
    fn real_engine_exit_paths_save_and_discard() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("exit") else { return };
        let song = fixture(&r, "exit.mmpz");
        // Save.
        let mut s = Session::supervise(r.spec(Some(&song)), r.opts("save"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        call(&s, "track.add", json!({"type": "instrument", "name": "Kept Strings", "instrument": "tripleoscillator"}));
        assert!(matches!(s.exit_check(), ExitCheck::Unsaved { known: true, .. }));
        let out = s.exit(ExitChoice::Save).unwrap();
        let ExitOutcome::Stopped { exit: Some(save_exit) } = out.clone() else { panic!("{out:?}") };
        assert!(save_exit.is_clean());
        r.record("save", &s, json!({"exit": save_exit.describe()}));
        // Discard.
        let mut s = Session::supervise(r.spec(Some(&song)), r.opts("discard"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        assert!(track_names(&s).iter().any(|n| n == "Kept Strings"), "the saved file holds the edit");
        call(&s, "track.add", json!({"type": "instrument", "name": "Dropped Brass", "instrument": "tripleoscillator"}));
        // A tempo change alone: the engine's flag stays false, the session still counts it.
        call(&s, "transport.set_tempo", json!({"bpm": 97}));
        assert!(matches!(s.exit_check(), ExitCheck::Unsaved { .. }));
        assert!(s.stop_if_clean().is_err(), "never quits without a choice while unsaved");
        let out = s.exit(ExitChoice::Discard).unwrap();
        let ExitOutcome::Stopped { exit: Some(discard_exit) } = out.clone() else { panic!("{out:?}") };
        assert!(discard_exit.is_clean());
        r.record("discard", &s, json!({"exit": discard_exit.describe()}));
        // Verify on disk through a fresh engine.
        let mut v = Session::supervise(r.spec(Some(&song)), r.opts("verify"));
        v.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        let names = track_names(&v);
        let tempo = call(&v, "project.get_state", json!({}))["tempo"].clone();
        assert!(names.iter().any(|n| n == "Kept Strings") && !names.iter().any(|n| n == "Dropped Brass"), "{names:?}");
        assert_ne!(tempo, 97);
        v.stop_if_clean().unwrap();
        r.record("verify", &v, json!({"tracks": names, "tempo": tempo}));
    }

    #[test]
    fn real_engine_tempo_change_does_not_set_modified() {
        // Evidence for the README's engine note (and the session's event-based tracking).
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("modified-flag") else { return };
        let song = fixture(&r, "flag.mmpz");
        let mut s = Session::supervise(r.spec(Some(&song)), r.opts("run"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        call(&s, "transport.set_tempo", json!({"bpm": 150}));
        let flag = call(&s, "project.get_state", json!({}))["modified"].clone();
        until(&mut s, "the event", |s| s.project().changed_since_save);
        eprintln!("engine modified flag after transport.set_tempo: {flag}");
        r.record("run", &s, json!({"engine_modified_after_set_tempo": flag}));
        s.exit(ExitChoice::Discard).unwrap();
    }

    #[test]
    fn real_engine_attach_never_stops_the_engine() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("attach") else { return };
        let mut owner = Session::supervise(r.spec(None), r.opts("owner"));
        owner.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        let pid = owner.pid().unwrap();
        let mut a = Session::attach(owner.socket_path(), r.opts("attached"));
        a.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        call(&a, "track.add", json!({"type": "instrument", "name": "From Attached", "instrument": "tripleoscillator"}));
        assert_eq!(a.exit_check(), ExitCheck::Detach);
        assert_eq!(a.exit(ExitChoice::Discard).unwrap(), ExitOutcome::Detached);
        r.record("attached", &a, json!({"engine_pid": pid}));
        drop(a);
        std::thread::sleep(Duration::from_millis(300));
        owner.tick();
        assert!(alive(pid) && owner.status().is_ready(), "{:?}", owner.status());
        assert!(track_names(&owner).iter().any(|n| n == "From Attached"));
        owner.exit(ExitChoice::Discard).unwrap();
        r.record("owner", &owner, json!({"engine_pid": pid}));
    }

    #[test]
    fn real_engine_stale_socket_is_cleaned_and_a_regular_file_is_refused() {
        let _one = ONE_AT_A_TIME.lock().unwrap_or_else(|e| e.into_inner());
        let Some(r) = real("socket-path") else { return };
        let rt = r.runtime.join("stale");
        std::fs::create_dir_all(&rt).unwrap();
        drop(UnixListener::bind(rt.join("engine.sock")).unwrap());
        let mut s = Session::supervise(r.spec(None), r.opts("stale"));
        s.wait_ready(T).unwrap_or_else(|st| panic!("{st:?}"));
        assert!(s.log().contains("removed a stale socket"));
        s.stop_if_clean().unwrap();
        r.record("stale", &s, json!({}));

        let rt = r.runtime.join("file");
        std::fs::create_dir_all(&rt).unwrap();
        std::fs::write(rt.join("engine.sock"), "not a socket").unwrap();
        let mut s = Session::supervise(r.spec(None), r.opts("file"));
        let err = s.wait_ready(T).unwrap_err();
        assert!(matches!(&err, Status::StartFailed { reason } if reason.contains("not a socket")), "{err:?}");
        assert_eq!(std::fs::read_to_string(rt.join("engine.sock")).unwrap(), "not a socket");
        assert!(s.pids().is_empty());
        r.record("file", &s, json!({"status": err.sentence()}));

        let s = Session::attach(r.runtime.join("nothing.sock"), r.opts("refused"));
        assert!(matches!(s.status(), Status::Disconnected { .. }));
        r.record("refused", &s, json!({"status": s.status().sentence()}));
    }
}
