//! The engine session: one engine, started and supervised by this process or attached to by socket
//! path, driven by [`Session::tick`] (the UI calls it once per frame; it never blocks).
//!
//! Lifecycle: `Starting` (process spawned) -> `Connecting` (socket open, `control.ping` until
//! `engine_ready`, checking `proto` and that the program really is in serve mode) -> `Ready`.
//! A dropped connection to a running engine goes to `Reconnecting` and back to `Ready` without
//! touching the process. An engine this session started that ends on its own is restarted (at most
//! `restart_limit` times in `restart_window`) with the last SAVED project file, and a notice says
//! plainly what was lost: serve mode keeps no autosave, so edits after the last save are gone.
//!
//! Data-safety rules (Q050-506 acceptance 2), each enforced here, not in the UI:
//! 1. `control.quit {save:false}` and SIGTERM are only ever sent by [`Session::exit`] when the caller
//!    chose [`ExitChoice::Discard`], or after a fresh read says nothing is unsaved.
//! 2. "Unsaved" is the engine's `modified` flag OR any `project_changed` event since the last
//!    `project.save`/`project.open` (the engine's flag misses some edits; see the README).
//! 3. Closing the front end never stops an engine it attached to.
//! 4. A session dropped without an exit decision stops its engine only if nothing is unsaved;
//!    otherwise it leaves the engine running and says how to reach it.

use std::collections::VecDeque;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use crate::conn::{ConnOptions, Connection};
use crate::error::LinkError;
use crate::plog::ProtocolLog;
use crate::process::{EngineProcess, EngineSpec, ExitInfo};
use crate::proto::{PROTO, Ping};
use crate::runtime::{Prepared, RuntimeDir, prepare_socket};
use crate::status::Status;
use crate::transport::{Transport, default_transport};

#[derive(Clone)]
pub struct SessionOptions {
    pub conn: ConnOptions,
    /// The engine must accept on its socket within this time after it was started.
    pub spawn_timeout: Duration,
    /// ... and report `engine_ready` within this time after the connection opened.
    pub ready_timeout: Duration,
    pub ping_interval: Duration,
    /// How often `project.get_state` is read while ready (file and modified flag).
    pub state_poll: Duration,
    pub reconnect_attempts: u32,
    pub reconnect_backoff: Duration,
    pub restart_limit: u32,
    pub restart_window: Duration,
    /// After `control.quit`, how long the process gets to end.
    pub stop_timeout: Duration,
    /// Where the protocol log and the engine's output go (default: the runtime directory).
    pub log_dir: Option<PathBuf>,
    /// The runtime directory of a supervised engine (default: a fresh private one).
    pub runtime_dir: Option<PathBuf>,
    pub subscribe_events: bool,
    pub transport: Arc<dyn Transport>,
}

impl Default for SessionOptions {
    fn default() -> Self {
        SessionOptions {
            conn: ConnOptions::default(),
            spawn_timeout: Duration::from_secs(20),
            ready_timeout: Duration::from_secs(30),
            ping_interval: Duration::from_millis(50),
            state_poll: Duration::from_secs(1),
            reconnect_attempts: 8,
            reconnect_backoff: Duration::from_millis(100),
            restart_limit: 3,
            restart_window: Duration::from_secs(60),
            stop_timeout: Duration::from_secs(10),
            log_dir: None,
            runtime_dir: None,
            subscribe_events: true,
            transport: Arc::from(default_transport()),
        }
    }
}

/// What the session knows about the engine's project.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ProjectInfo {
    /// The project's file; `None` if it was never saved.
    pub file: Option<PathBuf>,
    /// `project.get_state`'s `modified`.
    pub engine_modified: bool,
    /// A `project_changed` event arrived since the last save or open.
    pub changed_since_save: bool,
    /// At least one `project.get_state` has answered.
    pub known: bool,
}

impl ProjectInfo {
    pub fn modified(&self) -> bool {
        self.engine_modified || self.changed_since_save
    }
    fn read(&mut self, v: &Value) {
        self.file = v["file"].as_str().filter(|f| !f.is_empty()).map(PathBuf::from);
        self.engine_modified = v["modified"].as_bool().unwrap_or(false);
        self.known = true;
    }
}

/// What closing the front end needs.
#[derive(Clone, Debug, PartialEq)]
pub enum ExitCheck {
    /// No engine of ours is running.
    NoEngine,
    /// Attached: the engine keeps running; just disconnect.
    Detach,
    /// Our engine has nothing unsaved: it can be closed.
    Clean,
    /// Our engine has unsaved changes (or we cannot tell): the user must choose.
    Unsaved { file: Option<PathBuf>, known: bool },
}

