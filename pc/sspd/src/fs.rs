//! Filesystem helpers shared by FSP and SSP.

use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::path::{Component, Path, PathBuf};
use std::time::UNIX_EPOCH;

/// Splits a request path of the form `path[\npassword][\0...]`.
pub fn split_path(bytes: &[u8]) -> (String, Option<String>) {
    let end = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let s = String::from_utf8_lossy(&bytes[..end]);
    match s.split_once('\n') {
        Some((path, password)) => (path.to_string(), Some(password.to_string())),
        None => (s.into_owned(), None),
    }
}

/// Resolves a client path under `root`, refusing anything that escapes it.
pub fn resolve(root: &Path, path: &str) -> Option<PathBuf> {
    let mut out = root.to_path_buf();
    for component in Path::new(path).components() {
        match component {
            Component::Normal(part) => out.push(part),
            Component::RootDir | Component::CurDir => {}
            Component::ParentDir | Component::Prefix(_) => return None,
        }
    }
    Some(out)
}

pub fn mtime_secs(meta: &std::fs::Metadata) -> u32 {
    meta.modified()
        .ok()
        .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
        .map(|d| d.as_secs().min(u32::MAX as u64) as u32)
        .unwrap_or(0)
}

/// Reads as much of `buf` as the file holds from `offset`, returning the count.
pub fn read_at(file: &File, buf: &mut [u8], offset: u64) -> io::Result<usize> {
    let mut done = 0;
    while done < buf.len() {
        #[cfg(unix)]
        let n = std::os::unix::fs::FileExt::read_at(file, &mut buf[done..], offset + done as u64);
        #[cfg(windows)]
        let n = std::os::windows::fs::FileExt::seek_read(file, &mut buf[done..], offset + done as u64);
        match n {
            Ok(0) => break,
            Ok(n) => done += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => {}
            Err(e) => return Err(e),
        }
    }
    Ok(done)
}

/// Keeps recently used files open so streaming doesn't reopen per batch,
/// which matters when the root is a network share.
#[derive(Default)]
pub struct FileCache {
    files: HashMap<PathBuf, File>,
}

impl FileCache {
    const MAX_OPEN: usize = 16;

    pub fn get(&mut self, path: &Path) -> io::Result<&File> {
        if !self.files.contains_key(path) {
            if self.files.len() >= Self::MAX_OPEN {
                self.files.clear();
            }
            let file = File::open(path)?;
            if !file.metadata()?.is_file() {
                return Err(io::Error::new(io::ErrorKind::InvalidInput, "not a file"));
            }
            self.files.insert(path.to_path_buf(), file);
        }
        Ok(&self.files[path])
    }

    pub fn clear(&mut self) {
        self.files.clear();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_path_with_password() {
        assert_eq!(
            split_path(b"/games/a.iso\nsecret\0"),
            ("/games/a.iso".into(), Some("secret".into()))
        );
        assert_eq!(split_path(b"/games/a.iso\n\0"), ("/games/a.iso".into(), Some("".into())));
        assert_eq!(split_path(b"swiss\0"), ("swiss".into(), None));
    }

    #[test]
    fn resolve_stays_under_root() {
        let root = Path::new("/srv/gc");
        assert_eq!(resolve(root, "/games/a.iso"), Some(PathBuf::from("/srv/gc/games/a.iso")));
        assert_eq!(resolve(root, ""), Some(PathBuf::from("/srv/gc")));
        assert_eq!(resolve(root, "./swiss/patches"), Some(PathBuf::from("/srv/gc/swiss/patches")));
        assert_eq!(resolve(root, "/games/../../etc/passwd"), None);
    }
}
