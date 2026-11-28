//! One connection to the engine's control endpoint.
//!
//! - [`Connection::send`] writes the request and returns its id at once; the reply lands in a
//!   bounded completion queue that the UI drains once per frame ([`Connection::drain`]).
//!   [`Connection::send_with`] hands the reply to a callback instead (it runs on the reader thread).
//! - A reader thread reads every line, matches replies to requests by id in whatever order they
//!   come, and keeps event lines (no id) in a bounded queue of their own ([`Connection::drain_events`]).
//! - [`Connection::call`] waits for the reply: for start-up, shutdown and tests, never the UI frame.
//! - A keep-alive `control.ping` goes out when nothing was written for `keepalive`, because the engine
//!   closes a connection that has been idle for 30 s (`include/ControlServer.h`, `ClientIdleTimeoutMs`)
//!   unless it holds an `events.subscribe` subscription. Its reply is consumed here.
//! - Bounds: at most `max_in_flight` requests may be unanswered or answered-but-undrained; past that
//!   `send` answers [`LinkError::Backpressure`] (nothing is dropped). Events past `max_events` drop the
//!   oldest and are counted (events carry `seq`, so a consumer can see the gap and reread).
//! - When the stream ends or breaks, every waiting request completes with [`LinkError::Disconnected`]
//!   and [`Connection::is_open`] turns false. A connection is never reopened: the session makes a new one.
//!
//! A `Connection` is a cheap handle (`Clone`); [`Connection::close`] closes it for every handle.

use std::collections::{HashMap, VecDeque};
use std::io::{BufRead, BufReader, Write};
use std::path::Path;
use std::sync::mpsc;
use std::sync::{Arc, Condvar, Mutex};
use std::time::{Duration, Instant};

use serde_json::{Value, json};

use crate::error::LinkError;
use crate::plog::{Dir, ProtocolLog};
use crate::proto::{self, Incoming};
use crate::transport::{Stream, Transport};

pub type Callback = Box<dyn FnOnce(Result<Value, LinkError>) + Send>;

#[derive(Clone)]
pub struct ConnOptions {
    /// Ping when nothing was written for this long; `None` sends no keep-alive.
    pub keepalive: Option<Duration>,
    pub max_in_flight: usize,
    pub max_events: usize,
    /// A write that cannot finish in this time breaks the connection (the engine stopped reading).
    pub write_timeout: Duration,
    pub log: ProtocolLog,
}

impl Default for ConnOptions {
    fn default() -> Self {
        ConnOptions { keepalive: Some(Duration::from_secs(10)), max_in_flight: 1024, max_events: 4096, write_timeout: Duration::from_secs(5), log: ProtocolLog::default() }
    }
}

/// A finished request, as `drain` returns it.
#[derive(Debug)]
pub struct Completion {
    pub id: u64,
    pub cmd: String,
    pub result: Result<Value, LinkError>,
    pub elapsed: Duration,
}

enum Waiter {
    Queue,
    Callback(Callback),
    Sync(mpsc::Sender<Result<Value, LinkError>>),
    /// The keep-alive's own ping.
    Silent,
}

struct Pending {
    cmd: String,
    sent: Instant,
    waiter: Waiter,
}

struct State {
    next: u64,
    pending: HashMap<u64, Pending>,
    completions: VecDeque<Completion>,
    events: VecDeque<Value>,
    dropped_events: u64,
    closed: Option<String>,
    last_write: Instant,
    last_read: Instant,
}

struct Inner {
    writer: Mutex<Box<dyn Stream>>,
    /// A third handle, only for `shutdown` (the writer may be busy in a write).
    shutter: Box<dyn Stream>,
    state: Mutex<State>,
    wake: Condvar,
    opts: ConnOptions,
}

#[derive(Clone)]
pub struct Connection {
    inner: Arc<Inner>,
}

