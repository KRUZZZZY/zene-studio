//! The UI on the engine link, headless, against the fake engine (`zene_link::fake`): the status the
//! status bar shows, the model read from the engine, the close question and its three answers,
//! reload after an engine restart, reconnect, and that the demo still needs no engine.
//!
//! Supervised cases run the `zene-fake-engine` binary that `cargo test` builds for the workspace
//! (next to this test executable's directory); run alone with `-p zene-rune` it may be missing,
//! and those cases then print that they did not run.

#[cfg(not(unix))]
#[test]
fn link_ui_tests_skipped_on_this_platform() {
    eprintln!("SKIPPED: the UI link tests need the unix-socket transport; Windows is Q050-524");
}

#[test]
fn the_demo_needs_no_engine_and_quits_at_once() {
    let mut a = crate::state::App::new();
    assert!(crate::engine::Engine::link_status(&*a.engine).is_none());
    a.run(crate::command::Command::Quit);
    assert!(a.quit_requested && a.exit_prompt.is_none());
}

#[cfg(unix)]
mod unix {
    use std::path::{Path, PathBuf};
    use std::process::Command as Proc;
    use std::sync::atomic::{AtomicU32, Ordering};
    use std::time::{Duration, Instant};

    use rune::{Env, Runtime};
    use serde_json::json;
    use zene_link::fake::{FakeConfig, FakeEngine, load_project};
    use zene_link::{EngineSpec, Session, SessionOptions, Status};

    use crate::command::Command;
    use crate::state::*;
    use crate::ui;
    use crate::zene::{ZeneEngine, empty_project};

    static N: AtomicU32 = AtomicU32::new(0);

