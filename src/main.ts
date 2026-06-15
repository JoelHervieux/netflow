import "./style.css";
import { Terminal } from "@xterm/xterm";
import { FitAddon } from "@xterm/addon-fit";
import { WebLinksAddon } from "@xterm/addon-web-links";
import { WebglAddon } from "@xterm/addon-webgl";
import { invoke, Channel } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWindow } from "@tauri-apps/api/window";
import {
  readText as clipReadText,
  writeText as clipWriteText,
} from "@tauri-apps/plugin-clipboard-manager";
import {
  openSshModal,
  openSerialModal,
  openTypeChooser,
  openSettingsModal,
  loadSettings,
  applyTheme,
  Settings,
} from "./modal";

type SessionType = "ssh" | "serial";

interface Session {
  id: string;
  type: SessionType;
  title: string;
  term: Terminal;
  fit: FitAddon;
  panel: HTMLDivElement;
  connected: boolean;
  // Capture buffer — raw text accumulated while capture is on.
  captureOn: boolean;
  captureBuf: string;
}

function termTheme() {
  // Pull live CSS variables so the terminal matches the active app theme.
  const cs = getComputedStyle(document.documentElement);
  const v = (n: string) => cs.getPropertyValue(n).trim();
  return {
    background: v("--term-bg"),
    foreground: v("--term-fg"),
    cursor: v("--term-fg"),
    cursorAccent: v("--term-bg"),
    selectionBackground: v("--accent-dim"),
    black: v("--term-bg"),
    red: v("--error"),
    green: v("--success"),
    yellow: v("--warning"),
    blue: v("--accent"),
    magenta: "#c4a7ff",
    cyan: "#6ad9e2",
    white: v("--term-fg"),
    brightBlack: v("--text-muted"),
    brightRed: "#ff91a5",
    brightGreen: "#5ce6af",
    brightYellow: "#ffd58c",
    brightBlue: "#a5b2ff",
    brightMagenta: "#d6bfff",
    brightCyan: "#96e8f0",
    brightWhite: v("--term-fg"),
  };
}

const sessions = new Map<string, Session>();
let order: string[] = [];
let activeId: string | null = null;
let settings: Settings = loadSettings();

const tabsEl = document.getElementById("tabs") as HTMLElement;
const workspace = document.getElementById("workspace") as HTMLElement;
const emptyEl = document.getElementById("empty") as HTMLElement;
const sessionInfo = document.getElementById("session-info") as HTMLElement;
const statusDot = document.getElementById("status-dot") as HTMLElement;
const statusText = document.getElementById("status-text") as HTMLElement;
const statusHint = document.getElementById("status-hint") as HTMLElement;
const statusSize = document.getElementById("status-size") as HTMLElement;
const titleActions = document.getElementById("title-actions") as HTMLElement;
const btnCapture = document.getElementById("btn-capture") as HTMLButtonElement;
const btnClear = document.getElementById("btn-clear") as HTMLButtonElement;
const btnSettings = document.getElementById("btn-settings") as HTMLButtonElement;
const capLabel = document.getElementById("cap-label") as HTMLElement;

applyTheme(settings.theme);

// ---------------------------------------------------------------------------
// Session lifecycle
// ---------------------------------------------------------------------------
function createSession(id: string, type: SessionType, title: string): Session {
  const term = new Terminal({
    fontFamily: "'Cascadia Code', 'Cascadia Mono', Consolas, monospace",
    fontSize: settings.fontSize,
    lineHeight: 1.0,
    cursorBlink: true,
    cursorStyle: "block",
    scrollback: 10000,
    theme: termTheme(),
    allowProposedApi: true,
  });
  const fit = new FitAddon();
  term.loadAddon(fit);
  term.loadAddon(new WebLinksAddon());

  const panel = document.createElement("div");
  panel.className = "term-panel";
  workspace.appendChild(panel);
  term.open(panel);

  try {
    const webgl = new WebglAddon();
    webgl.onContextLoss(() => webgl.dispose());
    term.loadAddon(webgl);
  } catch {
    // GPU unavailable — fall back to DOM renderer silently.
  }

  term.onData((data) => {
    invoke("write_data", { id, data }).catch(() => {});
  });

  term.onResize(({ cols, rows }) => {
    invoke("resize_pty", { id, cols, rows }).catch(() => {});
    if (id === activeId) updateStatus();
  });

  const session: Session = {
    id,
    type,
    title,
    term,
    fit,
    panel,
    connected: true,
    captureOn: false,
    captureBuf: "",
  };
  sessions.set(id, session);
  order.push(id);
  return session;
}

function makeDataChannel(getId: () => string | null): Channel<string> {
  const ch = new Channel<string>();
  ch.onmessage = (text) => {
    const id = getId();
    if (!id) return;
    const s = sessions.get(id);
    if (!s) return;
    if (s.captureOn) s.captureBuf += text;
    s.term.write(text);
  };
  return ch;
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
  titleActions.classList.toggle("hidden", !id);
  renderTabs();
  updateStatus();
  updateCaptureButton();
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
  plus.title = "Nouvelle connexion (Ctrl+N)";
  plus.textContent = "+";
  plus.onclick = newConnection;
  tabsEl.appendChild(plus);
}