impl Connection {
    /// Connect through `transport` to `endpoint` and start the reader (and keep-alive) threads.
    pub fn open(transport: &dyn Transport, endpoint: &Path, opts: ConnOptions) -> Result<Connection, LinkError> {
        match transport.connect(endpoint) {
            Ok(s) => {
                opts.log.note(format!("connected to {}", endpoint.display()));
                Connection::from_stream(s, opts)
            }
            Err(e) => {
                opts.log.note(format!("connect {} failed: {e}", endpoint.display()));
                Err(e)
            }
        }
    }

    /// Run the protocol over an already connected stream.
    pub fn from_stream(stream: Box<dyn Stream>, opts: ConnOptions) -> Result<Connection, LinkError> {
        stream.set_write_timeout(Some(opts.write_timeout));
        let reader = stream.try_clone_box()?;
        let shutter = stream.try_clone_box()?;
        let now = Instant::now();
        let inner = Arc::new(Inner {
            writer: Mutex::new(stream),
            shutter,
            state: Mutex::new(State { next: 0, pending: HashMap::new(), completions: VecDeque::new(), events: VecDeque::new(), dropped_events: 0, closed: None, last_write: now, last_read: now }),
            wake: Condvar::new(),
            opts,
        });
        let r = inner.clone();
        std::thread::Builder::new().name("zene-link-reader".into()).spawn(move || read_loop(r, reader)).map_err(|e| LinkError::Io(e.to_string()))?;
        if let Some(every) = inner.opts.keepalive {
            let k = inner.clone();
            std::thread::Builder::new().name("zene-link-keepalive".into()).spawn(move || keepalive_loop(k, every)).map_err(|e| LinkError::Io(e.to_string()))?;
        }
        Ok(Connection { inner })
    }

    /// Send without waiting; the reply arrives in [`Connection::drain`].
    pub fn send(&self, cmd: &str, args: Value) -> Result<u64, LinkError> {
        self.submit(cmd, args, Waiter::Queue)
    }

    /// Send without waiting; `cb` gets the reply on the reader thread (keep it short).
    pub fn send_with(&self, cmd: &str, args: Value, cb: impl FnOnce(Result<Value, LinkError>) + Send + 'static) -> Result<u64, LinkError> {
        self.submit(cmd, args, Waiter::Callback(Box::new(cb)))
    }

    /// Send and wait up to `timeout` for the reply. Not for the UI frame: start-up, shutdown, tests.
    pub fn call(&self, cmd: &str, args: Value, timeout: Duration) -> Result<Value, LinkError> {
        let (tx, rx) = mpsc::channel();
        let id = self.submit(cmd, args, Waiter::Sync(tx))?;
        match rx.recv_timeout(timeout) {
            Ok(r) => r,
            Err(_) => {
                // Forget it, so a late reply is logged as unmatched rather than delivered to nobody.
                self.inner.state.lock().unwrap().pending.remove(&id);
                let ms = timeout.as_millis() as u64;
                self.inner.opts.log.note(format!("{cmd} (id {id}): no reply within {ms} ms"));
                Err(LinkError::Timeout { cmd: cmd.to_string(), ms })
            }
        }
    }

    fn submit(&self, cmd: &str, args: Value, waiter: Waiter) -> Result<u64, LinkError> {
        let id = {
            let mut st = self.inner.state.lock().unwrap();
            if let Some(why) = &st.closed {
                return Err(LinkError::Disconnected(why.clone()));
            }
            let busy = st.pending.len() + st.completions.len();
            if busy >= self.inner.opts.max_in_flight {
                return Err(LinkError::Backpressure(busy));
            }
            st.next += 1;
            let id = st.next;
            st.pending.insert(id, Pending { cmd: cmd.to_string(), sent: Instant::now(), waiter });
            st.last_write = Instant::now();
            id
        };
        let line = proto::encode(id, cmd, &args);
        self.inner.opts.log.push(Dir::Out, String::from_utf8_lossy(&line[..line.len() - 1]).into_owned());
        let res = {
            let mut w = self.inner.writer.lock().unwrap();
            w.write_all(&line).and_then(|_| w.flush())
        };
        if let Err(e) = res {
            // The request never left: take it back (its waiter must not also be failed by close()).
            self.inner.state.lock().unwrap().pending.remove(&id);
            let why = format!("writing {cmd} failed: {e}");
            close_with(&self.inner, why.clone());
            return Err(LinkError::Disconnected(why));
        }
        Ok(id)
    }

