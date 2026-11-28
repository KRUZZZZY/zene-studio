//! The engine process: how it is started (`zene --engine --control-socket <path> [project]`), how
//! its end is read (exit code or signal), and how it is stopped when asked to.

use std::ffi::OsString;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

use crate::error::LinkError;

/// How to start an engine.
#[derive(Clone, Debug, Default)]
pub struct EngineSpec {
    pub bin: PathBuf,
    /// A project file to open (`zene --engine ... <project>`).
    pub project: Option<PathBuf>,
    /// `--config <xml>`; `None` lets the engine use the user's own settings.
    pub config: Option<PathBuf>,
    /// Extra environment (a sandbox's HOME and XDG directories, a library path).
    pub env: Vec<(String, String)>,
    pub cwd: Option<PathBuf>,
    /// More arguments, placed before the project.
    pub extra_args: Vec<String>,
}

impl EngineSpec {
    pub fn new(bin: impl Into<PathBuf>) -> EngineSpec {
        EngineSpec { bin: bin.into(), ..Default::default() }
    }

    /// The argument list after the program name.
    pub fn argv(&self, socket: &Path) -> Vec<OsString> {
        let mut a: Vec<OsString> = Vec::new();
        if let Some(c) = &self.config {
            a.push("--config".into());
            a.push(c.into());
        }
        a.push("--control-socket".into());
        a.push(socket.into());
        a.push("--engine".into());
        a.extend(self.extra_args.iter().map(OsString::from));
        if let Some(p) = &self.project {
            a.push(p.into());
        }
        a
    }
}

/// How a process ended.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ExitInfo {
    pub code: Option<i32>,
    pub signal: Option<i32>,
}

impl ExitInfo {
    pub fn from_status(s: std::process::ExitStatus) -> ExitInfo {
        #[cfg(unix)]
        {
            use std::os::unix::process::ExitStatusExt;
            ExitInfo { code: s.code(), signal: s.signal() }
        }
        #[cfg(not(unix))]
        {
            ExitInfo { code: s.code(), signal: None }
        }
    }

    pub fn is_clean(&self) -> bool {
        self.code == Some(0)
    }

    pub fn describe(&self) -> String {
        match (self.code, self.signal) {
            (_, Some(sig)) => format!("killed by signal {sig}{}", signal_name(sig).map(|n| format!(", {n}")).unwrap_or_default()),
            (Some(0), None) => "exit code 0".into(),
            (Some(c), None) => format!("exit code {c}"),
            (None, None) => "ended, no exit code".into(),
        }
    }
}

fn signal_name(sig: i32) -> Option<&'static str> {
    Some(match sig {
        1 => "SIGHUP",
        2 => "SIGINT",
        3 => "SIGQUIT",
        6 => "SIGABRT",
        9 => "SIGKILL",
        11 => "SIGSEGV",
        13 => "SIGPIPE",
        15 => "SIGTERM",
        _ => return None,
    })
}

#[cfg(unix)]
mod sys {
    unsafe extern "C" {
        fn kill(pid: i32, sig: i32) -> i32;
    }
    pub const SIGTERM: i32 = 15;
    pub const SIGKILL: i32 = 9;
    /// Send a signal to a process this link started. 0 on success.
    pub fn signal(pid: u32, sig: i32) -> i32 {
        // SAFETY: kill(2) takes plain integers; the pid is one of our own children.
        unsafe { kill(pid as i32, sig) }
    }
}

pub struct EngineProcess {
    child: Child,
    pub pid: u32,
    exit: Option<ExitInfo>,
    /// The exit has been reported once (`try_exit_new`).
    seen: bool,
    /// The engine's stdout and stderr, together.
    pub log_path: PathBuf,
}

