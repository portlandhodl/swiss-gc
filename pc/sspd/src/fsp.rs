//! FSP v2 server side, covering what Swiss's fsplib and the FSP in-game
//! patch use.
//!
//! Packet: cmd u8, sum u8, key u16, seq u16, len u16, pos u32, then `len`
//! bytes of data and any extra bytes up to the end of the datagram. All
//! fields are big-endian.

use std::collections::HashMap;
use std::fs;
use std::io::{self, Write};
use std::net::{IpAddr, SocketAddr};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use crate::fs::{mtime_secs, read_at, resolve, split_path, FileCache};

pub const CC_VERSION: u8 = 0x10;
pub const CC_ERR: u8 = 0x40;
pub const CC_GET_DIR: u8 = 0x41;
pub const CC_GET_FILE: u8 = 0x42;
pub const CC_UP_LOAD: u8 = 0x43;
pub const CC_INSTALL: u8 = 0x44;
pub const CC_DEL_FILE: u8 = 0x45;
pub const CC_DEL_DIR: u8 = 0x46;
pub const CC_GET_PRO: u8 = 0x47;
pub const CC_MAKE_DIR: u8 = 0x49;
pub const CC_BYE: u8 = 0x4A;
pub const CC_STAT: u8 = 0x4D;
pub const CC_RENAME: u8 = 0x4E;

pub const HSIZE: usize = 12;
/// Payload size fsplib expects when a request doesn't name one.
pub const SPACE: usize = 1024;

pub const RDTYPE_END: u8 = 0x00;
pub const RDTYPE_FILE: u8 = 0x01;
pub const RDTYPE_DIR: u8 = 0x02;
pub const RDTYPE_SKIP: u8 = 0x2A;

pub const DIR_OWNER: u8 = 0x01;
pub const DIR_DEL: u8 = 0x02;
pub const DIR_ADD: u8 = 0x04;
pub const DIR_MKDIR: u8 = 0x08;
pub const DIR_GET: u8 = 0x10;
pub const DIR_LIST: u8 = 0x40;
pub const DIR_RENAME: u8 = 0x80;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Packet {
    pub cmd: u8,
    pub key: u16,
    pub seq: u16,
    pub pos: u32,
    pub data: Vec<u8>,
    pub extra: Vec<u8>,
}

/// Checksum as clients compute it: byte sum with the sum field zeroed, plus
/// the packet length, folded into 8 bits.
pub fn client_checksum(pkt: &[u8]) -> u8 {
    fold(byte_sum(pkt) + pkt.len() as u32)
}

/// Checksum as clients verify server replies: no length term.
pub fn server_checksum(pkt: &[u8]) -> u8 {
    fold(byte_sum(pkt))
}

fn byte_sum(pkt: &[u8]) -> u32 {
    pkt.iter().enumerate().filter(|&(i, _)| i != 1).map(|(_, &b)| b as u32).sum()
}

fn fold(sum: u32) -> u8 {
    (sum + (sum >> 8)) as u8
}

pub fn parse(buf: &[u8]) -> Option<Packet> {
    if buf.len() < HSIZE || buf[1] != client_checksum(buf) {
        return None;
    }
    let len = u16::from_be_bytes([buf[6], buf[7]]) as usize;
    if HSIZE + len > buf.len() {
        return None;
    }
    Some(Packet {
        cmd: buf[0],
        key: u16::from_be_bytes([buf[2], buf[3]]),
        seq: u16::from_be_bytes([buf[4], buf[5]]),
        pos: u32::from_be_bytes([buf[8], buf[9], buf[10], buf[11]]),
        data: buf[HSIZE..HSIZE + len].to_vec(),
        extra: buf[HSIZE + len..].to_vec(),
    })
}

pub fn encode(cmd: u8, key: u16, seq: u16, pos: u32, data: &[u8], extra: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(HSIZE + data.len() + extra.len());
    out.push(cmd);
    out.push(0);
    out.extend_from_slice(&key.to_be_bytes());
    out.extend_from_slice(&seq.to_be_bytes());
    out.extend_from_slice(&(data.len() as u16).to_be_bytes());
    out.extend_from_slice(&pos.to_be_bytes());
    out.extend_from_slice(data);
    out.extend_from_slice(extra);
    out[1] = server_checksum(&out);
    out
}