#[derive(Clone, Debug, PartialEq)]
pub enum ExitChoice {
    Save,
    SaveAs(PathBuf),
    Discard,
    Cancel,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ExitOutcome {
    Cancelled,
    Detached,
    Stopped { exit: Option<ExitInfo> },
    /// The engine was asked to quit and did not end in time; it was left running (never killed
    /// while a save might still be in progress).
    LeftRunning { pid: u32 },
}

/// How the latest `Ready` came about.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReadyKind {
    First,
    /// Same engine process, new connection: the front end rereads state; its pending edits still apply.
    Reconnected,
    /// A new engine process after an unexpected exit: the front end reloads everything.
    Restarted,
}

enum Mode {
    Supervised { spec: EngineSpec, runtime: RuntimeDir },
    Attached { socket: PathBuf },
}

#[derive(Clone, Copy)]
enum Phase {
    Idle,
    Spawned { since: Instant },
    Connecting { since: Instant, last_ping: Option<Instant> },
    Ready { last_poll: Instant },
    Reconnecting { attempt: u32, max: u32, next_at: Instant },
    Down,
}

type Slot = Arc<Mutex<Option<Result<Value, LinkError>>>>;

pub struct Session {
    mode: Mode,
    opts: SessionOptions,
    log: ProtocolLog,
    phase: Phase,
    status: Status,
    history: Vec<(f64, Status)>,
    born: Instant,
    conn: Option<Connection>,
    process: Option<EngineProcess>,
    pids: Vec<u32>,
    spawns: u32,
    restarts: VecDeque<Instant>,
    /// Set when the next Ready follows a restart.
    restarting: bool,
    ready_kind: ReadyKind,
    ever_ready_this_process: bool,
    generation: u64,
    project: Arc<Mutex<ProjectInfo>>,
    ping: Option<Ping>,
    ping_slot: Slot,
    poll_slot: Slot,
    poll_pending: bool,
    last_seq: Option<i64>,
    /// The engine's event sequence number when the subscription began (from its reply).
    sub_seq: Arc<Mutex<Option<i64>>>,
    events: VecDeque<Value>,
    notices: Vec<String>,
    stopping: bool,
}

const MAX_EVENTS_KEPT: usize = 4096;

impl Session {
    /// Start and supervise an engine. Returns at once; `tick` (or `wait_ready`) drives it.
    pub fn supervise(spec: EngineSpec, opts: SessionOptions) -> Session {
        let runtime = match &opts.runtime_dir {
            Some(d) => RuntimeDir::at(d),
            None => RuntimeDir::create(),
        };
        match runtime {
            Ok(runtime) => {
                let mut s = Session::new(Mode::Supervised { spec, runtime }, opts);
                s.spawn();
                s
            }
            Err(e) => {
                let mut s = Session::new(Mode::Attached { socket: PathBuf::new() }, opts);
                s.fail_start(format!("No runtime directory for the engine: {e}."));
                s
            }
        }
    }

    /// Connect to an engine someone else started. This session never stops it.
    pub fn attach(socket: impl Into<PathBuf>, opts: SessionOptions) -> Session {
        let mut s = Session::new(Mode::Attached { socket: socket.into() }, opts);
        s.phase = Phase::Reconnecting { attempt: 1, max: 1, next_at: Instant::now() };
        s.tick();
        s
    }

    fn new(mode: Mode, opts: SessionOptions) -> Session {
        let log = opts.conn.log.clone();
        let log_dir = opts.log_dir.clone().or_else(|| match &mode {
            Mode::Supervised { runtime, .. } => Some(runtime.path.clone()),
            Mode::Attached { .. } => None,
        });
        if let Some(d) = &log_dir {
            let _ = std::fs::create_dir_all(d);
            if let Err(e) = log.mirror_to(&d.join("protocol.log")) {
                log.note(format!("no protocol log file in {}: {e}", d.display()));
            }
        }
        let opts = SessionOptions { log_dir, ..opts };
        Session {
            mode,
            opts,
            log,
            phase: Phase::Idle,
            status: Status::Idle,
            history: Vec::new(),
            born: Instant::now(),
            conn: None,
            process: None,
            pids: Vec::new(),
            spawns: 0,
            restarts: VecDeque::new(),
            restarting: false,
            ready_kind: ReadyKind::First,
            ever_ready_this_process: false,
            generation: 0,
            project: Arc::new(Mutex::new(ProjectInfo::default())),
            ping: None,
            ping_slot: Arc::new(Mutex::new(None)),
            poll_slot: Arc::new(Mutex::new(None)),
            poll_pending: false,
            last_seq: None,
            sub_seq: Arc::new(Mutex::new(None)),
            events: VecDeque::new(),
            notices: Vec::new(),
            stopping: false,
        }
    }

    // ---- what the UI reads -------------------------------------------------------------------

