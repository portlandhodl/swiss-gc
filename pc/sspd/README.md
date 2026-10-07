# sspd

A file server for Swiss on the GameCube's Broadband Adapter. It speaks two
protocols on one UDP port:

- **FSP v2**, which Swiss uses to browse, stat and save patches.
- **SSP** (Swiss Streaming Protocol), which the in-game patch uses to stream
  disc reads in paced batches instead of FSP's one packet per round trip.

Swiss probes for SSP when the FSP device starts. If the server answers, games
boot with the SSP patch; with a stock `fspd`, Swiss falls back to the FSP
patch. The root directory can be a mounted SMB or NFS share, so games can live
on a NAS.

## Build and run

```sh
cargo build --release
./target/release/sspd -p 7717 /mnt/nas/gamecube
```

In Swiss, set **FSP Host IP** to this machine, **FSP Port** to the same port
and **FSP Password** to match `--password`, if set. Port 21, Swiss's default,
needs root on most systems.

| Option | Meaning |
|---|---|
| `-p, --port PORT` | UDP port (default 21) |
| `-b, --bind ADDR` | listen address (default 0.0.0.0) |
| `--password PW` | require this FSP password |
| `--read-only` | refuse uploads, deletes, renames and mkdir (Swiss can't save patches) |
| `--rate KBPS` | SSP pacing in KiB/s (default 2400) |
| `-v, --verbose` | log every request |

## Pacing

The BBA's receive ring holds about two full frames, and it drains over EXI,
which is slower than 100 Mbit Ethernet. If the server sends faster than EXI
drains, frames get dropped and the client falls back to re-requesting. Lower
`--rate` if `-v` shows the same offsets being requested repeatedly; raise it
until that starts happening to find your hardware's ceiling.

## SSP wire format

All fields are big-endian. Every packet starts with a 12-byte header:

| Field | Size | Meaning |
|---|---|---|
| op | 1 | `0xA0` HELLO, `0xA1` READ, `0xA2` DATA |
| arg | 1 | HELLO: version; READ: window in packets; DATA: flags (`1` last, `2` error) |
| req_id | 2 | READ: new per request; DATA: echoed |
| offset | 4 | file offset |
| length | 4 | READ: bytes wanted; DATA: payload bytes |

A READ follows the header with `u16 chunk` (payload bytes per DATA packet, a
multiple of 32) and the path as `path\npassword\0`.

The server answers with up to `window` DATA packets and marks the last one.
The client takes only the next expected offset for its current `req_id`. On
anything else, at the end of a batch, or after 100 ms of silence, it sends a
new READ from where it left off. A new READ replaces any batch still in
flight.

The client lives in `cube/patches/bba/ssp.c`.

## Tests

```sh
cargo test
```

The loopback tests run a client that mirrors `ssp.c` against a live server,
including runs with simulated packet loss, and an FSP client that browses
and saves a patch.