impl EngineProcess {
    pub fn spawn(spec: &EngineSpec, socket: &Path, log_path: &Path) -> Result<EngineProcess, LinkError> {
        let bin = &spec.bin;
        match std::fs::metadata(bin) {
            Ok(m) if m.is_file() => {}
            Ok(_) => return Err(LinkError::Spawn(format!("{} is not a file. Check the engine path (--engine-bin or ZENE_ENGINE_BIN).", bin.display()))),
            Err(e) => return Err(LinkError::Spawn(format!("{}: {e}. Check the engine path (--engine-bin or ZENE_ENGINE_BIN).", bin.display()))),
        }
        let out = File::create(log_path).map_err(|e| LinkError::Spawn(format!("cannot create the engine log {}: {e}", log_path.display())))?;
        let err = out.try_clone().map_err(|e| LinkError::Spawn(e.to_string()))?;
        let mut cmd = Command::new(bin);
        cmd.args(spec.argv(socket)).envs(spec.env.iter().map(|(k, v)| (k, v))).stdin(Stdio::null()).stdout(out).stderr(err);
        if let Some(d) = &spec.cwd {
            cmd.current_dir(d);
        }
        let child = cmd.spawn().map_err(|e| LinkError::Spawn(format!("{}: {e}", bin.display())))?;
        let pid = child.id();
        Ok(EngineProcess { child, pid, exit: None, seen: false, log_path: log_path.to_path_buf() })
    }

    /// How the process ended, if it has (never blocks).
    pub fn try_exit(&mut self) -> Option<ExitInfo> {
        if self.exit.is_none() {
            if let Ok(Some(s)) = self.child.try_wait() {
                self.exit = Some(ExitInfo::from_status(s));
            }
        }
        self.exit
    }

    /// The exit, the first time it is seen (later calls answer `None`).
    pub fn try_exit_new(&mut self) -> Option<ExitInfo> {
        let e = self.try_exit()?;
        if self.seen {
            return None;
        }
        self.seen = true;
        Some(e)
    }

    /// Treat the exit as reported (the caller knows why it ended).
    pub fn mark_seen(&mut self) {
        self.seen = true;
    }

    /// The exit, if it has been observed.
    pub fn exit_known(&self) -> Option<ExitInfo> {
        self.exit
    }

    /// Wait up to `timeout` for the process to end.
    pub fn wait_exit(&mut self, timeout: Duration) -> Option<ExitInfo> {
        let t0 = Instant::now();
        loop {
            if let Some(e) = self.try_exit() {
                return Some(e);
            }
            if t0.elapsed() >= timeout {
                return None;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
    }

    /// Ask the process to end: SIGTERM, which serve mode treats as quit without saving.
    pub fn terminate(&mut self) {
        if self.try_exit().is_some() {
            return;
        }
        #[cfg(unix)]
        sys::signal(self.pid, sys::SIGTERM);
        #[cfg(not(unix))]
        let _ = self.child.kill();
    }

    /// End the process at once.
    pub fn kill(&mut self) {
        if self.try_exit().is_some() {
            return;
        }
        #[cfg(unix)]
        sys::signal(self.pid, sys::SIGKILL);
        #[cfg(not(unix))]
        let _ = self.child.kill();
    }

    /// The last `max` bytes of the engine's output.
    pub fn log_tail(&self, max: usize) -> String {
        let text = std::fs::read(&self.log_path).unwrap_or_default();
        let from = text.len().saturating_sub(max);
        String::from_utf8_lossy(&text[from..]).into_owned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn argv_puts_config_socket_engine_extra_then_project() {
        let spec = EngineSpec { project: Some("p.mmpz".into()), config: Some("c.xml".into()), extra_args: vec!["--x".into()], ..EngineSpec::new("zene") };
        let a: Vec<String> = spec.argv(Path::new("/tmp/s.sock")).iter().map(|s| s.to_string_lossy().into_owned()).collect();
        assert_eq!(a, ["--config", "c.xml", "--control-socket", "/tmp/s.sock", "--engine", "--x", "p.mmpz"]);
    }

    #[test]
    fn exit_descriptions_name_the_signal() {
        assert_eq!(ExitInfo { code: None, signal: Some(9) }.describe(), "killed by signal 9, SIGKILL");
        assert_eq!(ExitInfo { code: Some(3), signal: None }.describe(), "exit code 3");
        assert!(ExitInfo { code: Some(0), signal: None }.is_clean());
    }

    #[test]
    fn a_missing_binary_is_a_spawn_error_with_an_action() {
        let dir = std::env::temp_dir();
        let e = EngineProcess::spawn(&EngineSpec::new(dir.join("no-such-zene")), &dir.join("s.sock"), &dir.join("zl-missing.log")).err().unwrap();
        assert!(matches!(e, LinkError::Spawn(_)) && e.to_string().contains("Check the engine path"), "{e}");
    }
}