    pub fn status(&self) -> &Status {
        &self.status
    }
    /// Every status this session went through, with seconds since it began.
    pub fn history(&self) -> &[(f64, Status)] {
        &self.history
    }
    /// The connection while there is one (send through it; drain its completions per frame).
    pub fn connection(&self) -> Option<&Connection> {
        self.conn.as_ref().filter(|_| matches!(self.phase, Phase::Ready { .. }))
    }
    /// Increases every time the session becomes ready (the front end rereads state when it changes).
    pub fn generation(&self) -> u64 {
        self.generation
    }
    pub fn ready_kind(&self) -> ReadyKind {
        self.ready_kind
    }
    pub fn project(&self) -> ProjectInfo {
        self.project.lock().unwrap().clone()
    }
    pub fn ping(&self) -> Option<&Ping> {
        self.ping.as_ref()
    }
    /// The engine process this session runs now, if any.
    pub fn pid(&self) -> Option<u32> {
        self.process.as_ref().filter(|p| p.exit_known().is_none()).map(|p| p.pid)
    }
    /// Every engine process this session started, oldest first.
    pub fn pids(&self) -> &[u32] {
        &self.pids
    }
    pub fn is_supervised(&self) -> bool {
        matches!(self.mode, Mode::Supervised { .. })
    }
    pub fn socket_path(&self) -> PathBuf {
        match &self.mode {
            Mode::Supervised { runtime, .. } => runtime.socket_path(),
            Mode::Attached { socket } => socket.clone(),
        }
    }
    pub fn log(&self) -> &ProtocolLog {
        &self.log
    }
    pub fn log_dir(&self) -> Option<&Path> {
        self.opts.log_dir.as_deref()
    }
    pub fn engine_log(&self) -> Option<PathBuf> {
        self.process.as_ref().map(|p| p.log_path.clone())
    }
    /// Plain sentences the user must see once (what was lost after a crash, a refused save).
    pub fn take_notices(&mut self) -> Vec<String> {
        std::mem::take(&mut self.notices)
    }
    /// Events from the engine since the last call, for the front end's model (Q050-507).
    pub fn take_events(&mut self) -> Vec<Value> {
        self.events.drain(..).collect()
    }

    // ---- driving ------------------------------------------------------------------------------

    /// Advance the state machine. Never blocks for long (a connect attempt on a local socket).
    /// Returns whether the status changed.
    pub fn tick(&mut self) -> bool {
        let before = self.status.clone();
        self.step();
        self.status != before
    }

    /// Tick until ready, a failure, or `timeout` (start-up and tests only: this blocks).
    pub fn wait_ready(&mut self, timeout: Duration) -> Result<(), Status> {
        let t0 = Instant::now();
        loop {
            self.tick();
            match (&self.phase, &self.status) {
                (Phase::Ready { .. }, _) => return Ok(()),
                (Phase::Down | Phase::Idle, s) => return Err(s.clone()),
                _ => {}
            }
            if t0.elapsed() >= timeout {
                return Err(self.status.clone());
            }
            std::thread::sleep(Duration::from_millis(5));
        }
    }

    /// Try to connect again now (the user's Engine > Reconnect).
    pub fn reconnect(&mut self) {
        if matches!(self.phase, Phase::Down | Phase::Reconnecting { .. }) && self.process_alive_or_attached() {
            self.log.note("reconnect requested");
            self.phase = Phase::Reconnecting { attempt: 1, max: self.opts.reconnect_attempts, next_at: Instant::now() };
            self.set_status(Status::Reconnecting { attempt: 1, max: self.opts.reconnect_attempts });
        }
    }

    /// Start a supervised engine again after it stopped (the user's Engine > Restart).
    pub fn restart(&mut self) {
        if self.is_supervised() && matches!(self.phase, Phase::Down) && !self.process_running() {
            self.log.note("restart requested");
            self.restarts.clear();
            self.reopen_last_saved();
            self.restarting = true;
            self.spawn();
        }
    }

    fn step(&mut self) {
        match self.phase {
            Phase::Idle | Phase::Down => {}
            Phase::Spawned { since } => self.step_spawned(since),
            Phase::Connecting { since, last_ping } => self.step_connecting(since, last_ping),
            Phase::Ready { last_poll } => self.step_ready(last_poll),
            Phase::Reconnecting { attempt, max, next_at } => self.step_reconnecting(attempt, max, next_at),
        }
    }

