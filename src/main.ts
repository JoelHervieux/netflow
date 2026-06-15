import "./style.css";
import { Terminal } from "@xterm/xterm";
import { FitAddon } from "@xterm/addon-fit";
import { WebLinksAddon } from "@xterm/addon-web-links";
import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import {
  readText as clipReadText,
  writeText as clipWriteText,
} from "@tauri-apps/plugin-clipboard-manager";
import { openSshModal, openSerialModal } from "./modal";

type SessionType = "ssh" | "serial";

interface Session {
  id: string;
  type: SessionType;
  title: string;
  term: Terminal;
  fit: FitAddon;
  panel: HTMLDivElement;
  connected: boolean;
}

const TERM_THEME = {
  background: "#14171e",
  foreground: "#e6e9f0",
  cursor: "#e6e9f0",
  cursorAccent: "#14171e",
  selectionBackground: "#4a548a",
  black: "#14171e",
  red: "#f06c80",
  green: "#34d399",
  yellow: "#fbbf6b",
  blue: "#7c8cff",
  magenta: "#c4a7ff",
  cyan: "#6ad9e2",
  white: "#e6e9f0",
  brightBlack: "#606979",
  brightRed: "#ff91a5",
  brightGreen: "#5ce6af",
  brightYellow: "#ffd58c",
  brightBlue: "#a5b2ff",
  brightMagenta: "#d6bfff",
  brightCyan: "#96e8f0",
  brightWhite: "#ffffff",
};

const sessions = new Map<string, Session>();
let order: string[] = [];
let activeId: string | null = null;

const tabsEl = document.getElementById("tabs") as HTMLElement;
const workspace = document.getElementById("workspace") as HTMLElement;
const emptyEl = document.getElementById("empty") as HTMLElement;
const sessionInfo = document.getElementById("session-info") as HTMLElement;
const statusDot = document.getElementById("status-dot") as HTMLElement;
const statusText = document.getElementById("status-text") as HTMLElement;
const statusHint = document.getElementById("status-hint") as HTMLElement;
const statusSize = document.getElementById("status-size") as HTMLElement;

// ---------------------------------------------------------------------------
// base64 -> bytes (for raw terminal data coming from the backend)
// ---------------------------------------------------------------------------
function b64ToBytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------
function createSession(id: string, type: SessionType, title: string): Session {
  const term = new Terminal({
    fontFamily: "'Cascadia Code', 'Cascadia Mono', Consolas, monospace",
    fontSize: 14,
    lineHeight: 1.0,
    cursorBlink: true,
    cursorStyle: "block",
    scrollback: 10000,
    theme: TERM_THEME,
    allowProposedApi: true,
  });
  const fit = new FitAddon();
  term.loadAddon(fit);
  term.loadAddon(new WebLinksAddon());

  const panel = document.createElement("div");
  panel.className = "term-panel";
  workspace.appendChild(panel);
  term.open(panel);

  term.onData((data) => {
    invoke("write_data", { id, data }).catch(() => {});
  });

  term.onResize(({ cols, rows }) => {
    invoke("resize_pty", { id, cols, rows }).catch(() => {});
    if (id === activeId) updateStatus();
  });

  const session: Session = { id, type, title, term, fit, panel, connected: true };
  sessions.set(id, session);
  order.push(id);
  return session;
}

function setActive(id: string | null) {
  activeId = id;
  for (const [sid, s] of sessions) {
    const on = sid === id;
    s.panel.classList.toggle("active", on);
    if (on) {
      requestAnimationFrame(() => {
        s.fit.fit();
        s.term.focus();
      });
    }
  }
  emptyEl.classList.toggle("hidden", sessions.size > 0);
  renderTabs();
  updateStatus();
}