/// Encodes a client request, for tests and tools.
pub fn encode_request(cmd: u8, key: u16, seq: u16, pos: u32, data: &[u8], extra: &[u8]) -> Vec<u8> {
    let mut out = encode(cmd, key, seq, pos, data, extra);
    out[1] = client_checksum(&out);
    out
}

/// Builds a directory listing in FSP blocks of `block` bytes. Entries never
/// straddle a block; the rest of a block is skipped with an RDTYPE_SKIP marker.
pub fn dir_listing(dir: &Path, block: usize) -> io::Result<Vec<u8>> {
    let mut entries: Vec<_> = fs::read_dir(dir)?.filter_map(Result::ok).collect();
    entries.sort_by_key(|e| e.file_name());

    let mut out = Vec::new();
    for entry in entries {
        let Ok(meta) = fs::metadata(entry.path()) else { continue };
        let kind = if meta.is_dir() { RDTYPE_DIR } else { RDTYPE_FILE };
        let size = meta.len().min(u32::MAX as u64) as u32;
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name.contains('\n') {
            continue;
        }
        let record = dir_record(mtime_secs(&meta), size, kind, name.as_bytes());
        if record.len() <= block {
            push_record(&mut out, block, &record);
        }
    }
    push_record(&mut out, block, &dir_record(0, 0, RDTYPE_END, b""));
    Ok(out)
}

fn dir_record(mtime: u32, size: u32, kind: u8, name: &[u8]) -> Vec<u8> {
    let mut rec = Vec::with_capacity(12 + name.len());
    rec.extend_from_slice(&mtime.to_be_bytes());
    rec.extend_from_slice(&size.to_be_bytes());
    rec.push(kind);
    if kind != RDTYPE_END {
        rec.extend_from_slice(name);
        rec.push(0);
    }
    while rec.len() % 4 != 0 {
        rec.push(0);
    }
    rec
}

fn push_record(out: &mut Vec<u8>, block: usize, rec: &[u8]) {
    let used = out.len() % block;
    if used + rec.len() > block {
        if block - used >= 9 {
            out.extend_from_slice(&[0; 8]);
            out.push(RDTYPE_SKIP);
        }
        out.resize(out.len().next_multiple_of(block), 0);
    }
    out.extend_from_slice(rec);
}

pub struct State {
    pub root: PathBuf,
    pub password: Option<String>,
    pub read_only: bool,
    pub verbose: bool,
    keys: HashMap<IpAddr, u16>,
    uploads: HashMap<SocketAddr, Vec<u8>>,
}

impl State {
    pub fn new(root: PathBuf, password: Option<String>, read_only: bool, verbose: bool) -> Self {
        State { root, password, read_only, verbose, keys: HashMap::new(), uploads: HashMap::new() }
    }