    fn step_spawned(&mut self, since: Instant) {
        if let Some(exit) = self.process_exit() {
            return self.on_exit(exit);
        }
        let sock = self.socket_path();
        match Connection::open(&*self.opts.transport, &sock, self.conn_opts()) {
            Ok(c) => self.connected(c),
            Err(LinkError::NotFound(_) | LinkError::Refused(_)) if since.elapsed() < self.opts.spawn_timeout => {}
            Err(e) => {
                let secs = self.opts.spawn_timeout.as_secs();
                let why = match e {
                    LinkError::NotFound(_) | LinkError::Refused(_) => format!("The engine did not open its control socket within {secs} s."),
                    e => format!("The engine's control socket could not be reached: {e}."),
                };
                self.stop_process_after_failed_start();
                self.fail_start(format!("{why} {}", self.log_hint()));
            }
        }
    }

    fn connected(&mut self, c: Connection) {
        self.conn = Some(c);
        *self.ping_slot.lock().unwrap() = None;
        self.phase = Phase::Connecting { since: Instant::now(), last_ping: None };
        self.set_status(Status::Connecting { reason: String::new() });
    }

    fn step_connecting(&mut self, since: Instant, last_ping: Option<Instant>) {
        if let Some(exit) = self.process_exit() {
            return self.on_exit(exit);
        }
        if self.conn.as_ref().is_some_and(|c| !c.is_open()) {
            return self.on_drop();
        }
        let reply = self.ping_slot.lock().unwrap().take();
        if let Some(reply) = reply {
            match reply {
                Ok(v) => {
                    let p = Ping::parse(&v);
                    if let Some(why) = self.refuse_engine(&p) {
                        self.ping = Some(p);
                        return self.refused(why);
                    }
                    let ready = p.engine_ready;
                    let reason = p.reason.clone();
                    self.ping = Some(p);
                    if ready {
                        return self.become_ready();
                    }
                    self.set_status(Status::Connecting { reason });
                }
                Err(e) => self.log.note(format!("control.ping failed: {e}")),
            }
            self.phase = Phase::Connecting { since, last_ping: None };
            return;
        }
        if since.elapsed() > self.opts.ready_timeout {
            let secs = self.opts.ready_timeout.as_secs();
            let last = self.ping.as_ref().map(|p| p.reason.clone()).filter(|r| !r.is_empty()).map(|r| format!(" Its last word: {r}.")).unwrap_or_default();
            let why = format!("The engine did not report ready within {secs} s.{last} {}", self.log_hint());
            if let Some(c) = &self.conn {
                c.close();
            }
            if self.is_supervised() {
                self.stop_process_after_failed_start();
                return self.fail_start(why);
            }
            return self.go_down(Status::Disconnected { reason: why });
        }
        if last_ping.is_none_or(|t| t.elapsed() >= self.opts.ping_interval) {
            let slot = self.ping_slot.clone();
            let sent = self.conn.as_ref().map(|c| c.send_with("control.ping", json!({}), move |r| *slot.lock().unwrap() = Some(r)));
            if let Some(Err(e)) = sent {
                self.log.note(format!("control.ping not sent: {e}"));
            }
            self.phase = Phase::Connecting { since, last_ping: Some(Instant::now()) };
        }
    }

    /// Why this engine cannot be used, if it cannot.
    fn refuse_engine(&self, p: &Ping) -> Option<String> {
        if let Some(proto) = p.proto.filter(|v| *v != PROTO) {
            return Some(format!(
                "The engine speaks control protocol {proto} and this Zene speaks {PROTO}. Use an engine and a front end from the same Zene release."
            ));
        }
        if self.is_supervised() && !p.is_serve() {
            let iface = if p.interface.is_empty() { "reports no interface mode".to_string() } else { format!("reports interface \"{}\"", p.interface) };
            return Some(format!(
                "The program started as the engine is not running in engine mode (it {iface}): it is probably a Zene without serve mode (zene --engine, Q050-517). Point --engine-bin at a Zene that has it."
            ));
        }
        None
    }

    fn refused(&mut self, why: String) {
        if let Some(c) = &self.conn {
            c.close();
        }
        if self.is_supervised() {
            self.stop_process_after_failed_start();
            self.fail_start(why);
        } else {
            self.go_down(Status::Disconnected { reason: why });
        }
    }

    fn become_ready(&mut self) {
        self.generation += 1;
        self.ready_kind = if self.restarting {
            ReadyKind::Restarted
        } else if self.generation == 1 || !self.ever_ready_this_process {
            ReadyKind::First
        } else {
            ReadyKind::Reconnected
        };
        self.restarting = false;
        self.ever_ready_this_process = true;
        self.last_seq = None;
        *self.sub_seq.lock().unwrap() = None;
        if let Some(c) = &self.conn {
            if self.opts.subscribe_events {
                let log = self.log.clone();
                let sub_seq = self.sub_seq.clone();
                let _ = c.send_with("events.subscribe", json!({"queue_limit": 4096}), move |r| match r {
                    Ok(v) => *sub_seq.lock().unwrap() = v["seq"].as_i64(),
                    Err(e) => log.note(format!("events.subscribe refused: {e} (unsaved-change tracking falls back to the engine's modified flag)")),
                });
            }
        }
        self.request_state();
        self.phase = Phase::Ready { last_poll: Instant::now() };
        let version = self.ping.as_ref().map(|p| p.version.clone()).unwrap_or_default();
        let pid = if self.is_supervised() { self.pid() } else { None };
        self.set_status(Status::Ready { pid, version });
    }

