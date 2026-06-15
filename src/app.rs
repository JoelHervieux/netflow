use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{channel, Receiver};
use std::sync::Arc;

use eframe::{egui, App, CreationContext, Frame};
use serde::{Deserialize, Serialize};
use tokio::runtime::Runtime;
use tokio::sync::mpsc::UnboundedSender;

use crate::dialogs::{Dialog, DialogAction, SerialForm, SettingsForm, SshForm};
use crate::serial::SerialHandle;
use crate::ssh::SshCmd;
use crate::term::TerminalState;

pub enum Conn {
    Ssh(UnboundedSender<SshCmd>),
    Serial(SerialHandle),
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SessionKind {
    Ssh,
    Serial,
}

pub struct Session {
    pub id: String,
    pub title: String,
    pub kind: SessionKind,
    pub conn: Conn,
    pub term: TerminalState,
    pub data_rx: Receiver<Vec<u8>>,
    pub closed: Arc<AtomicBool>,
    pub capture_on: bool,
    pub capture_buf: String,
}

#[derive(Serialize, Deserialize, Clone)]
pub struct Settings {
    pub font_size: f32,
}

impl Default for Settings {
    fn default() -> Self {
        Self { font_size: 14.0 }
    }
}

enum Shortcut {
    New,
    Settings,
    Close,
    Next,
    Clear,
    Capture,
    Paste,
}

pub struct NetflowApp {
    runtime: Runtime,
    sessions: Vec<Session>,
    active: Option<String>,
    settings: Settings,
    dialog: Dialog,
    error: Option<String>,
    clipboard: Option<arboard::Clipboard>,
}

impl NetflowApp {
    pub fn new(cc: &CreationContext<'_>) -> Self {
        let settings: Settings = cc
            .storage
            .and_then(|s| s.get_string("settings"))
            .and_then(|s| serde_json::from_str::<Settings>(&s).ok())
            .unwrap_or_default();

        cc.egui_ctx.set_visuals(egui::Visuals::dark());

        Self {
            runtime: tokio::runtime::Builder::new_multi_thread()
                .enable_all()
                .build()
                .expect("tokio runtime"),
            sessions: Vec::new(),
            active: None,
            settings,
            dialog: Dialog::None,
            error: None,
            clipboard: arboard::Clipboard::new().ok(),
        }
    }

    fn active_index(&self) -> Option<usize> {
        let id = self.active.as_ref()?;
        self.sessions.iter().position(|s| &s.id == id)
    }

    fn drain_sessions(&mut self) {
        for s in &mut self.sessions {
            while let Ok(bytes) = s.data_rx.try_recv() {
                if s.capture_on {
                    s.capture_buf.push_str(&String::from_utf8_lossy(&bytes));
                }
                s.term.feed(&bytes);
            }
        }
    }

    fn start_ssh(&mut self, form: SshForm, ctx: &egui::Context) {
        let (data_tx, data_rx) = channel::<Vec<u8>>();
        let id = uuid::Uuid::new_v4().to_string();
        let closed = Arc::new(AtomicBool::new(false));
        let ctx_data = ctx.clone();
        let ctx_close = ctx.clone();
        let closed_cb = closed.clone();

        let result = self.runtime.block_on(crate::ssh::connect(
            form.host.clone(),
            form.port,
            form.username.clone(),
            form.password.clone(),
            80,
            24,
            move |bytes| {
                let _ = data_tx.send(bytes);
                ctx_data.request_repaint();
            },
            move || {
                closed_cb.store(true, Ordering::SeqCst);
                ctx_close.request_repaint();
            },
        ));

        match result {
            Ok(tx) => {
                let title = format!("{}@{}", form.username, form.host);
                self.sessions.push(Session {
                    id: id.clone(),
                    title,
                    kind: SessionKind::Ssh,
                    conn: Conn::Ssh(tx),
                    term: TerminalState::new(24, 80, self.settings.font_size),
                    data_rx,
                    closed,
                    capture_on: false,
                    capture_buf: String::new(),
                });
                self.active = Some(id);
            }
            Err(e) => self.error = Some(e),
        }
    }