    fn key_for(&mut self, ip: IpAddr) -> u16 {
        *self.keys.entry(ip).or_insert_with(|| {
            let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.subsec_nanos()).unwrap_or(0);
            (nanos ^ (nanos >> 16)) as u16 | 1
        })
    }

    fn pro_bits(&self) -> u8 {
        if self.read_only {
            DIR_GET | DIR_LIST
        } else {
            DIR_OWNER | DIR_DEL | DIR_ADD | DIR_MKDIR | DIR_GET | DIR_LIST | DIR_RENAME
        }
    }

    /// Resolves a request path, checking the password when one is set.
    fn path(&self, bytes: &[u8]) -> Result<PathBuf, &'static str> {
        let (path, password) = split_path(bytes);
        if let Some(expected) = &self.password {
            if password.as_deref() != Some(expected.as_str()) {
                return Err("Permission denied");
            }
        }
        resolve(&self.root, &path).ok_or("Invalid path")
    }

    fn writable_path(&self, bytes: &[u8]) -> Result<PathBuf, &'static str> {
        if self.read_only {
            return Err("Server is read-only");
        }
        self.path(bytes)
    }

    pub fn handle(&mut self, buf: &[u8], from: SocketAddr, files: &mut FileCache) -> Option<Vec<u8>> {
        let req = parse(buf)?;
        let key = self.key_for(from.ip());
        if self.verbose {
            eprintln!("fsp {from} cmd={:#04x} pos={} {:?}", req.cmd, req.pos, split_path(&req.data).0);
        }
        let reply = |cmd: u8, pos: u32, data: &[u8], extra: &[u8]| encode(cmd, key, req.seq, pos, data, extra);
        let err = |msg: &str| {
            let mut data = msg.as_bytes().to_vec();
            data.push(0);
            encode(CC_ERR, key, req.seq, req.pos, &data, &[])
        };
        let preferred = match req.extra[..] {
            [hi, lo] => (u16::from_be_bytes([hi, lo]) as usize).clamp(1, 0xFFFF - HSIZE),
            _ => SPACE,
        };

        let result: Result<Vec<u8>, &str> = (|| match req.cmd {
            CC_VERSION => Ok(reply(CC_VERSION, 0, concat!("sspd ", env!("CARGO_PKG_VERSION"), "\0").as_bytes(), &[])),
            CC_GET_DIR => {
                let dir = self.path(&req.data)?;
                let block = (preferred & !3).max(64);
                let listing = dir_listing(&dir, block).map_err(|_| "No such directory")?;
                let start = (req.pos as usize).min(listing.len());
                let end = (start + block).min(listing.len());
                Ok(reply(CC_GET_DIR, req.pos, &listing[start..end], &[]))
            }
            CC_GET_FILE => {
                let path = self.path(&req.data)?;
                let file = files.get(&path).map_err(|_| "No such file")?;
                let mut data = vec![0; preferred];
                let n = read_at(file, &mut data, req.pos as u64).map_err(|_| "Read error")?;
                Ok(reply(CC_GET_FILE, req.pos, &data[..n], &[]))
            }
            CC_STAT => {
                let path = self.path(&req.data)?;
                let mut data = [0u8; 9];
                if let Ok(meta) = fs::metadata(&path) {
                    data[0..4].copy_from_slice(&mtime_secs(&meta).to_be_bytes());
                    data[4..8].copy_from_slice(&(meta.len().min(u32::MAX as u64) as u32).to_be_bytes());
                    data[8] = if meta.is_dir() { RDTYPE_DIR } else { RDTYPE_FILE };
                }
                Ok(reply(CC_STAT, 0, &data, &[]))
            }
            CC_GET_PRO => {
                let path = self.path(&req.data)?;
                if !path.is_dir() {
                    return Err("No such directory");
                }
                Ok(reply(CC_GET_PRO, 1, &[], &[self.pro_bits()]))
            }
            CC_UP_LOAD => {
                if self.read_only {
                    return Err("Server is read-only");
                }
                let upload = self.uploads.entry(from).or_default();
                if req.pos == 0 {
                    upload.clear();
                }
                let end = req.pos as usize + req.data.len();
                if upload.len() < end {
                    upload.resize(end, 0);
                }
                upload[req.pos as usize..end].copy_from_slice(&req.data);
                Ok(reply(CC_UP_LOAD, req.pos, &[], &[]))
            }
            CC_INSTALL => {
                let path = self.writable_path(&req.data)?;
                let data = self.uploads.remove(&from).unwrap_or_default();
                files.clear();
                write_atomic(&path, &data).map_err(|_| "Write error")?;
                Ok(reply(CC_INSTALL, 0, &[], &[]))
            }
            CC_DEL_FILE => {
                let path = self.writable_path(&req.data)?;
                files.clear();
                fs::remove_file(path).map_err(|_| "Cannot delete file")?;
                Ok(reply(CC_DEL_FILE, 0, &[], &[]))
            }
            CC_DEL_DIR => {
                let path = self.writable_path(&req.data)?;
                fs::remove_dir(path).map_err(|_| "Cannot delete directory")?;
                Ok(reply(CC_DEL_DIR, 0, &[], &[]))
            }
            CC_MAKE_DIR => {
                let path = self.writable_path(&req.data)?;
                fs::create_dir(path).map_err(|_| "Cannot create directory")?;
                Ok(reply(CC_MAKE_DIR, 0, &[], &[]))
            }
            CC_RENAME => {
                let from_path = self.writable_path(&req.data)?;
                let to_path = self.writable_path(&req.extra)?;
                files.clear();
                fs::rename(from_path, to_path).map_err(|_| "Cannot rename")?;
                Ok(reply(CC_RENAME, 0, &[], &[]))
            }
            CC_BYE => {
                self.uploads.remove(&from);
                Ok(reply(CC_BYE, 0, &[], &[]))
            }
            _ => Err("Unknown command"),
        })();

        Some(result.unwrap_or_else(|msg| {
            if self.verbose {
                eprintln!("fsp {from} error: {msg}");
            }
            err(msg)
        }))
    }
}