    fn step_ready(&mut self, last_poll: Instant) {
        if let Some(exit) = self.process_exit() {
            return self.on_exit(exit);
        }
        if self.conn.as_ref().is_none_or(|c| !c.is_open()) {
            return self.on_drop();
        }
        self.take_engine_events();
        let polled = self.poll_slot.lock().unwrap().take();
        if let Some(r) = polled {
            self.poll_pending = false;
            match r {
                Ok(v) => self.project.lock().unwrap().read(&v),
                Err(e) => self.log.note(format!("project.get_state failed: {e}")),
            }
        }
        if !self.poll_pending && last_poll.elapsed() >= self.opts.state_poll {
            self.request_state();
            self.phase = Phase::Ready { last_poll: Instant::now() };
        }
    }

    fn request_state(&mut self) {
        let slot = self.poll_slot.clone();
        if let Some(c) = &self.conn {
            if c.send_with("project.get_state", json!({}), move |r| *slot.lock().unwrap() = Some(r)).is_ok() {
                self.poll_pending = true;
            }
        }
    }

    /// Unsaved-change tracking from `project_changed` events (rule 2 in the module comment).
    fn take_engine_events(&mut self) {
        let Some(c) = &self.conn else { return };
        let dropped_before = c.dropped_events();
        let evs = c.drain_events();
        if self.last_seq.is_none() {
            self.last_seq = *self.sub_seq.lock().unwrap();
        }
        let mut info = self.project.lock().unwrap();
        for ev in &evs {
            if ev["event"] != "project_changed" {
                continue;
            }
            if let Some(seq) = ev["seq"].as_i64() {
                if self.last_seq.is_some_and(|l| seq != l + 1) {
                    // A gap: events were lost, so assume the worst about what they said.
                    info.changed_since_save = true;
                }
                self.last_seq = Some(seq);
            }
            match ev["command"].as_str().unwrap_or("") {
                "project.save" | "project.open" | "project.new" | "project.switch" => {
                    info.changed_since_save = false;
                    self.poll_pending = false; // reread file and flag at once
                }
                _ => info.changed_since_save = true,
            }
        }
        if c.dropped_events() > dropped_before {
            info.changed_since_save = true;
        }
        drop(info);
        for ev in evs {
            if self.events.len() >= MAX_EVENTS_KEPT {
                self.events.pop_front();
            }
            self.events.push_back(ev);
        }
    }

    fn on_drop(&mut self) {
        let why = self.conn.as_ref().and_then(|c| c.close_reason()).unwrap_or_else(|| "no connection".into());
        self.log.note(format!("connection lost: {why}"));
        // A killed engine closes its socket a moment before it can be reaped: give it that moment.
        if let Some(p) = self.process.as_mut() {
            if let Some(exit) = p.wait_exit(Duration::from_millis(300)) {
                p.mark_seen();
                return self.on_exit(exit);
            }
        }
        self.conn = None;
        let max = self.opts.reconnect_attempts;
        self.phase = Phase::Reconnecting { attempt: 1, max, next_at: Instant::now() };
        self.set_status(Status::Reconnecting { attempt: 1, max });
    }

    fn step_reconnecting(&mut self, attempt: u32, max: u32, next_at: Instant) {
        if let Some(exit) = self.process_exit() {
            return self.on_exit(exit);
        }
        if Instant::now() < next_at {
            return;
        }
        let sock = self.socket_path();
        match Connection::open(&*self.opts.transport, &sock, self.conn_opts()) {
            Ok(c) => self.connected(c),
            Err(e) if attempt >= max => {
                let reason = unreachable_reason(&sock, &e);
                self.go_down(Status::Disconnected { reason });
            }
            Err(_) => {
                let wait = (self.opts.reconnect_backoff * 2u32.saturating_pow(attempt - 1)).min(Duration::from_secs(2));
                self.phase = Phase::Reconnecting { attempt: attempt + 1, max, next_at: Instant::now() + wait };
                self.set_status(Status::Reconnecting { attempt: attempt + 1, max });
            }
        }
    }

