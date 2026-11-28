//! Deterministic lifecycle tests: the session against the fake engine (`zene-fake-engine`, a real
//! process the supervisor spawns, kills and watches, or the in-process `FakeEngine`), each in an
//! isolated runtime directory. Linux and macOS; on Windows the file compiles to one test that says
//! why it did not run (the unix-socket transport is the only one until Q050-524).

#[cfg(not(unix))]
#[test]
fn lifecycle_tests_skipped_on_this_platform() {
    eprintln!("SKIPPED: the lifecycle tests need the unix-socket transport; the Windows named-pipe transport is Q050-524");
}

#[cfg(unix)]
mod unix {
    use std::os::unix::net::UnixListener;
    use std::path::{Path, PathBuf};
    use std::process::Command;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::time::{Duration, Instant};

    use serde_json::json;
    use zene_link::fake::{FakeConfig, FakeEngine, load_project};
    use zene_link::session::ReadyKind;
    use zene_link::{EngineSpec, ExitCheck, ExitChoice, ExitOutcome, LinkError, Session, SessionOptions, Status};

    const BIN: &str = env!("CARGO_BIN_EXE_zene-fake-engine");
    const T: Duration = Duration::from_secs(10);
    static N: AtomicU32 = AtomicU32::new(0);

    /// A fresh short directory under /tmp (socket paths must stay short).
    fn dir() -> PathBuf {
        let d = PathBuf::from(format!("/tmp/zl-{}-{}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    fn opts(d: &Path) -> SessionOptions {
        SessionOptions { runtime_dir: Some(d.join("rt")), log_dir: Some(d.join("logs")), state_poll: Duration::from_millis(50), reconnect_backoff: Duration::from_millis(20), ..Default::default() }
    }

    fn spec(env: &[(&str, &str)], project: Option<&Path>) -> EngineSpec {
        EngineSpec { env: env.iter().map(|(k, v)| (k.to_string(), v.to_string())).collect(), project: project.map(Path::to_path_buf), ..EngineSpec::new(BIN) }
    }

    fn alive(pid: u32) -> bool {
        Command::new("kill").args(["-0", &pid.to_string()]).stderr(std::process::Stdio::null()).status().is_ok_and(|s| s.success())
    }
    fn kill9(pid: u32) {
        Command::new("kill").args(["-9", &pid.to_string()]).status().unwrap();
    }

    /// Tick until `f` holds (or fail after `T`).
    fn until(s: &mut Session, what: &str, f: impl Fn(&Session) -> bool) {
        let t0 = Instant::now();
        while !f(s) {
            assert!(t0.elapsed() < T, "timed out waiting for {what}; status {:?}; history {:#?}", s.status(), s.history());
            s.tick();
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    fn call(s: &Session, cmd: &str, args: serde_json::Value) -> serde_json::Value {
        s.connection().expect("ready").call(cmd, args, T).unwrap()
    }

    fn saved_project(d: &Path, tracks: &[&str]) -> PathBuf {
        let p = d.join("song.json");
        std::fs::write(&p, json!({"tracks": tracks, "tempo": 120}).to_string()).unwrap();
        p
    }

    #[test]
    fn normal_launch_reaches_ready_and_a_clean_close_stops_the_engine_with_exit_0() {
        let d = dir();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        s.wait_ready(T).unwrap();
        let pid = s.pid().unwrap();
        assert!(matches!(s.status(), Status::Ready { pid: Some(p), .. } if *p == pid));
        assert!(s.ping().unwrap().is_serve());
        until(&mut s, "project state", |s| s.project().known);
        assert_eq!(s.exit_check(), ExitCheck::Clean);
        let out = s.stop_if_clean().unwrap();
        assert!(matches!(out, ExitOutcome::Stopped { exit: Some(e) } if e.is_clean()), "{out:?}");
        assert!(!alive(pid));
        assert!(!d.join("rt/engine.sock").exists(), "the engine unlinks its socket on quit");
        assert_eq!(*s.status(), Status::Stopped);
        assert!(std::fs::read_to_string(d.join("logs/protocol.log")).unwrap().contains("control.quit"));
    }

    #[test]
    fn connection_refusal_is_reported_with_the_cause() {
        let d = dir();
        let missing = d.join("none.sock");
        let s = Session::attach(&missing, opts(&d));
        let Status::Disconnected { reason } = s.status().clone() else { panic!("{:?}", s.status()) };
        assert!(reason.contains("No engine is running") && reason.contains("none.sock"), "{reason}");
        assert!(s.status().sentence().contains("Engine > Reconnect"));
        // A socket file nobody accepts on: refused, and said so.
        let stale = d.join("stale.sock");
        drop(UnixListener::bind(&stale).unwrap());
        let s = Session::attach(&stale, opts(&d));
        let Status::Disconnected { reason } = s.status().clone() else { panic!("{:?}", s.status()) };
        assert!(reason.contains("connection refused"), "{reason}");
        assert!(stale.exists(), "attach never deletes anything");
    }

    #[test]
    fn startup_failures_are_actionable_and_leave_no_process() {
        let d = dir();
        let cases: [(&[(&str, &str)], &str); 3] = [
            (&[("FAKE_ZENE_EXIT_AT_START", "3")], "exit code 3"),
            (&[("FAKE_ZENE_NOT_SERVE", "1")], "not running in engine mode"),
            (&[("FAKE_ZENE_PROTO", "2")], "protocol 2"),
        ];
        for (env, want) in cases {
            let mut s = Session::supervise(spec(env, None), opts(&dir()));
            let err = s.wait_ready(T).unwrap_err();
            let Status::StartFailed { reason } = &err else { panic!("{err:?}") };
            assert!(reason.contains(want), "{env:?}: {reason}");
            for pid in s.pids() {
                assert!(!alive(*pid), "{env:?}: process {pid} still running");
            }
        }
        let mut slow = Session::supervise(spec(&[("FAKE_ZENE_NEVER_READY", "1")], None), SessionOptions { ready_timeout: Duration::from_millis(400), ..opts(&dir()) });
        let err = slow.wait_ready(T).unwrap_err();
        assert!(matches!(&err, Status::StartFailed { reason } if reason.contains("did not report ready") && reason.contains("loading the project")), "{err:?}");
        let mut missing = Session::supervise(EngineSpec::new(d.join("no-zene")), opts(&dir()));
        let err = missing.wait_ready(T).unwrap_err();
        assert!(matches!(&err, Status::StartFailed { reason } if reason.contains("Check the engine path")), "{err:?}");
    }

    #[test]
    fn an_engine_killed_mid_session_is_restarted_with_the_last_saved_file_and_the_loss_is_said() {
        let d = dir();
        let song = saved_project(&d, &["Bass"]);
        let mut s = Session::supervise(spec(&[], Some(&song)), opts(&d));
        s.wait_ready(T).unwrap();
        call(&s, "track.add", json!({"name": "Saved"}));
        call(&s, "project.save", json!({}));
        call(&s, "track.add", json!({"name": "Unsaved"}));
        until(&mut s, "the unsaved edit noticed", |s| s.project().modified());
        let first = s.pid().unwrap();
        kill9(first);
        until(&mut s, "ready again", |s| s.status().is_ready() && s.pid() != Some(first));
        assert!(s.history().iter().any(|(_, st)| matches!(st, Status::EngineExited { exit, restarting: true, .. } if exit.signal == Some(9))), "{:#?}", s.history());
        assert_eq!(s.ready_kind(), ReadyKind::Restarted);
        assert_eq!(s.generation(), 2);
        let notices = s.take_notices();
        assert!(notices.iter().any(|n| n.contains("signal 9") && n.contains("song.json") && n.contains("Changes made after that save were lost")), "{notices:?}");
        let state = call(&s, "project.get_state", json!({}));
        assert_eq!(state["file"], song.display().to_string());
        let tracks = call(&s, "track.overview", json!({}));
        let names: Vec<&str> = tracks["tracks"].as_array().unwrap().iter().map(|t| t["name"].as_str().unwrap()).collect();
        assert_eq!(names, ["Bass", "Saved"], "the last saved version, without the unsaved track");
        assert_eq!(s.pids().len(), 2);
        s.stop_if_clean().unwrap();
    }

    #[test]
    fn an_engine_that_keeps_dying_is_not_restarted_forever() {
        let d = dir();
        let mut s = Session::supervise(spec(&[], None), SessionOptions { restart_limit: 1, ..opts(&d) });
        s.wait_ready(T).unwrap();
        kill9(s.pid().unwrap());
        until(&mut s, "restarted once", |s| s.status().is_ready() && s.pids().len() == 2);
        kill9(s.pid().unwrap());
        until(&mut s, "given up", |s| matches!(s.status(), Status::EngineExited { restarting: false, .. }));
        let text = s.status().sentence();
        assert!(text.contains("Engine > Restart") && text.contains("engine-2.log"), "{text}");
        s.restart();
        s.wait_ready(T).unwrap();
        assert_eq!(s.pids().len(), 3);
        s.stop_if_clean().unwrap();
    }

    #[test]
    fn a_dropped_connection_reconnects_to_the_same_process_and_signals_a_reread() {
        let d = dir();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        s.wait_ready(T).unwrap();
        let pid = s.pid().unwrap();
        s.connection().unwrap().close();
        until(&mut s, "reconnecting", |s| matches!(s.status(), Status::Reconnecting { .. }));
        until(&mut s, "ready again", |s| s.status().is_ready());
        assert_eq!(s.pid(), Some(pid), "the engine was not restarted");
        assert_eq!(s.pids(), [pid]);
        assert_eq!((s.generation(), s.ready_kind()), (2, ReadyKind::Reconnected));
        s.stop_if_clean().unwrap();

        // The engine side dropping every client (attached, in-process engine).
        let sock = d.join("e.sock");
        let fe = FakeEngine::start(&sock, FakeConfig::default(), None).unwrap();
        let mut a = Session::attach(&sock, opts(&d));
        a.wait_ready(T).unwrap();
        fe.drop_connections();
        until(&mut a, "reattached", |a| a.status().is_ready() && a.generation() == 2);
        assert_eq!(a.ready_kind(), ReadyKind::Reconnected);
        // ... and the engine going away for good ends in Disconnected with the reason.
        fe.crash();
        until(&mut a, "disconnected", |a| matches!(a.status(), Status::Disconnected { .. }));
        assert!(a.status().sentence().contains("connection refused"), "{}", a.status().sentence());
    }

    #[test]
    fn a_stale_socket_is_cleaned_but_a_live_one_or_a_regular_file_is_never_touched() {
        // Stale: a socket file nobody accepts on.
        let d = dir();
        std::fs::create_dir_all(d.join("rt")).unwrap();
        drop(UnixListener::bind(d.join("rt/engine.sock")).unwrap());
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        s.wait_ready(T).unwrap();
        assert!(s.log().contains("removed a stale socket"));
        s.stop_if_clean().unwrap();

        // A regular file at the socket path: refused, left exactly as it was.
        let d = dir();
        std::fs::create_dir_all(d.join("rt")).unwrap();
        std::fs::write(d.join("rt/engine.sock"), "precious").unwrap();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        let err = s.wait_ready(T).unwrap_err();
        assert!(matches!(&err, Status::StartFailed { reason } if reason.contains("not a socket") && reason.contains("left untouched")), "{err:?}");
        assert_eq!(std::fs::read_to_string(d.join("rt/engine.sock")).unwrap(), "precious");
        assert!(s.pids().is_empty(), "no engine was started");

        // A live engine's socket: refused, the engine keeps serving.
        let d = dir();
        std::fs::create_dir_all(d.join("rt")).unwrap();
        let fe = FakeEngine::start(&d.join("rt/engine.sock"), FakeConfig::default(), None).unwrap();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        let err = s.wait_ready(T).unwrap_err();
        assert!(matches!(&err, Status::StartFailed { reason } if reason.contains("already running")), "{err:?}");
        let mut probe = Session::attach(&fe.socket, opts(&d));
        probe.wait_ready(T).unwrap();
        fe.stop();
    }

    #[test]
    fn keepalive_holds_an_idle_connection_that_the_engine_would_close() {
        let d = dir();
        let sock = d.join("e.sock");
        let _fe = FakeEngine::start(&sock, FakeConfig { idle_close: Some(Duration::from_millis(150)), ..Default::default() }, None).unwrap();
        let quiet = |keepalive| SessionOptions {
            subscribe_events: false,
            state_poll: Duration::from_secs(3600),
            conn: zene_link::ConnOptions { keepalive, ..Default::default() },
            ..opts(&d)
        };
        let mut with = Session::attach(&sock, quiet(Some(Duration::from_millis(40))));
        with.wait_ready(T).unwrap();
        let mut without = Session::attach(&sock, quiet(None));
        without.wait_ready(T).unwrap();
        let t0 = Instant::now();
        while t0.elapsed() < Duration::from_millis(700) {
            with.tick();
            without.tick();
            std::thread::sleep(Duration::from_millis(10));
        }
        assert_eq!(with.generation(), 1, "with keep-alive the first connection is still the one: {:#?}", with.history());
        assert!(with.status().is_ready());
        assert!(without.generation() > 1 || !without.status().is_ready(), "without keep-alive the engine closes the idle connection: {:#?}", without.history());
    }

    #[test]
    fn quitting_never_discards_unsaved_changes_without_an_explicit_choice() {
        let d = dir();
        let song = saved_project(&d, &["Bass"]);
        let mut s = Session::supervise(spec(&[], Some(&song)), opts(&d));
        s.wait_ready(T).unwrap();
        let pid = s.pid().unwrap();
        // The engine does not set `modified` for a tempo change; its event still counts.
        call(&s, "transport.set_tempo", json!({"bpm": 133}));
        assert_eq!(call(&s, "project.get_state", json!({}))["modified"], false);
        until(&mut s, "event seen", |s| s.project().changed_since_save);
        assert_eq!(s.exit_check(), ExitCheck::Unsaved { file: Some(song.clone()), known: true });
        assert!(matches!(s.stop_if_clean(), Err(LinkError::UnsavedChanges(_))));
        assert!(alive(pid) && s.status().is_ready(), "refusing to quit leaves the engine running");
        assert_eq!(s.exit(ExitChoice::Cancel).unwrap(), ExitOutcome::Cancelled);
        assert!(alive(pid));
        let out = s.exit(ExitChoice::Save).unwrap();
        assert!(matches!(out, ExitOutcome::Stopped { exit: Some(e) } if e.is_clean()), "{out:?}");
        assert_eq!(load_project(&song).unwrap()["tempo"], 133, "the saved file holds the edit");

        // Discard: chosen explicitly, the edit is not written.
        let mut s = Session::supervise(spec(&[], Some(&song)), opts(&dir()));
        s.wait_ready(T).unwrap();
        call(&s, "track.add", json!({"name": "Scratch"}));
        assert!(matches!(s.exit_check(), ExitCheck::Unsaved { .. }));
        assert!(matches!(s.exit(ExitChoice::Discard).unwrap(), ExitOutcome::Stopped { exit: Some(e) } if e.is_clean()));
        assert_eq!(load_project(&song).unwrap()["tracks"], json!(["Bass"]));
    }

    #[test]
    fn discard_while_the_connection_is_down_stops_the_engine_at_once() {
        let d = dir();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        s.wait_ready(T).unwrap();
        let pid = s.pid().unwrap();
        s.connection().unwrap().close();
        // Save cannot work without a connection: refused, engine untouched.
        assert!(s.exit(ExitChoice::Save).is_err());
        assert!(alive(pid));
        let t0 = Instant::now();
        let out = s.exit(ExitChoice::Discard).unwrap();
        assert!(matches!(out, ExitOutcome::Stopped { exit: Some(e) } if e.signal == Some(15)), "{out:?}");
        assert!(t0.elapsed() < Duration::from_secs(3), "no wait for a quit that never reached the engine");
    }

    #[test]
    fn an_untitled_project_cannot_be_saved_on_quit_until_it_has_a_file() {
        let d = dir();
        let mut s = Session::supervise(spec(&[], None), opts(&d));
        s.wait_ready(T).unwrap();
        let pid = s.pid().unwrap();
        call(&s, "track.add", json!({"name": "New idea"}));
        assert_eq!(s.exit_check(), ExitCheck::Unsaved { file: None, known: true });
        let err = s.exit(ExitChoice::Save).unwrap_err();
        assert!(matches!(&err, LinkError::UnsavedChanges(m) if m.contains("never been saved") && m.contains("Save As")), "{err}");
        assert!(alive(pid) && s.status().is_ready());
        let target = d.join("idea.json");
        assert!(matches!(s.exit(ExitChoice::SaveAs(target.clone())).unwrap(), ExitOutcome::Stopped { .. }));
        assert!(load_project(&target).unwrap()["tracks"].as_array().unwrap().iter().any(|t| t == "New idea"));
    }

    #[test]
    fn attached_engines_are_never_stopped_and_dropped_sessions_keep_unsaved_engines_alive() {
        let d = dir();
        let sock = d.join("e.sock");
        let fe = FakeEngine::start(&sock, FakeConfig::default(), None).unwrap();
        let mut a = Session::attach(&sock, opts(&d));
        a.wait_ready(T).unwrap();
        call(&a, "track.add", json!({"name": "x"}));
        assert_eq!(a.exit_check(), ExitCheck::Detach);
        assert_eq!(a.exit(ExitChoice::Discard).unwrap(), ExitOutcome::Detached);
        drop(a);
        assert_eq!(fe.quit_requested(), None, "no control.quit reached the attached engine");
        assert!(!fe.requests().iter().any(|r| r["cmd"] == "control.quit"));

        // A supervised session dropped with unsaved changes leaves its engine running.
        let mut s = Session::supervise(spec(&[], None), opts(&dir()));
        s.wait_ready(T).unwrap();
        call(&s, "track.add", json!({"name": "unsaved"}));
        until(&mut s, "noticed", |s| s.project().modified());
        let pid = s.pid().unwrap();
        drop(s);
        assert!(alive(pid), "the engine with unsaved changes was left running");
        kill9(pid);
        // ... and one with nothing unsaved is stopped.
        let mut s = Session::supervise(spec(&[], None), opts(&dir()));
        s.wait_ready(T).unwrap();
        until(&mut s, "state", |s| s.project().known);
        let pid = s.pid().unwrap();
        drop(s);
        let t0 = Instant::now();
        while alive(pid) && t0.elapsed() < T {
            std::thread::sleep(Duration::from_millis(10));
        }
        assert!(!alive(pid));
    }
}
