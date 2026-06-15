#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod app;
mod dialogs;
mod keys;
mod serial;
mod ssh;
mod term;

fn main() -> eframe::Result<()> {
    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_inner_size([1200.0, 800.0])
            .with_min_inner_size([700.0, 450.0])
            .with_title("NetFlow"),
        ..Default::default()
    };
    eframe::run_native(
        "NetFlow",
        native_options,
        Box::new(|cc| Ok(Box::new(app::NetflowApp::new(cc)))),
    )
}