    /// Every finished request since the last drain, in completion order.
    pub fn drain(&self) -> Vec<Completion> {
        self.inner.state.lock().unwrap().completions.drain(..).collect()
    }

    /// Every event line since the last drain, in arrival order.
    pub fn drain_events(&self) -> Vec<Value> {
        self.inner.state.lock().unwrap().events.drain(..).collect()
    }

    pub fn is_open(&self) -> bool {
        self.inner.state.lock().unwrap().closed.is_none()
    }

    /// Why the connection closed, once it has.
    pub fn close_reason(&self) -> Option<String> {
        self.inner.state.lock().unwrap().closed.clone()
    }

    /// Requests sent and not yet answered.
    pub fn in_flight(&self) -> usize {
        self.inner.state.lock().unwrap().pending.len()
    }

    pub fn dropped_events(&self) -> u64 {
        self.inner.state.lock().unwrap().dropped_events
    }

    /// How long since anything arrived from the engine.
    pub fn since_last_read(&self) -> Duration {
        self.inner.state.lock().unwrap().last_read.elapsed()
    }

    pub fn log(&self) -> &ProtocolLog {
        &self.inner.opts.log
    }

    /// Close the connection (for every handle). Waiting requests complete as disconnected.
    pub fn close(&self) {
        close_with(&self.inner, "closed by the front end".into());
    }

    /// Wait until the connection has closed, up to `timeout`; true if it has.
    pub fn wait_closed(&self, timeout: Duration) -> bool {
        let st = self.inner.state.lock().unwrap();
        let (st, _) = self.inner.wake.wait_timeout_while(st, timeout, |s| s.closed.is_none()).unwrap();
        st.closed.is_some()
    }
}

/// Deliver one result to its waiter. Never called with the state lock held (callbacks may send).
fn deliver(inner: &Inner, id: u64, p: Pending, result: Result<Value, LinkError>) {
    match p.waiter {
        Waiter::Queue => {
            inner.state.lock().unwrap().completions.push_back(Completion { id, cmd: p.cmd, result, elapsed: p.sent.elapsed() });
            inner.wake.notify_all();
        }
        Waiter::Callback(cb) => cb(result),
        Waiter::Sync(tx) => {
            let _ = tx.send(result);
        }
        Waiter::Silent => {}
    }
}

fn close_with(inner: &Inner, why: String) {
    let pending: Vec<(u64, Pending)> = {
        let mut st = inner.state.lock().unwrap();
        if st.closed.is_some() {
            return;
        }
        st.closed = Some(why.clone());
        st.pending.drain().collect()
    };
    inner.opts.log.note(format!("connection closed: {why}"));
    inner.shutter.shutdown();
    for (id, p) in pending {
        let cmd = p.cmd.clone();
        deliver(inner, id, p, Err(LinkError::Disconnected(format!("{why}; {cmd} got no reply"))));
    }
    inner.wake.notify_all();
}

