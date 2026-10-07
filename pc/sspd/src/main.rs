use std::net::UdpSocket;
use std::path::PathBuf;
use std::process::ExitCode;

use sspd::{Config, Server};

const USAGE: &str = "\
Usage: sspd [options] [ROOT]

Serves ROOT (default: current directory) to Swiss over FSP, and streams games
to the in-game patch over the Swiss Streaming Protocol on the same port.
ROOT may be a mounted SMB/NFS share.

Options:
  -p, --port PORT       UDP port (default 21, Swiss's default FSP port)
  -b, --bind ADDR       address to listen on (default 0.0.0.0)
      --password PW     require this FSP password
      --read-only       refuse uploads, deletes, renames and mkdir
      --rate KBPS       SSP pacing rate in KiB/s (default 2400)
  -v, --verbose         log every request
  -h, --help            show this help";

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(msg) => {
            eprintln!("sspd: {msg}");
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<(), String> {
    let mut port: u16 = 21;
    let mut bind = String::from("0.0.0.0");
    let mut root = None;
    let mut cfg = Config { root: PathBuf::new(), password: None, read_only: false, rate: 2400 * 1024, verbose: false };

    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        let mut value = |name: &str| args.next().ok_or_else(|| format!("{name} needs a value"));
        match arg.as_str() {
            "-p" | "--port" => port = value(&arg)?.parse().map_err(|_| "invalid port")?,
            "-b" | "--bind" => bind = value(&arg)?,
            "--password" => cfg.password = Some(value(&arg)?),
            "--read-only" => cfg.read_only = true,
            "--rate" => {
                let kbps: u64 = value(&arg)?.parse().map_err(|_| "invalid rate")?;
                cfg.rate = kbps.max(1) * 1024;
            }
            "-v" | "--verbose" => cfg.verbose = true,
            "-h" | "--help" => {
                println!("{USAGE}");
                return Ok(());
            }
            _ if arg.starts_with('-') => return Err(format!("unknown option {arg}\n\n{USAGE}")),
            _ if root.is_none() => root = Some(PathBuf::from(arg)),
            _ => return Err(format!("unexpected argument {arg}")),
        }
    }

    let root = root.unwrap_or_else(|| PathBuf::from("."));
    cfg.root = root.canonicalize().map_err(|e| format!("{}: {e}", root.display()))?;
    if !cfg.root.is_dir() {
        return Err(format!("{} is not a directory", cfg.root.display()));
    }

    let sock = UdpSocket::bind((bind.as_str(), port)).map_err(|e| format!("bind {bind}:{port}: {e}"))?;
    eprintln!(
        "sspd: serving {} on {} ({}, SSP pacing {} KiB/s)",
        cfg.root.display(),
        sock.local_addr().map_err(|e| e.to_string())?,
        if cfg.read_only { "read-only" } else { "read-write" },
        cfg.rate / 1024
    );
    Server::new(sock, cfg).run().map_err(|e| e.to_string())
}