    fn start_serial(&mut self, form: SerialForm, ctx: &egui::Context) {
        let (data_tx, data_rx) = channel::<Vec<u8>>();
        let id = uuid::Uuid::new_v4().to_string();
        let closed = Arc::new(AtomicBool::new(false));
        let ctx_data = ctx.clone();
        let ctx_close = ctx.clone();
        let closed_cb = closed.clone();

        let result = crate::serial::connect(
            &form.port,
            form.baud_rate,
            form.data_bits,
            form.parity,
            form.stop_bits,
            move |bytes| {
                let _ = data_tx.send(bytes);
                ctx_data.request_repaint();
            },
            move || {
                closed_cb.store(true, Ordering::SeqCst);
                ctx_close.request_repaint();
            },
        );

        match result {
            Ok(h) => {
                let title = format!("{} — {}", form.port, form.baud_rate);
                self.sessions.push(Session {
                    id: id.clone(),
                    title,
                    kind: SessionKind::Serial,
                    conn: Conn::Serial(h),
                    term: TerminalState::new(24, 80, self.settings.font_size),
                    data_rx,
                    closed,
                    capture_on: false,
                    capture_buf: String::new(),
                });
                self.active = Some(id);
            }
            Err(e) => self.error = Some(e),
        }
    }

    fn close_session(&mut self, id: &str) {
        if let Some(idx) = self.sessions.iter().position(|s| s.id == id) {
            let s = self.sessions.remove(idx);
            match s.conn {
                Conn::Ssh(tx) => {
                    let _ = tx.send(SshCmd::Close);
                }
                Conn::Serial(h) => h.close(),
            }
            if self.active.as_deref() == Some(id) {
                self.active = self.sessions.last().map(|s| s.id.clone());
            }
        }
    }

    fn send_input(&self, idx: usize, bytes: Vec<u8>) {
        let s = &self.sessions[idx];
        match &s.conn {
            Conn::Ssh(tx) => {
                let _ = tx.send(SshCmd::Data(bytes));
            }
            Conn::Serial(h) => h.write(&bytes),
        }
    }

    fn send_resize(&self, idx: usize, cols: u16, rows: u16) {
        let s = &self.sessions[idx];
        if let Conn::Ssh(tx) = &s.conn {
            let _ = tx.send(SshCmd::Resize {
                cols: cols as u32,
                rows: rows as u32,
            });
        }
    }

    fn toggle_capture(&mut self, idx: usize) {
        let s = &mut self.sessions[idx];
        if !s.capture_on {
            s.capture_on = true;
            s.capture_buf.clear();
            return;
        }
        s.capture_on = false;
        let text = clean_for_save(&s.capture_buf);
        s.capture_buf.clear();
        if text.trim().is_empty() {
            return;
        }
        let title = s.title.clone();
        let safe: String = title
            .chars()
            .map(|c| {
                if c.is_ascii_alphanumeric() || c == '.' || c == '_' || c == '-' {
                    c
                } else {
                    '_'
                }
            })
            .collect();
        let fname = format!("netflow_{}_{}.txt", safe, timestamp_slug());
        if let Some(path) = rfd::FileDialog::new().set_file_name(&fname).save_file() {
            if let Err(e) = std::fs::write(&path, text) {
                self.error = Some(format!("Échec de l'enregistrement : {e}"));
            }
        }
    }

    fn paste_clipboard(&mut self, idx: usize) {
        let text = match self.clipboard.as_mut() {
            Some(cb) => match cb.get_text() {
                Ok(t) => t,
                Err(_) => return,
            },
            None => return,
        };
        let s = &self.sessions[idx];
        match &s.conn {
            Conn::Ssh(tx) => {
                let _ = tx.send(SshCmd::Data(text.into_bytes()));
            }
            Conn::Serial(h) => h.write(text.as_bytes()),
        }
    }