fn read_loop(inner: Arc<Inner>, stream: Box<dyn Stream>) {
    let mut r = BufReader::with_capacity(1 << 16, stream);
    let mut line = String::new();
    let why = loop {
        line.clear();
        match r.read_line(&mut line) {
            Ok(0) => break "the engine closed the connection".to_string(),
            Ok(_) => {}
            Err(e) => break format!("reading failed: {e}"),
        }
        inner.state.lock().unwrap().last_read = Instant::now();
        inner.opts.log.push(Dir::In, line.trim_end().to_string());
        match proto::decode(&line) {
            Incoming::Reply { id, result } => {
                let Some(p) = inner.state.lock().unwrap().pending.remove(&id) else {
                    inner.opts.log.note(format!("reply to id {id}, which nothing is waiting for (timed out or unknown)"));
                    continue;
                };
                let result = result.map_err(|(kind, message)| LinkError::Remote { cmd: p.cmd.clone(), kind, message });
                deliver(&inner, id, p, result);
            }
            Incoming::Event(v) => {
                let mut st = inner.state.lock().unwrap();
                if st.events.len() >= inner.opts.max_events {
                    st.events.pop_front();
                    st.dropped_events += 1;
                }
                st.events.push_back(v);
                drop(st);
                inner.wake.notify_all();
            }
            Incoming::Orphan { id, error } => inner.opts.log.note(format!("reply with id {id} names no request: {error:?}")),
            Incoming::Malformed(m) => inner.opts.log.note(format!("not protocol: {m}")),
        }
    };
    close_with(&inner, why);
}

fn keepalive_loop(inner: Arc<Inner>, every: Duration) {
    let tick = (every / 4).clamp(Duration::from_millis(5), Duration::from_millis(250));
    loop {
        let due = {
            let st = inner.state.lock().unwrap();
            let (st, _) = inner.wake.wait_timeout_while(st, tick, |s| s.closed.is_none()).unwrap();
            if st.closed.is_some() {
                return;
            }
            st.last_write.elapsed() >= every
        };
        if due {
            let c = Connection { inner: inner.clone() };
            if c.submit("control.ping", json!({}), Waiter::Silent).is_err() {
                return;
            }
        }
    }
}

#[cfg(all(test, unix))]
mod tests {
    use super::*;
    use std::os::unix::net::UnixStream;

    fn pair(opts: ConnOptions) -> (Connection, BufReader<UnixStream>, UnixStream) {
        let (a, b) = UnixStream::pair().unwrap();
        let c = Connection::from_stream(Box::new(a), opts).unwrap();
        let w = b.try_clone().unwrap();
        (c, BufReader::new(b), w)
    }
    fn read_req(r: &mut BufReader<UnixStream>) -> Value {
        let mut l = String::new();
        r.read_line(&mut l).unwrap();
        serde_json::from_str(&l).unwrap()
    }
    fn reply(w: &mut UnixStream, id: &Value, result: Value) {
        writeln!(w, "{}", json!({"id": id, "ok": true, "result": result})).unwrap();
    }

    #[test]
    fn replies_are_matched_by_id_when_they_come_out_of_order() {
        let (c, mut r, mut w) = pair(ConnOptions { keepalive: None, ..Default::default() });
        let a = c.send("a.one", json!({})).unwrap();
        let b = c.send("b.two", json!({})).unwrap();
        let (ra, rb) = (read_req(&mut r), read_req(&mut r));
        // Answer the second first, with an event in between.
        reply(&mut w, &rb["id"], json!({"who": "b"}));
        writeln!(w, "{}", json!({"event": "project_changed", "seq": 1})).unwrap();
        reply(&mut w, &ra["id"], json!({"who": "a"}));
        let t0 = Instant::now();
        let mut done = Vec::new();
        while done.len() < 2 && t0.elapsed() < Duration::from_secs(5) {
            done.extend(c.drain());
            std::thread::sleep(Duration::from_millis(2));
        }
        assert_eq!(done.len(), 2);
        assert_eq!((done[0].id, done[0].cmd.as_str()), (b, "b.two"));
        assert_eq!(done[0].result.as_ref().unwrap()["who"], "b");
        assert_eq!((done[1].id, done[1].cmd.as_str()), (a, "a.one"));
        assert_eq!(done[1].result.as_ref().unwrap()["who"], "a");
        assert_eq!(c.drain_events().len(), 1);
    }