    /// Our engine process ended.
    fn on_exit(&mut self, exit: ExitInfo) {
        if let Some(c) = self.conn.take() {
            c.close();
        }
        self.log.note(format!("engine process ended: {}", exit.describe()));
        if self.stopping {
            return self.go_down(Status::Stopped);
        }
        if !self.ever_ready_this_process {
            let why = format!("The engine stopped while starting ({}). {}", exit.describe(), self.log_hint());
            return self.fail_start(why);
        }
        let now = Instant::now();
        while self.restarts.front().is_some_and(|t| now.duration_since(*t) > self.opts.restart_window) {
            self.restarts.pop_front();
        }
        let log = self.engine_log().map(|p| p.display().to_string()).unwrap_or_default();
        let info = self.project();
        if self.restarts.len() as u32 >= self.opts.restart_limit {
            self.notices.push(lost_sentence(&exit, &info, false));
            return self.go_down(Status::EngineExited { exit, restarting: false, log });
        }
        self.restarts.push_back(now);
        self.set_status(Status::EngineExited { exit, restarting: true, log });
        self.notices.push(lost_sentence(&exit, &info, true));
        self.reopen_last_saved();
        self.restarting = true;
        self.spawn();
    }

    /// The next start opens the project's last saved file (or nothing if it never had one).
    fn reopen_last_saved(&mut self) {
        let file = self.project().file.filter(|f| f.is_file());
        if let Mode::Supervised { spec, .. } = &mut self.mode {
            if file.is_some() {
                spec.project = file;
            } else if self.generation > 0 {
                spec.project = None;
            }
        }
        let mut info = self.project.lock().unwrap();
        info.engine_modified = false;
        info.changed_since_save = false;
        info.known = false;
    }

    fn spawn(&mut self) {
        let Mode::Supervised { spec, runtime } = &self.mode else { return };
        let (spec, sock) = (spec.clone(), runtime.socket_path());
        match prepare_socket(&sock, &*self.opts.transport) {
            Ok(Prepared::Free) => {}
            Ok(Prepared::RemovedStale) => self.log.note(format!("removed a stale socket at {} (nothing was accepting on it)", sock.display())),
            Err(e) => {
                let why = match &e {
                    LinkError::NotASocket(p) => format!("Something that is not a socket is at {p}; it was left untouched. Move it away or start the engine with another runtime directory."),
                    LinkError::InUse(p) => format!("An engine is already running at {p}; it was left running. Attach to it with --engine {p}, or stop it first."),
                    e => format!("The engine's socket path cannot be used: {e}."),
                };
                return self.fail_start(why);
            }
        }
        self.spawns += 1;
        let log_dir = self.opts.log_dir.clone().unwrap_or_else(std::env::temp_dir);
        let log_path = log_dir.join(format!("engine-{}.log", self.spawns));
        match EngineProcess::spawn(&spec, &sock, &log_path) {
            Ok(p) => {
                self.log.note(format!("started engine process {}: {} {:?} (output: {})", p.pid, spec.bin.display(), spec.argv(&sock), log_path.display()));
                self.pids.push(p.pid);
                self.set_status(Status::Starting { pid: p.pid });
                self.process = Some(p);
                self.ever_ready_this_process = false;
                self.phase = Phase::Spawned { since: Instant::now() };
            }
            Err(e) => self.fail_start(e.to_string()),
        }
    }

    // ---- exit -----------------------------------------------------------------------------------

    /// What closing the front end requires. Reads the engine's state afresh when it can (blocks
    /// for at most two seconds: exit path only).
    pub fn exit_check(&mut self) -> ExitCheck {
        if !self.is_supervised() {
            return ExitCheck::Detach;
        }
        if !self.process_running() {
            return ExitCheck::NoEngine;
        }
        let fresh = self.refresh_project();
        let info = self.project();
        if fresh && !info.modified() {
            ExitCheck::Clean
        } else {
            ExitCheck::Unsaved { file: info.file, known: fresh }
        }
    }

    /// Read `project.get_state` now and fold in every event the engine has sent so far; false if
    /// the engine did not answer. Events can trail replies (an edit from another client, or one
    /// still in the engine's queue), so it first asks `events.get_state` for the newest sequence
    /// number and waits (at most a second) until the events up to it have arrived.
    fn refresh_project(&mut self) -> bool {
        let Some(c) = self.conn.clone().filter(|c| c.is_open() && matches!(self.phase, Phase::Ready { .. })) else {
            self.take_engine_events();
            return false;
        };
        if self.opts.subscribe_events {
            match c.call("events.get_state", json!({}), Duration::from_secs(2)).map(|v| v["seq"].as_i64()) {
                Ok(Some(target)) => {
                    let t0 = Instant::now();
                    loop {
                        self.take_engine_events();
                        if self.last_seq.is_some_and(|l| l >= target) || t0.elapsed() > Duration::from_secs(1) {
                            break;
                        }
                        std::thread::sleep(Duration::from_millis(5));
                    }
                    if self.last_seq.is_none_or(|l| l < target) {
                        self.log.note(format!("events up to {target} did not all arrive; assuming unsaved changes"));
                        self.project.lock().unwrap().changed_since_save = true;
                    }
                }
                _ => self.take_engine_events(),
            }
        } else {
            self.take_engine_events();
        }
        match c.call("project.get_state", json!({}), Duration::from_secs(2)) {
            Ok(v) => {
                self.project.lock().unwrap().read(&v);
                true
            }
            Err(e) => {
                self.log.note(format!("project.get_state at exit failed: {e}"));
                false
            }
        }
    }