function closeSession(id: string) {
  const s = sessions.get(id);
  if (!s) return;
  invoke("disconnect", { id }).catch(() => {});
  s.term.dispose();
  s.panel.remove();
  sessions.delete(id);
  order = order.filter((x) => x !== id);

  if (activeId === id) {
    setActive(order.length ? order[order.length - 1] : null);
  } else {
    renderTabs();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
function renderTabs() {
  tabsEl.innerHTML = "";
  for (const id of order) {
    const s = sessions.get(id);
    if (!s) continue;
    const tab = document.createElement("div");
    tab.className = `tab ${s.type === "serial" ? "serial" : "ssh"}${id === activeId ? " active" : ""}`;
    if (!s.connected) tab.style.opacity = "0.6";
    tab.innerHTML = `
      <span class="tab-badge">${s.type === "serial" ? "COM" : "SSH"}</span>
      <span class="tab-title">${escapeHtml(s.title)}</span>
      <button class="tab-close" title="Fermer">&times;</button>
      <span class="tab-underline"></span>
    `;
    tab.onmousedown = (e) => {
      if ((e.target as HTMLElement).classList.contains("tab-close")) return;
      setActive(id);
    };
    (tab.querySelector(".tab-close") as HTMLButtonElement).onclick = (e) => {
      e.stopPropagation();
      closeSession(id);
    };
    tabsEl.appendChild(tab);
  }
  const plus = document.createElement("button");
  plus.className = "tab-plus";
  plus.title = "Nouvelle session (Ctrl+N)";
  plus.textContent = "+";
  plus.onclick = newSsh;
  tabsEl.appendChild(plus);
}

function updateStatus() {
  const s = activeId ? sessions.get(activeId) : null;
  if (!s) {
    statusDot.style.background = "var(--text-dim)";
    statusText.textContent = "Aucune session active";
    statusText.classList.add("muted");
    statusHint.textContent = "Ctrl+N SSH   Ctrl+Shift+N Serial";
    statusSize.textContent = "";
    sessionInfo.textContent = "";
    return;
  }
  statusText.classList.remove("muted");
  statusDot.style.background = s.connected ? "var(--success)" : "var(--error)";
  const kind = s.type === "serial" ? "Serial" : "SSH";
  statusText.textContent = `${s.connected ? "connecté" : "déconnecté"}  ${kind}  •  ${s.title}`;
  statusHint.textContent = "Ctrl+Shift+C copier   Ctrl+Shift+V coller";
  statusSize.textContent = `${s.term.cols} × ${s.term.rows}`;
  sessionInfo.textContent = `${kind}   ${s.title}`;
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"]/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c] as string
  );
}

// ---------------------------------------------------------------------------
// Connection actions
// ---------------------------------------------------------------------------
function newSsh() {
  openSshModal(async (p) => {
    const id = await invoke<string>("ssh_connect", {
      host: p.host,
      port: p.port,
      username: p.username,
      password: p.password,
    });
    const title = `${p.username}@${p.host}`;
    const s = createSession(id, "ssh", title);
    setActive(s.id);
  });
}

function newSerial() {
  openSerialModal(async (p) => {
    const id = await invoke<string>("serial_connect", {
      port: p.port,
      baudRate: p.baudRate,
      dataBits: p.dataBits,
      parity: p.parity,
      stopBits: p.stopBits,
    });
    const title = `${p.port} — ${p.baudRate}`;
    const s = createSession(id, "serial", title);
    setActive(s.id);
  });
}

// ---------------------------------------------------------------------------
// Backend events
// ---------------------------------------------------------------------------
listen<{ id: string; data: string }>("term-data", (e) => {
  const s = sessions.get(e.payload.id);
  if (s) s.term.write(b64ToBytes(e.payload.data));
});

listen<{ id: string }>("term-closed", (e) => {
  const s = sessions.get(e.payload.id);
  if (!s) return;
  s.connected = false;
  s.term.write("\r\n\x1b[38;5;210m[session terminée]\x1b[0m\r\n");
  renderTabs();
  if (e.payload.id === activeId) updateStatus();
});

// ---------------------------------------------------------------------------
// Window controls
// ---------------------------------------------------------------------------
const appWindow = getCurrentWindow();
(document.getElementById("btn-min") as HTMLButtonElement).onclick = () => appWindow.minimize();
(document.getElementById("btn-max") as HTMLButtonElement).onclick = () => appWindow.toggleMaximize();
(document.getElementById("btn-close") as HTMLButtonElement).onclick = () => appWindow.close();

// ---------------------------------------------------------------------------
// Empty-state buttons + keyboard shortcuts
// ---------------------------------------------------------------------------
(document.getElementById("empty-ssh") as HTMLButtonElement).onclick = newSsh;
(document.getElementById("empty-serial") as HTMLButtonElement).onclick = newSerial;

async function copySelection() {
  const s = activeId ? sessions.get(activeId) : null;
  if (s && s.term.hasSelection()) await clipWriteText(s.term.getSelection());
}
async function pasteClipboard() {
  const s = activeId ? sessions.get(activeId) : null;
  if (!s) return;
  const text = await clipReadText();
  if (text) invoke("write_data", { id: s.id, data: text }).catch(() => {});
}

window.addEventListener("keydown", (e) => {
  const ctrl = e.ctrlKey || e.metaKey;
  if (ctrl && e.shiftKey && (e.key === "N" || e.key === "n")) {
    e.preventDefault();
    newSerial();
  } else if (ctrl && (e.key === "N" || e.key === "n")) {
    e.preventDefault();
    newSsh();
  } else if (ctrl && e.shiftKey && (e.key === "C" || e.key === "c")) {
    e.preventDefault();
    copySelection();
  } else if (ctrl && e.shiftKey && (e.key === "V" || e.key === "v")) {
    e.preventDefault();
    pasteClipboard();
  } else if (ctrl && e.shiftKey && (e.key === "W" || e.key === "w")) {
    e.preventDefault();
    if (activeId) closeSession(activeId);
  } else if (ctrl && e.key === "Tab") {
    e.preventDefault();
    if (order.length > 1 && activeId) {
      const i = order.indexOf(activeId);
      setActive(order[(i + 1) % order.length]);
    }
  }
});

// ---------------------------------------------------------------------------
// Resize: refit the active terminal
// ---------------------------------------------------------------------------
let resizeRaf = 0;
window.addEventListener("resize", () => {
  cancelAnimationFrame(resizeRaf);
  resizeRaf = requestAnimationFrame(() => {
    const s = activeId ? sessions.get(activeId) : null;
    if (s) s.fit.fit();
  });
});

renderTabs();
updateStatus();