    fn dir() -> PathBuf {
        let d = PathBuf::from(format!("/tmp/zr-{}-{}", std::process::id(), N.fetch_add(1, Ordering::SeqCst)));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    /// The fake engine binary, if `cargo test` built it for the workspace.
    fn fake_bin() -> Option<PathBuf> {
        let exe = std::env::current_exe().ok()?;
        let b = exe.parent()?.parent()?.join("zene-fake-engine");
        b.is_file().then_some(b)
    }

    fn opts(d: &Path) -> SessionOptions {
        SessionOptions { runtime_dir: Some(d.join("rt")), log_dir: Some(d.join("logs")), state_poll: Duration::from_millis(50), reconnect_backoff: Duration::from_millis(20), ..Default::default() }
    }

    fn rig(session: Session) -> Runtime<App> {
        let mut e = ZeneEngine::start(session);
        let model = crate::engine::Engine::take_reload(&mut e).unwrap_or_else(empty_project);
        let mut app = App::with(model, Box::new(e));
        app.zoom_fit();
        let mut rt = Runtime::new(app, ui::view, (1366.0, 768.0), Env { system_dark: true, scale: 1.0 });
        rt.on_key = Some(crate::on_key);
        rt.poll = Some(crate::poll);
        frames(&mut rt, 4);
        rt
    }

    fn frames(rt: &mut Runtime<App>, n: usize) {
        for _ in 0..n {
            rt.frame(1.0 / 60.0);
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    fn until(rt: &mut Runtime<App>, what: &str, mut f: impl FnMut(&mut App) -> bool) {
        let t0 = Instant::now();
        while !f(&mut rt.state) {
            assert!(t0.elapsed() < Duration::from_secs(10), "timed out waiting for {what}; status line {:?}", rt.state.status);
            frames(rt, 1);
        }
    }

    fn link(rt: &mut Runtime<App>) -> &mut ZeneEngine {
        rt.state.engine.as_any().downcast_mut::<ZeneEngine>().unwrap()
    }

    fn chip(rt: &Runtime<App>) -> String {
        rt.state.engine.link_status().map(|s| s.label()).unwrap_or_default()
    }

    fn alive(pid: u32) -> bool {
        Proc::new("kill").args(["-0", &pid.to_string()]).stderr(std::process::Stdio::null()).status().is_ok_and(|s| s.success())
    }

    #[test]
    fn attached_the_ui_shows_ready_and_the_engines_project_and_detaches_on_quit() {
        let d = dir();
        let fe = FakeEngine::start(&d.join("e.sock"), FakeConfig::default(), None).unwrap();
        let mut rt = rig(Session::attach(&fe.socket, opts(&d)));
        assert_eq!(chip(&rt), "Engine ready");
        assert!(rt.find("engine-status").is_some());
        let names: Vec<&str> = rt.state.project.lanes().map(|t| t.name.as_str()).collect();
        assert_eq!(names, ["Bass", "Keys"], "the model is the engine's project");
        assert!(rt.state.status.contains("keeps running when this window closes"), "{}", rt.state.status);
        // The engine drops the connection: the status says so, then the UI is back.
        fe.drop_connections();
        until(&mut rt, "reconnected", |a| a.engine_link().is_some_and(|e| e.session.generation() >= 2 && e.session.status().is_ready()));
        frames(&mut rt, 2);
        assert!(rt.state.status.contains("Connected"), "{}", rt.state.status);
        rt.update(|s, _| s.run(Command::Quit));
        assert!(rt.state.quit_requested && rt.state.exit_prompt.is_none(), "attached: no question, just detach");
        assert_eq!(fe.quit_requested(), None);
    }

    #[test]
    fn an_unreachable_engine_opens_the_ui_with_an_actionable_status() {
        let d = dir();
        let rt = rig(Session::attach(d.join("missing.sock"), opts(&d)));
        assert_eq!(chip(&rt), "Engine disconnected");
        assert!(rt.state.status.contains("No engine is running") && rt.state.status.contains("Engine > Reconnect"), "{}", rt.state.status);
        assert!(rt.state.project.tracks.is_empty());
    }

    #[test]
    fn closing_with_unsaved_changes_asks_and_each_answer_does_what_it_says() {
        let Some(bin) = fake_bin() else {
            eprintln!("DID NOT RUN: zene-fake-engine was not built next to the test (run cargo test for the workspace)");
            return;
        };
        let d = dir();
        let song = d.join("song.json");
        std::fs::write(&song, json!({"tracks": ["Bass"], "tempo": 120}).to_string()).unwrap();
        let spec = || EngineSpec { project: Some(song.clone()), ..EngineSpec::new(&bin) };

        // Cancel, then Save.
        let mut rt = rig(Session::supervise(spec(), opts(&dir())));
        assert_eq!(chip(&rt), "Engine ready");
        let pid = link(&mut rt).session.pid().unwrap();
        rt.update(|s, _| s.run(Command::NudgeTempo(1.0)));
        until(&mut rt, "the edit seen as unsaved", |a| a.engine_modified());
        rt.update(|s, _| s.run(Command::Quit));
        let p = rt.state.exit_prompt.clone().expect("the question");
        assert_eq!(p.file.as_deref(), Some(song.as_path()));
        frames(&mut rt, 2);
        assert!(rt.find("exit-dialog").is_some() && rt.find("exit-save").is_some() && rt.find("exit-discard").is_some());
        rt.update(|s, _| s.run(Command::ExitCancel));
        assert!(rt.state.exit_prompt.is_none() && !rt.state.quit_requested && alive(pid));
        rt.update(|s, _| s.run(Command::Quit));
        rt.update(|s, _| s.run(Command::ExitSave));
        assert!(rt.state.quit_requested);
        assert!(!alive(pid), "the engine ended");
        assert_eq!(load_project(&song).unwrap()["tempo"], 121, "the saved file holds the edit");

        // Discard.
        let mut rt = rig(Session::supervise(spec(), opts(&dir())));
        let pid = link(&mut rt).session.pid().unwrap();
        rt.update(|s, _| s.run(Command::NudgeTempo(5.0)));
        until(&mut rt, "unsaved", |a| a.engine_modified());
        rt.update(|s, _| s.run(Command::Quit));
        assert!(rt.state.exit_prompt.is_some());
        rt.update(|s, _| s.run(Command::ExitDiscard));
        assert!(rt.state.quit_requested && !alive(pid));
        assert_eq!(load_project(&song).unwrap()["tempo"], 121, "discarded: the file is as last saved");

        // Nothing unsaved: no question.
        let mut rt = rig(Session::supervise(spec(), opts(&dir())));
        let pid = link(&mut rt).session.pid().unwrap();
        until(&mut rt, "state read", |a| a.engine_state_known());
        rt.update(|s, _| s.run(Command::Quit));
        assert!(rt.state.quit_requested && rt.state.exit_prompt.is_none() && !alive(pid));
    }

    #[test]
    fn after_an_engine_crash_the_ui_reloads_the_saved_project_and_says_what_was_lost() {
        let Some(bin) = fake_bin() else {
            eprintln!("DID NOT RUN: zene-fake-engine was not built next to the test (run cargo test for the workspace)");
            return;
        };
        let d = dir();
        let song = d.join("song.json");
        std::fs::write(&song, json!({"tracks": ["Bass", "Keys"], "tempo": 120}).to_string()).unwrap();
        let mut rt = rig(Session::supervise(EngineSpec { project: Some(song.clone()), ..EngineSpec::new(&bin) }, opts(&d)));
        let pid = link(&mut rt).session.pid().unwrap();
        rt.update(|s, _| s.run(Command::NudgeTempo(3.0)));
        until(&mut rt, "unsaved", |a| a.engine_modified());
        Proc::new("kill").args(["-9", &pid.to_string()]).status().unwrap();
        until(&mut rt, "ready on a new engine", |a| a.engine.link_status().is_some_and(|s| s.is_ready()) && a.status.contains("lost"));
        assert!(rt.state.status.contains("song.json") && rt.state.status.contains("Changes made after that save were lost"), "{}", rt.state.status);
        assert_eq!(rt.state.project.tempo, 120.0, "the model is the reopened saved file");
        assert_ne!(link(&mut rt).session.pid(), Some(pid));
        assert!(matches!(link(&mut rt).session.history().iter().find(|(_, s)| matches!(s, Status::EngineExited { .. })), Some(_)));
        rt.update(|s, _| s.run(Command::Quit));
        assert!(rt.state.quit_requested);
    }

    /// The real engine under the real UI (gated on `ZENE_ENGINE_BIN`, like `zene-link`'s
    /// `tests/real_engine.rs`): supervised start with no interface, the engine's project as the
    /// model, an edit made through the UI, and Quit > Save writing it to the file.
    #[test]
    fn real_engine_ui_supervised_shows_the_engines_project_and_saves_on_quit() {
        let Some(bin) = std::env::var_os("ZENE_ENGINE_BIN").map(PathBuf::from) else {
            eprintln!("DID NOT RUN real_engine_ui_supervised_shows_the_engines_project_and_saves_on_quit: set ZENE_ENGINE_BIN (see rune/README.md)");
            return;
        };
        let base = std::env::var_os("ZENE_ENGINE_EVIDENCE").map(PathBuf::from).unwrap_or_else(|| std::env::temp_dir().join(format!("zene-rune-real-{}", std::process::id())));
        let ev = base.join("ui");
        let _ = std::fs::remove_dir_all(&ev);
        std::fs::create_dir_all(&ev).unwrap();
        let sb = zene_link::runtime::Sandbox::create(&ev.join("sandbox")).unwrap();
        let spec = |project: Option<&Path>| EngineSpec { config: Some(sb.config.clone()), env: sb.env.clone(), cwd: Some(sb.root.clone()), project: project.map(Path::to_path_buf), ..EngineSpec::new(&bin) };
        let o = |sub: &str| SessionOptions { runtime_dir: Some(dir().join("rt")), log_dir: Some(ev.join(sub)), ready_timeout: Duration::from_secs(60), spawn_timeout: Duration::from_secs(60), ..Default::default() };
        // A saved project with a recognisable track, made through the engine.
        let song = ev.join("ui.mmpz");
        {
            let mut s = Session::supervise(spec(None), o("fixture"));
            s.wait_ready(Duration::from_secs(60)).unwrap();
            let c = s.connection().unwrap();
            c.call("track.add", json!({"type": "instrument", "name": "Fixture Lead", "instrument": "tripleoscillator"}), Duration::from_secs(30)).unwrap();
            c.call("project.save", json!({"path": song}), Duration::from_secs(30)).unwrap();
            s.stop_if_clean().unwrap();
        }
        let mut rt = rig(Session::supervise(spec(Some(&song)), o("ui")));
        assert_eq!(chip(&rt), "Engine ready", "{}", rt.state.status);
        let e = link(&mut rt);
        assert_eq!(e.session.ping().unwrap().interface, "none", "no Qt interface in the engine");
        let pid = e.session.pid().unwrap();
        let engine_names: Vec<String> = e.session.connection().unwrap().call("track.overview", json!({}), Duration::from_secs(30)).unwrap()["tracks"]
            .as_array().unwrap().iter().filter(|t| matches!(t["type"].as_str(), Some("instrument" | "sample"))).map(|t| t["name"].as_str().unwrap().to_string()).collect();
        let ui_names: Vec<String> = rt.state.project.lanes().map(|t| t.name.clone()).collect();
        assert_eq!(ui_names, engine_names, "the UI shows the engine's project");
        assert!(ui_names.iter().any(|n| n == "Fixture Lead"));
        let lead = rt.state.project.lanes().find(|t| t.name == "Fixture Lead").unwrap().id;
        rt.update(|s, _| {
            s.run(Command::SelectTrack(lead));
            s.run(Command::Rename("Renamed In Rune".into()));
        });
        frames(&mut rt, 6);
        rt.update(|s, _| s.run(Command::Quit));
        assert!(rt.state.exit_prompt.as_ref().is_some_and(|p| p.file.as_deref() == Some(song.as_path())), "{:?}", rt.state.exit_prompt);
        rt.update(|s, _| s.run(Command::ExitSave));
        assert!(rt.state.quit_requested, "{}", rt.state.status);
        assert!(!alive(pid));
        let hist: Vec<String> = link(&mut rt).session.history().iter().map(|(t, s)| format!("{t:.3} {}", s.label())).collect();
        std::fs::write(ev.join("ui-status-history.txt"), hist.join("\n")).unwrap();
        // Read it back through a fresh engine.
        let mut v = Session::supervise(spec(Some(&song)), o("verify"));
        v.wait_ready(Duration::from_secs(60)).unwrap();
        let names = v.connection().unwrap().call("track.overview", json!({}), Duration::from_secs(30)).unwrap().to_string();
        assert!(names.contains("Renamed In Rune"), "{names}");
        v.stop_if_clean().unwrap();
    }

    impl App {
        fn engine_link(&mut self) -> Option<&mut ZeneEngine> {
            self.engine.as_any().downcast_mut::<ZeneEngine>()
        }
        fn engine_modified(&mut self) -> bool {
            self.engine_link().is_some_and(|e| e.session.project().modified())
        }
        fn engine_state_known(&mut self) -> bool {
            self.engine_link().is_some_and(|e| e.session.project().known)
        }
    }
}