    /// Carry out the user's choice. Errors leave the engine running and the session usable.
    pub fn exit(&mut self, choice: ExitChoice) -> Result<ExitOutcome, LinkError> {
        self.log.note(format!("exit requested: {choice:?}"));
        if choice == ExitChoice::Cancel {
            return Ok(ExitOutcome::Cancelled);
        }
        if !self.is_supervised() {
            if let ExitChoice::SaveAs(p) = &choice {
                self.call_ready("project.save", json!({"path": p}), Duration::from_secs(60))?;
            } else if choice == ExitChoice::Save {
                self.call_ready("project.save", json!({}), Duration::from_secs(60))?;
            }
            if let Some(c) = self.conn.take() {
                c.close();
            }
            self.go_down(Status::Stopped);
            return Ok(ExitOutcome::Detached);
        }
        if !self.process_running() {
            self.go_down(Status::Stopped);
            return Ok(ExitOutcome::Stopped { exit: self.process.as_ref().and_then(|p| p.exit_known()) });
        }
        match choice {
            ExitChoice::Save => self.quit(true, false),
            ExitChoice::SaveAs(path) => {
                self.call_ready("project.save", json!({"path": path}), Duration::from_secs(60))?;
                self.quit(true, false)
            }
            ExitChoice::Discard => self.quit(false, true),
            ExitChoice::Cancel => unreachable!(),
        }
    }

    /// Stop our engine only if it has nothing unsaved (rule 1); otherwise `UnsavedChanges`.
    pub fn stop_if_clean(&mut self) -> Result<ExitOutcome, LinkError> {
        match self.exit_check() {
            ExitCheck::NoEngine => Ok(ExitOutcome::Stopped { exit: self.process.as_ref().and_then(|p| p.exit_known()) }),
            ExitCheck::Detach => self.exit(ExitChoice::Discard),
            ExitCheck::Clean => self.quit(false, false),
            ExitCheck::Unsaved { .. } => Err(LinkError::UnsavedChanges("The project has unsaved changes; the engine was left running.".into())),
        }
    }

    /// The one place `control.quit` is sent.
    fn quit(&mut self, save: bool, discard_confirmed: bool) -> Result<ExitOutcome, LinkError> {
        if !save && !discard_confirmed && !(self.refresh_project() && !self.project().modified()) {
            return Err(LinkError::UnsavedChanges("Closing the engine now would discard unsaved changes; choose Save or Discard.".into()));
        }
        let mut delivered = true;
        match self.call_ready("control.quit", json!({"save": save}), Duration::from_secs(10)) {
            Ok(_) => {}
            Err(LinkError::Remote { kind, message, .. }) if save && kind == "refused" => {
                let file = self.project().file;
                let why = if file.is_none() {
                    "This project has never been saved, so there is no file to save it to. Choose Save As, or Discard to close without saving.".to_string()
                } else {
                    format!("The engine refused to save: {message}")
                };
                self.notices.push(why.clone());
                return Err(LinkError::UnsavedChanges(why));
            }
            Err(e) if discard_confirmed => {
                self.log.note(format!("control.quit failed ({e}); stopping the process instead"));
                delivered = false;
            }
            Err(e) => return Err(e),
        }
        self.stopping = true;
        let timeout = self.opts.stop_timeout;
        let p = self.process.as_mut().expect("running");
        let mut exit = p.wait_exit(if delivered { timeout } else { Duration::ZERO });
        if exit.is_none() {
            if save {
                // It may still be writing the file: never kill it here.
                let pid = p.pid;
                self.log.note(format!("engine {pid} did not end within {} s after quit with save; left running", timeout.as_secs()));
                return Ok(ExitOutcome::LeftRunning { pid });
            }
            p.terminate();
            exit = p.wait_exit(Duration::from_secs(3));
            if exit.is_none() {
                p.kill();
                exit = p.wait_exit(Duration::from_secs(3));
            }
        }
        if let Some(c) = self.conn.take() {
            c.close();
        }
        self.log.note(format!("engine stopped: {}", exit.map(|e| e.describe()).unwrap_or_else(|| "unknown".into())));
        self.go_down(Status::Stopped);
        if let Mode::Supervised { runtime, .. } = &self.mode {
            if self.opts.log_dir.as_deref() != Some(runtime.path.as_path()) {
                runtime.remove();
            } else {
                let _ = std::fs::remove_file(runtime.socket_path());
            }
        }
        Ok(ExitOutcome::Stopped { exit })
    }

