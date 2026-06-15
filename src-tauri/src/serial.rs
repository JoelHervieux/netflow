use std::io::{Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serialport::{DataBits, Parity, SerialPort, StopBits};

/// A live serial connection: a shared port handle plus a run flag that the
/// reader thread polls.
pub struct SerialHandle {
    port: Arc<Mutex<Box<dyn SerialPort>>>,
    running: Arc<AtomicBool>,
}

impl SerialHandle {
    pub fn write(&self, data: &[u8]) {
        if let Ok(mut p) = self.port.lock() {
            let _ = p.write_all(data);
            let _ = p.flush();
        }
    }

    pub fn close(&self) {
        self.running.store(false, Ordering::SeqCst);
    }
}

pub fn list_ports() -> Vec<String> {
    serialport::available_ports()
        .map(|ports| ports.into_iter().map(|p| p.port_name).collect())
        .unwrap_or_default()
}

fn data_bits(n: u32) -> DataBits {
    match n {
        5 => DataBits::Five,
        6 => DataBits::Six,
        7 => DataBits::Seven,
        _ => DataBits::Eight,
    }
}

fn parity(n: u32) -> Parity {
    match n {
        1 => Parity::Odd,
        2 => Parity::Even,
        _ => Parity::None,
    }
}

fn stop_bits(n: u32) -> StopBits {
    match n {
        2 => StopBits::Two,
        _ => StopBits::One,
    }
}

/// Open a serial port and spawn a reader thread. `on_data` receives raw bytes,
/// `on_close` fires once when the reader stops.
pub fn connect(
    port_name: &str,
    baud_rate: u32,
    data_bits_n: u32,
    parity_n: u32,
    stop_bits_n: u32,
    on_data: impl Fn(Vec<u8>) + Send + 'static,
    on_close: impl FnOnce() + Send + 'static,
) -> Result<SerialHandle, String> {
    let port = serialport::new(port_name, baud_rate)
        .data_bits(data_bits(data_bits_n))
        .parity(parity(parity_n))
        .stop_bits(stop_bits(stop_bits_n))
        .timeout(Duration::from_millis(10))
        .open()
        .map_err(|e| format!("Ouverture de {port_name} impossible : {e}"))?;

    let reader = port
        .try_clone()
        .map_err(|e| format!("Clonage du port impossible : {e}"))?;

    let running = Arc::new(AtomicBool::new(true));
    let handle = SerialHandle {
        port: Arc::new(Mutex::new(port)),
        running: running.clone(),
    };

    std::thread::spawn(move || {
        let mut reader = reader;
        let mut buf = [0u8; 4096];
        while running.load(Ordering::SeqCst) {
            match reader.read(&mut buf) {
                Ok(0) => {}
                Ok(n) => on_data(buf[..n].to_vec()),
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(_) => break,
            }
        }
        on_close();
    });

    Ok(handle)
}