fn write_atomic(path: &Path, data: &[u8]) -> io::Result<()> {
    let mut tmp = path.as_os_str().to_owned();
    tmp.push(".sspd-tmp");
    let tmp = PathBuf::from(tmp);
    let mut file = fs::File::create(&tmp)?;
    file.write_all(data)?;
    file.sync_all()?;
    drop(file);
    fs::rename(&tmp, path)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Port of fsplib's fsp_readdir_native, to check listings parse.
    fn read_listing(data: &[u8], block: usize) -> Vec<(String, u32, u8)> {
        let mut out = Vec::new();
        let mut pos = 0;
        while pos < data.len() {
            let kind = if block - pos % block < 9 { RDTYPE_SKIP } else { data[pos + 8] };
            match kind {
                RDTYPE_END => break,
                RDTYPE_SKIP => pos = (pos / block + 1) * block,
                _ => {
                    let size = u32::from_be_bytes(data[pos + 4..pos + 8].try_into().unwrap());
                    pos += 9;
                    let len = data[pos..].iter().position(|&b| b == 0).unwrap();
                    out.push((String::from_utf8(data[pos..pos + len].to_vec()).unwrap(), size, kind));
                    pos += len + 1;
                    pos = pos.next_multiple_of(4);
                }
            }
        }
        out
    }

    #[test]
    fn checksums_match_fsplib() {
        let req = encode_request(CC_GET_FILE, 0x1234, 0x5678, 42, b"/a.iso\n\0", &[0x04, 0x00]);
        assert!(parse(&req).is_some());
        let mut bad = req.clone();
        bad[13] ^= 1;
        assert!(parse(&bad).is_none());

        // fsplib's fsp_pkt_read: -sum + all bytes, folded, compared to sum.
        let rep = encode(CC_GET_FILE, 1, 2, 3, b"hello", &[]);
        let mut mysum: i32 = -(rep[1] as i32);
        for &b in &rep {
            mysum += b as i32;
        }
        assert_eq!(((mysum + (mysum >> 8)) & 0xff) as u8, rep[1]);
    }

    #[test]
    fn listing_round_trips_across_blocks() {
        let dir = std::env::temp_dir().join(format!("sspd-list-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(dir.join("sub")).unwrap();
        let mut names = vec![];
        for i in 0..60 {
            let name = format!("game-{i:02}-{}.iso", "x".repeat(i % 23));
            fs::write(dir.join(&name), vec![0; i]).unwrap();
            names.push(name);
        }
        for block in [64, 256, 1024] {
            let listing = dir_listing(&dir, block).unwrap();
            let parsed = read_listing(&listing, block);
            assert_eq!(parsed.len(), 61, "block {block}");
            assert!(parsed.contains(&("sub".into(), parsed.iter().find(|e| e.0 == "sub").unwrap().1, RDTYPE_DIR)));
            for (i, name) in names.iter().enumerate() {
                assert!(parsed.contains(&(name.clone(), i as u32, RDTYPE_FILE)), "{name} in block {block}");
            }
        }
        fs::remove_dir_all(&dir).unwrap();
    }
}
