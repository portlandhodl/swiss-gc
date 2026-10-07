//! Swiss Streaming Protocol, server side.
//!
//! Header (12 bytes, big-endian), shared by every op:
//!
//! ```text
//! u8  op       0xA0 HELLO, 0xA1 READ, 0xA2 DATA
//! u8  arg      HELLO: version, READ: window (packets), DATA: flags
//! u16 req_id   READ: new per request, DATA: echoed
//! u32 offset   READ: file offset wanted, DATA: file offset of the payload
//! u32 length   READ: bytes wanted, DATA: payload bytes
//! ```
//!
//! A READ is followed by `u16 chunk` (payload bytes per DATA packet, a
//! multiple of 32) and the path as `path\npassword\0`. The server answers with
//! up to `window` DATA packets, paced so the BBA receive ring doesn't
//! overflow, and flags the final one with FLAG_LAST. A newer READ from the
//! same client replaces whatever batch is still being sent.

use std::net::SocketAddr;
use std::time::{Duration, Instant};

use crate::fs::{read_at, resolve, split_path, FileCache};

pub const OP_HELLO: u8 = 0xA0;
pub const OP_READ: u8 = 0xA1;
pub const OP_DATA: u8 = 0xA2;

pub const VERSION: u8 = 1;

pub const FLAG_LAST: u8 = 1 << 0;
pub const FLAG_ERROR: u8 = 1 << 1;

pub const HSIZE: usize = 12;
const MAX_WINDOW: u8 = 64;
const MAX_CHUNK: usize = 1984;
/// Ethernet + IPv4 + UDP + SSP headers, preamble and FCS, for pacing.
const FRAME_OVERHEAD: u64 = 14 + 20 + 8 + HSIZE as u64 + 8 + 4;

pub fn is_ssp(buf: &[u8]) -> bool {
    matches!(buf.first(), Some(&(OP_HELLO | OP_READ | OP_DATA)))
}

