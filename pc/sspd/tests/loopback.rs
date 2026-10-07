//! End-to-end tests over loopback UDP against a running server.

use std::fs;
use std::net::UdpSocket;
use std::path::PathBuf;
use std::time::Duration;

use sspd::{fsp, ssp, Config, Server};

fn start_server(root: PathBuf, password: Option<&str>) -> std::net::SocketAddr {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    let cfg = Config {
        root,
        password: password.map(String::from),
        read_only: false,
        rate: 200 * 1024 * 1024,
        verbose: false,
    };
    let mut server = Server::new(sock, cfg);
    let addr = server.local_addr().unwrap();
    std::thread::spawn(move || server.run().unwrap());
    addr
}

fn temp_root(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("sspd-{name}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn pattern(len: usize) -> Vec<u8> {
    (0..len).map(|i| (i * 7 + i / 251) as u8).collect()
}

/// Mirrors the in-game client in cube/patches/bba/ssp.c: accept only the
/// expected offset for the current req_id, re-request on anything else, on
/// the end of a batch, and on timeout. `drop_every` simulates lost packets.
fn ssp_read(sock: &UdpSocket, path: &[u8], pos: u32, len: u32, drop_every: Option<usize>) -> Vec<u8> {
    const CHUNK: u16 = 1440;
    let mut buf = vec![0u8; len as usize];
    let mut done = 0u32;
    let mut req_id = 0u16;
    let mut received = 0usize;
    let mut rx = [0u8; 4096];

    let request = |req_id: &mut u16, done: u32| {
        *req_id = req_id.wrapping_add(1);
        sock.send(&ssp::encode_read(16, *req_id, pos + done, len - done, CHUNK, path)).unwrap();
    };
    request(&mut req_id, done);

    while done < len {
        let n = match sock.recv(&mut rx) {
            Ok(n) => n,
            Err(_) => {
                request(&mut req_id, done);
                continue;
            }
        };
        let h = ssp::parse_header(&rx[..n]).unwrap();
        if h.op != ssp::OP_DATA || h.req_id != req_id || h.arg & ssp::FLAG_ERROR != 0 {
            continue;
        }
        received += 1;
        if drop_every.is_some_and(|d| received.is_multiple_of(d)) {
            continue;
        }
        if h.offset != pos + done {
            request(&mut req_id, done);
            continue;
        }
        let size = (len - done).min(h.length & !31);
        buf[done as usize..(done + size) as usize].copy_from_slice(&rx[ssp::HSIZE..ssp::HSIZE + size as usize]);
        done += size;
        if done < len && h.arg & ssp::FLAG_LAST != 0 {
            request(&mut req_id, done);
        }
    }
    buf
}

fn client(server: std::net::SocketAddr) -> UdpSocket {
    let sock = UdpSocket::bind("127.0.0.1:0").unwrap();
    sock.connect(server).unwrap();
    sock.set_read_timeout(Some(Duration::from_millis(100))).unwrap();
    sock
}

#[test]
fn hello() {
    let server = start_server(temp_root("hello"), None);
    let sock = client(server);
    sock.send(&ssp::header(ssp::OP_HELLO, ssp::VERSION, 9, 0, 0)).unwrap();
    let mut rx = [0u8; 64];
    let n = sock.recv(&mut rx).unwrap();
    let h = ssp::parse_header(&rx[..n]).unwrap();
    assert_eq!((h.op, h.arg, h.req_id), (ssp::OP_HELLO, ssp::VERSION, 9));
}

#[test]
fn streams_whole_file_like_dvd_reads() {
    let root = temp_root("stream");
    let data = pattern(3 * 1024 * 1024 + 4096);
    fs::create_dir(root.join("games")).unwrap();
    fs::write(root.join("games/game.iso"), &data).unwrap();
    let server = start_server(root, None);
    let sock = client(server);

    let mut out = Vec::new();
    for pos in (0..data.len()).step_by(256 * 1024) {
        let len = (data.len() - pos).min(256 * 1024) as u32;
        out.extend(ssp_read(&sock, b"/games/game.iso\n\0", pos as u32, len, None));
    }
    assert!(out == data);
}

#[test]
fn recovers_from_packet_loss() {
    let root = temp_root("loss");
    let data = pattern(512 * 1024);
    fs::write(root.join("game.iso"), &data).unwrap();
    let server = start_server(root, None);
    let sock = client(server);

    for drop_every in [3, 7, 16] {
        let out = ssp_read(&sock, b"/game.iso\n\0", 0, data.len() as u32, Some(drop_every));
        assert!(out == data, "drop_every {drop_every}");
    }
}

#[test]
fn zero_fills_past_end_of_trimmed_image() {
    let root = temp_root("trim");
    fs::write(root.join("trimmed.iso"), pattern(1000)).unwrap();
    let server = start_server(root, None);
    let sock = client(server);

    let out = ssp_read(&sock, b"/trimmed.iso\n\0", 0, 4096, None);
    assert!(out[..1000] == pattern(1000)[..]);
    assert!(out[1000..].iter().all(|&b| b == 0));
}

#[test]
fn checks_password() {
    let root = temp_root("password");
    fs::write(root.join("game.iso"), pattern(4096)).unwrap();
    let server = start_server(root, Some("hunter2"));
    let sock = client(server);

    let out = ssp_read(&sock, b"/game.iso\nhunter2\0", 0, 4096, None);
    assert!(out == pattern(4096));

    sock.send(&ssp::encode_read(16, 1, 0, 4096, 1440, b"/game.iso\nwrong\0")).unwrap();
    let mut rx = [0u8; 64];
    let n = sock.recv(&mut rx).unwrap();
    assert!(ssp::parse_header(&rx[..n]).unwrap().arg & ssp::FLAG_ERROR != 0);
}

fn fsp_call(sock: &UdpSocket, cmd: u8, pos: u32, data: &[u8], extra: &[u8]) -> fsp::Packet {
    sock.send(&fsp::encode_request(cmd, 0, 0x1230, pos, data, extra)).unwrap();
    let mut rx = [0u8; 2048];
    let n = sock.recv(&mut rx).unwrap();
    let pkt = &rx[..n];
    assert_eq!(pkt[1], fsp::server_checksum(pkt));
    let len = u16::from_be_bytes([pkt[6], pkt[7]]) as usize;
    fsp::Packet {
        cmd: pkt[0],
        key: u16::from_be_bytes([pkt[2], pkt[3]]),
        seq: u16::from_be_bytes([pkt[4], pkt[5]]),
        pos: u32::from_be_bytes(pkt[8..12].try_into().unwrap()),
        data: pkt[12..12 + len].to_vec(),
        extra: pkt[12 + len..].to_vec(),
    }
}

#[test]
fn fsp_browse_and_save_patch() {
    let root = temp_root("fsp");
    fs::write(root.join("game.iso"), pattern(3000)).unwrap();
    let server = start_server(root.clone(), None);
    let sock = client(server);

    let pro = fsp_call(&sock, fsp::CC_GET_PRO, 0, b"\n\0", &[]);
    assert_eq!((pro.cmd, pro.pos, pro.seq), (fsp::CC_GET_PRO, 1, 0x1230));
    assert!(pro.extra[0] & fsp::DIR_OWNER != 0);
    assert_eq!(fsp_call(&sock, fsp::CC_GET_PRO, 0, b"swiss/patches\n\0", &[]).cmd, fsp::CC_ERR);

    let stat = fsp_call(&sock, fsp::CC_STAT, 0, b"/game.iso\n\0", &[]);
    assert_eq!(u32::from_be_bytes(stat.data[4..8].try_into().unwrap()), 3000);
    assert_eq!(stat.data[8], fsp::RDTYPE_FILE);
    assert_eq!(fsp_call(&sock, fsp::CC_STAT, 0, b"/missing\n\0", &[]).data[8], 0);

    let read = fsp_call(&sock, fsp::CC_GET_FILE, 2048, b"/game.iso\n\0", &[]);
    assert_eq!((read.pos, read.data.len()), (2048, 952));
    assert!(read.data == pattern(3000)[2048..]);
    // The in-game FSP patch names its own payload size.
    let read = fsp_call(&sock, fsp::CC_GET_FILE, 0, b"/game.iso\n\0", &1440u16.to_be_bytes());
    assert_eq!(read.data.len(), 1440);

    assert_eq!(fsp_call(&sock, fsp::CC_MAKE_DIR, 0, b"swiss\n\0", &[]).cmd, fsp::CC_MAKE_DIR);
    assert_eq!(fsp_call(&sock, fsp::CC_MAKE_DIR, 0, b"swiss/patches\n\0", &[]).cmd, fsp::CC_MAKE_DIR);
    let patch = pattern(2500);
    for (i, block) in patch.chunks(1024).enumerate() {
        assert_eq!(fsp_call(&sock, fsp::CC_UP_LOAD, (i * 1024) as u32, block, &[]).cmd, fsp::CC_UP_LOAD);
    }
    assert_eq!(fsp_call(&sock, fsp::CC_INSTALL, 0, b"swiss/patches/a.bin\n\0", &[]).cmd, fsp::CC_INSTALL);
    assert!(fs::read(root.join("swiss/patches/a.bin")).unwrap() == patch);

    assert_eq!(
        fsp_call(&sock, fsp::CC_RENAME, 2, b"swiss/patches/a.bin\n\0", b"swiss/patches/b.bin\n\0").cmd,
        fsp::CC_RENAME
    );
    assert!(root.join("swiss/patches/b.bin").exists());

    let dir = fsp_call(&sock, fsp::CC_GET_DIR, 0, b"/\n\0", &[]);
    assert_eq!(dir.cmd, fsp::CC_GET_DIR);
    assert!(dir.data.windows(9).any(|w| w == b"game.iso\0"));

    assert_eq!(fsp_call(&sock, fsp::CC_GET_FILE, 0, b"/../etc/passwd\n\0", &[]).cmd, fsp::CC_ERR);
}
