//! Where a supervised engine lives: a private runtime directory with a short socket path, the
//! stale-socket rule, and the silent throwaway world tests (and `--engine-sandbox`) run it in.
//!
//! Stale-socket rule ([`prepare_socket`]): a path that does not exist is free; a socket nobody
//! accepts on (connection refused: its engine died without unlinking it) is removed; a socket a live
//! engine answers on is NEVER removed ([`LinkError::InUse`]); anything else at the path (a regular
//! file, a directory, a link) is NEVER touched ([`LinkError::NotASocket`]).

use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

use crate::error::LinkError;
use crate::transport::{MAX_SOCKET_PATH, Transport};

pub const SOCKET_NAME: &str = "engine.sock";

pub struct RuntimeDir {
    pub path: PathBuf,
}

static SEQ: AtomicU32 = AtomicU32::new(0);

impl RuntimeDir {
    /// A fresh private directory for one engine: `$XDG_RUNTIME_DIR/zene-rune/<pid>-<n>` when that
    /// keeps the socket path short enough, else `<temp>/zene-rune-<uid-ish>/<pid>-<n>`.
    pub fn create() -> Result<RuntimeDir, LinkError> {
        let leaf = format!("{}-{}", std::process::id(), SEQ.fetch_add(1, Ordering::Relaxed));
        let mut bases: Vec<PathBuf> = Vec::new();
        if let Some(x) = std::env::var_os("XDG_RUNTIME_DIR").filter(|x| !x.is_empty()) {
            bases.push(PathBuf::from(x).join("zene-rune"));
        }
        #[cfg(unix)]
        bases.push(PathBuf::from("/tmp").join(format!("zene-rune-{}", user_tag())));
        bases.push(std::env::temp_dir().join("zene-rune"));
        for base in bases {
            let dir = base.join(&leaf);
            if dir.join(SOCKET_NAME).as_os_str().len() > MAX_SOCKET_PATH {
                continue;
            }
            if create_private(&base).is_ok() && create_private(&dir).is_ok() {
                return Ok(RuntimeDir { path: dir });
            }
        }
        Err(LinkError::Io("no runtime directory with a short enough socket path could be created (tried $XDG_RUNTIME_DIR, /tmp and the system temp directory)".into()))
    }

    /// Use (and create) a given directory.
    pub fn at(path: &Path) -> Result<RuntimeDir, LinkError> {
        create_private(path)?;
        Ok(RuntimeDir { path: path.to_path_buf() })
    }

    pub fn socket_path(&self) -> PathBuf {
        self.path.join(SOCKET_NAME)
    }

    /// Remove the directory and what is in it.
    pub fn remove(&self) {
        let _ = std::fs::remove_dir_all(&self.path);
    }
}

#[cfg(unix)]
fn user_tag() -> String {
    std::env::var("USER").or_else(|_| std::env::var("LOGNAME")).unwrap_or_else(|_| "user".into())
}

fn create_private(dir: &Path) -> io::Result<()> {
    std::fs::create_dir_all(dir)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(dir, std::fs::Permissions::from_mode(0o700))?;
    }
    Ok(())
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Prepared {
    /// Nothing was at the path.
    Free,
    /// A dead engine's socket was there and was removed.
    RemovedStale,
}

/// Make `path` usable for a new engine's socket, by the rule in the module comment.
pub fn prepare_socket(path: &Path, transport: &dyn Transport) -> Result<Prepared, LinkError> {
    let shown = path.display().to_string();
    if path.as_os_str().len() > MAX_SOCKET_PATH {
        return Err(LinkError::PathTooLong(shown));
    }
    let meta = match std::fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(Prepared::Free),
        Err(e) => return Err(LinkError::Io(format!("{shown}: {e}"))),
    };
    if !is_socket(&meta) {
        return Err(LinkError::NotASocket(shown));
    }
    match transport.probe(path) {
        Ok(()) => Err(LinkError::InUse(shown)),
        Err(LinkError::Refused(_)) => {
            std::fs::remove_file(path).map_err(|e| LinkError::Io(format!("removing the stale socket {shown}: {e}")))?;
            Ok(Prepared::RemovedStale)
        }
        // Anything uncertain (permissions, an odd error): leave the file alone.
        Err(e) => Err(e),
    }
}