    #[test]
    fn a_typed_error_comes_back_as_remote_and_a_callback_gets_its_reply() {
        let (c, mut r, mut w) = pair(ConnOptions { keepalive: None, ..Default::default() });
        let (tx, rx) = mpsc::channel();
        c.send_with("x.cb", json!({}), move |res| tx.send(res).unwrap()).unwrap();
        let q = read_req(&mut r);
        writeln!(w, "{}", json!({"id": q["id"], "ok": false, "error": {"kind": "refused", "message": "nope"}})).unwrap();
        let res = rx.recv_timeout(Duration::from_secs(5)).unwrap();
        assert_eq!(res.unwrap_err().remote_kind(), Some("refused"));
    }

    #[test]
    fn a_closed_stream_fails_every_waiter_and_further_sends() {
        let (c, mut r, w) = pair(ConnOptions { keepalive: None, ..Default::default() });
        let c2 = c.clone();
        let waiter = std::thread::spawn(move || c2.call("slow.one", json!({}), Duration::from_secs(10)));
        let _ = read_req(&mut r);
        drop(w);
        drop(r);
        let res = waiter.join().unwrap();
        assert!(matches!(res, Err(LinkError::Disconnected(_))), "{res:?}");
        assert!(c.wait_closed(Duration::from_secs(5)));
        assert!(!c.is_open());
        assert!(matches!(c.send("late", json!({})), Err(LinkError::Disconnected(_))));
    }

    #[test]
    fn backpressure_refuses_past_the_bound_without_dropping() {
        let (c, _r, _w) = pair(ConnOptions { keepalive: None, max_in_flight: 3, ..Default::default() });
        for _ in 0..3 {
            c.send("q", json!({})).unwrap();
        }
        assert!(matches!(c.send("q", json!({})), Err(LinkError::Backpressure(3))));
        assert_eq!(c.in_flight(), 3);
    }

    #[test]
    fn call_times_out_and_a_late_reply_is_only_logged() {
        let (c, mut r, mut w) = pair(ConnOptions { keepalive: None, ..Default::default() });
        let res = c.call("slow", json!({}), Duration::from_millis(50));
        assert!(matches!(res, Err(LinkError::Timeout { .. })));
        let q = read_req(&mut r);
        reply(&mut w, &q["id"], json!({}));
        let t0 = Instant::now();
        while !c.log().contains("nothing is waiting") && t0.elapsed() < Duration::from_secs(5) {
            std::thread::sleep(Duration::from_millis(2));
        }
        assert!(c.log().contains("nothing is waiting"));
        assert!(c.drain().is_empty());
    }

    #[test]
    fn events_past_the_bound_drop_the_oldest() {
        let (c, _r, mut w) = pair(ConnOptions { keepalive: None, max_events: 2, ..Default::default() });
        for seq in 1..=5 {
            writeln!(w, "{}", json!({"event": "e", "seq": seq})).unwrap();
        }
        let t0 = Instant::now();
        while c.dropped_events() < 3 && t0.elapsed() < Duration::from_secs(5) {
            std::thread::sleep(Duration::from_millis(2));
        }
        let ev = c.drain_events();
        assert_eq!(ev.iter().map(|e| e["seq"].as_i64().unwrap()).collect::<Vec<_>>(), [4, 5]);
        assert_eq!(c.dropped_events(), 3);
    }

    #[test]
    fn keepalive_pings_when_idle_and_swallows_the_reply() {
        let (c, mut r, mut w) = pair(ConnOptions { keepalive: Some(Duration::from_millis(40)), ..Default::default() });
        let q = read_req(&mut r);
        assert_eq!(q["cmd"], "control.ping");
        reply(&mut w, &q["id"], json!({"pong": true}));
        std::thread::sleep(Duration::from_millis(30));
        assert!(c.drain().is_empty(), "the keep-alive reply is not the caller's");
        c.close();
    }
}
