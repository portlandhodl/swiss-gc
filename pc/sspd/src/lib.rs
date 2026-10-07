//! FSP v2 + Swiss Streaming Protocol (SSP) server.
//!
//! Swiss browses, stats and saves patches over FSP (`fsp`), and the in-game
//! patch streams disc reads over SSP (`ssp`). Both share one UDP port, told
//! apart by the first byte of each datagram.

pub mod fs;
pub mod fsp;
pub mod server;
pub mod ssp;

pub use server::{Config, Server};
