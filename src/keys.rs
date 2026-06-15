use egui::{Key, Modifiers};

pub fn key_to_bytes(key: Key, mods: Modifiers) -> Option<Vec<u8>> {
    use Key::*;
    let base: &[u8] = match key {
        Enter => b"\r",
        Backspace => b"\x7f",
        Tab => b"\t",
        Escape => b"\x1b",
        ArrowUp => b"\x1b[A",
        ArrowDown => b"\x1b[B",
        ArrowRight => b"\x1b[C",
        ArrowLeft => b"\x1b[D",
        Home => b"\x1b[H",
        End => b"\x1b[F",
        PageUp => b"\x1b[5~",
        PageDown => b"\x1b[6~",
        Delete => b"\x1b[3~",
        Insert => b"\x1b[2~",
        F1 => b"\x1bOP",
        F2 => b"\x1bOQ",
        F3 => b"\x1bOR",
        F4 => b"\x1bOS",
        F5 => b"\x1b[15~",
        F6 => b"\x1b[17~",
        F7 => b"\x1b[18~",
        F8 => b"\x1b[19~",
        F9 => b"\x1b[20~",
        F10 => b"\x1b[21~",
        F11 => b"\x1b[23~",
        F12 => b"\x1b[24~",
        _ => return ctrl_letter(key, mods),
    };
    Some(base.to_vec())
}

fn ctrl_letter(key: Key, mods: Modifiers) -> Option<Vec<u8>> {
    if !mods.ctrl || mods.shift || mods.alt {
        return None;
    }
    let name = key.name();
    let bytes = name.as_bytes();
    if bytes.len() == 1 && bytes[0].is_ascii_alphabetic() {
        let code = bytes[0].to_ascii_uppercase() - b'A' + 1;
        Some(vec![code])
    } else {
        None
    }
}