function updateStatus() {
  const s = activeId ? sessions.get(activeId) : null;
  if (!s) {
    statusDot.style.background = "var(--text-dim)";
    statusText.textContent = "Aucune session active";
    statusText.classList.add("muted");
    statusHint.textContent = "";
    statusSize.textContent = "";
    sessionInfo.textContent = "";
    return;
  }
  statusText.classList.remove("muted");
  statusDot.style.background = s.connected ? "var(--success)" : "var(--error)";
  const kind = s.type === "serial" ? "Serial" : "SSH";
  statusText.textContent = `${s.connected ? "connecté" : "déconnecté"}  ${kind}  •  ${s.title}`;
  statusHint.textContent = s.captureOn
    ? "● capture en cours"
    : "Ctrl+Shift+C copier   Ctrl+Shift+V coller";
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
function newConnection() {
  openTypeChooser((kind) => {
    if (kind === "ssh") newSsh();
    else newSerial();
  });
}

function newSsh() {
  openSshModal(async (p) => {
    let assignedId: string | null = null;
    const onData = makeDataChannel(() => assignedId);
    const id = await invoke<string>("ssh_connect", {
      host: p.host,
      port: p.port,
      username: p.username,
      password: p.password,
      onData,
    });
    assignedId = id;
    const title = `${p.username}@${p.host}`;
    const s = createSession(id, "ssh", title);
    setActive(s.id);
  });
}

function newSerial() {
  openSerialModal(async (p) => {
    let assignedId: string | null = null;
    const onData = makeDataChannel(() => assignedId);
    const id = await invoke<string>("serial_connect", {
      port: p.port,
      baudRate: p.baudRate,
      dataBits: p.dataBits,
      parity: p.parity,
      stopBits: p.stopBits,
      onData,
    });
    assignedId = id;
    const title = `${p.port} — ${p.baudRate}`;
    const s = createSession(id, "serial", title);
    setActive(s.id);
  });
}

// ---------------------------------------------------------------------------
// Capture / export — toggle button: start, then stop = save to file.
// ---------------------------------------------------------------------------
function updateCaptureButton() {
  const s = activeId ? sessions.get(activeId) : null;
  const on = !!s && s.captureOn;
  btnCapture.classList.toggle("capturing", on);
  capLabel.textContent = on ? "Arrêter" : "Capture";
}

// Strip ANSI escape sequences and normalize line endings for the saved file.
function cleanForSave(raw: string): string {
  // CSI / OSC / single-byte ESC sequences. Covers the bulk of what terminals emit.
  const ansi = /\x1b\][^\x07]*\x07|\x1b\[[0-9;?]*[ -\/]*[@-~]|\x1b[@-Z\\-_]/g;
  return raw.replace(ansi, "").replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function downloadText(filename: string, content: string) {
  const blob = new Blob([content], { type: "text/plain;charset=utf-8" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => {
    URL.revokeObjectURL(url);
    a.remove();
  }, 0);
}

function timestampSlug(): string {
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
}

function toggleCapture() {
  const s = activeId ? sessions.get(activeId) : null;
  if (!s) return;
  if (!s.captureOn) {
    s.captureOn = true;
    s.captureBuf = "";
    s.term.write(
      "\r\n\x1b[38;5;215m[capture démarrée — tapez vos commandes, puis cliquez Arrêter]\x1b[0m\r\n"
    );
  } else {
    s.captureOn = false;
    const text = cleanForSave(s.captureBuf);
    s.captureBuf = "";
    if (text.trim().length > 0) {
      const safe = s.title.replace(/[^a-zA-Z0-9._-]+/g, "_");
      downloadText(`netflow_${safe}_${timestampSlug()}.txt`, text);
      s.term.write("\r\n\x1b[38;5;42m[capture enregistrée]\x1b[0m\r\n");
    } else {
      s.term.write("\r\n\x1b[38;5;215m[capture vide — rien à enregistrer]\x1b[0m\r\n");
    }
  }
  updateCaptureButton();
  updateStatus();
}

// ---------------------------------------------------------------------------
// Settings application
// ---------------------------------------------------------------------------
function applySettingsToAllTerms() {
  const theme = termTheme();
  for (const s of sessions.values()) {
    s.term.options.fontSize = settings.fontSize;
    s.term.options.theme = theme;
    s.fit.fit();
  }
}

function openSettings() {
  openSettingsModal((s) => {
    settings = s;
    applySettingsToAllTerms();
  });
}

// ---------------------------------------------------------------------------
// Backend events
// ---------------------------------------------------------------------------
listen<{ id: string }>("term-closed", (e) => {
  const s = sessions.get(e.payload.id);
  if (!s) return;
  s.connected = false;
  s.term.write("\r\n\x1b[38;5;210m[session terminée]\x1b[0m\r\n");
  renderTabs();
  if (e.payload.id === activeId) updateStatus();
});

// ---------------------------------------------------------------------------
// Window controls + toolbar wiring
// ---------------------------------------------------------------------------
const appWindow = getCurrentWindow();
(document.getElementById("btn-min") as HTMLButtonElement).onclick = () => appWindow.minimize();
(document.getElementById("btn-max") as HTMLButtonElement).onclick = () => appWindow.toggleMaximize();
(document.getElementById("btn-close") as HTMLButtonElement).onclick = () => appWindow.close();

btnCapture.onclick = toggleCapture;
btnClear.onclick = () => {
  const s = activeId ? sessions.get(activeId) : null;
  if (s) s.term.clear();
};
btnSettings.onclick = openSettings;

(document.getElementById("empty-new") as HTMLButtonElement).onclick = newConnection;

// ---------------------------------------------------------------------------
// Clipboard + keyboard
// ---------------------------------------------------------------------------
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
  if (ctrl && e.key === ",") {
    e.preventDefault();
    openSettings();
  } else if (ctrl && (e.key === "E" || e.key === "e")) {
    e.preventDefault();
    if (activeId) toggleCapture();
  } else if (ctrl && (e.key === "L" || e.key === "l")) {
    const s = activeId ? sessions.get(activeId) : null;
    if (s) {
      e.preventDefault();
      s.term.clear();
    }
  } else if (ctrl && (e.key === "N" || e.key === "n")) {
    e.preventDefault();
    newConnection();
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
titleActions.classList.add("hidden");
