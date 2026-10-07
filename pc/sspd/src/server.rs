//! UDP event loop: answers FSP and SSP requests and paces SSP batches.

use std::collections::HashMap;
use std::io;
use std::net::{SocketAddr, UdpSocket};
use std::path::PathBuf;
use std::time::{Duration, Instant};

use crate::fs::FileCache;
use crate::{fsp, ssp};

pub struct Config {
    pub root: PathBuf,
    pub password: Option<String>,
    pub read_only: bool,
    /// SSP pacing rate in bytes per second.
    pub rate: u64,
    pub verbose: bool,
}

pub struct Server {
    sock: UdpSocket,
    fsp: fsp::State,
    ssp: ssp::State,
    files: FileCache,
    streams: HashMap<SocketAddr, ssp::Stream>,
    buf: Vec<u8>,
}

const IDLE_WAIT: Duration = Duration::from_millis(500);

impl Server {
    pub fn new(sock: UdpSocket, cfg: Config) -> Self {
        Server {
            sock,
            fsp: fsp::State::new(cfg.root.clone(), cfg.password.clone(), cfg.read_only, cfg.verbose),
            ssp: ssp::State { root: cfg.root, password: cfg.password, rate: cfg.rate, verbose: cfg.verbose },
            files: FileCache::default(),
            streams: HashMap::new(),
            buf: vec![0; 65536],
        }
    }

    pub fn local_addr(&self) -> io::Result<SocketAddr> {
        self.sock.local_addr()
    }

    pub fn run(&mut self) -> io::Result<()> {
        loop {
            self.poll()?;
        }
    }

    /// Waits for one datagram or the next paced send, whichever comes first.
    pub fn poll(&mut self) -> io::Result<()> {
        let now = Instant::now();
        let wait = self
            .streams
            .values()
            .map(|s| s.next_at.saturating_duration_since(now))
            .min()
            .unwrap_or(IDLE_WAIT);

        if wait.is_zero() {
            self.sock.set_nonblocking(true)?;
        } else {
            self.sock.set_nonblocking(false)?;
            self.sock.set_read_timeout(Some(wait))?;
        }

        match self.sock.recv_from(&mut self.buf) {
            Ok((n, from)) => self.dispatch(n, from),
            Err(e) if matches!(e.kind(), io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut) => {}
            // A previous send hit a closed port; nothing to do.
            Err(e) if e.kind() == io::ErrorKind::ConnectionReset => {}
            Err(e) => return Err(e),
        }

        self.pump();
        Ok(())
    }

    fn dispatch(&mut self, n: usize, from: SocketAddr) {
        let pkt = &self.buf[..n];
        if ssp::is_ssp(pkt) {
            match self.ssp.handle(pkt, from, &mut self.files, Instant::now()) {
                ssp::Action::Reply(reply) => send(&self.sock, &reply, from),
                ssp::Action::Stream(stream) => {
                    self.streams.insert(from, stream);
                }
                ssp::Action::Ignore => {}
            }
        } else if let Some(reply) = self.fsp.handle(pkt, from, &mut self.files) {
            send(&self.sock, &reply, from);
        }
    }

    fn pump(&mut self) {
        let now = Instant::now();
        for (addr, stream) in &mut self.streams {
            if let Some(pkt) = stream.next_packet(now) {
                send(&self.sock, &pkt, *addr);
            }
        }
        self.streams.retain(|_, s| !s.is_done());
    }
}

/// Send failures (an unreachable client, a full buffer) are left to the
/// client's retry logic rather than stopping the server.
fn send(sock: &UdpSocket, pkt: &[u8], to: SocketAddr) {
    if let Err(e) = sock.send_to(pkt, to) {
        eprintln!("send to {to} failed: {e}");
    }
}
