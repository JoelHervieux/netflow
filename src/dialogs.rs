#[derive(Clone)]
pub struct SshForm {
    pub host: String,
    pub port: u16,
    pub username: String,
    pub password: String,
}

impl Default for SshForm {
    fn default() -> Self {
        Self {
            host: String::new(),
            port: 22,
            username: String::new(),
            password: String::new(),
        }
    }
}

#[derive(Clone)]
pub struct SerialForm {
    pub port: String,
    pub ports: Vec<String>,
    pub baud_rate: u32,
    pub data_bits: u32,
    pub parity: u32,
    pub stop_bits: u32,
}

impl SerialForm {
    pub fn new() -> Self {
        let ports = crate::serial::list_ports();
        let port = ports.first().cloned().unwrap_or_default();
        Self {
            port,
            ports,
            baud_rate: 115_200,
            data_bits: 8,
            parity: 0,
            stop_bits: 1,
        }
    }
}

#[derive(Clone)]
pub struct SettingsForm {
    pub font_size: f32,
}

pub enum Dialog {
    None,
    TypeChooser,
    Ssh(SshForm),
    Serial(SerialForm),
    Settings(SettingsForm),
}

pub enum DialogAction {
    None,
    Close,
    PickSsh,
    PickSerial,
    ConfirmSsh(SshForm),
    ConfirmSerial(SerialForm),
    ConfirmSettings(SettingsForm),
}

pub fn show(ctx: &egui::Context, dialog: &mut Dialog) -> DialogAction {
    match dialog {
        Dialog::None => DialogAction::None,
        Dialog::TypeChooser => show_type_chooser(ctx),
        Dialog::Ssh(form) => show_ssh(ctx, form),
        Dialog::Serial(form) => show_serial(ctx, form),
        Dialog::Settings(form) => show_settings(ctx, form),
    }
}

fn show_type_chooser(ctx: &egui::Context) -> DialogAction {
    let mut action = DialogAction::None;
    egui::Window::new("Nouvelle connexion")
        .collapsible(false)
        .resizable(false)
        .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
        .show(ctx, |ui| {
            ui.label("Choisis le type de connexion :");
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                if ui.button("  SSH  ").clicked() {
                    action = DialogAction::PickSsh;
                }
                if ui.button(" Serial ").clicked() {
                    action = DialogAction::PickSerial;
                }
            });
            ui.add_space(8.0);
            if ui.button("Annuler").clicked() {
                action = DialogAction::Close;
            }
        });
    action
}

fn show_ssh(ctx: &egui::Context, form: &mut SshForm) -> DialogAction {
    let mut action = DialogAction::None;
    egui::Window::new("Connexion SSH")
        .collapsible(false)
        .resizable(false)
        .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
        .show(ctx, |ui| {
            egui::Grid::new("ssh_form")
                .num_columns(2)
                .spacing([10.0, 6.0])
                .show(ui, |ui| {
                    ui.label("Hôte");
                    ui.text_edit_singleline(&mut form.host);
                    ui.end_row();
                    ui.label("Port");
                    ui.add(egui::DragValue::new(&mut form.port).range(1..=65535));
                    ui.end_row();
                    ui.label("Utilisateur");
                    ui.text_edit_singleline(&mut form.username);
                    ui.end_row();
                    ui.label("Mot de passe");
                    ui.add(egui::TextEdit::singleline(&mut form.password).password(true));
                    ui.end_row();
                });
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                let can_connect = !form.host.trim().is_empty() && !form.username.trim().is_empty();
                if ui.add_enabled(can_connect, egui::Button::new("Connecter")).clicked() {
                    action = DialogAction::ConfirmSsh(form.clone());
                }
                if ui.button("Annuler").clicked() {
                    action = DialogAction::Close;
                }
            });
        });
    action
}

fn show_serial(ctx: &egui::Context, form: &mut SerialForm) -> DialogAction {
    let mut action = DialogAction::None;
    egui::Window::new("Connexion Serial")
        .collapsible(false)
        .resizable(false)
        .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
        .show(ctx, |ui| {
            egui::Grid::new("serial_form")
                .num_columns(2)
                .spacing([10.0, 6.0])
                .show(ui, |ui| {
                    ui.label("Port");
                    ui.horizontal(|ui| {
                        egui::ComboBox::from_id_salt("serial_port")
                            .selected_text(&form.port)
                            .show_ui(ui, |ui| {
                                for p in &form.ports {
                                    ui.selectable_value(&mut form.port, p.clone(), p);
                                }
                            });
                        if ui.small_button("↻").on_hover_text("Rafraîchir").clicked() {
                            form.ports = crate::serial::list_ports();
                            if !form.ports.iter().any(|p| p == &form.port) {
                                form.port = form.ports.first().cloned().unwrap_or_default();
                            }
                        }
                    });
                    ui.end_row();

                    ui.label("Baud");
                    egui::ComboBox::from_id_salt("baud")
                        .selected_text(form.baud_rate.to_string())
                        .show_ui(ui, |ui| {
                            for b in [
                                9600u32, 19200, 38400, 57600, 115200, 230400, 460800, 921600,
                            ] {
                                ui.selectable_value(&mut form.baud_rate, b, b.to_string());
                            }
                        });
                    ui.end_row();

                    ui.label("Data bits");
                    egui::ComboBox::from_id_salt("data_bits")
                        .selected_text(form.data_bits.to_string())
                        .show_ui(ui, |ui| {
                            for n in [5u32, 6, 7, 8] {
                                ui.selectable_value(&mut form.data_bits, n, n.to_string());
                            }
                        });
                    ui.end_row();

                    ui.label("Parité");
                    egui::ComboBox::from_id_salt("parity")
                        .selected_text(parity_label(form.parity))
                        .show_ui(ui, |ui| {
                            ui.selectable_value(&mut form.parity, 0, "Aucune");
                            ui.selectable_value(&mut form.parity, 1, "Impaire");
                            ui.selectable_value(&mut form.parity, 2, "Paire");
                        });
                    ui.end_row();

                    ui.label("Stop bits");
                    egui::ComboBox::from_id_salt("stop_bits")
                        .selected_text(form.stop_bits.to_string())
                        .show_ui(ui, |ui| {
                            for n in [1u32, 2] {
                                ui.selectable_value(&mut form.stop_bits, n, n.to_string());
                            }
                        });
                    ui.end_row();
                });
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                let can_connect = !form.port.trim().is_empty();
                if ui.add_enabled(can_connect, egui::Button::new("Connecter")).clicked() {
                    action = DialogAction::ConfirmSerial(form.clone());
                }
                if ui.button("Annuler").clicked() {
                    action = DialogAction::Close;
                }
            });
        });
    action
}

fn parity_label(p: u32) -> &'static str {
    match p {
        1 => "Impaire",
        2 => "Paire",
        _ => "Aucune",
    }
}

fn show_settings(ctx: &egui::Context, form: &mut SettingsForm) -> DialogAction {
    let mut action = DialogAction::None;
    egui::Window::new("Paramètres")
        .collapsible(false)
        .resizable(false)
        .anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0])
        .show(ctx, |ui| {
            ui.horizontal(|ui| {
                ui.label("Taille de police");
                ui.add(egui::Slider::new(&mut form.font_size, 8.0..=28.0).step_by(1.0));
            });
            ui.add_space(8.0);
            ui.horizontal(|ui| {
                if ui.button("Appliquer").clicked() {
                    action = DialogAction::ConfirmSettings(form.clone());
                }
                if ui.button("Annuler").clicked() {
                    action = DialogAction::Close;
                }
            });
        });
    action
}
