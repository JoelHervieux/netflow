use std::sync::Arc;

use async_trait::async_trait;
use russh::client::{self, Handle, Msg};
use russh::keys::key::PublicKey;
use russh::{Channel, ChannelMsg, Disconnect};
use tokio::sync::mpsc;

struct Client;

#[async_trait]
impl client::Handler for Client {
    type Error = russh::Error;

    async fn check_server_key(&mut self, _key: &PublicKey) -> Result<bool, Self::Error> {
        Ok(true)
    }
}

pub enum SshCmd {
    Data(Vec<u8>),
    Resize { cols: u32, rows: u32 },
    Close,
}

pub async fn connect(
    host: String,
    port: u16,
    username: String,
    password: String,
    cols: u32,
    rows: u32,
    on_data: impl Fn(Vec<u8>) + Send + 'static,
    on_close: impl FnOnce() + Send + 'static,
) -> Result<mpsc::UnboundedSender<SshCmd>, String> {
    let config = Arc::new(client::Config::default());

    let mut handle: Handle<Client> = client::connect(config, (host.as_str(), port), Client)
        .await
        .map_err(|e| format!("Connexion impossible : {e}"))?;

    let authed = handle
        .authenticate_password(&username, &password)
        .await
        .map_err(|e| format!("Erreur d'authentification : {e}"))?;
    if !authed {
        return Err("Authentification refusée (utilisateur ou mot de passe).".into());
    }

    let mut channel: Channel<Msg> = handle
        .channel_open_session()
        .await
        .map_err(|e| format!("Ouverture du canal impossible : {e}"))?;

    channel
        .request_pty(false, "xterm-256color", cols, rows, 0, 0, &[])
        .await
        .map_err(|e| format!("Allocation du PTY impossible : {e}"))?;
    channel
        .request_shell(false)
        .await
        .map_err(|e| format!("Ouverture du shell impossible : {e}"))?;

    let (tx, mut rx) = mpsc::unbounded_channel::<SshCmd>();

    tokio::spawn(async move {
        loop {
            tokio::select! {
                msg = channel.wait() => {
                    match msg {
                        Some(ChannelMsg::Data { data }) => on_data(data.to_vec()),
                        Some(ChannelMsg::ExtendedData { data, .. }) => on_data(data.to_vec()),
                        Some(ChannelMsg::Eof) | Some(ChannelMsg::Close) | None => break,
                        _ => {}
                    }
                }
                cmd = rx.recv() => {
                    match cmd {
                        Some(SshCmd::Data(bytes)) => {
                            if channel.data(&bytes[..]).await.is_err() { break; }
                        }
                        Some(SshCmd::Resize { cols, rows }) => {
                            let _ = channel.window_change(cols, rows, 0, 0).await;
                        }
                        Some(SshCmd::Close) | None => {
                            let _ = channel.eof().await;
                            break;
                        }
                    }
                }
            }
        }

        let _ = handle
            .disconnect(Disconnect::ByApplication, "", "English")
            .await;
        on_close();
    });

    Ok(tx)
}
