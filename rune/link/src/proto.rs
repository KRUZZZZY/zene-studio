//! The control protocol's wire format (`include/ControlServer.h`, `docs/ENGINE-SERVE.md`).
//!
//! Out: one line `{"id":N,"cmd":"group.verb","args":{...},"proto":1}`.
//! Back: a reply `{"id":N,"ok":true,"result":{...}}` or `{"id":N,"ok":false,"error":{"kind","message"}}`,
//! or an event line with no id (`{"event":"project_changed",...}` after `events.subscribe`).
//! A request the engine could not parse is answered with `"id":-1` (known by design).

use serde_json::{Value, json};

/// The protocol version this link speaks; `control.ping` reports the engine's.
pub const PROTO: i64 = 1;

pub fn encode(id: u64, cmd: &str, args: &Value) -> Vec<u8> {
    let mut out = serde_json::to_vec(&json!({"id": id, "cmd": cmd, "args": args, "proto": PROTO})).unwrap_or_default();
    out.push(b'\n');
    out
}

#[derive(Clone, Debug, PartialEq)]
pub enum Incoming {
    /// A reply to request `id`: the `result` object, or the typed error (kind, message).
    Reply { id: u64, result: Result<Value, (String, String)> },
    /// A line without an id: an event.
    Event(Value),
    /// A reply that names no request of ours (`id:-1`: the engine could not parse a request).
    Orphan { id: i64, error: Option<(String, String)> },
    /// Not the protocol at all.
    Malformed(String),
}

pub fn decode(line: &str) -> Incoming {
    let mut v: Value = match serde_json::from_str(line.trim_end()) {
        Ok(v) => v,
        Err(e) => return Incoming::Malformed(format!("{e}: {}", clip(line))),
    };
    if !v.is_object() {
        return Incoming::Malformed(format!("not an object: {}", clip(line)));
    }
    let error = |v: &Value| v.get("error").map(|e| (str_of(&e["kind"]), str_of(&e["message"])));
    match v.get("id") {
        None | Some(Value::Null) => Incoming::Event(v),
        Some(id) => match id.as_u64() {
            Some(id) => {
                if v.get("ok").and_then(Value::as_bool) == Some(true) {
                    Incoming::Reply { id, result: Ok(v.get_mut("result").map(Value::take).unwrap_or(Value::Null)) }
                } else if v.get("ok").and_then(Value::as_bool) == Some(false) {
                    Incoming::Reply { id, result: Err(error(&v).unwrap_or_else(|| ("unknown".into(), String::new()))) }
                } else {
                    Incoming::Malformed(format!("reply without ok: {}", clip(line)))
                }
            }
            None => Incoming::Orphan { id: id.as_i64().unwrap_or(-1), error: error(&v) },
        },
    }
}

fn str_of(v: &Value) -> String {
    v.as_str().map(String::from).unwrap_or_else(|| if v.is_null() { String::new() } else { v.to_string() })
}

/// At most 200 characters of a line, for messages and logs.
pub fn clip(line: &str) -> String {
    let t = line.trim_end();
    match t.char_indices().nth(200) {
        Some((i, _)) => format!("{}...", &t[..i]),
        None => t.to_string(),
    }
}

/// What `control.ping` says about the engine, read defensively.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Ping {
    pub engine_ready: bool,
    /// `"none"` in serve mode, `"qt"` in the application; empty if not reported (an older engine).
    pub interface: String,
    pub proto: Option<i64>,
    pub version: String,
    pub reason: String,
}

impl Ping {
    pub fn parse(v: &Value) -> Ping {
        Ping {
            engine_ready: v["engine_ready"].as_bool().unwrap_or(false),
            interface: v["interface"].as_str().unwrap_or("").to_string(),
            proto: v["proto"].as_i64(),
            version: v["version"].as_str().unwrap_or("").to_string(),
            reason: v["reason"].as_str().unwrap_or("").to_string(),
        }
    }
    pub fn is_serve(&self) -> bool {
        self.interface == "none"
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn replies_events_orphans_and_garbage_decode_apart() {
        assert_eq!(decode(r#"{"id":3,"ok":true,"result":{"a":1}}"#), Incoming::Reply { id: 3, result: Ok(json!({"a":1})) });
        assert_eq!(
            decode(r#"{"id":4,"ok":false,"error":{"kind":"refused","message":"no"}}"#),
            Incoming::Reply { id: 4, result: Err(("refused".into(), "no".into())) }
        );
        assert!(matches!(decode(r#"{"event":"project_changed","command":"track.add"}"#), Incoming::Event(_)));
        assert_eq!(
            decode(r#"{"error":{"kind":"invalid_args","message":"malformed JSON request"},"id":-1,"ok":false}"#),
            Incoming::Orphan { id: -1, error: Some(("invalid_args".into(), "malformed JSON request".into())) }
        );
        assert!(matches!(decode("not json"), Incoming::Malformed(_)));
        assert!(matches!(decode("[1,2]"), Incoming::Malformed(_)));
        assert!(matches!(decode(r#"{"id":5}"#), Incoming::Malformed(_)));
    }

    #[test]
    fn encode_is_one_line_with_proto() {
        let b = encode(7, "control.ping", &json!({}));
        assert_eq!(*b.last().unwrap(), b'\n');
        let v: Value = serde_json::from_slice(&b).unwrap();
        assert_eq!(v, json!({"id": 7, "cmd": "control.ping", "args": {}, "proto": 1}));
    }

    #[test]
    fn ping_reads_serve_mode_and_tolerates_missing_fields() {
        let p = Ping::parse(&json!({"pong": true, "engine_ready": true, "interface": "none", "proto": 1, "version": "0.4"}));
        assert!(p.engine_ready && p.is_serve() && p.proto == Some(1));
        let old = Ping::parse(&json!({"pong": true}));
        assert!(!old.engine_ready && !old.is_serve() && old.proto.is_none());
    }
}