    fn handle_shortcuts(&mut self, ctx: &egui::Context) {
        let mut actions: Vec<Shortcut> = Vec::new();
        ctx.input(|i| {
            for ev in &i.events {
                if let egui::Event::Key {
                    key,
                    pressed: true,
                    modifiers,
                    ..
                } = ev
                {
                    let ctrl = modifiers.command || modifiers.ctrl;
                    if ctrl && !modifiers.shift && !modifiers.alt {
                        match key {
                            egui::Key::N => actions.push(Shortcut::New),
                            egui::Key::E => actions.push(Shortcut::Capture),
                            egui::Key::L => actions.push(Shortcut::Clear),
                            egui::Key::Tab => actions.push(Shortcut::Next),
                            egui::Key::Comma => actions.push(Shortcut::Settings),
                            _ => {}
                        }
                    } else if ctrl && modifiers.shift && !modifiers.alt {
                        match key {
                            egui::Key::V => actions.push(Shortcut::Paste),
                            egui::Key::W => actions.push(Shortcut::Close),
                            _ => {}
                        }
                    }
                }
            }
        });

        for a in actions {
            match a {
                Shortcut::New => self.dialog = Dialog::TypeChooser,
                Shortcut::Settings => {
                    self.dialog = Dialog::Settings(SettingsForm {
                        font_size: self.settings.font_size,
                    })
                }
                Shortcut::Close => {
                    if let Some(idx) = self.active_index() {
                        let id = self.sessions[idx].id.clone();
                        self.close_session(&id);
                    }
                }
                Shortcut::Next => {
                    if let Some(idx) = self.active_index() {
                        let n = self.sessions.len();
                        if n > 1 {
                            self.active = Some(self.sessions[(idx + 1) % n].id.clone());
                        }
                    }
                }
                Shortcut::Clear => {
                    if let Some(idx) = self.active_index() {
                        self.sessions[idx].term.clear();
                    }
                }
                Shortcut::Capture => {
                    if let Some(idx) = self.active_index() {
                        self.toggle_capture(idx);
                    }
                }
                Shortcut::Paste => {
                    if let Some(idx) = self.active_index() {
                        self.paste_clipboard(idx);
                    }
                }
            }
        }
    }

    fn empty_state(dialog: &mut Dialog, ui: &mut egui::Ui) {
        ui.vertical_centered(|ui| {
            ui.add_space(60.0);
            ui.heading("NetFlow");
            ui.add_space(8.0);
            ui.label("Client SSH et Serial natif.");
            ui.add_space(20.0);
            if ui.button("  + Nouvelle connexion  ").clicked() {
                *dialog = Dialog::TypeChooser;
            }
            ui.add_space(8.0);
            ui.label("Raccourcis : Ctrl+N nouvelle · Ctrl+, paramètres");
        });
    }

    fn show_tab_bar(&mut self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            let mut to_activate: Option<String> = None;
            let mut to_close: Option<String> = None;
            for s in &self.sessions {
                let badge = if s.kind == SessionKind::Ssh { "SSH" } else { "COM" };
                let is_active = self.active.as_deref() == Some(&s.id);
                let closed = s.closed.load(Ordering::SeqCst);
                let label = if closed {
                    format!("{}  {} (fermé)", badge, s.title)
                } else {
                    format!("{}  {}", badge, s.title)
                };
                if ui.selectable_label(is_active, label).clicked() {
                    to_activate = Some(s.id.clone());
                }
                if ui.small_button("×").on_hover_text("Fermer").clicked() {
                    to_close = Some(s.id.clone());
                }
                ui.add_space(4.0);
            }
            if ui
                .button("+")
                .on_hover_text("Nouvelle connexion (Ctrl+N)")
                .clicked()
            {
                self.dialog = Dialog::TypeChooser;
            }
            if let Some(id) = to_activate {
                self.active = Some(id);
            }
            if let Some(id) = to_close {
                self.close_session(&id);
            }

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                if ui.button("⚙ Paramètres").clicked() {
                    self.dialog = Dialog::Settings(SettingsForm {
                        font_size: self.settings.font_size,
                    });
                }
                if let Some(idx) = self.active_index() {
                    if ui.button("Effacer").clicked() {
                        self.sessions[idx].term.clear();
                    }
                    let cap_on = self.sessions[idx].capture_on;
                    let label = if cap_on { "■ Arrêter" } else { "● Capture" };
                    if ui.button(label).clicked() {
                        self.toggle_capture(idx);
                    }
                }
            });
        });
    }

    fn show_status_bar(&self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            if let Some(idx) = self.active_index() {
                let s = &self.sessions[idx];
                let conn_ok = !s.closed.load(Ordering::SeqCst);
                let dot = if conn_ok { "●" } else { "○" };
                let kind = if s.kind == SessionKind::Ssh { "SSH" } else { "Serial" };
                let state = if conn_ok { "connecté" } else { "déconnecté" };
                ui.label(format!("{} {}  {}  •  {}", dot, state, kind, s.title));
                ui.separator();
                ui.label(format!("{} × {}", s.term.cols, s.term.rows));
                if s.capture_on {
                    ui.separator();
                    ui.colored_label(
                        egui::Color32::from_rgb(255, 200, 100),
                        "● capture en cours",
                    );
                }
            } else {
                ui.label("Aucune session active");
            }

            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.label("Ctrl+N nouvelle · Ctrl+Shift+V coller · Ctrl+E capture · Ctrl+L effacer");
            });
        });
    }
}

