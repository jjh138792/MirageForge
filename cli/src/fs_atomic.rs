use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
use std::path::Path;
use std::process;

pub fn is_safe_token(s: &str) -> bool {
    if s.is_empty() || s.len() > 128 || s.contains("..") {
        return false;
    }
    s.chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.' || c == '-')
}

pub fn ensure_dir(path: &str, mode: u32) -> bool {
    let p = Path::new(path);
    if !p.exists() && fs::create_dir_all(p).is_err() {
        return false;
    }
    if let Ok(meta) = fs::metadata(p) {
        let mut perms = meta.permissions();
        perms.set_mode(mode);
        let _ = fs::set_permissions(p, perms);
        return true;
    }
    false
}

pub fn ensure_empty_log_file(path: &str, mode: u32) {
    let p = Path::new(path);
    if !p.exists() {
        if let Ok(f) = OpenOptions::new()
            .create(true)
            .write(true)
            .mode(mode)
            .open(p)
        {
            let _ = f.sync_all();
        }
    }
    if let Ok(meta) = fs::metadata(p) {
        let mut perms = meta.permissions();
        perms.set_mode(mode);
        let _ = fs::set_permissions(p, perms);
    }
}

pub fn write_file_atomic(path: &str, content: &str, mode: u32) -> bool {
    let tmp_path = format!("{}.tmp.{}", path, process::id());
    let res = (|| -> std::io::Result<()> {
        let mut file = OpenOptions::new()
            .write(true)
            .create(true)
            .truncate(true)
            .mode(mode)
            .open(&tmp_path)?;
        file.write_all(content.as_bytes())?;
        file.sync_all()?;
        let mut perms = file.metadata()?.permissions();
        perms.set_mode(mode);
        fs::set_permissions(&tmp_path, perms)?;
        drop(file);
        fs::rename(&tmp_path, path)?;
        Ok(())
    })();

    if res.is_err() {
        let _ = fs::remove_file(&tmp_path);
        return false;
    }
    true
}

pub fn read_file_str(path: &str) -> String {
    fs::read_to_string(path).unwrap_or_default()
}

pub fn truncate_file(path: &str) {
    if let Ok(f) = File::create(path) {
        let _ = f.set_len(0);
    }
}
