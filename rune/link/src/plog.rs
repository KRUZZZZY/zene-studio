//! The protocol log: every request, reply, event and lifecycle note, for evidence and diagnosis.
//! A bounded ring buffer in memory (the newest `capacity` entries), optionally mirrored line by line
//! to a file. Cheap to clone (one shared buffer).

use std::collections::VecDeque;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::sync::{Arc, Mutex};
use std::time::{Instant, SystemTime, UNIX_EPOCH};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Dir {
    /// Sent to the engine.
    Out,
    /// Received from the engine.
    In,
    /// A note from the link itself (connect, status change, spawn, exit).
    Note,
}

#[derive(Clone, Debug)]
pub struct Entry {
    /// Seconds since the log was created.
    pub t: f64,
    pub dir: Dir,
    pub text: String,
}

struct Inner {
    start: Instant,
    capacity: usize,
    entries: VecDeque<Entry>,
    dropped: u64,
    file: Option<File>,
}

#[derive(Clone)]
pub struct ProtocolLog(Arc<Mutex<Inner>>);

impl Default for ProtocolLog {
    fn default() -> Self {
        ProtocolLog::new(4096)
    }
}

/// Lines longer than this are cut in the log (a whole `control.commands_list` reply is ~1 MB).
const MAX_TEXT: usize = 4000;

impl ProtocolLog {
    pub fn new(capacity: usize) -> ProtocolLog {
        ProtocolLog(Arc::new(Mutex::new(Inner { start: Instant::now(), capacity: capacity.max(1), entries: VecDeque::new(), dropped: 0, file: None })))
    }

    /// Mirror every entry from now on to `path` (appended). The file's first line says when.
    pub fn mirror_to(&self, path: &Path) -> std::io::Result<()> {
        let mut f = OpenOptions::new().create(true).append(true).open(path)?;
        let wall = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs_f64()).unwrap_or(0.0);
        writeln!(f, "# zene-link protocol log, unix time {wall:.3}; columns: seconds, direction (> sent, < received, # link), text")?;
        self.0.lock().unwrap().file = Some(f);
        Ok(())
    }

    pub fn push(&self, dir: Dir, text: impl Into<String>) {
        let mut text = text.into();
        if text.len() > MAX_TEXT {
            let mut cut = MAX_TEXT;
            while !text.is_char_boundary(cut) {
                cut -= 1;
            }
            text.truncate(cut);
            text.push_str(" ...(cut)");
        }
        let mut g = self.0.lock().unwrap();
        let t = g.start.elapsed().as_secs_f64();
        if let Some(f) = g.file.as_mut() {
            let mark = match dir {
                Dir::Out => '>',
                Dir::In => '<',
                Dir::Note => '#',
            };
            let _ = writeln!(f, "{t:.4} {mark} {text}");
        }
        if g.entries.len() == g.capacity {
            g.entries.pop_front();
            g.dropped += 1;
        }
        g.entries.push_back(Entry { t, dir, text });
    }

    pub fn note(&self, text: impl Into<String>) {
        self.push(Dir::Note, text);
    }

    pub fn entries(&self) -> Vec<Entry> {
        self.0.lock().unwrap().entries.iter().cloned().collect()
    }

    /// Entries pushed out of the ring buffer so far (the file, if any, still has them).
    pub fn dropped(&self) -> u64 {
        self.0.lock().unwrap().dropped
    }

    pub fn contains(&self, needle: &str) -> bool {
        self.0.lock().unwrap().entries.iter().any(|e| e.text.contains(needle))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_ring_keeps_the_newest_entries_and_counts_the_rest() {
        let log = ProtocolLog::new(3);
        for i in 0..5 {
            log.push(Dir::Out, format!("line {i}"));
        }
        let e = log.entries();
        assert_eq!(e.iter().map(|e| e.text.as_str()).collect::<Vec<_>>(), ["line 2", "line 3", "line 4"]);
        assert_eq!(log.dropped(), 2);
    }

    #[test]
    fn long_lines_are_cut_and_the_file_mirror_gets_every_line() {
        let dir = std::env::temp_dir().join(format!("zl-plog-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("p.log");
        let log = ProtocolLog::new(2);
        log.mirror_to(&path).unwrap();
        log.push(Dir::In, "é".repeat(5000));
        for i in 0..4 {
            log.note(format!("n{i}"));
        }
        assert!(log.entries().iter().all(|e| e.text.len() < MAX_TEXT + 20));
        let text = std::fs::read_to_string(&path).unwrap();
        assert_eq!(text.lines().count(), 6, "header + 5 entries");
        assert!(text.contains(" # n0") && text.contains("...(cut)"));
        let _ = std::fs::remove_dir_all(&dir);
    }
}
