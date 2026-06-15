mod serial;
mod ssh;

use std::collections::HashMap;
use std::sync::Mutex;

use base64::Engine;
use serial::SerialHandle;
use ssh::SshCmd;
use tauri::{AppHandle, Emitter, State};
use tokio::sync::mpsc::UnboundedSender;

/// A live connection, keyed by session id in the registry.
enum Conn {
    Ssh(UnboundedSender<SshCmd>),
    Serial(SerialHandle),
}

#[derive(Default)]
struct Registry(Mutex<HashMap<String, Conn>>);

#[derive(Clone, serde::Serialize)]
struct TermData {
    id: String,
    data: String,
}

#[derive(Clone, serde::Serialize)]
struct TermClosed {
    id: String,
}

fn emit_data(app: &AppHandle, id: &str, bytes: Vec<u8>) {
    let data = base64::engine::general_purpose::STANDARD.encode(&bytes);
    let _ = app.emit(
        "term-data",
        TermData {
            id: id.to_string(),
            data,
        },
    );
}

fn emit_closed(app: &AppHandle, id: &str) {
    let _ = app.emit("term-closed", TermClosed { id: id.to_string() });
}

#[tauri::command]
async fn ssh_connect(
    app: AppHandle,
    registry: State<'_, Registry>,
    host: String,
    port: u16,
    username: String,
    password: String,
) -> Result<String, String> {
    let id = uuid::Uuid::new_v4().to_string();
    let app_data = app.clone();
    let app_close = app.clone();
    let id_data = id.clone();
    let id_close = id.clone();

    let tx = ssh::connect(
        host,
        port,
        username,
        password,
        80,
        24,
        move |bytes| emit_data(&app_data, &id_data, bytes),
        move || emit_closed(&app_close, &id_close),
    )
    .await?;

    registry.0.lock().unwrap().insert(id.clone(), Conn::Ssh(tx));
    Ok(id)
}

#[tauri::command]
fn serial_connect(
    app: AppHandle,
    registry: State<'_, Registry>,
    port: String,
    baud_rate: u32,
    data_bits: u32,
    parity: u32,
    stop_bits: u32,
) -> Result<String, String> {
    let id = uuid::Uuid::new_v4().to_string();
    let app_data = app.clone();
    let app_close = app.clone();
    let id_data = id.clone();
    let id_close = id.clone();

    let handle = serial::connect(
        &port,
        baud_rate,
        data_bits,
        parity,
        stop_bits,
        move |bytes| emit_data(&app_data, &id_data, bytes),
        move || emit_closed(&app_close, &id_close),
    )?;

    registry
        .0
        .lock()
        .unwrap()
        .insert(id.clone(), Conn::Serial(handle));
    Ok(id)
}

#[tauri::command]
fn write_data(registry: State<'_, Registry>, id: String, data: String) {
    let map = registry.0.lock().unwrap();
    match map.get(&id) {
        Some(Conn::Ssh(tx)) => {
            let _ = tx.send(SshCmd::Data(data.into_bytes()));
        }
        Some(Conn::Serial(h)) => h.write(data.as_bytes()),
        None => {}
    }
}

#[tauri::command]
fn resize_pty(registry: State<'_, Registry>, id: String, cols: u32, rows: u32) {
    let map = registry.0.lock().unwrap();
    if let Some(Conn::Ssh(tx)) = map.get(&id) {
        let _ = tx.send(SshCmd::Resize { cols, rows });
    }
}

#[tauri::command]
fn disconnect(registry: State<'_, Registry>, id: String) {
    let conn = registry.0.lock().unwrap().remove(&id);
    match conn {
        Some(Conn::Ssh(tx)) => {
            let _ = tx.send(SshCmd::Close);
        }
        Some(Conn::Serial(h)) => h.close(),
        None => {}
    }
}

#[tauri::command]
fn list_serial_ports() -> Vec<String> {
    serial::list_ports()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_clipboard_manager::init())
        .manage(Registry::default())
        .invoke_handler(tauri::generate_handler![
            ssh_connect,
            serial_connect,
            write_data,
            resize_pty,
            disconnect,
            list_serial_ports
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