pub fn header(op: u8, arg: u8, req_id: u16, offset: u32, length: u32) -> [u8; HSIZE] {
    let mut h = [0u8; HSIZE];
    h[0] = op;
    h[1] = arg;
    h[2..4].copy_from_slice(&req_id.to_be_bytes());
    h[4..8].copy_from_slice(&offset.to_be_bytes());
    h[8..12].copy_from_slice(&length.to_be_bytes());
    h
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Header {
    pub op: u8,
    pub arg: u8,
    pub req_id: u16,
    pub offset: u32,
    pub length: u32,
}

pub fn parse_header(buf: &[u8]) -> Option<Header> {
    if buf.len() < HSIZE {
        return None;
    }
    Some(Header {
        op: buf[0],
        arg: buf[1],
        req_id: u16::from_be_bytes([buf[2], buf[3]]),
        offset: u32::from_be_bytes(buf[4..8].try_into().unwrap()),
        length: u32::from_be_bytes(buf[8..12].try_into().unwrap()),
    })
}

/// Encodes a READ, as the in-game patch sends it.
pub fn encode_read(window: u8, req_id: u16, offset: u32, length: u32, chunk: u16, path: &[u8]) -> Vec<u8> {
    let mut out = header(OP_READ, window, req_id, offset, length).to_vec();
    out.extend_from_slice(&chunk.to_be_bytes());
    out.extend_from_slice(path);
    out
}

/// One batch of DATA packets being sent to a client.
pub struct Stream {
    req_id: u16,
    offset: u32,
    data: Vec<u8>,
    chunk: usize,
    sent: usize,
    gap_per_byte: Duration,
    pub next_at: Instant,
}

impl Stream {
    /// Builds the next DATA packet once it's due, or None if not yet.
    pub fn next_packet(&mut self, now: Instant) -> Option<Vec<u8>> {
        if now < self.next_at || self.is_done() {
            return None;
        }
        let len = self.chunk.min(self.data.len() - self.sent);
        let last = self.sent + len == self.data.len();
        let mut pkt = header(
            OP_DATA,
            if last { FLAG_LAST } else { 0 },
            self.req_id,
            self.offset + self.sent as u32,
            len as u32,
        )
        .to_vec();
        pkt.extend_from_slice(&self.data[self.sent..self.sent + len]);
        self.sent += len;

        let gap = self.gap_per_byte * (len as u32 + FRAME_OVERHEAD as u32);
        // Don't let a late wakeup turn into a burst the BBA can't absorb.
        self.next_at = self.next_at.max(now.checked_sub(gap).unwrap_or(now)) + gap;
        Some(pkt)
    }

    pub fn is_done(&self) -> bool {
        self.sent >= self.data.len()
    }
}

pub struct State {
    pub root: std::path::PathBuf,
    pub password: Option<String>,
    /// Pacing rate in bytes per second on the wire.
    pub rate: u64,
    pub verbose: bool,
}

pub enum Action {
    Reply(Vec<u8>),
    Stream(Stream),
    Ignore,
}

impl State {
    pub fn handle(&self, buf: &[u8], from: SocketAddr, files: &mut FileCache, now: Instant) -> Action {
        let Some(h) = parse_header(buf) else { return Action::Ignore };
        match h.op {
            OP_HELLO => Action::Reply(header(OP_HELLO, VERSION, h.req_id, 0, 0).to_vec()),
            OP_READ if buf.len() >= HSIZE + 2 => match self.read(&h, &buf[HSIZE..], files) {
                Ok((data, chunk)) => {
                    if self.verbose {
                        eprintln!("ssp {from} read id={} off={:#x} len={} -> {} bytes", h.req_id, h.offset, h.length, data.len());
                    }
                    Action::Stream(Stream {
                        req_id: h.req_id,
                        offset: h.offset,
                        data,
                        chunk,
                        sent: 0,
                        gap_per_byte: Duration::from_nanos(1_000_000_000 / self.rate.max(1)),
                        next_at: now,
                    })
                }
                Err(msg) => {
                    if self.verbose {
                        eprintln!("ssp {from} read id={} error: {msg}", h.req_id);
                    }
                    Action::Reply(header(OP_DATA, FLAG_ERROR | FLAG_LAST, h.req_id, h.offset, 0).to_vec())
                }
            },
            _ => Action::Ignore,
        }
    }

    fn read(&self, h: &Header, body: &[u8], files: &mut FileCache) -> Result<(Vec<u8>, usize), &'static str> {
        let chunk = u16::from_be_bytes([body[0], body[1]]) as usize;
        let chunk = (chunk & !31).clamp(32, MAX_CHUNK);
        let window = h.arg.clamp(1, MAX_WINDOW) as usize;

        let (path, password) = split_path(&body[2..]);
        if let Some(expected) = &self.password {
            if password.as_deref() != Some(expected.as_str()) {
                return Err("permission denied");
            }
        }
        let path = resolve(&self.root, &path).ok_or("invalid path")?;
        let file = files.get(&path).map_err(|_| "no such file")?;

        let len = (h.length as usize).min(window * chunk);
        let mut data = vec![0u8; len];
        // Reads past the end (trimmed images) come back zero-filled.
        read_at(file, &mut data, h.offset as u64).map_err(|_| "read error")?;
        Ok((data, chunk))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stream_splits_and_flags_last() {
        let now = Instant::now();
        let mut s = Stream {
            req_id: 7,
            offset: 0x1000,
            data: (0..100u8).collect(),
            chunk: 32,
            sent: 0,
            gap_per_byte: Duration::ZERO,
            next_at: now,
        };
        let mut got = vec![];
        while let Some(p) = s.next_packet(now) {
            got.push(p);
        }
        assert_eq!(got.len(), 4);
        let hs: Vec<_> = got.iter().map(|p| parse_header(p).unwrap()).collect();
        assert_eq!(hs.iter().map(|h| h.offset).collect::<Vec<_>>(), [0x1000, 0x1020, 0x1040, 0x1060]);
        assert_eq!(hs.iter().map(|h| h.length).collect::<Vec<_>>(), [32, 32, 32, 4]);
        assert_eq!(hs.iter().map(|h| h.arg).collect::<Vec<_>>(), [0, 0, 0, FLAG_LAST]);
        assert!(hs.iter().all(|h| h.req_id == 7 && h.op == OP_DATA));
    }

    #[test]
    fn stream_paces_packets() {
        let now = Instant::now();
        let mut s = Stream {
            req_id: 1,
            offset: 0,
            data: vec![0; 64],
            chunk: 32,
            sent: 0,
            gap_per_byte: Duration::from_micros(1),
            next_at: now,
        };
        assert!(s.next_packet(now).is_some());
        assert!(s.next_packet(now).is_none());
        assert!(s.next_packet(now + Duration::from_micros(32 + FRAME_OVERHEAD)).is_some());
    }
}