#[cfg(unix)]
fn is_socket(meta: &std::fs::Metadata) -> bool {
    use std::os::unix::fs::FileTypeExt;
    meta.file_type().is_socket()
}

#[cfg(not(unix))]
fn is_socket(_meta: &std::fs::Metadata) -> bool {
    false
}

/// A throwaway world for an engine that must not touch the user's settings or play sound: its own
/// HOME and XDG directories and a `--config` naming the Dummy audio device, the same recipe as the
/// product's control-socket harness (`tests/control_socket_harness.py`, `Instance.env`/`config_xml`).
pub struct Sandbox {
    pub root: PathBuf,
    pub config: PathBuf,
    pub workspace: PathBuf,
    pub env: Vec<(String, String)>,
}

/// The audio device name the engine's Dummy driver reports (`AudioDummy::name()`), byte for byte.
pub const DUMMY_DEVICE: &str = "Dummy (no sound output)";

impl Sandbox {
    pub fn create(root: &Path) -> io::Result<Sandbox> {
        let (home, cfg_home, data_home, workspace) = (root.join("home"), root.join("home/config"), root.join("home/data"), root.join("workspace"));
        for d in [&home, &cfg_home, &data_home, &workspace] {
            std::fs::create_dir_all(d)?;
        }
        let config = root.join("zenerc.xml");
        std::fs::write(&config, config_xml(&workspace))?;
        let s = |p: &Path| p.to_string_lossy().into_owned();
        let env = vec![
            ("HOME".to_string(), s(&home)),
            ("XDG_CONFIG_HOME".to_string(), s(&cfg_home)),
            ("XDG_DATA_HOME".to_string(), s(&data_home)),
            ("QT_QPA_PLATFORM".to_string(), "offscreen".to_string()),
            ("LMMS_STEM_OFFLINE".to_string(), "1".to_string()),
        ];
        Ok(Sandbox { root: root.to_path_buf(), config, workspace, env })
    }
}

fn config_xml(workspace: &Path) -> String {
    let ws: String = workspace.to_string_lossy().chars().map(|c| match c {
        '&' => "&amp;".to_string(),
        '<' => "&lt;".to_string(),
        '>' => "&gt;".to_string(),
        '"' => "&quot;".to_string(),
        '\'' => "&apos;".to_string(),
        c => c.to_string(),
    }).collect();
    format!(
        "<?xml version=\"1.0\"?>\n<!DOCTYPE lmms-config-file>\n<lmmsconfig version=\"0.2.0-alpha\" configversion=\"3\">\n  <app configured=\"1\"/>\n  <audioengine audiodev=\"{DUMMY_DEVICE}\"/>\n  <paths workingdir=\"{ws}\"/>\n</lmmsconfig>\n"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_sandbox_config_names_the_dummy_device_and_escapes_the_path() {
        let root = std::env::temp_dir().join(format!("zl-sb-{}-a&b", std::process::id()));
        let sb = Sandbox::create(&root).unwrap();
        let xml = std::fs::read_to_string(&sb.config).unwrap();
        assert!(xml.contains("audiodev=\"Dummy (no sound output)\""));
        assert!(xml.contains("a&amp;b") && !xml.contains("a&b"));
        assert!(sb.env.iter().any(|(k, v)| k == "HOME" && v.starts_with(&*root.to_string_lossy())));
        let _ = std::fs::remove_dir_all(&root);
    }

    #[test]
    fn runtime_dirs_are_fresh_and_their_socket_paths_fit() {
        let a = RuntimeDir::create().unwrap();
        let b = RuntimeDir::create().unwrap();
        assert_ne!(a.path, b.path);
        assert!(a.socket_path().as_os_str().len() <= MAX_SOCKET_PATH);
        a.remove();
        b.remove();
        assert!(!a.path.exists());
    }
}
