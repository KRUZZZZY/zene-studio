//! One error type for the link. Each variant's `Display` is a plain sentence a status line can show.

use std::fmt;

#[derive(Clone, Debug, PartialEq)]
pub enum LinkError {
    /// This platform has no transport yet (Windows: the named pipe is Q050-524).
    Unsupported(String),
    /// Nothing listens at the socket path (the file is missing).
    NotFound(String),
    /// A socket file exists but no process accepts on it (a stale socket, or the engine is gone).
    Refused(String),
    /// Some other I/O failure, with the operating system's words.
    Io(String),
    /// The connection closed or broke; the string says how.
    Disconnected(String),
    /// No reply within the time allowed.
    Timeout { cmd: String, ms: u64 },
    /// The engine answered `ok:false`.
    Remote { cmd: String, kind: String, message: String },
    /// A line from the engine that is not the protocol (bad JSON, no id, wrong shape).
    Protocol(String),
    /// Too many requests are waiting for replies; the caller must slow down.
    Backpressure(usize),
    /// The socket path is taken by something that is not a socket. It was left alone.
    NotASocket(String),
    /// A live engine already answers at the socket path. It was left alone.
    InUse(String),
    /// The socket path is too long for a unix socket address.
    PathTooLong(String),
    /// The engine process could not be started.
    Spawn(String),
    /// Quitting would discard unsaved changes and the caller did not choose to discard them.
    UnsavedChanges(String),
}

impl LinkError {
    /// The engine's typed error kind, for `Remote`.
    pub fn remote_kind(&self) -> Option<&str> {
        match self {
            LinkError::Remote { kind, .. } => Some(kind),
            _ => None,
        }
    }
    pub fn is_disconnect(&self) -> bool {
        matches!(self, LinkError::Disconnected(_) | LinkError::Refused(_) | LinkError::NotFound(_))
    }
}

impl fmt::Display for LinkError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            LinkError::Unsupported(s) => write!(f, "not supported on this platform yet: {s}"),
            LinkError::NotFound(p) => write!(f, "no engine socket at {p}"),
            LinkError::Refused(p) => write!(f, "nothing is accepting connections at {p}"),
            LinkError::Io(s) => write!(f, "{s}"),
            LinkError::Disconnected(s) => write!(f, "the connection to the engine closed ({s})"),
            LinkError::Timeout { cmd, ms } => write!(f, "{cmd}: no reply from the engine within {ms} ms"),
            LinkError::Remote { cmd, kind, message } => write!(f, "{cmd}: {message} ({kind})"),
            LinkError::Protocol(s) => write!(f, "the engine sent something that is not the control protocol: {s}"),
            LinkError::Backpressure(n) => write!(f, "{n} requests are already waiting for the engine"),
            LinkError::NotASocket(p) => write!(f, "{p} exists and is not a socket"),
            LinkError::InUse(p) => write!(f, "another engine is already running at {p}"),
            LinkError::PathTooLong(p) => write!(f, "the socket path is too long for a unix socket: {p}"),
            LinkError::Spawn(s) => write!(f, "the engine could not be started: {s}"),
            LinkError::UnsavedChanges(s) => write!(f, "{s}"),
        }
    }
}

impl std::error::Error for LinkError {}

impl From<std::io::Error> for LinkError {
    fn from(e: std::io::Error) -> Self {
        LinkError::Io(e.to_string())
    }
}