    fn call_ready(&mut self, cmd: &str, args: Value, timeout: Duration) -> Result<Value, LinkError> {
        let c = self.conn.clone().filter(|c| c.is_open()).ok_or_else(|| LinkError::Disconnected(format!("{cmd}: the engine is not connected")))?;
        c.call(cmd, args, timeout)
    }

    // ---- helpers --------------------------------------------------------------------------------

    fn conn_opts(&self) -> ConnOptions {
        ConnOptions { log: self.log.clone(), ..self.opts.conn.clone() }
    }

    fn process_exit(&mut self) -> Option<ExitInfo> {
        self.process.as_mut().and_then(|p| p.try_exit_new())
    }

    fn process_running(&mut self) -> bool {
        self.process.as_mut().is_some_and(|p| p.try_exit().is_none())
    }

    fn process_alive_or_attached(&mut self) -> bool {
        !self.is_supervised() || self.process_running()
    }

    fn stop_process_after_failed_start(&mut self) {
        // A process that never became ready holds no user data (it opened the file it was given).
        if let Some(p) = self.process.as_mut() {
            p.terminate();
            if p.wait_exit(Duration::from_secs(3)).is_none() {
                p.kill();
                p.wait_exit(Duration::from_secs(3));
            }
            p.mark_seen();
        }
    }

    fn log_hint(&self) -> String {
        match &self.process {
            Some(p) => {
                let tail = p.log_tail(600);
                let last = tail.lines().rev().find(|l| !l.trim().is_empty()).unwrap_or("").trim().to_string();
                if last.is_empty() {
                    format!("The engine's output is in {}.", p.log_path.display())
                } else {
                    format!("Its last output line: \"{last}\". Full output: {}.", p.log_path.display())
                }
            }
            None => String::new(),
        }
    }

    fn fail_start(&mut self, reason: String) {
        self.go_down(Status::StartFailed { reason });
    }

    fn go_down(&mut self, status: Status) {
        self.phase = Phase::Down;
        self.set_status(status);
    }

    fn set_status(&mut self, s: Status) {
        if s != self.status {
            self.log.note(format!("status: {} - {}", s.label(), s.sentence()));
            self.history.push((self.born.elapsed().as_secs_f64(), s.clone()));
            self.status = s;
        }
    }
}

/// The sentence shown after an unexpected engine exit: what was kept and what was lost.
fn lost_sentence(exit: &ExitInfo, info: &ProjectInfo, restarting: bool) -> String {
    let what = exit.describe();
    let reopen = match (&info.file, restarting) {
        (Some(f), true) => format!("Zene restarted it and reopened {}, the last saved version.", f.display()),
        (Some(f), false) => format!("Its last saved version is {}.", f.display()),
        (None, true) => "Zene restarted it with a new, empty project because this project was never saved.".to_string(),
        (None, false) => "This project was never saved.".to_string(),
    };
    let lost = match (info.modified(), info.file.is_some()) {
        (true, true) => "Changes made after that save were lost: the engine keeps no autosave.",
        (true, false) => "Its contents were lost: the engine keeps no autosave.",
        (false, true) => "Zene had seen no unsaved changes, so nothing should be lost.",
        (false, false) => "Zene had seen no changes in it.",
    };
    format!("The engine stopped unexpectedly ({what}). {reopen} {lost}")
}

fn unreachable_reason(sock: &Path, e: &LinkError) -> String {
    let p = sock.display();
    match e {
        LinkError::NotFound(_) => format!("No engine is running at {p} (there is no socket file)."),
        LinkError::Refused(_) => format!("Nothing answers at {p}: the engine that made it has stopped (connection refused)."),
        LinkError::Unsupported(_) => format!("{e}."),
        e => format!("Could not reach the engine at {p}: {e}."),
    }
}

impl Drop for Session {
    fn drop(&mut self) {
        if !self.is_supervised() {
            if let Some(c) = self.conn.take() {
                c.close();
            }
            return;
        }
        if !self.process_running() {
            if let Some(c) = self.conn.take() {
                c.close();
            }
            return;
        }
        match self.stop_if_clean() {
            Ok(_) => {}
            Err(e) => {
                let pid = self.pid().unwrap_or(0);
                let sock = self.socket_path();
                let msg = format!(
                    "zene-link: the engine (process {pid}) was left running so nothing is lost ({e}). Reach it with: zene-rune --engine {}",
                    sock.display()
                );
                self.log.note(msg.clone());
                eprintln!("{msg}");
                if let Some(c) = self.conn.take() {
                    c.close();
                }
            }
        }
    }
}
