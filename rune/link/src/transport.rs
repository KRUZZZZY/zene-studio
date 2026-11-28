//! The byte stream under a connection. A transport turns an endpoint (a socket path today) into a
//! [`Stream`]; the connection layer above never touches a platform type.
//!
//! - Linux and macOS: unix-domain sockets ([`UnixTransport`]).
//! - Windows: the engine listens on a named pipe (`src/core/ControlServerWin32.cpp`). That transport
//!   is Q050-524's; until it lands, [`default_transport`] answers [`LinkError::Unsupported`] with a
//!   sentence saying so, and everything else in this crate still compiles and runs its tests.

use std::io::{Read, Write};
use std::path::Path;
use std::time::Duration;

use crate::error::LinkError;

/// One duplex byte stream to the engine.
pub trait Stream: Read + Write + Send + Sync {
    /// A second handle on the same stream (the reader thread reads one, writers write the other).
    fn try_clone_box(&self) -> Result<Box<dyn Stream>, LinkError>;
    /// Close both directions; a blocked read on any handle returns.
    fn shutdown(&self);
    fn set_write_timeout(&self, t: Option<Duration>);
}

pub trait Transport: Send + Sync {
    fn connect(&self, endpoint: &Path) -> Result<Box<dyn Stream>, LinkError>;
    /// Whether a live engine accepts at `endpoint` right now (connects and closes at once).
    fn probe(&self, endpoint: &Path) -> Result<(), LinkError> {
        self.connect(endpoint).map(|s| s.shutdown())
    }
}

/// The transport for this platform.
pub fn default_transport() -> Box<dyn Transport> {
    #[cfg(unix)]
    {
        Box::new(UnixTransport)
    }
    #[cfg(not(unix))]
    {
        Box::new(Unsupported)
    }
}

/// The longest socket path the platform's `sockaddr_un` takes (Linux 108 bytes, the BSDs and
/// macOS 104), minus the terminating NUL.
pub const MAX_SOCKET_PATH: usize = if cfg!(target_os = "linux") { 107 } else { 103 };

#[cfg(unix)]
pub use unix::UnixTransport;

#[cfg(unix)]
mod unix {
    use std::io::ErrorKind;
    use std::net::Shutdown;
    use std::os::unix::net::UnixStream;

    use super::*;

    pub struct UnixTransport;

    impl Stream for UnixStream {
        fn try_clone_box(&self) -> Result<Box<dyn Stream>, LinkError> {
            Ok(Box::new(self.try_clone()?))
        }
        fn shutdown(&self) {
            let _ = UnixStream::shutdown(self, Shutdown::Both);
        }
        fn set_write_timeout(&self, t: Option<Duration>) {
            let _ = UnixStream::set_write_timeout(self, t);
        }
    }

    impl Transport for UnixTransport {
        fn connect(&self, endpoint: &Path) -> Result<Box<dyn Stream>, LinkError> {
            let shown = endpoint.display().to_string();
            if endpoint.as_os_str().len() > MAX_SOCKET_PATH {
                return Err(LinkError::PathTooLong(shown));
            }
            match UnixStream::connect(endpoint) {
                Ok(s) => Ok(Box::new(s)),
                Err(e) => Err(match e.kind() {
                    ErrorKind::NotFound => LinkError::NotFound(shown),
                    ErrorKind::ConnectionRefused => LinkError::Refused(shown),
                    _ => LinkError::Io(format!("{shown}: {e}")),
                }),
            }
        }
    }
}

/// Windows until Q050-524: an honest refusal, not a compile failure.
#[cfg(not(unix))]
pub struct Unsupported;

#[cfg(not(unix))]
impl Transport for Unsupported {
    fn connect(&self, endpoint: &Path) -> Result<Box<dyn Stream>, LinkError> {
        Err(LinkError::Unsupported(format!(
            "the engine link on Windows needs the named-pipe transport (Q050-524); cannot reach {}",
            endpoint.display()
        )))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(unix)]
    #[test]
    fn missing_and_overlong_paths_are_typed() {
        let t = default_transport();
        let dir = std::env::temp_dir().join(format!("zl-t-{}", std::process::id()));
        let missing = dir.join("nope.sock");
        assert!(matches!(t.connect(&missing).err(), Some(LinkError::NotFound(_))));
        let long = Path::new("/tmp").join("x".repeat(200));
        assert!(matches!(t.connect(&long).err(), Some(LinkError::PathTooLong(_))));
    }

    #[cfg(not(unix))]
    #[test]
    fn windows_says_not_supported_yet() {
        let e = default_transport().connect(Path::new(r"\\.\pipe\zene")).err().unwrap();
        assert!(matches!(e, LinkError::Unsupported(_)), "{e}");
        assert!(e.to_string().contains("Q050-524"));
    }
}
