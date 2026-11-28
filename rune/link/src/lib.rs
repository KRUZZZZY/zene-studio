//! The Zene Studio engine link: how a front end (the Rune UI, a headless tool, a test) reaches the
//! engine process. No GUI dependency; std plus `serde_json`.
//!
//! Layers, bottom up:
//!
//! - [`transport`]: a byte stream to the engine's control endpoint. Unix-domain sockets on Linux
//!   and macOS. Windows (a named pipe, `ControlServerWin32.cpp`) is Q050-524's job: there the
//!   transport answers [`LinkError::Unsupported`] instead of failing to compile.
//! - [`proto`]: the JSON-lines wire format (`{"id","cmd","args","proto":1}` out, one reply line or
//!   an event line back).
//! - [`conn`]: one connection. Requests go out without the caller waiting; a reader thread matches
//!   replies to requests by id (in any order) and puts them in a bounded completion queue the UI
//!   drains once per frame, or hands them to a callback. A blocking `call` exists for start-up and
//!   tests. A keep-alive ping beats the engine's 30 s idle close. Every line in either direction is
//!   kept in a [`plog::ProtocolLog`] (a ring buffer, optionally mirrored to a file).
//! - [`runtime`] and [`process`]: an isolated runtime directory with a short socket path, stale-socket
//!   cleanup that never removes a live engine's socket or a file that is not a socket, spawning
//!   `zene --engine`, and the engine's exit (code or signal).
//! - [`session`]: the lifecycle state machine on top: start or attach, readiness through
//!   `control.ping`, reconnect after a dropped connection, restart after an unexpected exit, and the
//!   data-safety rules for quitting (see `session.rs`). Its [`status::Status`] is what the UI shows;
//!   every failure carries a sentence saying what happened and what the user can do.
//!
//! [`fake`] is a small fake engine (in-process server or the `zene-fake-engine` binary) for the
//! deterministic tests; nothing in the UI starts it.

pub mod conn;
pub mod error;
pub mod fake;
pub mod plog;
pub mod process;
pub mod proto;
pub mod runtime;
pub mod session;
pub mod status;
pub mod transport;

pub use conn::{Completion, ConnOptions, Connection};
pub use error::LinkError;
pub use plog::ProtocolLog;
pub use process::{EngineSpec, ExitInfo};
pub use session::{ExitCheck, ExitChoice, ExitOutcome, ProjectInfo, Session, SessionOptions};
pub use status::Status;