impl App for NetflowApp {
    fn save(&mut self, storage: &mut dyn eframe::Storage) {
        if let Ok(s) = serde_json::to_string(&self.settings) {
            storage.set_string("settings", s);
        }
    }

    fn update(&mut self, ctx: &egui::Context, _frame: &mut Frame) {
        self.drain_sessions();

        egui::TopBottomPanel::top("tabs").show(ctx, |ui| self.show_tab_bar(ui));
        egui::TopBottomPanel::bottom("status").show(ctx, |ui| self.show_status_bar(ui));

        egui::CentralPanel::default().show(ctx, |ui| {
            if let Some(idx) = self.active_index() {
                let font_size = self.settings.font_size;
                let s = &mut self.sessions[idx];
                s.term.font_size = font_size;

                let mut buf = Vec::<u8>::new();
                let resize = crate::term::show(ui, &mut s.term, |b| buf.extend_from_slice(&b));

                if !buf.is_empty() {
                    self.send_input(idx, buf);
                }
                if let Some(r) = resize {
                    self.send_resize(idx, r.cols, r.rows);
                }
            } else {
                Self::empty_state(&mut self.dialog, ui);
            }
        });

        let action = crate::dialogs::show(ctx, &mut self.dialog);
        match action {
            DialogAction::None => {}
            DialogAction::Close => self.dialog = Dialog::None,
            DialogAction::PickSsh => self.dialog = Dialog::Ssh(SshForm::default()),
            DialogAction::PickSerial => self.dialog = Dialog::Serial(SerialForm::new()),
            DialogAction::ConfirmSsh(form) => {
                self.dialog = Dialog::None;
                self.start_ssh(form, ctx);
            }
            DialogAction::ConfirmSerial(form) => {
                self.dialog = Dialog::None;
                self.start_serial(form, ctx);
            }
            DialogAction::ConfirmSettings(form) => {
                self.dialog = Dialog::None;
                self.settings.font_size = form.font_size;
            }
        }

        if self.error.is_some() {
            let mut dismiss = false;
            let msg = self.error.clone().unwrap();
            egui::Window::new("Erreur")
                .collapsible(false)
                .resizable(false)
                .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
                .show(ctx, |ui| {
                    ui.label(&msg);
                    ui.add_space(8.0);
                    if ui.button("OK").clicked() {
                        dismiss = true;
                    }
                });
            if dismiss {
                self.error = None;
            }
        }

        self.handle_shortcuts(ctx);
    }
}

fn clean_for_save(raw: &str) -> String {
    let mut out = String::with_capacity(raw.len());
    let mut chars = raw.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\x1b' {
            match chars.peek() {
                Some('[') => {
                    chars.next();
                    while let Some(c) = chars.next() {
                        if matches!(c, '@'..='~') {
                            break;
                        }
                    }
                }
                Some(']') => {
                    chars.next();
                    while let Some(c) = chars.next() {
                        if c == '\x07' {
                            break;
                        }
                        if c == '\x1b' {
                            chars.next();
                            break;
                        }
                    }
                }
                Some(_) => {
                    chars.next();
                }
                None => {}
            }
        } else if c == '\r' {
            if chars.peek() == Some(&'\n') {
                // consume only the CR; the LF will come through next iteration
            } else {
                out.push('\n');
            }
        } else {
            out.push(c);
        }
    }
    out
}

fn timestamp_slug() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    format!("{secs}")
}
