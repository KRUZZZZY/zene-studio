//! What the UI shows about the engine link: a short label for the status bar and a sentence that
//! says what happened and what the user can do. Every failure state carries its cause.

use crate::process::ExitInfo;

#[derive(Clone, Debug, PartialEq)]
pub enum Status {
    /// No engine (demo mode).
    Idle,
    /// The engine process was started; waiting for its control socket.
    Starting { pid: u32 },
    /// Connected; waiting for `control.ping` to report `engine_ready` (the reason it gives, if any).
    Connecting { reason: String },
    Ready { pid: Option<u32>, version: String },
    /// The connection dropped while the engine process is (as far as we know) still running.
    Reconnecting { attempt: u32, max: u32 },
    /// No connection and no more automatic attempts.
    Disconnected { reason: String },
    /// The engine process we started has ended without being asked to.
    EngineExited { exit: ExitInfo, restarting: bool, log: String },
    /// The engine could not be started, or is not an engine this link can drive.
    StartFailed { reason: String },
    /// The engine was closed on purpose.
    Stopped,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Severity {
    Ok,
    Busy,
    Problem,
}

impl Status {
    pub fn is_ready(&self) -> bool {
        matches!(self, Status::Ready { .. })
    }

    pub fn severity(&self) -> Severity {
        match self {
            Status::Idle | Status::Ready { .. } | Status::Stopped => Severity::Ok,
            Status::Starting { .. } | Status::Connecting { .. } | Status::Reconnecting { .. } => Severity::Busy,
            Status::EngineExited { restarting: true, .. } => Severity::Busy,
            _ => Severity::Problem,
        }
    }

    /// A few words for the status bar.
    pub fn label(&self) -> String {
        match self {
            Status::Idle => "No engine (demo)".into(),
            Status::Starting { .. } => "Starting engine".into(),
            Status::Connecting { .. } => "Engine starting up".into(),
            Status::Ready { .. } => "Engine ready".into(),
            Status::Reconnecting { attempt, max } => format!("Reconnecting ({attempt}/{max})"),
            Status::Disconnected { .. } => "Engine disconnected".into(),
            Status::EngineExited { restarting: true, .. } => "Engine stopped, restarting".into(),
            Status::EngineExited { .. } => "Engine stopped".into(),
            Status::StartFailed { .. } => "Engine failed to start".into(),
            Status::Stopped => "Engine closed".into(),
        }
    }

    /// What happened and what the user can do about it.
    pub fn sentence(&self) -> String {
        match self {
            Status::Idle => "No engine is connected: this is the demo project with simulated playback, and no sound is produced.".into(),
            Status::Starting { pid } => format!("Starting the Zene engine (process {pid}). This normally takes under a second."),
            Status::Connecting { reason } if reason.is_empty() => "Connected to the engine; waiting for it to finish starting.".into(),
            Status::Connecting { reason } => format!("Connected to the engine; waiting for it to finish starting ({reason})."),
            Status::Ready { pid: Some(pid), version } => format!("Connected to the Zene engine {version} (process {pid}, no engine window). Edits go to the engine."),
            Status::Ready { pid: None, version } => format!("Connected to a running Zene engine {version}. It keeps running when this window closes."),
            Status::Reconnecting { attempt, max } => format!(
                "The connection to the engine dropped. Reconnecting, attempt {attempt} of {max}. The engine is still running and the project is still in it; edits wait until the connection is back."
            ),
            Status::Disconnected { reason } => format!("{reason} Start the engine again or check that it is running, then choose Engine > Reconnect."),
            Status::EngineExited { exit, restarting: true, .. } => format!("The engine stopped unexpectedly ({}). Restarting it now.", exit.describe()),
            Status::EngineExited { exit, restarting: false, log } => format!(
                "The engine stopped unexpectedly ({}) and was not restarted again. Its output is in {log}. Choose Engine > Restart to start it again.",
                exit.describe()
            ),
            Status::StartFailed { reason } => reason.clone(),
            Status::Stopped => "The engine was closed.".into(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn every_failure_says_what_to_do() {
        let failures = [
            Status::Disconnected { reason: "Lost the engine at /tmp/x.sock (connection refused).".into() },
            Status::EngineExited { exit: ExitInfo { code: None, signal: Some(9) }, restarting: false, log: "/tmp/engine.log".into() },
            Status::StartFailed { reason: "The engine binary /x does not exist. Check the --engine-bin path.".into() },
        ];
        for s in &failures {
            assert_eq!(s.severity(), Severity::Problem, "{s:?}");
            let text = s.sentence();
            assert!(text.contains("Engine >") || text.contains("Check"), "no action in: {text}");
        }
        let exited = &failures[1];
        assert!(exited.sentence().contains("signal 9"), "{}", exited.sentence());
    }

    #[test]
    fn ready_says_whether_the_engine_is_owned() {
        assert!(Status::Ready { pid: Some(7), version: "0.4".into() }.sentence().contains("process 7"));
        assert!(Status::Ready { pid: None, version: "0.4".into() }.sentence().contains("keeps running"));
        assert!(Status::Reconnecting { attempt: 2, max: 8 }.label().contains("2/8"));
    }
}
